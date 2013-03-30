#include "cache.h"
#include "refs.h"
#include "object.h"
#include "tag.h"
#include "dir.h"

/* ISSYMREF=0x01, ISPACKED=0x02 and ISBROKEN=0x04 are public interfaces */
#define REF_KNOWS_PEELED 0x10

struct ref_entry {
	unsigned char flag; /* ISSYMREF? ISPACKED? */
	unsigned char sha1[20];
	unsigned char peeled[20];
	/* The full name of the reference (e.g., "refs/heads/master"): */
	char name[FLEX_ARRAY];
};

struct ref_array {
	int nr, alloc;
	struct ref_entry **refs;
};

/*
 * Parse one line from a packed-refs file.  Write the SHA1 to sha1.
 * Return a pointer to the refname within the line (null-terminated),
 * or NULL if there was a problem.
 */
static const char *parse_ref_line(char *line, unsigned char *sha1)
{
	/*
	 * 42: the answer to everything.
	 *
	 * In this case, it happens to be the answer to
	 *  40 (length of sha1 hex representation)
	 *  +1 (space in between hex and name)
	 *  +1 (newline at the end of the line)
	 */
	int len = strlen(line) - 42;

	if (len <= 0)
		return NULL;
	if (get_sha1_hex(line, sha1) < 0)
		return NULL;
	if (!isspace(line[40]))
		return NULL;
	line += 41;
	if (isspace(*line))
		return NULL;
	if (line[len] != '\n')
		return NULL;
	line[len] = 0;

	return line;
}

static struct ref_entry *create_ref_entry(const char *refname,
					  const unsigned char *sha1, int flag,
					  int check_name)
{
	int len;
	struct ref_entry *ref;

	if (check_name &&
	    check_refname_format(refname, REFNAME_ALLOW_ONELEVEL|REFNAME_DOT_COMPONENT))
		die("Reference has invalid format: '%s'", refname);
	len = strlen(refname) + 1;
	ref = xmalloc(sizeof(struct ref_entry) + len);
	hashcpy(ref->sha1, sha1);
	hashclr(ref->peeled);
	memcpy(ref->name, refname, len);
	ref->flag = flag;
	return ref;
}

/* Add a ref_entry to the end of the ref_array (unsorted). */
static void add_ref(struct ref_array *refs, struct ref_entry *ref)
{
	ALLOC_GROW(refs->refs, refs->nr + 1, refs->alloc);
	refs->refs[refs->nr++] = ref;
}

static int ref_entry_cmp(const void *a, const void *b)
{
	struct ref_entry *one = *(struct ref_entry **)a;
	struct ref_entry *two = *(struct ref_entry **)b;
	return strcmp(one->name, two->name);
}

/*
 * Emit a warning and return true iff ref1 and ref2 have the same name
 * and the same sha1.  Die if they have the same name but different
 * sha1s.
 */
static int is_dup_ref(const struct ref_entry *ref1, const struct ref_entry *ref2)
{
	if (!strcmp(ref1->name, ref2->name)) {
		/* Duplicate name; make sure that the SHA1s match: */
		if (hashcmp(ref1->sha1, ref2->sha1))
			die("Duplicated ref, and SHA1s don't match: %s",
			    ref1->name);
		warning("Duplicated ref: %s", ref1->name);
		return 1;
	} else {
		return 0;
	}
}

static void sort_ref_array(struct ref_array *array)
{
	int i, j;

	/* Nothing to sort unless there are at least two entries */
	if (array->nr < 2)
		return;

	qsort(array->refs, array->nr, sizeof(*array->refs), ref_entry_cmp);

	/* Remove any duplicates from the ref_array */
	i = 0;
	for (j = 1; j < array->nr; j++) {
		if (is_dup_ref(array->refs[i], array->refs[j])) {
			free(array->refs[j]);
			continue;
		}
		array->refs[++i] = array->refs[j];
	}
	array->nr = i + 1;
}

static struct ref_entry *search_ref_array(struct ref_array *array, const char *refname)
{
	struct ref_entry *e, **r;
	int len;

	if (refname == NULL)
		return NULL;

	if (!array->nr)
		return NULL;

	len = strlen(refname) + 1;
	e = xmalloc(sizeof(struct ref_entry) + len);
	memcpy(e->name, refname, len);

	r = bsearch(&e, array->refs, array->nr, sizeof(*array->refs), ref_entry_cmp);

	free(e);

	if (r == NULL)
		return NULL;

	return *r;
}

/*
 * Future: need to be in "struct repository"
 * when doing a full libification.
 */
static struct ref_cache {
	struct ref_cache *next;
	char did_loose;
	char did_packed;
	struct ref_array loose;
	struct ref_array packed;
	/* The submodule name, or "" for the main repo. */
	char name[FLEX_ARRAY];
} *ref_cache;

static struct ref_entry *current_ref;

static struct ref_array extra_refs;

static void clear_ref_array(struct ref_array *array)
{
	int i;
	for (i = 0; i < array->nr; i++)
		free(array->refs[i]);
	free(array->refs);
	array->nr = array->alloc = 0;
	array->refs = NULL;
}

static void clear_packed_ref_cache(struct ref_cache *refs)
{
	if (refs->did_packed)
		clear_ref_array(&refs->packed);
	refs->did_packed = 0;
}

static void clear_loose_ref_cache(struct ref_cache *refs)
{
	if (refs->did_loose)
		clear_ref_array(&refs->loose);
	refs->did_loose = 0;
}

static struct ref_cache *create_ref_cache(const char *submodule)
{
	int len;
	struct ref_cache *refs;
	if (!submodule)
		submodule = "";
	len = strlen(submodule) + 1;
	refs = xcalloc(1, sizeof(struct ref_cache) + len);
	memcpy(refs->name, submodule, len);
	return refs;
}

/*
 * Return a pointer to a ref_cache for the specified submodule. For
 * the main repository, use submodule==NULL. The returned structure
 * will be allocated and initialized but not necessarily populated; it
 * should not be freed.
 */
static struct ref_cache *get_ref_cache(const char *submodule)
{
	struct ref_cache *refs = ref_cache;
	if (!submodule)
		submodule = "";
	while (refs) {
		if (!strcmp(submodule, refs->name))
			return refs;
		refs = refs->next;
	}

	refs = create_ref_cache(submodule);
	refs->next = ref_cache;
	ref_cache = refs;
	return refs;
}

void invalidate_ref_cache(const char *submodule)
{
	struct ref_cache *refs = get_ref_cache(submodule);
	clear_packed_ref_cache(refs);
	clear_loose_ref_cache(refs);
}

static void read_packed_refs(FILE *f, struct ref_array *array)
{
	struct ref_entry *last = NULL;
	char refline[PATH_MAX];
	int flag = REF_ISPACKED;

	while (fgets(refline, sizeof(refline), f)) {
		unsigned char sha1[20];
		const char *refname;
		static const char header[] = "# pack-refs with:";

		if (!strncmp(refline, header, sizeof(header)-1)) {
			const char *traits = refline + sizeof(header) - 1;
			if (strstr(traits, " peeled "))
				flag |= REF_KNOWS_PEELED;
			/* perhaps other traits later as well */
			continue;
		}

		refname = parse_ref_line(refline, sha1);
		if (refname) {
			last = create_ref_entry(refname, sha1, flag, 1);
			add_ref(array, last);
			continue;
		}
		if (last &&
		    refline[0] == '^' &&
		    strlen(refline) == 42 &&
		    refline[41] == '\n' &&
		    !get_sha1_hex(refline + 1, sha1))
			hashcpy(last->peeled, sha1);
	}
	sort_ref_array(array);
}

void add_extra_ref(const char *refname, const unsigned char *sha1, int flag)
{
	add_ref(&extra_refs, create_ref_entry(refname, sha1, flag, 0));
}

void clear_extra_refs(void)
{
	clear_ref_array(&extra_refs);
}

static struct ref_array *get_packed_refs(struct ref_cache *refs)
{
	if (!refs->did_packed) {
		const char *packed_refs_file;
		FILE *f;

		if (*refs->name)
			packed_refs_file = git_path_submodule(refs->name, "packed-refs");
		else
			packed_refs_file = git_path("packed-refs");
		f = fopen(packed_refs_file, "r");
		if (f) {
			read_packed_refs(f, &refs->packed);
			fclose(f);
		}
		refs->did_packed = 1;
	}
	return &refs->packed;
}

static void get_ref_dir(struct ref_cache *refs, const char *base,
			struct ref_array *array)
{
	DIR *dir;
	const char *path;

	if (*refs->name)
		path = git_path_submodule(refs->name, "%s", base);
	else
		path = git_path("%s", base);


	dir = opendir(path);

	if (dir) {
		struct dirent *de;
		int baselen = strlen(base);
		char *refname = xmalloc(baselen + 257);

		memcpy(refname, base, baselen);
		if (baselen && base[baselen-1] != '/')
			refname[baselen++] = '/';

		while ((de = readdir(dir)) != NULL) {
			unsigned char sha1[20];
			struct stat st;
			int flag;
			int namelen;
			const char *refdir;

			if (de->d_name[0] == '.')
				continue;
			namelen = strlen(de->d_name);
			if (namelen > 255)
				continue;
			if (has_extension(de->d_name, ".lock"))
				continue;
			memcpy(refname + baselen, de->d_name, namelen+1);
			refdir = *refs->name
				? git_path_submodule(refs->name, "%s", refname)
				: git_path("%s", refname);
			if (stat(refdir, &st) < 0)
				continue;
			if (S_ISDIR(st.st_mode)) {
				get_ref_dir(refs, refname, array);
				continue;
			}
			if (*refs->name) {
				hashclr(sha1);
				flag = 0;
				if (resolve_gitlink_ref(refs->name, refname, sha1) < 0) {
					hashclr(sha1);
					flag |= REF_ISBROKEN;
				}
			} else if (read_ref_full(refname, sha1, 1, &flag)) {
				hashclr(sha1);
				flag |= REF_ISBROKEN;
			}
			add_ref(array, create_ref_entry(refname, sha1, flag, 1));
		}
		free(refname);
		closedir(dir);
	}
}

struct warn_if_dangling_data {
	FILE *fp;
	const char *refname;
	const char *msg_fmt;
};

static int warn_if_dangling_symref(const char *refname, const unsigned char *sha1,
				   int flags, void *cb_data)
{
	struct warn_if_dangling_data *d = cb_data;
	const char *resolves_to;
	unsigned char junk[20];

	if (!(flags & REF_ISSYMREF))
		return 0;

	resolves_to = resolve_ref_unsafe(refname, junk, 0, NULL);
	if (!resolves_to || strcmp(resolves_to, d->refname))
		return 0;

	fprintf(d->fp, d->msg_fmt, refname);
	return 0;
}

void warn_dangling_symref(FILE *fp, const char *msg_fmt, const char *refname)
{
	struct warn_if_dangling_data data;

	data.fp = fp;
	data.refname = refname;
	data.msg_fmt = msg_fmt;
	for_each_rawref(warn_if_dangling_symref, &data);
}

static struct ref_array *get_loose_refs(struct ref_cache *refs)
{
	if (!refs->did_loose) {
		get_ref_dir(refs, "refs", &refs->loose);
		sort_ref_array(&refs->loose);
		refs->did_loose = 1;
	}
	return &refs->loose;
}

/* We allow "recursive" symbolic refs. Only within reason, though */
#define MAXDEPTH 5
#define MAXREFLEN (1024)

/*
 * Called by resolve_gitlink_ref_recursive() after it failed to read
 * from the loose refs in ref_cache refs. Find <refname> in the
 * packed-refs file for the submodule.
 */
static int resolve_gitlink_packed_ref(struct ref_cache *refs,
				      const char *refname, unsigned char *sha1)
{
	struct ref_entry *ref;
	struct ref_array *array = get_packed_refs(refs);

	ref = search_ref_array(array, refname);
	if (ref == NULL)
		return -1;

	memcpy(sha1, ref->sha1, 20);
	return 0;
}

static int resolve_gitlink_ref_recursive(struct ref_cache *refs,
					 const char *refname, unsigned char *sha1,
					 int recursion)
{
	int fd, len;
	char buffer[128], *p;
	char *path;

	if (recursion > MAXDEPTH || strlen(refname) > MAXREFLEN)
		return -1;
	path = *refs->name
		? git_path_submodule(refs->name, "%s", refname)
		: git_path("%s", refname);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return resolve_gitlink_packed_ref(refs, refname, sha1);

	len = read(fd, buffer, sizeof(buffer)-1);
	close(fd);
	if (len < 0)
		return -1;
	while (len && isspace(buffer[len-1]))
		len--;
	buffer[len] = 0;

	/* Was it a detached head or an old-fashioned symlink? */
	if (!get_sha1_hex(buffer, sha1))
		return 0;

	/* Symref? */
	if (strncmp(buffer, "ref:", 4))
		return -1;
	p = buffer + 4;
	while (isspace(*p))
		p++;

	return resolve_gitlink_ref_recursive(refs, p, sha1, recursion+1);
}

int resolve_gitlink_ref(const char *path, const char *refname, unsigned char *sha1)
{
	int len = strlen(path), retval;
	char *submodule;
	struct ref_cache *refs;

	while (len && path[len-1] == '/')
		len--;
	if (!len)
		return -1;
	submodule = xstrndup(path, len);
	refs = get_ref_cache(submodule);
	free(submodule);

	retval = resolve_gitlink_ref_recursive(refs, refname, sha1, 0);
	return retval;
}

/*
 * Try to read ref from the packed references.  On success, set sha1
 * and return 0; otherwise, return -1.
 */
static int get_packed_ref(const char *refname, unsigned char *sha1)
{
	struct ref_array *packed = get_packed_refs(get_ref_cache(NULL));
	struct ref_entry *entry = search_ref_array(packed, refname);
	if (entry) {
		hashcpy(sha1, entry->sha1);
		return 0;
	}
	return -1;
}

const char *resolve_ref_unsafe(const char *refname, unsigned char *sha1, int reading, int *flag)
{
	int depth = MAXDEPTH;
	ssize_t len;
	char buffer[256];
	static char refname_buffer[256];

	if (flag)
		*flag = 0;

	if (check_refname_format(refname, REFNAME_ALLOW_ONELEVEL))
		return NULL;

	for (;;) {
		char path[PATH_MAX];
		struct stat st;
		char *buf;
		int fd;

		if (--depth < 0)
			return NULL;

		git_snpath(path, sizeof(path), "%s", refname);

		if (lstat(path, &st) < 0) {
			if (errno != ENOENT)
				return NULL;
			/*
			 * The loose reference file does not exist;
			 * check for a packed reference.
			 */
			if (!get_packed_ref(refname, sha1)) {
				if (flag)
					*flag |= REF_ISPACKED;
				return refname;
			}
			/* The reference is not a packed reference, either. */
			if (reading) {
				return NULL;
			} else {
				hashclr(sha1);
				return refname;
			}
		}

		/* Follow "normalized" - ie "refs/.." symlinks by hand */
		if (S_ISLNK(st.st_mode)) {
			len = readlink(path, buffer, sizeof(buffer)-1);
			if (len < 0)
				return NULL;
			buffer[len] = 0;
			if (!prefixcmp(buffer, "refs/") &&
					!check_refname_format(buffer, 0)) {
				strcpy(refname_buffer, buffer);
				refname = refname_buffer;
				if (flag)
					*flag |= REF_ISSYMREF;
				continue;
			}
		}

		/* Is it a directory? */
		if (S_ISDIR(st.st_mode)) {
			errno = EISDIR;
			return NULL;
		}

		/*
		 * Anything else, just open it and try to use it as
		 * a ref
		 */
		fd = open(path, O_RDONLY);
		if (fd < 0)
			return NULL;
		len = read_in_full(fd, buffer, sizeof(buffer)-1);
		close(fd);
		if (len < 0)
			return NULL;
		while (len && isspace(buffer[len-1]))
			len--;
		buffer[len] = '\0';

		/*
		 * Is it a symbolic ref?
		 */
		if (prefixcmp(buffer, "ref:"))
			break;
		if (flag)
			*flag |= REF_ISSYMREF;
		buf = buffer + 4;
		while (isspace(*buf))
			buf++;
		if (check_refname_format(buf, REFNAME_ALLOW_ONELEVEL)) {
			if (flag)
				*flag |= REF_ISBROKEN;
			return NULL;
		}
		refname = strcpy(refname_buffer, buf);
	}
	/* Please note that FETCH_HEAD has a second line containing other data. */
	if (get_sha1_hex(buffer, sha1) || (buffer[40] != '\0' && !isspace(buffer[40]))) {
		if (flag)
			*flag |= REF_ISBROKEN;
		return NULL;
	}
	return refname;
}

char *resolve_refdup(const char *ref, unsigned char *sha1, int reading, int *flag)
{
	const char *ret = resolve_ref_unsafe(ref, sha1, reading, flag);
	return ret ? xstrdup(ret) : NULL;
}

/* The argument to filter_refs */
struct ref_filter {
	const char *pattern;
	each_ref_fn *fn;
	void *cb_data;
};

int read_ref_full(const char *refname, unsigned char *sha1, int reading, int *flags)
{
	if (resolve_ref_unsafe(refname, sha1, reading, flags))
		return 0;
	return -1;
}

int read_ref(const char *refname, unsigned char *sha1)
{
	return read_ref_full(refname, sha1, 1, NULL);
}

#define DO_FOR_EACH_INCLUDE_BROKEN 01
static int do_one_ref(const char *base, each_ref_fn fn, int trim,
		      int flags, void *cb_data, struct ref_entry *entry)
{
	if (prefixcmp(entry->name, base))
		return 0;

	if (!(flags & DO_FOR_EACH_INCLUDE_BROKEN)) {
		if (entry->flag & REF_ISBROKEN)
			return 0; /* ignore broken refs e.g. dangling symref */
		if (!has_sha1_file(entry->sha1)) {
			error("%s does not point to a valid object!", entry->name);
			return 0;
		}
	}
	current_ref = entry;
	return fn(entry->name + trim, entry->sha1, entry->flag, cb_data);
}

static int filter_refs(const char *refname, const unsigned char *sha1, int flags,
		       void *data)
{
	struct ref_filter *filter = (struct ref_filter *)data;
	if (fnmatch(filter->pattern, refname, 0))
		return 0;
	return filter->fn(refname, sha1, flags, filter->cb_data);
}

int peel_ref(const char *refname, unsigned char *sha1)
{
	int flag;
	unsigned char base[20];
	struct object *o;

	if (current_ref && (current_ref->name == refname
		|| !strcmp(current_ref->name, refname))) {
		if (current_ref->flag & REF_KNOWS_PEELED) {
			hashcpy(sha1, current_ref->peeled);
			return 0;
		}
		hashcpy(base, current_ref->sha1);
		goto fallback;
	}

	if (read_ref_full(refname, base, 1, &flag))
		return -1;

	if ((flag & REF_ISPACKED)) {
		struct ref_array *array = get_packed_refs(get_ref_cache(NULL));
		struct ref_entry *r = search_ref_array(array, refname);

		if (r != NULL && r->flag & REF_KNOWS_PEELED) {
			hashcpy(sha1, r->peeled);
			return 0;
		}
	}

fallback:
	o = parse_object(base);
	if (o && o->type == OBJ_TAG) {
		o = deref_tag(o, refname, 0);
		if (o) {
			hashcpy(sha1, o->sha1);
			return 0;
		}
	}
	return -1;
}

static int do_for_each_ref(const char *submodule, const char *base, each_ref_fn fn,
			   int trim, int flags, void *cb_data)
{
	int retval = 0, i, p = 0, l = 0;
	struct ref_cache *refs = get_ref_cache(submodule);
	struct ref_array *packed = get_packed_refs(refs);
	struct ref_array *loose = get_loose_refs(refs);

	struct ref_array *extra = &extra_refs;

	for (i = 0; i < extra->nr; i++)
		retval = do_one_ref(base, fn, trim, flags, cb_data, extra->refs[i]);

	while (p < packed->nr && l < loose->nr) {
		struct ref_entry *entry;
		int cmp = strcmp(packed->refs[p]->name, loose->refs[l]->name);
		if (!cmp) {
			p++;
			continue;
		}
		if (cmp > 0) {
			entry = loose->refs[l++];
		} else {
			entry = packed->refs[p++];
		}
		retval = do_one_ref(base, fn, trim, flags, cb_data, entry);
		if (retval)
			goto end_each;
	}

	if (l < loose->nr) {
		p = l;
		packed = loose;
	}

	for (; p < packed->nr; p++) {
		retval = do_one_ref(base, fn, trim, flags, cb_data, packed->refs[p]);
		if (retval)
			goto end_each;
	}

end_each:
	current_ref = NULL;
	return retval;
}


static int do_head_ref(const char *submodule, each_ref_fn fn, void *cb_data)
{
	unsigned char sha1[20];
	int flag;

	if (submodule) {
		if (resolve_gitlink_ref(submodule, "HEAD", sha1) == 0)
			return fn("HEAD", sha1, 0, cb_data);

		return 0;
	}

	if (!read_ref_full("HEAD", sha1, 1, &flag))
		return fn("HEAD", sha1, flag, cb_data);

	return 0;
}

int head_ref(each_ref_fn fn, void *cb_data)
{
	return do_head_ref(NULL, fn, cb_data);
}

int head_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data)
{
	return do_head_ref(submodule, fn, cb_data);
}

int for_each_ref(each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(NULL, "", fn, 0, 0, cb_data);
}

int for_each_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(submodule, "", fn, 0, 0, cb_data);
}

int for_each_ref_in(const char *prefix, each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(NULL, prefix, fn, strlen(prefix), 0, cb_data);
}

int for_each_ref_in_submodule(const char *submodule, const char *prefix,
		each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(submodule, prefix, fn, strlen(prefix), 0, cb_data);
}

int for_each_tag_ref(each_ref_fn fn, void *cb_data)
{
	return for_each_ref_in("refs/tags/", fn, cb_data);
}

int for_each_tag_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data)
{
	return for_each_ref_in_submodule(submodule, "refs/tags/", fn, cb_data);
}

int for_each_branch_ref(each_ref_fn fn, void *cb_data)
{
	return for_each_ref_in("refs/heads/", fn, cb_data);
}

int for_each_branch_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data)
{
	return for_each_ref_in_submodule(submodule, "refs/heads/", fn, cb_data);
}

int for_each_remote_ref(each_ref_fn fn, void *cb_data)
{
	return for_each_ref_in("refs/remotes/", fn, cb_data);
}

int for_each_remote_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data)
{
	return for_each_ref_in_submodule(submodule, "refs/remotes/", fn, cb_data);
}

int for_each_replace_ref(each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(NULL, "refs/replace/", fn, 13, 0, cb_data);
}

int head_ref_namespaced(each_ref_fn fn, void *cb_data)
{
	struct strbuf buf = STRBUF_INIT;
	int ret = 0;
	unsigned char sha1[20];
	int flag;

	strbuf_addf(&buf, "%sHEAD", get_git_namespace());
	if (!read_ref_full(buf.buf, sha1, 1, &flag))
		ret = fn(buf.buf, sha1, flag, cb_data);
	strbuf_release(&buf);

	return ret;
}

int for_each_namespaced_ref(each_ref_fn fn, void *cb_data)
{
	struct strbuf buf = STRBUF_INIT;
	int ret;
	strbuf_addf(&buf, "%srefs/", get_git_namespace());
	ret = do_for_each_ref(NULL, buf.buf, fn, 0, 0, cb_data);
	strbuf_release(&buf);
	return ret;
}

int for_each_glob_ref_in(each_ref_fn fn, const char *pattern,
	const char *prefix, void *cb_data)
{
	struct strbuf real_pattern = STRBUF_INIT;
	struct ref_filter filter;
	int ret;

	if (!prefix && prefixcmp(pattern, "refs/"))
		strbuf_addstr(&real_pattern, "refs/");
	else if (prefix)
		strbuf_addstr(&real_pattern, prefix);
	strbuf_addstr(&real_pattern, pattern);

	if (!has_glob_specials(pattern)) {
		/* Append implied '/' '*' if not present. */
		if (real_pattern.buf[real_pattern.len - 1] != '/')
			strbuf_addch(&real_pattern, '/');
		/* No need to check for '*', there is none. */
		strbuf_addch(&real_pattern, '*');
	}

	filter.pattern = real_pattern.buf;
	filter.fn = fn;
	filter.cb_data = cb_data;
	ret = for_each_ref(filter_refs, &filter);

	strbuf_release(&real_pattern);
	return ret;
}

int for_each_glob_ref(each_ref_fn fn, const char *pattern, void *cb_data)
{
	return for_each_glob_ref_in(fn, pattern, NULL, cb_data);
}

int for_each_rawref(each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref(NULL, "", fn, 0,
			       DO_FOR_EACH_INCLUDE_BROKEN, cb_data);
}

/*
 * Make sure "ref" is something reasonable to have under ".git/refs/";
 * We do not like it if:
 *
 * - any path component of it begins with ".", or
 * - it has double dots "..", or
 * - it has ASCII control character, "~", "^", ":" or SP, anywhere, or
 * - it ends with a "/".
 * - it ends with ".lock"
 * - it contains a "\" (backslash)
 */

/* Return true iff ch is not allowed in reference names. */
static inline int bad_ref_char(int ch)
{
	if (((unsigned) ch) <= ' ' || ch == 0x7f ||
	    ch == '~' || ch == '^' || ch == ':' || ch == '\\')
		return 1;
	/* 2.13 Pattern Matching Notation */
	if (ch == '*' || ch == '?' || ch == '[') /* Unsupported */
		return 1;
	return 0;
}

/*
 * Try to read one refname component from the front of refname.  Return
 * the length of the component found, or -1 if the component is not
 * legal.
 */
static int check_refname_component(const char *refname, int flags)
{
	const char *cp;
	char last = '\0';

	for (cp = refname; ; cp++) {
		char ch = *cp;
		if (ch == '\0' || ch == '/')
			break;
		if (bad_ref_char(ch))
			return -1; /* Illegal character in refname. */
		if (last == '.' && ch == '.')
			return -1; /* Refname contains "..". */
		if (last == '@' && ch == '{')
			return -1; /* Refname contains "@{". */
		last = ch;
	}
	if (cp == refname)
		return -1; /* Component has zero length. */
	if (refname[0] == '.') {
		if (!(flags & REFNAME_DOT_COMPONENT))
			return -1; /* Component starts with '.'. */
		/*
		 * Even if leading dots are allowed, don't allow "."
		 * as a component (".." is prevented by a rule above).
		 */
		if (refname[1] == '\0')
			return -1; /* Component equals ".". */
	}
	if (cp - refname >= 5 && !memcmp(cp - 5, ".lock", 5))
		return -1; /* Refname ends with ".lock". */
	return cp - refname;
}

int check_refname_format(const char *refname, int flags)
{
	int component_len, component_count = 0;

	while (1) {
		/* We are at the start of a path component. */
		component_len = check_refname_component(refname, flags);
		if (component_len < 0) {
			if ((flags & REFNAME_REFSPEC_PATTERN) &&
					refname[0] == '*' &&
					(refname[1] == '\0' || refname[1] == '/')) {
				/* Accept one wildcard as a full refname component. */
				flags &= ~REFNAME_REFSPEC_PATTERN;
				component_len = 1;
			} else {
				return -1;
			}
		}
		component_count++;
		if (refname[component_len] == '\0')
			break;
		/* Skip to next component. */
		refname += component_len + 1;
	}

	if (refname[component_len - 1] == '.')
		return -1; /* Refname ends with '.'. */
	if (!(flags & REFNAME_ALLOW_ONELEVEL) && component_count < 2)
		return -1; /* Refname has only one component. */
	return 0;
}

const char *prettify_refname(const char *name)
{
	return name + (
		!prefixcmp(name, "refs/heads/") ? 11 :
		!prefixcmp(name, "refs/tags/") ? 10 :
		!prefixcmp(name, "refs/remotes/") ? 13 :
		0);
}

const char *ref_rev_parse_rules[] = {
	"%.*s",
	"refs/%.*s",
	"refs/tags/%.*s",
	"refs/heads/%.*s",
	"refs/remotes/%.*s",
	"refs/remotes/%.*s/HEAD",
	NULL
};

int refname_match(const char *abbrev_name, const char *full_name, const char **rules)
{
	const char **p;
	const int abbrev_name_len = strlen(abbrev_name);

	for (p = rules; *p; p++) {
		if (!strcmp(full_name, mkpath(*p, abbrev_name_len, abbrev_name))) {
			return 1;
		}
	}

	return 0;
}

static struct ref_lock *verify_lock(struct ref_lock *lock,
	const unsigned char *old_sha1, int mustexist)
{
	if (read_ref_full(lock->ref_name, lock->old_sha1, mustexist, NULL)) {
		error("Can't verify ref %s", lock->ref_name);
		unlock_ref(lock);
		return NULL;
	}
	if (hashcmp(lock->old_sha1, old_sha1)) {
		error("Ref %s is at %s but expected %s", lock->ref_name,
			sha1_to_hex(lock->old_sha1), sha1_to_hex(old_sha1));
		unlock_ref(lock);
		return NULL;
	}
	return lock;
}

static int remove_empty_directories(const char *file)
{
	/* we want to create a file but there is a directory there;
	 * if that is an empty directory (or a directory that contains
	 * only empty directories), remove them.
	 */
	struct strbuf path;
	int result;

	strbuf_init(&path, 20);
	strbuf_addstr(&path, file);

	result = remove_dir_recursively(&path, REMOVE_DIR_EMPTY_ONLY);

	strbuf_release(&path);

	return result;
}

/*
 * Return true iff a reference named refname could be created without
 * conflicting with the name of an existing reference.  If oldrefname
 * is non-NULL, ignore potential conflicts with oldrefname (e.g.,
 * because oldrefname is scheduled for deletion in the same
 * operation).
 */
static int is_refname_available(const char *refname, const char *oldrefname,
				struct ref_array *array)
{
	int i, namlen = strlen(refname); /* e.g. 'foo/bar' */
	for (i = 0; i < array->nr; i++ ) {
		struct ref_entry *entry = array->refs[i];
		/* entry->name could be 'foo' or 'foo/bar/baz' */
		if (!oldrefname || strcmp(oldrefname, entry->name)) {
			int len = strlen(entry->name);
			int cmplen = (namlen < len) ? namlen : len;
			const char *lead = (namlen < len) ? entry->name : refname;
			if (!strncmp(refname, entry->name, cmplen) &&
			    lead[cmplen] == '/') {
				error("'%s' exists; cannot create '%s'",
				      entry->name, refname);
				return 0;
			}
		}
	}
	return 1;
}

/*
 * *string and *len will only be substituted, and *string returned (for
 * later free()ing) if the string passed in is a magic short-hand form
 * to name a branch.
 */
static char *substitute_branch_name(const char **string, int *len)
{
	struct strbuf buf = STRBUF_INIT;
	int ret = interpret_branch_name(*string, &buf);

	if (ret == *len) {
		size_t size;
		*string = strbuf_detach(&buf, &size);
		*len = size;
		return (char *)*string;
	}

	return NULL;
}

int dwim_ref(const char *str, int len, unsigned char *sha1, char **ref)
{
	char *last_branch = substitute_branch_name(&str, &len);
	const char **p, *r;
	int refs_found = 0;

	*ref = NULL;
	for (p = ref_rev_parse_rules; *p; p++) {
		char fullref[PATH_MAX];
		unsigned char sha1_from_ref[20];
		unsigned char *this_result;
		int flag;

		this_result = refs_found ? sha1_from_ref : sha1;
		mksnpath(fullref, sizeof(fullref), *p, len, str);
		r = resolve_ref_unsafe(fullref, this_result, 1, &flag);
		if (r) {
			if (!refs_found++)
				*ref = xstrdup(r);
			if (!warn_ambiguous_refs)
				break;
		} else if ((flag & REF_ISSYMREF) && strcmp(fullref, "HEAD")) {
			warning("ignoring dangling symref %s.", fullref);
		} else if ((flag & REF_ISBROKEN) && strchr(fullref, '/')) {
			warning("ignoring broken ref %s.", fullref);
		}
	}
	free(last_branch);
	return refs_found;
}

int dwim_log(const char *str, int len, unsigned char *sha1, char **log)
{
	char *last_branch = substitute_branch_name(&str, &len);
	const char **p;
	int logs_found = 0;

	*log = NULL;
	for (p = ref_rev_parse_rules; *p; p++) {
		struct stat st;
		unsigned char hash[20];
		char path[PATH_MAX];
		const char *ref, *it;

		mksnpath(path, sizeof(path), *p, len, str);
		ref = resolve_ref_unsafe(path, hash, 1, NULL);
		if (!ref)
			continue;
		if (!stat(git_path("logs/%s", path), &st) &&
		    S_ISREG(st.st_mode))
			it = path;
		else if (strcmp(ref, path) &&
			 !stat(git_path("logs/%s", ref), &st) &&
			 S_ISREG(st.st_mode))
			it = ref;
		else
			continue;
		if (!logs_found++) {
			*log = xstrdup(it);
			hashcpy(sha1, hash);
		}
		if (!warn_ambiguous_refs)
			break;
	}
	free(last_branch);
	return logs_found;
}

static struct ref_lock *lock_ref_sha1_basic(const char *refname,
					    const unsigned char *old_sha1,
					    int flags, int *type_p)
{
	char *ref_file;
	const char *orig_refname = refname;
	struct ref_lock *lock;
	int last_errno = 0;
	int type, lflags;
	int mustexist = (old_sha1 && !is_null_sha1(old_sha1));
	int missing = 0;

	lock = xcalloc(1, sizeof(struct ref_lock));
	lock->lock_fd = -1;

	refname = resolve_ref_unsafe(refname, lock->old_sha1, mustexist, &type);
	if (!refname && errno == EISDIR) {
		/* we are trying to lock foo but we used to
		 * have foo/bar which now does not exist;
		 * it is normal for the empty directory 'foo'
		 * to remain.
		 */
		ref_file = git_path("%s", orig_refname);
		if (remove_empty_directories(ref_file)) {
			last_errno = errno;
			error("there are still refs under '%s'", orig_refname);
			goto error_return;
		}
		refname = resolve_ref_unsafe(orig_refname, lock->old_sha1, mustexist, &type);
	}
	if (type_p)
	    *type_p = type;
	if (!refname) {
		last_errno = errno;
		error("unable to resolve reference %s: %s",
			orig_refname, strerror(errno));
		goto error_return;
	}
	missing = is_null_sha1(lock->old_sha1);
	/* When the ref did not exist and we are creating it,
	 * make sure there is no existing ref that is packed
	 * whose name begins with our refname, nor a ref whose
	 * name is a proper prefix of our refname.
	 */
	if (missing &&
	     !is_refname_available(refname, NULL, get_packed_refs(get_ref_cache(NULL)))) {
		last_errno = ENOTDIR;
		goto error_return;
	}

	lock->lk = xcalloc(1, sizeof(struct lock_file));

	lflags = LOCK_DIE_ON_ERROR;
	if (flags & REF_NODEREF) {
		refname = orig_refname;
		lflags |= LOCK_NODEREF;
	}
	lock->ref_name = xstrdup(refname);
	lock->orig_ref_name = xstrdup(orig_refname);
	ref_file = git_path("%s", refname);
	if (missing)
		lock->force_write = 1;
	if ((flags & REF_NODEREF) && (type & REF_ISSYMREF))
		lock->force_write = 1;

	if (safe_create_leading_directories(ref_file)) {
		last_errno = errno;
		error("unable to create directory for %s", ref_file);
		goto error_return;
	}

	lock->lock_fd = hold_lock_file_for_update(lock->lk, ref_file, lflags);
	return old_sha1 ? verify_lock(lock, old_sha1, mustexist) : lock;

 error_return:
	unlock_ref(lock);
	errno = last_errno;
	return NULL;
}

struct ref_lock *lock_ref_sha1(const char *refname, const unsigned char *old_sha1)
{
	char refpath[PATH_MAX];
	if (check_refname_format(refname, 0))
		return NULL;
	strcpy(refpath, mkpath("refs/%s", refname));
	return lock_ref_sha1_basic(refpath, old_sha1, 0, NULL);
}

struct ref_lock *lock_any_ref_for_update(const char *refname,
					 const unsigned char *old_sha1, int flags)
{
	if (check_refname_format(refname, REFNAME_ALLOW_ONELEVEL))
		return NULL;
	return lock_ref_sha1_basic(refname, old_sha1, flags, NULL);
}

static struct lock_file packlock;

static int repack_without_ref(const char *refname)
{
	struct ref_array *packed;
	int fd, i;

	packed = get_packed_refs(get_ref_cache(NULL));
	if (search_ref_array(packed, refname) == NULL)
		return 0;
	fd = hold_lock_file_for_update(&packlock, git_path("packed-refs"), 0);
	if (fd < 0) {
		unable_to_lock_error(git_path("packed-refs"), errno);
		return error("cannot delete '%s' from packed refs", refname);
	}

	for (i = 0; i < packed->nr; i++) {
		char line[PATH_MAX + 100];
		int len;
		struct ref_entry *ref = packed->refs[i];

		if (!strcmp(refname, ref->name))
			continue;
		len = snprintf(line, sizeof(line), "%s %s\n",
			       sha1_to_hex(ref->sha1), ref->name);
		/* this should not happen but just being defensive */
		if (len > sizeof(line))
			die("too long a refname '%s'", ref->name);
		write_or_die(fd, line, len);
	}
	return commit_lock_file(&packlock);
}

int delete_ref(const char *refname, const unsigned char *sha1, int delopt)
{
	struct ref_lock *lock;
	int err, i = 0, ret = 0, flag = 0;

	lock = lock_ref_sha1_basic(refname, sha1, 0, &flag);
	if (!lock)
		return 1;
	if (!(flag & REF_ISPACKED) || flag & REF_ISSYMREF) {
		/* loose */
		const char *path;

		if (!(delopt & REF_NODEREF)) {
			i = strlen(lock->lk->filename) - 5; /* .lock */
			lock->lk->filename[i] = 0;
			path = lock->lk->filename;
		} else {
			path = git_path("%s", refname);
		}
		err = unlink_or_warn(path);
		if (err && errno != ENOENT)
			ret = 1;

		if (!(delopt & REF_NODEREF))
			lock->lk->filename[i] = '.';
	}
	/* removing the loose one could have resurrected an earlier
	 * packed one.  Also, if it was not loose we need to repack
	 * without it.
	 */
	ret |= repack_without_ref(refname);

	unlink_or_warn(git_path("logs/%s", lock->ref_name));
	invalidate_ref_cache(NULL);
	unlock_ref(lock);
	return ret;
}

/*
 * People using contrib's git-new-workdir have .git/logs/refs ->
 * /some/other/path/.git/logs/refs, and that may live on another device.
 *
 * IOW, to avoid cross device rename errors, the temporary renamed log must
 * live into logs/refs.
 */
#define TMP_RENAMED_LOG  "logs/refs/.tmp-renamed-log"

int rename_ref(const char *oldrefname, const char *newrefname, const char *logmsg)
{
	unsigned char sha1[20], orig_sha1[20];
	int flag = 0, logmoved = 0;
	struct ref_lock *lock;
	struct stat loginfo;
	int log = !lstat(git_path("logs/%s", oldrefname), &loginfo);
	const char *symref = NULL;
	struct ref_cache *refs = get_ref_cache(NULL);

	if (log && S_ISLNK(loginfo.st_mode))
		return error("reflog for %s is a symlink", oldrefname);

	symref = resolve_ref_unsafe(oldrefname, orig_sha1, 1, &flag);
	if (flag & REF_ISSYMREF)
		return error("refname %s is a symbolic ref, renaming it is not supported",
			oldrefname);
	if (!symref)
		return error("refname %s not found", oldrefname);

	if (!is_refname_available(newrefname, oldrefname, get_packed_refs(refs)))
		return 1;

	if (!is_refname_available(newrefname, oldrefname, get_loose_refs(refs)))
		return 1;

	if (log && rename(git_path("logs/%s", oldrefname), git_path(TMP_RENAMED_LOG)))
		return error("unable to move logfile logs/%s to "TMP_RENAMED_LOG": %s",
			oldrefname, strerror(errno));

	if (delete_ref(oldrefname, orig_sha1, REF_NODEREF)) {
		error("unable to delete old %s", oldrefname);
		goto rollback;
	}

	if (!read_ref_full(newrefname, sha1, 1, &flag) &&
	    delete_ref(newrefname, sha1, REF_NODEREF)) {
		if (errno==EISDIR) {
			if (remove_empty_directories(git_path("%s", newrefname))) {
				error("Directory not empty: %s", newrefname);
				goto rollback;
			}
		} else {
			error("unable to delete existing %s", newrefname);
			goto rollback;
		}
	}

	if (log && safe_create_leading_directories(git_path("logs/%s", newrefname))) {
		error("unable to create directory for %s", newrefname);
		goto rollback;
	}

 retry:
	if (log && rename(git_path(TMP_RENAMED_LOG), git_path("logs/%s", newrefname))) {
		if (errno==EISDIR || errno==ENOTDIR) {
			/*
			 * rename(a, b) when b is an existing
			 * directory ought to result in ISDIR, but
			 * Solaris 5.8 gives ENOTDIR.  Sheesh.
			 */
			if (remove_empty_directories(git_path("logs/%s", newrefname))) {
				error("Directory not empty: logs/%s", newrefname);
				goto rollback;
			}
			goto retry;
		} else {
			error("unable to move logfile "TMP_RENAMED_LOG" to logs/%s: %s",
				newrefname, strerror(errno));
			goto rollback;
		}
	}
	logmoved = log;

	lock = lock_ref_sha1_basic(newrefname, NULL, 0, NULL);
	if (!lock) {
		error("unable to lock %s for update", newrefname);
		goto rollback;
	}
	lock->force_write = 1;
	hashcpy(lock->old_sha1, orig_sha1);
	if (write_ref_sha1(lock, orig_sha1, logmsg)) {
		error("unable to write current sha1 into %s", newrefname);
		goto rollback;
	}

	return 0;

 rollback:
	lock = lock_ref_sha1_basic(oldrefname, NULL, 0, NULL);
	if (!lock) {
		error("unable to lock %s for rollback", oldrefname);
		goto rollbacklog;
	}

	lock->force_write = 1;
	flag = log_all_ref_updates;
	log_all_ref_updates = 0;
	if (write_ref_sha1(lock, orig_sha1, NULL))
		error("unable to write current sha1 into %s", oldrefname);
	log_all_ref_updates = flag;

 rollbacklog:
	if (logmoved && rename(git_path("logs/%s", newrefname), git_path("logs/%s", oldrefname)))
		error("unable to restore logfile %s from %s: %s",
			oldrefname, newrefname, strerror(errno));
	if (!logmoved && log &&
	    rename(git_path(TMP_RENAMED_LOG), git_path("logs/%s", oldrefname)))
		error("unable to restore logfile %s from "TMP_RENAMED_LOG": %s",
			oldrefname, strerror(errno));

	return 1;
}

int close_ref(struct ref_lock *lock)
{
	if (close_lock_file(lock->lk))
		return -1;
	lock->lock_fd = -1;
	return 0;
}

int commit_ref(struct ref_lock *lock)
{
	if (commit_lock_file(lock->lk))
		return -1;
	lock->lock_fd = -1;
	return 0;
}

void unlock_ref(struct ref_lock *lock)
{
	/* Do not free lock->lk -- atexit() still looks at them */
	if (lock->lk)
		rollback_lock_file(lock->lk);
	free(lock->ref_name);
	free(lock->orig_ref_name);
	free(lock);
}

/*
 * copy the reflog message msg to buf, which has been allocated sufficiently
 * large, while cleaning up the whitespaces.  Especially, convert LF to space,
 * because reflog file is one line per entry.
 */
static int copy_msg(char *buf, const char *msg)
{
	char *cp = buf;
	char c;
	int wasspace = 1;

	*cp++ = '\t';
	while ((c = *msg++)) {
		if (wasspace && isspace(c))
			continue;
		wasspace = isspace(c);
		if (wasspace)
			c = ' ';
		*cp++ = c;
	}
	while (buf < cp && isspace(cp[-1]))
		cp--;
	*cp++ = '\n';
	return cp - buf;
}

int log_ref_setup(const char *refname, char *logfile, int bufsize)
{
	int logfd, oflags = O_APPEND | O_WRONLY;

	git_snpath(logfile, bufsize, "logs/%s", refname);
	if (log_all_ref_updates &&
	    (!prefixcmp(refname, "refs/heads/") ||
	     !prefixcmp(refname, "refs/remotes/") ||
	     !prefixcmp(refname, "refs/notes/") ||
	     !strcmp(refname, "HEAD"))) {
		if (safe_create_leading_directories(logfile) < 0)
			return error("unable to create directory for %s",
				     logfile);
		oflags |= O_CREAT;
	}

	logfd = open(logfile, oflags, 0666);
	if (logfd < 0) {
		if (!(oflags & O_CREAT) && errno == ENOENT)
			return 0;

		if ((oflags & O_CREAT) && errno == EISDIR) {
			if (remove_empty_directories(logfile)) {
				return error("There are still logs under '%s'",
					     logfile);
			}
			logfd = open(logfile, oflags, 0666);
		}

		if (logfd < 0)
			return error("Unable to append to %s: %s",
				     logfile, strerror(errno));
	}

	adjust_shared_perm(logfile);
	close(logfd);
	return 0;
}

static int log_ref_write(const char *refname, const unsigned char *old_sha1,
			 const unsigned char *new_sha1, const char *msg)
{
	int logfd, result, written, oflags = O_APPEND | O_WRONLY;
	unsigned maxlen, len;
	int msglen;
	char log_file[PATH_MAX];
	char *logrec;
	const char *committer;

	if (log_all_ref_updates < 0)
		log_all_ref_updates = !is_bare_repository();

	result = log_ref_setup(refname, log_file, sizeof(log_file));
	if (result)
		return result;

	logfd = open(log_file, oflags);
	if (logfd < 0)
		return 0;
	msglen = msg ? strlen(msg) : 0;
	committer = git_committer_info(0);
	maxlen = strlen(committer) + msglen + 100;
	logrec = xmalloc(maxlen);
	len = sprintf(logrec, "%s %s %s\n",
		      sha1_to_hex(old_sha1),
		      sha1_to_hex(new_sha1),
		      committer);
	if (msglen)
		len += copy_msg(logrec + len - 1, msg) - 1;
	written = len <= maxlen ? write_in_full(logfd, logrec, len) : -1;
	free(logrec);
	if (close(logfd) != 0 || written != len)
		return error("Unable to append to %s", log_file);
	return 0;
}

static int is_branch(const char *refname)
{
	return !strcmp(refname, "HEAD") || !prefixcmp(refname, "refs/heads/");
}

int write_ref_sha1(struct ref_lock *lock,
	const unsigned char *sha1, const char *logmsg)
{
	static char term = '\n';
	struct object *o;

	if (!lock)
		return -1;
	if (!lock->force_write && !hashcmp(lock->old_sha1, sha1)) {
		unlock_ref(lock);
		return 0;
	}
	o = parse_object(sha1);
	if (!o) {
		error("Trying to write ref %s with nonexistent object %s",
			lock->ref_name, sha1_to_hex(sha1));
		unlock_ref(lock);
		return -1;
	}
	if (o->type != OBJ_COMMIT && is_branch(lock->ref_name)) {
		error("Trying to write non-commit object %s to branch %s",
			sha1_to_hex(sha1), lock->ref_name);
		unlock_ref(lock);
		return -1;
	}
	if (write_in_full(lock->lock_fd, sha1_to_hex(sha1), 40) != 40 ||
	    write_in_full(lock->lock_fd, &term, 1) != 1
		|| close_ref(lock) < 0) {
		error("Couldn't write %s", lock->lk->filename);
		unlock_ref(lock);
		return -1;
	}
	clear_loose_ref_cache(get_ref_cache(NULL));
	if (log_ref_write(lock->ref_name, lock->old_sha1, sha1, logmsg) < 0 ||
	    (strcmp(lock->ref_name, lock->orig_ref_name) &&
	     log_ref_write(lock->orig_ref_name, lock->old_sha1, sha1, logmsg) < 0)) {
		unlock_ref(lock);
		return -1;
	}
	if (strcmp(lock->orig_ref_name, "HEAD") != 0) {
		/*
		 * Special hack: If a branch is updated directly and HEAD
		 * points to it (may happen on the remote side of a push
		 * for example) then logically the HEAD reflog should be
		 * updated too.
		 * A generic solution implies reverse symref information,
		 * but finding all symrefs pointing to the given branch
		 * would be rather costly for this rare event (the direct
		 * update of a branch) to be worth it.  So let's cheat and
		 * check with HEAD only which should cover 99% of all usage
		 * scenarios (even 100% of the default ones).
		 */
		unsigned char head_sha1[20];
		int head_flag;
		const char *head_ref;
		head_ref = resolve_ref_unsafe("HEAD", head_sha1, 1, &head_flag);
		if (head_ref && (head_flag & REF_ISSYMREF) &&
		    !strcmp(head_ref, lock->ref_name))
			log_ref_write("HEAD", lock->old_sha1, sha1, logmsg);
	}
	if (commit_ref(lock)) {
		error("Couldn't set %s", lock->ref_name);
		unlock_ref(lock);
		return -1;
	}
	unlock_ref(lock);
	return 0;
}

int create_symref(const char *ref_target, const char *refs_heads_master,
		  const char *logmsg)
{
	const char *lockpath;
	char ref[1000];
	int fd, len, written;
	char *git_HEAD = git_pathdup("%s", ref_target);
	unsigned char old_sha1[20], new_sha1[20];

	if (logmsg && read_ref(ref_target, old_sha1))
		hashclr(old_sha1);

	if (safe_create_leading_directories(git_HEAD) < 0)
		return error("unable to create directory for %s", git_HEAD);

#ifndef NO_SYMLINK_HEAD
	if (prefer_symlink_refs) {
		unlink(git_HEAD);
		if (!symlink(refs_heads_master, git_HEAD))
			goto done;
		fprintf(stderr, "no symlink - falling back to symbolic ref\n");
	}
#endif

	len = snprintf(ref, sizeof(ref), "ref: %s\n", refs_heads_master);
	if (sizeof(ref) <= len) {
		error("refname too long: %s", refs_heads_master);
		goto error_free_return;
	}
	lockpath = mkpath("%s.lock", git_HEAD);
	fd = open(lockpath, O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (fd < 0) {
		error("Unable to open %s for writing", lockpath);
		goto error_free_return;
	}
	written = write_in_full(fd, ref, len);
	if (close(fd) != 0 || written != len) {
		error("Unable to write to %s", lockpath);
		goto error_unlink_return;
	}
	if (rename(lockpath, git_HEAD) < 0) {
		error("Unable to create %s", git_HEAD);
		goto error_unlink_return;
	}
	if (adjust_shared_perm(git_HEAD)) {
		error("Unable to fix permissions on %s", lockpath);
	error_unlink_return:
		unlink_or_warn(lockpath);
	error_free_return:
		free(git_HEAD);
		return -1;
	}

#ifndef NO_SYMLINK_HEAD
	done:
#endif
	if (logmsg && !read_ref(refs_heads_master, new_sha1))
		log_ref_write(ref_target, old_sha1, new_sha1, logmsg);

	free(git_HEAD);
	return 0;
}

static char *ref_msg(const char *line, const char *endp)
{
	const char *ep;
	line += 82;
	ep = memchr(line, '\n', endp - line);
	if (!ep)
		ep = endp;
	return xmemdupz(line, ep - line);
}

int read_ref_at(const char *refname, unsigned long at_time, int cnt,
		unsigned char *sha1, char **msg,
		unsigned long *cutoff_time, int *cutoff_tz, int *cutoff_cnt)
{
	const char *logfile, *logdata, *logend, *rec, *lastgt, *lastrec;
	char *tz_c;
	int logfd, tz, reccnt = 0;
	struct stat st;
	unsigned long date;
	unsigned char logged_sha1[20];
	void *log_mapped;
	size_t mapsz;

	logfile = git_path("logs/%s", refname);
	logfd = open(logfile, O_RDONLY, 0);
	if (logfd < 0)
		die_errno("Unable to read log '%s'", logfile);
	fstat(logfd, &st);
	if (!st.st_size)
		die("Log %s is empty.", logfile);
	mapsz = xsize_t(st.st_size);
	log_mapped = xmmap(NULL, mapsz, PROT_READ, MAP_PRIVATE, logfd, 0);
	logdata = log_mapped;
	close(logfd);

	lastrec = NULL;
	rec = logend = logdata + st.st_size;
	while (logdata < rec) {
		reccnt++;
		if (logdata < rec && *(rec-1) == '\n')
			rec--;
		lastgt = NULL;
		while (logdata < rec && *(rec-1) != '\n') {
			rec--;
			if (*rec == '>')
				lastgt = rec;
		}
		if (!lastgt)
			die("Log %s is corrupt.", logfile);
		date = strtoul(lastgt + 1, &tz_c, 10);
		if (date <= at_time || cnt == 0) {
			tz = strtoul(tz_c, NULL, 10);
			if (msg)
				*msg = ref_msg(rec, logend);
			if (cutoff_time)
				*cutoff_time = date;
			if (cutoff_tz)
				*cutoff_tz = tz;
			if (cutoff_cnt)
				*cutoff_cnt = reccnt - 1;
			if (lastrec) {
				if (get_sha1_hex(lastrec, logged_sha1))
					die("Log %s is corrupt.", logfile);
				if (get_sha1_hex(rec + 41, sha1))
					die("Log %s is corrupt.", logfile);
				if (hashcmp(logged_sha1, sha1)) {
					warning("Log %s has gap after %s.",
						logfile, show_date(date, tz, DATE_RFC2822));
				}
			}
			else if (date == at_time) {
				if (get_sha1_hex(rec + 41, sha1))
					die("Log %s is corrupt.", logfile);
			}
			else {
				if (get_sha1_hex(rec + 41, logged_sha1))
					die("Log %s is corrupt.", logfile);
				if (hashcmp(logged_sha1, sha1)) {
					warning("Log %s unexpectedly ended on %s.",
						logfile, show_date(date, tz, DATE_RFC2822));
				}
			}
			munmap(log_mapped, mapsz);
			return 0;
		}
		lastrec = rec;
		if (cnt > 0)
			cnt--;
	}

	rec = logdata;
	while (rec < logend && *rec != '>' && *rec != '\n')
		rec++;
	if (rec == logend || *rec == '\n')
		die("Log %s is corrupt.", logfile);
	date = strtoul(rec + 1, &tz_c, 10);
	tz = strtoul(tz_c, NULL, 10);
	if (get_sha1_hex(logdata, sha1))
		die("Log %s is corrupt.", logfile);
	if (is_null_sha1(sha1)) {
		if (get_sha1_hex(logdata + 41, sha1))
			die("Log %s is corrupt.", logfile);
	}
	if (msg)
		*msg = ref_msg(logdata, logend);
	munmap(log_mapped, mapsz);

	if (cutoff_time)
		*cutoff_time = date;
	if (cutoff_tz)
		*cutoff_tz = tz;
	if (cutoff_cnt)
		*cutoff_cnt = reccnt;
	return 1;
}

int for_each_recent_reflog_ent(const char *refname, each_reflog_ent_fn fn, long ofs, void *cb_data)
{
	const char *logfile;
	FILE *logfp;
	struct strbuf sb = STRBUF_INIT;
	int ret = 0;

	logfile = git_path("logs/%s", refname);
	logfp = fopen(logfile, "r");
	if (!logfp)
		return -1;

	if (ofs) {
		struct stat statbuf;
		if (fstat(fileno(logfp), &statbuf) ||
		    statbuf.st_size < ofs ||
		    fseek(logfp, -ofs, SEEK_END) ||
		    strbuf_getwholeline(&sb, logfp, '\n')) {
			fclose(logfp);
			strbuf_release(&sb);
			return -1;
		}
	}

	while (!strbuf_getwholeline(&sb, logfp, '\n')) {
		unsigned char osha1[20], nsha1[20];
		char *email_end, *message;
		unsigned long timestamp;
		int tz;

		/* old SP new SP name <email> SP time TAB msg LF */
		if (sb.len < 83 || sb.buf[sb.len - 1] != '\n' ||
		    get_sha1_hex(sb.buf, osha1) || sb.buf[40] != ' ' ||
		    get_sha1_hex(sb.buf + 41, nsha1) || sb.buf[81] != ' ' ||
		    !(email_end = strchr(sb.buf + 82, '>')) ||
		    email_end[1] != ' ' ||
		    !(timestamp = strtoul(email_end + 2, &message, 10)) ||
		    !message || message[0] != ' ' ||
		    (message[1] != '+' && message[1] != '-') ||
		    !isdigit(message[2]) || !isdigit(message[3]) ||
		    !isdigit(message[4]) || !isdigit(message[5]))
			continue; /* corrupt? */
		email_end[1] = '\0';
		tz = strtol(message + 1, NULL, 10);
		if (message[6] != '\t')
			message += 6;
		else
			message += 7;
		ret = fn(osha1, nsha1, sb.buf + 82, timestamp, tz, message,
			 cb_data);
		if (ret)
			break;
	}
	fclose(logfp);
	strbuf_release(&sb);
	return ret;
}

int for_each_reflog_ent(const char *refname, each_reflog_ent_fn fn, void *cb_data)
{
	return for_each_recent_reflog_ent(refname, fn, 0, cb_data);
}

static int do_for_each_reflog(const char *base, each_ref_fn fn, void *cb_data)
{
	DIR *dir = opendir(git_path("logs/%s", base));
	int retval = 0;

	if (dir) {
		struct dirent *de;
		int baselen = strlen(base);
		char *log = xmalloc(baselen + 257);

		memcpy(log, base, baselen);
		if (baselen && base[baselen-1] != '/')
			log[baselen++] = '/';

		while ((de = readdir(dir)) != NULL) {
			struct stat st;
			int namelen;

			if (de->d_name[0] == '.')
				continue;
			namelen = strlen(de->d_name);
			if (namelen > 255)
				continue;
			if (has_extension(de->d_name, ".lock"))
				continue;
			memcpy(log + baselen, de->d_name, namelen+1);
			if (stat(git_path("logs/%s", log), &st) < 0)
				continue;
			if (S_ISDIR(st.st_mode)) {
				retval = do_for_each_reflog(log, fn, cb_data);
			} else {
				unsigned char sha1[20];
				if (read_ref_full(log, sha1, 0, NULL))
					retval = error("bad ref for %s", log);
				else
					retval = fn(log, sha1, 0, cb_data);
			}
			if (retval)
				break;
		}
		free(log);
		closedir(dir);
	}
	else if (*base)
		return errno;
	return retval;
}

int for_each_reflog(each_ref_fn fn, void *cb_data)
{
	return do_for_each_reflog("", fn, cb_data);
}

int update_ref(const char *action, const char *refname,
		const unsigned char *sha1, const unsigned char *oldval,
		int flags, enum action_on_err onerr)
{
	static struct ref_lock *lock;
	lock = lock_any_ref_for_update(refname, oldval, flags);
	if (!lock) {
		const char *str = "Cannot lock the ref '%s'.";
		switch (onerr) {
		case MSG_ON_ERR: error(str, refname); break;
		case DIE_ON_ERR: die(str, refname); break;
		case QUIET_ON_ERR: break;
		}
		return 1;
	}
	if (write_ref_sha1(lock, sha1, action) < 0) {
		const char *str = "Cannot update the ref '%s'.";
		switch (onerr) {
		case MSG_ON_ERR: error(str, refname); break;
		case DIE_ON_ERR: die(str, refname); break;
		case QUIET_ON_ERR: break;
		}
		return 1;
	}
	return 0;
}

int ref_exists(const char *refname)
{
	unsigned char sha1[20];
	return !!resolve_ref_unsafe(refname, sha1, 1, NULL);
}

struct ref *find_ref_by_name(const struct ref *list, const char *name)
{
	for ( ; list; list = list->next)
		if (!strcmp(list->name, name))
			return (struct ref *)list;
	return NULL;
}

/*
 * generate a format suitable for scanf from a ref_rev_parse_rules
 * rule, that is replace the "%.*s" spec with a "%s" spec
 */
static void gen_scanf_fmt(char *scanf_fmt, const char *rule)
{
	char *spec;

	spec = strstr(rule, "%.*s");
	if (!spec || strstr(spec + 4, "%.*s"))
		die("invalid rule in ref_rev_parse_rules: %s", rule);

	/* copy all until spec */
	strncpy(scanf_fmt, rule, spec - rule);
	scanf_fmt[spec - rule] = '\0';
	/* copy new spec */
	strcat(scanf_fmt, "%s");
	/* copy remaining rule */
	strcat(scanf_fmt, spec + 4);

	return;
}

char *shorten_unambiguous_ref(const char *refname, int strict)
{
	int i;
	static char **scanf_fmts;
	static int nr_rules;
	char *short_name;

	/* pre generate scanf formats from ref_rev_parse_rules[] */
	if (!nr_rules) {
		size_t total_len = 0;

		/* the rule list is NULL terminated, count them first */
		for (; ref_rev_parse_rules[nr_rules]; nr_rules++)
			/* no +1 because strlen("%s") < strlen("%.*s") */
			total_len += strlen(ref_rev_parse_rules[nr_rules]);

		scanf_fmts = xmalloc(nr_rules * sizeof(char *) + total_len);

		total_len = 0;
		for (i = 0; i < nr_rules; i++) {
			scanf_fmts[i] = (char *)&scanf_fmts[nr_rules]
					+ total_len;
			gen_scanf_fmt(scanf_fmts[i], ref_rev_parse_rules[i]);
			total_len += strlen(ref_rev_parse_rules[i]);
		}
	}

	/* bail out if there are no rules */
	if (!nr_rules)
		return xstrdup(refname);

	/* buffer for scanf result, at most refname must fit */
	short_name = xstrdup(refname);

	/* skip first rule, it will always match */
	for (i = nr_rules - 1; i > 0 ; --i) {
		int j;
		int rules_to_fail = i;
		int short_name_len;

		if (1 != sscanf(refname, scanf_fmts[i], short_name))
			continue;

		short_name_len = strlen(short_name);

		/*
		 * in strict mode, all (except the matched one) rules
		 * must fail to resolve to a valid non-ambiguous ref
		 */
		if (strict)
			rules_to_fail = nr_rules;

		/*
		 * check if the short name resolves to a valid ref,
		 * but use only rules prior to the matched one
		 */
		for (j = 0; j < rules_to_fail; j++) {
			const char *rule = ref_rev_parse_rules[j];
			char refname[PATH_MAX];

			/* skip matched rule */
			if (i == j)
				continue;

			/*
			 * the short name is ambiguous, if it resolves
			 * (with this previous rule) to a valid ref
			 * read_ref() returns 0 on success
			 */
			mksnpath(refname, sizeof(refname),
				 rule, short_name_len, short_name);
			if (ref_exists(refname))
				break;
		}

		/*
		 * short name is non-ambiguous if all previous rules
		 * haven't resolved to a valid ref
		 */
		if (j == rules_to_fail)
			return short_name;
	}

	free(short_name);
	return xstrdup(refname);
}
