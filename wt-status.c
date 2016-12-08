#include "cache.h"
#include "wt-status.h"
#include "object.h"
#include "dir.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "diffcore.h"
#include "quote.h"
#include "run-command.h"
#include "argv-array.h"
#include "remote.h"
#include "refs.h"
#include "submodule.h"
#include "column.h"
#include "strbuf.h"
#include "utf8.h"
#include "worktree.h"
#include "lockfile.h"

static const char cut_line[] =
"------------------------ >8 ------------------------\n";

static char default_wt_status_colors[][COLOR_MAXLEN] = {
	GIT_COLOR_NORMAL, /* WT_STATUS_HEADER */
	GIT_COLOR_GREEN,  /* WT_STATUS_UPDATED */
	GIT_COLOR_RED,    /* WT_STATUS_CHANGED */
	GIT_COLOR_RED,    /* WT_STATUS_UNTRACKED */
	GIT_COLOR_RED,    /* WT_STATUS_NOBRANCH */
	GIT_COLOR_RED,    /* WT_STATUS_UNMERGED */
	GIT_COLOR_GREEN,  /* WT_STATUS_LOCAL_BRANCH */
	GIT_COLOR_RED,    /* WT_STATUS_REMOTE_BRANCH */
	GIT_COLOR_NIL,    /* WT_STATUS_ONBRANCH */
};

static const char *color(int slot, struct wt_status *s)
{
	const char *c = "";
	if (want_color(s->use_color))
		c = s->color_palette[slot];
	if (slot == WT_STATUS_ONBRANCH && color_is_nil(c))
		c = s->color_palette[WT_STATUS_HEADER];
	return c;
}

static void status_vprintf(struct wt_status *s, int at_bol, const char *color,
		const char *fmt, va_list ap, const char *trail)
{
	struct strbuf sb = STRBUF_INIT;
	struct strbuf linebuf = STRBUF_INIT;
	const char *line, *eol;

	strbuf_vaddf(&sb, fmt, ap);
	if (!sb.len) {
		if (s->display_comment_prefix) {
			strbuf_addch(&sb, comment_line_char);
			if (!trail)
				strbuf_addch(&sb, ' ');
		}
		color_print_strbuf(s->fp, color, &sb);
		if (trail)
			fprintf(s->fp, "%s", trail);
		strbuf_release(&sb);
		return;
	}
	for (line = sb.buf; *line; line = eol + 1) {
		eol = strchr(line, '\n');

		strbuf_reset(&linebuf);
		if (at_bol && s->display_comment_prefix) {
			strbuf_addch(&linebuf, comment_line_char);
			if (*line != '\n' && *line != '\t')
				strbuf_addch(&linebuf, ' ');
		}
		if (eol)
			strbuf_add(&linebuf, line, eol - line);
		else
			strbuf_addstr(&linebuf, line);
		color_print_strbuf(s->fp, color, &linebuf);
		if (eol)
			fprintf(s->fp, "\n");
		else
			break;
		at_bol = 1;
	}
	if (trail)
		fprintf(s->fp, "%s", trail);
	strbuf_release(&linebuf);
	strbuf_release(&sb);
}

void status_printf_ln(struct wt_status *s, const char *color,
			const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	status_vprintf(s, 1, color, fmt, ap, "\n");
	va_end(ap);
}

void status_printf(struct wt_status *s, const char *color,
			const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	status_vprintf(s, 1, color, fmt, ap, NULL);
	va_end(ap);
}

static void status_printf_more(struct wt_status *s, const char *color,
			       const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	status_vprintf(s, 0, color, fmt, ap, NULL);
	va_end(ap);
}

void wt_status_prepare(struct wt_status *s)
{
	unsigned char sha1[20];

	memset(s, 0, sizeof(*s));
	memcpy(s->color_palette, default_wt_status_colors,
	       sizeof(default_wt_status_colors));
	s->show_untracked_files = SHOW_NORMAL_UNTRACKED_FILES;
	s->use_color = -1;
	s->relative_paths = 1;
	s->branch = resolve_refdup("HEAD", 0, sha1, NULL);
	s->reference = "HEAD";
	s->fp = stdout;
	s->index_file = get_index_file();
	s->change.strdup_strings = 1;
	s->untracked.strdup_strings = 1;
	s->ignored.strdup_strings = 1;
	s->show_branch = -1;  /* unspecified */
	s->display_comment_prefix = 0;
}

static void wt_longstatus_print_unmerged_header(struct wt_status *s)
{
	int i;
	int del_mod_conflict = 0;
	int both_deleted = 0;
	int not_deleted = 0;
	const char *c = color(WT_STATUS_HEADER, s);

	status_printf_ln(s, c, _("Unmerged paths:"));

	for (i = 0; i < s->change.nr; i++) {
		struct string_list_item *it = &(s->change.items[i]);
		struct wt_status_change_data *d = it->util;

		switch (d->stagemask) {
		case 0:
			break;
		case 1:
			both_deleted = 1;
			break;
		case 3:
		case 5:
			del_mod_conflict = 1;
			break;
		default:
			not_deleted = 1;
			break;
		}
	}

	if (!s->hints)
		return;
	if (s->whence != FROM_COMMIT)
		;
	else if (!s->is_initial)
		status_printf_ln(s, c, _("  (use \"git reset %s <file>...\" to unstage)"), s->reference);
	else
		status_printf_ln(s, c, _("  (use \"git rm --cached <file>...\" to unstage)"));

	if (!both_deleted) {
		if (!del_mod_conflict)
			status_printf_ln(s, c, _("  (use \"git add <file>...\" to mark resolution)"));
		else
			status_printf_ln(s, c, _("  (use \"git add/rm <file>...\" as appropriate to mark resolution)"));
	} else if (!del_mod_conflict && !not_deleted) {
		status_printf_ln(s, c, _("  (use \"git rm <file>...\" to mark resolution)"));
	} else {
		status_printf_ln(s, c, _("  (use \"git add/rm <file>...\" as appropriate to mark resolution)"));
	}
	status_printf_ln(s, c, "%s", "");
}

static void wt_longstatus_print_cached_header(struct wt_status *s)
{
	const char *c = color(WT_STATUS_HEADER, s);

	status_printf_ln(s, c, _("Changes to be committed:"));
	if (!s->hints)
		return;
	if (s->whence != FROM_COMMIT)
		; /* NEEDSWORK: use "git reset --unresolve"??? */
	else if (!s->is_initial)
		status_printf_ln(s, c, _("  (use \"git reset %s <file>...\" to unstage)"), s->reference);
	else
		status_printf_ln(s, c, _("  (use \"git rm --cached <file>...\" to unstage)"));
	status_printf_ln(s, c, "%s", "");
}

static void wt_longstatus_print_dirty_header(struct wt_status *s,
					     int has_deleted,
					     int has_dirty_submodules)
{
	const char *c = color(WT_STATUS_HEADER, s);

	status_printf_ln(s, c, _("Changes not staged for commit:"));
	if (!s->hints)
		return;
	if (!has_deleted)
		status_printf_ln(s, c, _("  (use \"git add <file>...\" to update what will be committed)"));
	else
		status_printf_ln(s, c, _("  (use \"git add/rm <file>...\" to update what will be committed)"));
	status_printf_ln(s, c, _("  (use \"git checkout -- <file>...\" to discard changes in working directory)"));
	if (has_dirty_submodules)
		status_printf_ln(s, c, _("  (commit or discard the untracked or modified content in submodules)"));
	status_printf_ln(s, c, "%s", "");
}

static void wt_longstatus_print_other_header(struct wt_status *s,
					     const char *what,
					     const char *how)
{
	const char *c = color(WT_STATUS_HEADER, s);
	status_printf_ln(s, c, "%s:", what);
	if (!s->hints)
		return;
	status_printf_ln(s, c, _("  (use \"git %s <file>...\" to include in what will be committed)"), how);
	status_printf_ln(s, c, "%s", "");
}

static void wt_longstatus_print_trailer(struct wt_status *s)
{
	status_printf_ln(s, color(WT_STATUS_HEADER, s), "%s", "");
}

#define quote_path quote_path_relative

static const char *wt_status_unmerged_status_string(int stagemask)
{
	switch (stagemask) {
	case 1:
		return _("both deleted:");
	case 2:
		return _("added by us:");
	case 3:
		return _("deleted by them:");
	case 4:
		return _("added by them:");
	case 5:
		return _("deleted by us:");
	case 6:
		return _("both added:");
	case 7:
		return _("both modified:");
	default:
		die("BUG: unhandled unmerged status %x", stagemask);
	}
}

static const char *wt_status_diff_status_string(int status)
{
	switch (status) {
	case DIFF_STATUS_ADDED:
		return _("new file:");
	case DIFF_STATUS_COPIED:
		return _("copied:");
	case DIFF_STATUS_DELETED:
		return _("deleted:");
	case DIFF_STATUS_MODIFIED:
		return _("modified:");
	case DIFF_STATUS_RENAMED:
		return _("renamed:");
	case DIFF_STATUS_TYPE_CHANGED:
		return _("typechange:");
	case DIFF_STATUS_UNKNOWN:
		return _("unknown:");
	case DIFF_STATUS_UNMERGED:
		return _("unmerged:");
	default:
		return NULL;
	}
}

static int maxwidth(const char *(*label)(int), int minval, int maxval)
{
	int result = 0, i;

	for (i = minval; i <= maxval; i++) {
		const char *s = label(i);
		int len = s ? utf8_strwidth(s) : 0;
		if (len > result)
			result = len;
	}
	return result;
}

static void wt_longstatus_print_unmerged_data(struct wt_status *s,
					      struct string_list_item *it)
{
	const char *c = color(WT_STATUS_UNMERGED, s);
	struct wt_status_change_data *d = it->util;
	struct strbuf onebuf = STRBUF_INIT;
	static char *padding;
	static int label_width;
	const char *one, *how;
	int len;

	if (!padding) {
		label_width = maxwidth(wt_status_unmerged_status_string, 1, 7);
		label_width += strlen(" ");
		padding = xmallocz(label_width);
		memset(padding, ' ', label_width);
	}

	one = quote_path(it->string, s->prefix, &onebuf);
	status_printf(s, color(WT_STATUS_HEADER, s), "\t");

	how = wt_status_unmerged_status_string(d->stagemask);
	len = label_width - utf8_strwidth(how);
	status_printf_more(s, c, "%s%.*s%s\n", how, len, padding, one);
	strbuf_release(&onebuf);
}

static void wt_longstatus_print_change_data(struct wt_status *s,
					    int change_type,
					    struct string_list_item *it)
{
	struct wt_status_change_data *d = it->util;
	const char *c = color(change_type, s);
	int status;
	char *one_name;
	char *two_name;
	const char *one, *two;
	struct strbuf onebuf = STRBUF_INIT, twobuf = STRBUF_INIT;
	struct strbuf extra = STRBUF_INIT;
	static char *padding;
	static int label_width;
	const char *what;
	int len;

	if (!padding) {
		/* If DIFF_STATUS_* uses outside the range [A..Z], we're in trouble */
		label_width = maxwidth(wt_status_diff_status_string, 'A', 'Z');
		label_width += strlen(" ");
		padding = xmallocz(label_width);
		memset(padding, ' ', label_width);
	}

	one_name = two_name = it->string;
	switch (change_type) {
	case WT_STATUS_UPDATED:
		status = d->index_status;
		if (d->head_path)
			one_name = d->head_path;
		break;
	case WT_STATUS_CHANGED:
		if (d->new_submodule_commits || d->dirty_submodule) {
			strbuf_addstr(&extra, " (");
			if (d->new_submodule_commits)
				strbuf_addstr(&extra, _("new commits, "));
			if (d->dirty_submodule & DIRTY_SUBMODULE_MODIFIED)
				strbuf_addstr(&extra, _("modified content, "));
			if (d->dirty_submodule & DIRTY_SUBMODULE_UNTRACKED)
				strbuf_addstr(&extra, _("untracked content, "));
			strbuf_setlen(&extra, extra.len - 2);
			strbuf_addch(&extra, ')');
		}
		status = d->worktree_status;
		break;
	default:
		die("BUG: unhandled change_type %d in wt_longstatus_print_change_data",
		    change_type);
	}

	one = quote_path(one_name, s->prefix, &onebuf);
	two = quote_path(two_name, s->prefix, &twobuf);

	status_printf(s, color(WT_STATUS_HEADER, s), "\t");
	what = wt_status_diff_status_string(status);
	if (!what)
		die("BUG: unhandled diff status %c", status);
	len = label_width - utf8_strwidth(what);
	assert(len >= 0);
	if (status == DIFF_STATUS_COPIED || status == DIFF_STATUS_RENAMED)
		status_printf_more(s, c, "%s%.*s%s -> %s",
				   what, len, padding, one, two);
	else
		status_printf_more(s, c, "%s%.*s%s",
				   what, len, padding, one);
	if (extra.len) {
		status_printf_more(s, color(WT_STATUS_HEADER, s), "%s", extra.buf);
		strbuf_release(&extra);
	}
	status_printf_more(s, GIT_COLOR_NORMAL, "\n");
	strbuf_release(&onebuf);
	strbuf_release(&twobuf);
}

static void wt_status_collect_changed_cb(struct diff_queue_struct *q,
					 struct diff_options *options,
					 void *data)
{
	struct wt_status *s = data;
	int i;

	if (!q->nr)
		return;
	s->workdir_dirty = 1;
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p;
		struct string_list_item *it;
		struct wt_status_change_data *d;

		p = q->queue[i];
		it = string_list_insert(&s->change, p->one->path);
		d = it->util;
		if (!d) {
			d = xcalloc(1, sizeof(*d));
			it->util = d;
		}
		if (!d->worktree_status)
			d->worktree_status = p->status;
		d->dirty_submodule = p->two->dirty_submodule;
		if (S_ISGITLINK(p->two->mode))
			d->new_submodule_commits = !!oidcmp(&p->one->oid,
							    &p->two->oid);

		switch (p->status) {
		case DIFF_STATUS_ADDED:
			d->mode_worktree = p->two->mode;
			break;

		case DIFF_STATUS_DELETED:
			d->mode_index = p->one->mode;
			oidcpy(&d->oid_index, &p->one->oid);
			/* mode_worktree is zero for a delete. */
			break;

		case DIFF_STATUS_MODIFIED:
		case DIFF_STATUS_TYPE_CHANGED:
		case DIFF_STATUS_UNMERGED:
			d->mode_index = p->one->mode;
			d->mode_worktree = p->two->mode;
			oidcpy(&d->oid_index, &p->one->oid);
			break;

		case DIFF_STATUS_UNKNOWN:
			die("BUG: worktree status unknown???");
			break;
		}

	}
}

static int unmerged_mask(const char *path)
{
	int pos, mask;
	const struct cache_entry *ce;

	pos = cache_name_pos(path, strlen(path));
	if (0 <= pos)
		return 0;

	mask = 0;
	pos = -pos-1;
	while (pos < active_nr) {
		ce = active_cache[pos++];
		if (strcmp(ce->name, path) || !ce_stage(ce))
			break;
		mask |= (1 << (ce_stage(ce) - 1));
	}
	return mask;
}

static void wt_status_collect_updated_cb(struct diff_queue_struct *q,
					 struct diff_options *options,
					 void *data)
{
	struct wt_status *s = data;
	int i;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p;
		struct string_list_item *it;
		struct wt_status_change_data *d;

		p = q->queue[i];
		it = string_list_insert(&s->change, p->two->path);
		d = it->util;
		if (!d) {
			d = xcalloc(1, sizeof(*d));
			it->util = d;
		}
		if (!d->index_status)
			d->index_status = p->status;
		switch (p->status) {
		case DIFF_STATUS_ADDED:
			/* Leave {mode,oid}_head zero for an add. */
			d->mode_index = p->two->mode;
			oidcpy(&d->oid_index, &p->two->oid);
			break;
		case DIFF_STATUS_DELETED:
			d->mode_head = p->one->mode;
			oidcpy(&d->oid_head, &p->one->oid);
			/* Leave {mode,oid}_index zero for a delete. */
			break;

		case DIFF_STATUS_COPIED:
		case DIFF_STATUS_RENAMED:
			d->head_path = xstrdup(p->one->path);
			d->score = p->score * 100 / MAX_SCORE;
			/* fallthru */
		case DIFF_STATUS_MODIFIED:
		case DIFF_STATUS_TYPE_CHANGED:
			d->mode_head = p->one->mode;
			d->mode_index = p->two->mode;
			oidcpy(&d->oid_head, &p->one->oid);
			oidcpy(&d->oid_index, &p->two->oid);
			break;
		case DIFF_STATUS_UNMERGED:
			d->stagemask = unmerged_mask(p->two->path);
			/*
			 * Don't bother setting {mode,oid}_{head,index} since the print
			 * code will output the stage values directly and not use the
			 * values in these fields.
			 */
			break;
		}
	}
}

static void wt_status_collect_changes_worktree(struct wt_status *s)
{
	struct rev_info rev;

	init_revisions(&rev, NULL);
	setup_revisions(0, NULL, &rev, NULL);
	rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
	DIFF_OPT_SET(&rev.diffopt, DIRTY_SUBMODULES);
	rev.diffopt.ita_invisible_in_index = 1;
	if (!s->show_untracked_files)
		DIFF_OPT_SET(&rev.diffopt, IGNORE_UNTRACKED_IN_SUBMODULES);
	if (s->ignore_submodule_arg) {
		DIFF_OPT_SET(&rev.diffopt, OVERRIDE_SUBMODULE_CONFIG);
		handle_ignore_submodules_arg(&rev.diffopt, s->ignore_submodule_arg);
	}
	rev.diffopt.format_callback = wt_status_collect_changed_cb;
	rev.diffopt.format_callback_data = s;
	copy_pathspec(&rev.prune_data, &s->pathspec);
	run_diff_files(&rev, 0);
}

static void wt_status_collect_changes_index(struct wt_status *s)
{
	struct rev_info rev;
	struct setup_revision_opt opt;

	init_revisions(&rev, NULL);
	memset(&opt, 0, sizeof(opt));
	opt.def = s->is_initial ? EMPTY_TREE_SHA1_HEX : s->reference;
	setup_revisions(0, NULL, &rev, &opt);

	DIFF_OPT_SET(&rev.diffopt, OVERRIDE_SUBMODULE_CONFIG);
	rev.diffopt.ita_invisible_in_index = 1;
	if (s->ignore_submodule_arg) {
		handle_ignore_submodules_arg(&rev.diffopt, s->ignore_submodule_arg);
	} else {
		/*
		 * Unless the user did explicitly request a submodule ignore
		 * mode by passing a command line option we do not ignore any
		 * changed submodule SHA-1s when comparing index and HEAD, no
		 * matter what is configured. Otherwise the user won't be
		 * shown any submodules she manually added (and which are
		 * staged to be committed), which would be really confusing.
		 */
		handle_ignore_submodules_arg(&rev.diffopt, "dirty");
	}

	rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = wt_status_collect_updated_cb;
	rev.diffopt.format_callback_data = s;
	rev.diffopt.detect_rename = 1;
	rev.diffopt.rename_limit = 200;
	rev.diffopt.break_opt = 0;
	copy_pathspec(&rev.prune_data, &s->pathspec);
	run_diff_index(&rev, 1);
}

static void wt_status_collect_changes_initial(struct wt_status *s)
{
	int i;

	for (i = 0; i < active_nr; i++) {
		struct string_list_item *it;
		struct wt_status_change_data *d;
		const struct cache_entry *ce = active_cache[i];

		if (!ce_path_match(ce, &s->pathspec, NULL))
			continue;
		if (ce_intent_to_add(ce))
			continue;
		it = string_list_insert(&s->change, ce->name);
		d = it->util;
		if (!d) {
			d = xcalloc(1, sizeof(*d));
			it->util = d;
		}
		if (ce_stage(ce)) {
			d->index_status = DIFF_STATUS_UNMERGED;
			d->stagemask |= (1 << (ce_stage(ce) - 1));
			/*
			 * Don't bother setting {mode,oid}_{head,index} since the print
			 * code will output the stage values directly and not use the
			 * values in these fields.
			 */
		} else {
			d->index_status = DIFF_STATUS_ADDED;
			/* Leave {mode,oid}_head zero for adds. */
			d->mode_index = ce->ce_mode;
			hashcpy(d->oid_index.hash, ce->oid.hash);
		}
	}
}

static void wt_status_collect_untracked(struct wt_status *s)
{
	int i;
	struct dir_struct dir;
	uint64_t t_begin = getnanotime();

	if (!s->show_untracked_files)
		return;

	memset(&dir, 0, sizeof(dir));
	if (s->show_untracked_files != SHOW_ALL_UNTRACKED_FILES)
		dir.flags |=
			DIR_SHOW_OTHER_DIRECTORIES | DIR_HIDE_EMPTY_DIRECTORIES;
	if (s->show_ignored_files)
		dir.flags |= DIR_SHOW_IGNORED_TOO;
	else
		dir.untracked = the_index.untracked;
	setup_standard_excludes(&dir);

	fill_directory(&dir, &s->pathspec);

	for (i = 0; i < dir.nr; i++) {
		struct dir_entry *ent = dir.entries[i];
		if (cache_name_is_other(ent->name, ent->len) &&
		    dir_path_match(ent, &s->pathspec, 0, NULL))
			string_list_insert(&s->untracked, ent->name);
		free(ent);
	}

	for (i = 0; i < dir.ignored_nr; i++) {
		struct dir_entry *ent = dir.ignored[i];
		if (cache_name_is_other(ent->name, ent->len) &&
		    dir_path_match(ent, &s->pathspec, 0, NULL))
			string_list_insert(&s->ignored, ent->name);
		free(ent);
	}

	free(dir.entries);
	free(dir.ignored);
	clear_directory(&dir);

	if (advice_status_u_option)
		s->untracked_in_ms = (getnanotime() - t_begin) / 1000000;
}

void wt_status_collect(struct wt_status *s)
{
	wt_status_collect_changes_worktree(s);

	if (s->is_initial)
		wt_status_collect_changes_initial(s);
	else
		wt_status_collect_changes_index(s);
	wt_status_collect_untracked(s);
}

static void wt_longstatus_print_unmerged(struct wt_status *s)
{
	int shown_header = 0;
	int i;

	for (i = 0; i < s->change.nr; i++) {
		struct wt_status_change_data *d;
		struct string_list_item *it;
		it = &(s->change.items[i]);
		d = it->util;
		if (!d->stagemask)
			continue;
		if (!shown_header) {
			wt_longstatus_print_unmerged_header(s);
			shown_header = 1;
		}
		wt_longstatus_print_unmerged_data(s, it);
	}
	if (shown_header)
		wt_longstatus_print_trailer(s);

}

static void wt_longstatus_print_updated(struct wt_status *s)
{
	int shown_header = 0;
	int i;

	for (i = 0; i < s->change.nr; i++) {
		struct wt_status_change_data *d;
		struct string_list_item *it;
		it = &(s->change.items[i]);
		d = it->util;
		if (!d->index_status ||
		    d->index_status == DIFF_STATUS_UNMERGED)
			continue;
		if (!shown_header) {
			wt_longstatus_print_cached_header(s);
			s->commitable = 1;
			shown_header = 1;
		}
		wt_longstatus_print_change_data(s, WT_STATUS_UPDATED, it);
	}
	if (shown_header)
		wt_longstatus_print_trailer(s);
}

/*
 * -1 : has delete
 *  0 : no change
 *  1 : some change but no delete
 */
static int wt_status_check_worktree_changes(struct wt_status *s,
					     int *dirty_submodules)
{
	int i;
	int changes = 0;

	*dirty_submodules = 0;

	for (i = 0; i < s->change.nr; i++) {
		struct wt_status_change_data *d;
		d = s->change.items[i].util;
		if (!d->worktree_status ||
		    d->worktree_status == DIFF_STATUS_UNMERGED)
			continue;
		if (!changes)
			changes = 1;
		if (d->dirty_submodule)
			*dirty_submodules = 1;
		if (d->worktree_status == DIFF_STATUS_DELETED)
			changes = -1;
	}
	return changes;
}

static void wt_longstatus_print_changed(struct wt_status *s)
{
	int i, dirty_submodules;
	int worktree_changes = wt_status_check_worktree_changes(s, &dirty_submodules);

	if (!worktree_changes)
		return;

	wt_longstatus_print_dirty_header(s, worktree_changes < 0, dirty_submodules);

	for (i = 0; i < s->change.nr; i++) {
		struct wt_status_change_data *d;
		struct string_list_item *it;
		it = &(s->change.items[i]);
		d = it->util;
		if (!d->worktree_status ||
		    d->worktree_status == DIFF_STATUS_UNMERGED)
			continue;
		wt_longstatus_print_change_data(s, WT_STATUS_CHANGED, it);
	}
	wt_longstatus_print_trailer(s);
}

static void wt_longstatus_print_submodule_summary(struct wt_status *s, int uncommitted)
{
	struct child_process sm_summary = CHILD_PROCESS_INIT;
	struct strbuf cmd_stdout = STRBUF_INIT;
	struct strbuf summary = STRBUF_INIT;
	char *summary_content;

	argv_array_pushf(&sm_summary.env_array, "GIT_INDEX_FILE=%s",
			 s->index_file);

	argv_array_push(&sm_summary.args, "submodule");
	argv_array_push(&sm_summary.args, "summary");
	argv_array_push(&sm_summary.args, uncommitted ? "--files" : "--cached");
	argv_array_push(&sm_summary.args, "--for-status");
	argv_array_push(&sm_summary.args, "--summary-limit");
	argv_array_pushf(&sm_summary.args, "%d", s->submodule_summary);
	if (!uncommitted)
		argv_array_push(&sm_summary.args, s->amend ? "HEAD^" : "HEAD");

	sm_summary.git_cmd = 1;
	sm_summary.no_stdin = 1;

	capture_command(&sm_summary, &cmd_stdout, 1024);

	/* prepend header, only if there's an actual output */
	if (cmd_stdout.len) {
		if (uncommitted)
			strbuf_addstr(&summary, _("Submodules changed but not updated:"));
		else
			strbuf_addstr(&summary, _("Submodule changes to be committed:"));
		strbuf_addstr(&summary, "\n\n");
	}
	strbuf_addbuf(&summary, &cmd_stdout);
	strbuf_release(&cmd_stdout);

	if (s->display_comment_prefix) {
		size_t len;
		summary_content = strbuf_detach(&summary, &len);
		strbuf_add_commented_lines(&summary, summary_content, len);
		free(summary_content);
	}

	fputs(summary.buf, s->fp);
	strbuf_release(&summary);
}

static void wt_longstatus_print_other(struct wt_status *s,
				      struct string_list *l,
				      const char *what,
				      const char *how)
{
	int i;
	struct strbuf buf = STRBUF_INIT;
	static struct string_list output = STRING_LIST_INIT_DUP;
	struct column_options copts;

	if (!l->nr)
		return;

	wt_longstatus_print_other_header(s, what, how);

	for (i = 0; i < l->nr; i++) {
		struct string_list_item *it;
		const char *path;
		it = &(l->items[i]);
		path = quote_path(it->string, s->prefix, &buf);
		if (column_active(s->colopts)) {
			string_list_append(&output, path);
			continue;
		}
		status_printf(s, color(WT_STATUS_HEADER, s), "\t");
		status_printf_more(s, color(WT_STATUS_UNTRACKED, s),
				   "%s\n", path);
	}

	strbuf_release(&buf);
	if (!column_active(s->colopts))
		goto conclude;

	strbuf_addf(&buf, "%s%s\t%s",
		    color(WT_STATUS_HEADER, s),
		    s->display_comment_prefix ? "#" : "",
		    color(WT_STATUS_UNTRACKED, s));
	memset(&copts, 0, sizeof(copts));
	copts.padding = 1;
	copts.indent = buf.buf;
	if (want_color(s->use_color))
		copts.nl = GIT_COLOR_RESET "\n";
	print_columns(&output, s->colopts, &copts);
	string_list_clear(&output, 0);
	strbuf_release(&buf);
conclude:
	status_printf_ln(s, GIT_COLOR_NORMAL, "%s", "");
}

void wt_status_truncate_message_at_cut_line(struct strbuf *buf)
{
	const char *p;
	struct strbuf pattern = STRBUF_INIT;

	strbuf_addf(&pattern, "\n%c %s", comment_line_char, cut_line);
	if (starts_with(buf->buf, pattern.buf + 1))
		strbuf_setlen(buf, 0);
	else if ((p = strstr(buf->buf, pattern.buf)))
		strbuf_setlen(buf, p - buf->buf + 1);
	strbuf_release(&pattern);
}

void wt_status_add_cut_line(FILE *fp)
{
	const char *explanation = _("Do not touch the line above.\nEverything below will be removed.");
	struct strbuf buf = STRBUF_INIT;

	fprintf(fp, "%c %s", comment_line_char, cut_line);
	strbuf_add_commented_lines(&buf, explanation, strlen(explanation));
	fputs(buf.buf, fp);
	strbuf_release(&buf);
}

static void wt_longstatus_print_verbose(struct wt_status *s)
{
	struct rev_info rev;
	struct setup_revision_opt opt;
	int dirty_submodules;
	const char *c = color(WT_STATUS_HEADER, s);

	init_revisions(&rev, NULL);
	DIFF_OPT_SET(&rev.diffopt, ALLOW_TEXTCONV);
	rev.diffopt.ita_invisible_in_index = 1;

	memset(&opt, 0, sizeof(opt));
	opt.def = s->is_initial ? EMPTY_TREE_SHA1_HEX : s->reference;
	setup_revisions(0, NULL, &rev, &opt);

	rev.diffopt.output_format |= DIFF_FORMAT_PATCH;
	rev.diffopt.detect_rename = 1;
	rev.diffopt.file = s->fp;
	rev.diffopt.close_file = 0;
	/*
	 * If we're not going to stdout, then we definitely don't
	 * want color, since we are going to the commit message
	 * file (and even the "auto" setting won't work, since it
	 * will have checked isatty on stdout). But we then do want
	 * to insert the scissor line here to reliably remove the
	 * diff before committing.
	 */
	if (s->fp != stdout) {
		rev.diffopt.use_color = 0;
		wt_status_add_cut_line(s->fp);
	}
	if (s->verbose > 1 && s->commitable) {
		/* print_updated() printed a header, so do we */
		if (s->fp != stdout)
			wt_longstatus_print_trailer(s);
		status_printf_ln(s, c, _("Changes to be committed:"));
		rev.diffopt.a_prefix = "c/";
		rev.diffopt.b_prefix = "i/";
	} /* else use prefix as per user config */
	run_diff_index(&rev, 1);
	if (s->verbose > 1 &&
	    wt_status_check_worktree_changes(s, &dirty_submodules)) {
		status_printf_ln(s, c,
			"--------------------------------------------------");
		status_printf_ln(s, c, _("Changes not staged for commit:"));
		setup_work_tree();
		rev.diffopt.a_prefix = "i/";
		rev.diffopt.b_prefix = "w/";
		run_diff_files(&rev, 0);
	}
}

static void wt_longstatus_print_tracking(struct wt_status *s)
{
	struct strbuf sb = STRBUF_INIT;
	const char *cp, *ep, *branch_name;
	struct branch *branch;
	char comment_line_string[3];
	int i;

	assert(s->branch && !s->is_initial);
	if (!skip_prefix(s->branch, "refs/heads/", &branch_name))
		return;
	branch = branch_get(branch_name);
	if (!format_tracking_info(branch, &sb))
		return;

	i = 0;
	if (s->display_comment_prefix) {
		comment_line_string[i++] = comment_line_char;
		comment_line_string[i++] = ' ';
	}
	comment_line_string[i] = '\0';

	for (cp = sb.buf; (ep = strchr(cp, '\n')) != NULL; cp = ep + 1)
		color_fprintf_ln(s->fp, color(WT_STATUS_HEADER, s),
				 "%s%.*s", comment_line_string,
				 (int)(ep - cp), cp);
	if (s->display_comment_prefix)
		color_fprintf_ln(s->fp, color(WT_STATUS_HEADER, s), "%c",
				 comment_line_char);
	else
		fputs("", s->fp);
}

static int has_unmerged(struct wt_status *s)
{
	int i;

	for (i = 0; i < s->change.nr; i++) {
		struct wt_status_change_data *d;
		d = s->change.items[i].util;
		if (d->stagemask)
			return 1;
	}
	return 0;
}

static void show_merge_in_progress(struct wt_status *s,
				struct wt_status_state *state,
				const char *color)
{
	if (has_unmerged(s)) {
		status_printf_ln(s, color, _("You have unmerged paths."));
		if (s->hints) {
			status_printf_ln(s, color,
					 _("  (fix conflicts and run \"git commit\")"));
			status_printf_ln(s, color,
					 _("  (use \"git merge --abort\" to abort the merge)"));
		}
	} else {
		s-> commitable = 1;
		status_printf_ln(s, color,
			_("All conflicts fixed but you are still merging."));
		if (s->hints)
			status_printf_ln(s, color,
				_("  (use \"git commit\" to conclude merge)"));
	}
	wt_longstatus_print_trailer(s);
}

static void show_am_in_progress(struct wt_status *s,
				struct wt_status_state *state,
				const char *color)
{
	status_printf_ln(s, color,
		_("You are in the middle of an am session."));
	if (state->am_empty_patch)
		status_printf_ln(s, color,
			_("The current patch is empty."));
	if (s->hints) {
		if (!state->am_empty_patch)
			status_printf_ln(s, color,
				_("  (fix conflicts and then run \"git am --continue\")"));
		status_printf_ln(s, color,
			_("  (use \"git am --skip\" to skip this patch)"));
		status_printf_ln(s, color,
			_("  (use \"git am --abort\" to restore the original branch)"));
	}
	wt_longstatus_print_trailer(s);
}

static char *read_line_from_git_path(const char *filename)
{
	struct strbuf buf = STRBUF_INIT;
	FILE *fp = fopen(git_path("%s", filename), "r");
	if (!fp) {
		strbuf_release(&buf);
		return NULL;
	}
	strbuf_getline_lf(&buf, fp);
	if (!fclose(fp)) {
		return strbuf_detach(&buf, NULL);
	} else {
		strbuf_release(&buf);
		return NULL;
	}
}

static int split_commit_in_progress(struct wt_status *s)
{
	int split_in_progress = 0;
	char *head = read_line_from_git_path("HEAD");
	char *orig_head = read_line_from_git_path("ORIG_HEAD");
	char *rebase_amend = read_line_from_git_path("rebase-merge/amend");
	char *rebase_orig_head = read_line_from_git_path("rebase-merge/orig-head");

	if (!head || !orig_head || !rebase_amend || !rebase_orig_head ||
	    !s->branch || strcmp(s->branch, "HEAD"))
		return split_in_progress;

	if (!strcmp(rebase_amend, rebase_orig_head)) {
		if (strcmp(head, rebase_amend))
			split_in_progress = 1;
	} else if (strcmp(orig_head, rebase_orig_head)) {
		split_in_progress = 1;
	}

	if (!s->amend && !s->nowarn && !s->workdir_dirty)
		split_in_progress = 0;

	free(head);
	free(orig_head);
	free(rebase_amend);
	free(rebase_orig_head);
	return split_in_progress;
}

/*
 * Turn
 * "pick d6a2f0303e897ec257dd0e0a39a5ccb709bc2047 some message"
 * into
 * "pick d6a2f03 some message"
 *
 * The function assumes that the line does not contain useless spaces
 * before or after the command.
 */
static void abbrev_sha1_in_line(struct strbuf *line)
{
	struct strbuf **split;
	int i;

	if (starts_with(line->buf, "exec ") ||
	    starts_with(line->buf, "x "))
		return;

	split = strbuf_split_max(line, ' ', 3);
	if (split[0] && split[1]) {
		unsigned char sha1[20];

		/*
		 * strbuf_split_max left a space. Trim it and re-add
		 * it after abbreviation.
		 */
		strbuf_trim(split[1]);
		if (!get_sha1(split[1]->buf, sha1)) {
			strbuf_reset(split[1]);
			strbuf_add_unique_abbrev(split[1], sha1,
						 DEFAULT_ABBREV);
			strbuf_addch(split[1], ' ');
			strbuf_reset(line);
			for (i = 0; split[i]; i++)
				strbuf_addbuf(line, split[i]);
		}
	}
	strbuf_list_free(split);
}

static void read_rebase_todolist(const char *fname, struct string_list *lines)
{
	struct strbuf line = STRBUF_INIT;
	FILE *f = fopen(git_path("%s", fname), "r");

	if (!f)
		die_errno("Could not open file %s for reading",
			  git_path("%s", fname));
	while (!strbuf_getline_lf(&line, f)) {
		if (line.len && line.buf[0] == comment_line_char)
			continue;
		strbuf_trim(&line);
		if (!line.len)
			continue;
		abbrev_sha1_in_line(&line);
		string_list_append(lines, line.buf);
	}
}

static void show_rebase_information(struct wt_status *s,
					struct wt_status_state *state,
					const char *color)
{
	if (state->rebase_interactive_in_progress) {
		int i;
		int nr_lines_to_show = 2;

		struct string_list have_done = STRING_LIST_INIT_DUP;
		struct string_list yet_to_do = STRING_LIST_INIT_DUP;

		read_rebase_todolist("rebase-merge/done", &have_done);
		read_rebase_todolist("rebase-merge/git-rebase-todo", &yet_to_do);

		if (have_done.nr == 0)
			status_printf_ln(s, color, _("No commands done."));
		else {
			status_printf_ln(s, color,
				Q_("Last command done (%d command done):",
					"Last commands done (%d commands done):",
					have_done.nr),
				have_done.nr);
			for (i = (have_done.nr > nr_lines_to_show)
				? have_done.nr - nr_lines_to_show : 0;
				i < have_done.nr;
				i++)
				status_printf_ln(s, color, "   %s", have_done.items[i].string);
			if (have_done.nr > nr_lines_to_show && s->hints)
				status_printf_ln(s, color,
					_("  (see more in file %s)"), git_path("rebase-merge/done"));
		}

		if (yet_to_do.nr == 0)
			status_printf_ln(s, color,
					 _("No commands remaining."));
		else {
			status_printf_ln(s, color,
				Q_("Next command to do (%d remaining command):",
					"Next commands to do (%d remaining commands):",
					yet_to_do.nr),
				yet_to_do.nr);
			for (i = 0; i < nr_lines_to_show && i < yet_to_do.nr; i++)
				status_printf_ln(s, color, "   %s", yet_to_do.items[i].string);
			if (s->hints)
				status_printf_ln(s, color,
					_("  (use \"git rebase --edit-todo\" to view and edit)"));
		}
		string_list_clear(&yet_to_do, 0);
		string_list_clear(&have_done, 0);
	}
}

static void print_rebase_state(struct wt_status *s,
				struct wt_status_state *state,
				const char *color)
{
	if (state->branch)
		status_printf_ln(s, color,
				 _("You are currently rebasing branch '%s' on '%s'."),
				 state->branch,
				 state->onto);
	else
		status_printf_ln(s, color,
				 _("You are currently rebasing."));
}

static void show_rebase_in_progress(struct wt_status *s,
				struct wt_status_state *state,
				const char *color)
{
	struct stat st;

	show_rebase_information(s, state, color);
	if (has_unmerged(s)) {
		print_rebase_state(s, state, color);
		if (s->hints) {
			status_printf_ln(s, color,
				_("  (fix conflicts and then run \"git rebase --continue\")"));
			status_printf_ln(s, color,
				_("  (use \"git rebase --skip\" to skip this patch)"));
			status_printf_ln(s, color,
				_("  (use \"git rebase --abort\" to check out the original branch)"));
		}
	} else if (state->rebase_in_progress || !stat(git_path_merge_msg(), &st)) {
		print_rebase_state(s, state, color);
		if (s->hints)
			status_printf_ln(s, color,
				_("  (all conflicts fixed: run \"git rebase --continue\")"));
	} else if (split_commit_in_progress(s)) {
		if (state->branch)
			status_printf_ln(s, color,
					 _("You are currently splitting a commit while rebasing branch '%s' on '%s'."),
					 state->branch,
					 state->onto);
		else
			status_printf_ln(s, color,
					 _("You are currently splitting a commit during a rebase."));
		if (s->hints)
			status_printf_ln(s, color,
				_("  (Once your working directory is clean, run \"git rebase --continue\")"));
	} else {
		if (state->branch)
			status_printf_ln(s, color,
					 _("You are currently editing a commit while rebasing branch '%s' on '%s'."),
					 state->branch,
					 state->onto);
		else
			status_printf_ln(s, color,
					 _("You are currently editing a commit during a rebase."));
		if (s->hints && !s->amend) {
			status_printf_ln(s, color,
				_("  (use \"git commit --amend\" to amend the current commit)"));
			status_printf_ln(s, color,
				_("  (use \"git rebase --continue\" once you are satisfied with your changes)"));
		}
	}
	wt_longstatus_print_trailer(s);
}

static void show_cherry_pick_in_progress(struct wt_status *s,
					struct wt_status_state *state,
					const char *color)
{
	status_printf_ln(s, color, _("You are currently cherry-picking commit %s."),
			find_unique_abbrev(state->cherry_pick_head_sha1, DEFAULT_ABBREV));
	if (s->hints) {
		if (has_unmerged(s))
			status_printf_ln(s, color,
				_("  (fix conflicts and run \"git cherry-pick --continue\")"));
		else
			status_printf_ln(s, color,
				_("  (all conflicts fixed: run \"git cherry-pick --continue\")"));
		status_printf_ln(s, color,
			_("  (use \"git cherry-pick --abort\" to cancel the cherry-pick operation)"));
	}
	wt_longstatus_print_trailer(s);
}

static void show_revert_in_progress(struct wt_status *s,
					struct wt_status_state *state,
					const char *color)
{
	status_printf_ln(s, color, _("You are currently reverting commit %s."),
			 find_unique_abbrev(state->revert_head_sha1, DEFAULT_ABBREV));
	if (s->hints) {
		if (has_unmerged(s))
			status_printf_ln(s, color,
				_("  (fix conflicts and run \"git revert --continue\")"));
		else
			status_printf_ln(s, color,
				_("  (all conflicts fixed: run \"git revert --continue\")"));
		status_printf_ln(s, color,
			_("  (use \"git revert --abort\" to cancel the revert operation)"));
	}
	wt_longstatus_print_trailer(s);
}

static void show_bisect_in_progress(struct wt_status *s,
				struct wt_status_state *state,
				const char *color)
{
	if (state->branch)
		status_printf_ln(s, color,
				 _("You are currently bisecting, started from branch '%s'."),
				 state->branch);
	else
		status_printf_ln(s, color,
				 _("You are currently bisecting."));
	if (s->hints)
		status_printf_ln(s, color,
			_("  (use \"git bisect reset\" to get back to the original branch)"));
	wt_longstatus_print_trailer(s);
}

/*
 * Extract branch information from rebase/bisect
 */
static char *get_branch(const struct worktree *wt, const char *path)
{
	struct strbuf sb = STRBUF_INIT;
	unsigned char sha1[20];
	const char *branch_name;

	if (strbuf_read_file(&sb, worktree_git_path(wt, "%s", path), 0) <= 0)
		goto got_nothing;

	while (sb.len && sb.buf[sb.len - 1] == '\n')
		strbuf_setlen(&sb, sb.len - 1);
	if (!sb.len)
		goto got_nothing;
	if (skip_prefix(sb.buf, "refs/heads/", &branch_name))
		strbuf_remove(&sb, 0, branch_name - sb.buf);
	else if (starts_with(sb.buf, "refs/"))
		;
	else if (!get_sha1_hex(sb.buf, sha1)) {
		strbuf_reset(&sb);
		strbuf_add_unique_abbrev(&sb, sha1, DEFAULT_ABBREV);
	} else if (!strcmp(sb.buf, "detached HEAD")) /* rebase */
		goto got_nothing;
	else			/* bisect */
		;
	return strbuf_detach(&sb, NULL);

got_nothing:
	strbuf_release(&sb);
	return NULL;
}

struct grab_1st_switch_cbdata {
	struct strbuf buf;
	unsigned char nsha1[20];
};

static int grab_1st_switch(unsigned char *osha1, unsigned char *nsha1,
			   const char *email, unsigned long timestamp, int tz,
			   const char *message, void *cb_data)
{
	struct grab_1st_switch_cbdata *cb = cb_data;
	const char *target = NULL, *end;

	if (!skip_prefix(message, "checkout: moving from ", &message))
		return 0;
	target = strstr(message, " to ");
	if (!target)
		return 0;
	target += strlen(" to ");
	strbuf_reset(&cb->buf);
	hashcpy(cb->nsha1, nsha1);
	end = strchrnul(target, '\n');
	strbuf_add(&cb->buf, target, end - target);
	if (!strcmp(cb->buf.buf, "HEAD")) {
		/* HEAD is relative. Resolve it to the right reflog entry. */
		strbuf_reset(&cb->buf);
		strbuf_add_unique_abbrev(&cb->buf, nsha1, DEFAULT_ABBREV);
	}
	return 1;
}

static void wt_status_get_detached_from(struct wt_status_state *state)
{
	struct grab_1st_switch_cbdata cb;
	struct commit *commit;
	unsigned char sha1[20];
	char *ref = NULL;

	strbuf_init(&cb.buf, 0);
	if (for_each_reflog_ent_reverse("HEAD", grab_1st_switch, &cb) <= 0) {
		strbuf_release(&cb.buf);
		return;
	}

	if (dwim_ref(cb.buf.buf, cb.buf.len, sha1, &ref) == 1 &&
	    /* sha1 is a commit? match without further lookup */
	    (!hashcmp(cb.nsha1, sha1) ||
	     /* perhaps sha1 is a tag, try to dereference to a commit */
	     ((commit = lookup_commit_reference_gently(sha1, 1)) != NULL &&
	      !hashcmp(cb.nsha1, commit->object.oid.hash)))) {
		const char *from = ref;
		if (!skip_prefix(from, "refs/tags/", &from))
			skip_prefix(from, "refs/remotes/", &from);
		state->detached_from = xstrdup(from);
	} else
		state->detached_from =
			xstrdup(find_unique_abbrev(cb.nsha1, DEFAULT_ABBREV));
	hashcpy(state->detached_sha1, cb.nsha1);
	state->detached_at = !get_sha1("HEAD", sha1) &&
			     !hashcmp(sha1, state->detached_sha1);

	free(ref);
	strbuf_release(&cb.buf);
}

int wt_status_check_rebase(const struct worktree *wt,
			   struct wt_status_state *state)
{
	struct stat st;

	if (!stat(worktree_git_path(wt, "rebase-apply"), &st)) {
		if (!stat(worktree_git_path(wt, "rebase-apply/applying"), &st)) {
			state->am_in_progress = 1;
			if (!stat(worktree_git_path(wt, "rebase-apply/patch"), &st) && !st.st_size)
				state->am_empty_patch = 1;
		} else {
			state->rebase_in_progress = 1;
			state->branch = get_branch(wt, "rebase-apply/head-name");
			state->onto = get_branch(wt, "rebase-apply/onto");
		}
	} else if (!stat(worktree_git_path(wt, "rebase-merge"), &st)) {
		if (!stat(worktree_git_path(wt, "rebase-merge/interactive"), &st))
			state->rebase_interactive_in_progress = 1;
		else
			state->rebase_in_progress = 1;
		state->branch = get_branch(wt, "rebase-merge/head-name");
		state->onto = get_branch(wt, "rebase-merge/onto");
	} else
		return 0;
	return 1;
}

int wt_status_check_bisect(const struct worktree *wt,
			   struct wt_status_state *state)
{
	struct stat st;

	if (!stat(worktree_git_path(wt, "BISECT_LOG"), &st)) {
		state->bisect_in_progress = 1;
		state->branch = get_branch(wt, "BISECT_START");
		return 1;
	}
	return 0;
}

void wt_status_get_state(struct wt_status_state *state,
			 int get_detached_from)
{
	struct stat st;
	unsigned char sha1[20];

	if (!stat(git_path_merge_head(), &st)) {
		state->merge_in_progress = 1;
	} else if (wt_status_check_rebase(NULL, state)) {
		;		/* all set */
	} else if (!stat(git_path_cherry_pick_head(), &st) &&
			!get_sha1("CHERRY_PICK_HEAD", sha1)) {
		state->cherry_pick_in_progress = 1;
		hashcpy(state->cherry_pick_head_sha1, sha1);
	}
	wt_status_check_bisect(NULL, state);
	if (!stat(git_path_revert_head(), &st) &&
	    !get_sha1("REVERT_HEAD", sha1)) {
		state->revert_in_progress = 1;
		hashcpy(state->revert_head_sha1, sha1);
	}

	if (get_detached_from)
		wt_status_get_detached_from(state);
}

static void wt_longstatus_print_state(struct wt_status *s,
				      struct wt_status_state *state)
{
	const char *state_color = color(WT_STATUS_HEADER, s);
	if (state->merge_in_progress)
		show_merge_in_progress(s, state, state_color);
	else if (state->am_in_progress)
		show_am_in_progress(s, state, state_color);
	else if (state->rebase_in_progress || state->rebase_interactive_in_progress)
		show_rebase_in_progress(s, state, state_color);
	else if (state->cherry_pick_in_progress)
		show_cherry_pick_in_progress(s, state, state_color);
	else if (state->revert_in_progress)
		show_revert_in_progress(s, state, state_color);
	if (state->bisect_in_progress)
		show_bisect_in_progress(s, state, state_color);
}

static void wt_longstatus_print(struct wt_status *s)
{
	const char *branch_color = color(WT_STATUS_ONBRANCH, s);
	const char *branch_status_color = color(WT_STATUS_HEADER, s);
	struct wt_status_state state;

	memset(&state, 0, sizeof(state));
	wt_status_get_state(&state,
			    s->branch && !strcmp(s->branch, "HEAD"));

	if (s->branch) {
		const char *on_what = _("On branch ");
		const char *branch_name = s->branch;
		if (!strcmp(branch_name, "HEAD")) {
			branch_status_color = color(WT_STATUS_NOBRANCH, s);
			if (state.rebase_in_progress || state.rebase_interactive_in_progress) {
				if (state.rebase_interactive_in_progress)
					on_what = _("interactive rebase in progress; onto ");
				else
					on_what = _("rebase in progress; onto ");
				branch_name = state.onto;
			} else if (state.detached_from) {
				branch_name = state.detached_from;
				if (state.detached_at)
					on_what = _("HEAD detached at ");
				else
					on_what = _("HEAD detached from ");
			} else {
				branch_name = "";
				on_what = _("Not currently on any branch.");
			}
		} else
			skip_prefix(branch_name, "refs/heads/", &branch_name);
		status_printf(s, color(WT_STATUS_HEADER, s), "%s", "");
		status_printf_more(s, branch_status_color, "%s", on_what);
		status_printf_more(s, branch_color, "%s\n", branch_name);
		if (!s->is_initial)
			wt_longstatus_print_tracking(s);
	}

	wt_longstatus_print_state(s, &state);
	free(state.branch);
	free(state.onto);
	free(state.detached_from);

	if (s->is_initial) {
		status_printf_ln(s, color(WT_STATUS_HEADER, s), "%s", "");
		status_printf_ln(s, color(WT_STATUS_HEADER, s), _("Initial commit"));
		status_printf_ln(s, color(WT_STATUS_HEADER, s), "%s", "");
	}

	wt_longstatus_print_updated(s);
	wt_longstatus_print_unmerged(s);
	wt_longstatus_print_changed(s);
	if (s->submodule_summary &&
	    (!s->ignore_submodule_arg ||
	     strcmp(s->ignore_submodule_arg, "all"))) {
		wt_longstatus_print_submodule_summary(s, 0);  /* staged */
		wt_longstatus_print_submodule_summary(s, 1);  /* unstaged */
	}
	if (s->show_untracked_files) {
		wt_longstatus_print_other(s, &s->untracked, _("Untracked files"), "add");
		if (s->show_ignored_files)
			wt_longstatus_print_other(s, &s->ignored, _("Ignored files"), "add -f");
		if (advice_status_u_option && 2000 < s->untracked_in_ms) {
			status_printf_ln(s, GIT_COLOR_NORMAL, "%s", "");
			status_printf_ln(s, GIT_COLOR_NORMAL,
					 _("It took %.2f seconds to enumerate untracked files. 'status -uno'\n"
					   "may speed it up, but you have to be careful not to forget to add\n"
					   "new files yourself (see 'git help status')."),
					 s->untracked_in_ms / 1000.0);
		}
	} else if (s->commitable)
		status_printf_ln(s, GIT_COLOR_NORMAL, _("Untracked files not listed%s"),
			s->hints
			? _(" (use -u option to show untracked files)") : "");

	if (s->verbose)
		wt_longstatus_print_verbose(s);
	if (!s->commitable) {
		if (s->amend)
			status_printf_ln(s, GIT_COLOR_NORMAL, _("No changes"));
		else if (s->nowarn)
			; /* nothing */
		else if (s->workdir_dirty) {
			if (s->hints)
				printf(_("no changes added to commit "
					 "(use \"git add\" and/or \"git commit -a\")\n"));
			else
				printf(_("no changes added to commit\n"));
		} else if (s->untracked.nr) {
			if (s->hints)
				printf(_("nothing added to commit but untracked files "
					 "present (use \"git add\" to track)\n"));
			else
				printf(_("nothing added to commit but untracked files present\n"));
		} else if (s->is_initial) {
			if (s->hints)
				printf(_("nothing to commit (create/copy files "
					 "and use \"git add\" to track)\n"));
			else
				printf(_("nothing to commit\n"));
		} else if (!s->show_untracked_files) {
			if (s->hints)
				printf(_("nothing to commit (use -u to show untracked files)\n"));
			else
				printf(_("nothing to commit\n"));
		} else
			printf(_("nothing to commit, working tree clean\n"));
	}
}

static void wt_shortstatus_unmerged(struct string_list_item *it,
			   struct wt_status *s)
{
	struct wt_status_change_data *d = it->util;
	const char *how = "??";

	switch (d->stagemask) {
	case 1: how = "DD"; break; /* both deleted */
	case 2: how = "AU"; break; /* added by us */
	case 3: how = "UD"; break; /* deleted by them */
	case 4: how = "UA"; break; /* added by them */
	case 5: how = "DU"; break; /* deleted by us */
	case 6: how = "AA"; break; /* both added */
	case 7: how = "UU"; break; /* both modified */
	}
	color_fprintf(s->fp, color(WT_STATUS_UNMERGED, s), "%s", how);
	if (s->null_termination) {
		fprintf(stdout, " %s%c", it->string, 0);
	} else {
		struct strbuf onebuf = STRBUF_INIT;
		const char *one;
		one = quote_path(it->string, s->prefix, &onebuf);
		printf(" %s\n", one);
		strbuf_release(&onebuf);
	}
}

static void wt_shortstatus_status(struct string_list_item *it,
			 struct wt_status *s)
{
	struct wt_status_change_data *d = it->util;

	if (d->index_status)
		color_fprintf(s->fp, color(WT_STATUS_UPDATED, s), "%c", d->index_status);
	else
		putchar(' ');
	if (d->worktree_status)
		color_fprintf(s->fp, color(WT_STATUS_CHANGED, s), "%c", d->worktree_status);
	else
		putchar(' ');
	putchar(' ');
	if (s->null_termination) {
		fprintf(stdout, "%s%c", it->string, 0);
		if (d->head_path)
			fprintf(stdout, "%s%c", d->head_path, 0);
	} else {
		struct strbuf onebuf = STRBUF_INIT;
		const char *one;
		if (d->head_path) {
			one = quote_path(d->head_path, s->prefix, &onebuf);
			if (*one != '"' && strchr(one, ' ') != NULL) {
				putchar('"');
				strbuf_addch(&onebuf, '"');
				one = onebuf.buf;
			}
			printf("%s -> ", one);
			strbuf_release(&onebuf);
		}
		one = quote_path(it->string, s->prefix, &onebuf);
		if (*one != '"' && strchr(one, ' ') != NULL) {
			putchar('"');
			strbuf_addch(&onebuf, '"');
			one = onebuf.buf;
		}
		printf("%s\n", one);
		strbuf_release(&onebuf);
	}
}

static void wt_shortstatus_other(struct string_list_item *it,
				 struct wt_status *s, const char *sign)
{
	if (s->null_termination) {
		fprintf(stdout, "%s %s%c", sign, it->string, 0);
	} else {
		struct strbuf onebuf = STRBUF_INIT;
		const char *one;
		one = quote_path(it->string, s->prefix, &onebuf);
		color_fprintf(s->fp, color(WT_STATUS_UNTRACKED, s), "%s", sign);
		printf(" %s\n", one);
		strbuf_release(&onebuf);
	}
}

static void wt_shortstatus_print_tracking(struct wt_status *s)
{
	struct branch *branch;
	const char *header_color = color(WT_STATUS_HEADER, s);
	const char *branch_color_local = color(WT_STATUS_LOCAL_BRANCH, s);
	const char *branch_color_remote = color(WT_STATUS_REMOTE_BRANCH, s);

	const char *base;
	const char *branch_name;
	int num_ours, num_theirs;
	int upstream_is_gone = 0;

	color_fprintf(s->fp, color(WT_STATUS_HEADER, s), "## ");

	if (!s->branch)
		return;
	branch_name = s->branch;

	if (s->is_initial)
		color_fprintf(s->fp, header_color, _("Initial commit on "));

	if (!strcmp(s->branch, "HEAD")) {
		color_fprintf(s->fp, color(WT_STATUS_NOBRANCH, s), "%s",
			      _("HEAD (no branch)"));
		goto conclude;
	}

	skip_prefix(branch_name, "refs/heads/", &branch_name);

	branch = branch_get(branch_name);

	color_fprintf(s->fp, branch_color_local, "%s", branch_name);

	if (stat_tracking_info(branch, &num_ours, &num_theirs, &base) < 0) {
		if (!base)
			goto conclude;

		upstream_is_gone = 1;
	}

	base = shorten_unambiguous_ref(base, 0);
	color_fprintf(s->fp, header_color, "...");
	color_fprintf(s->fp, branch_color_remote, "%s", base);
	free((char *)base);

	if (!upstream_is_gone && !num_ours && !num_theirs)
		goto conclude;

#define LABEL(string) (s->no_gettext ? (string) : _(string))

	color_fprintf(s->fp, header_color, " [");
	if (upstream_is_gone) {
		color_fprintf(s->fp, header_color, LABEL(N_("gone")));
	} else if (!num_ours) {
		color_fprintf(s->fp, header_color, LABEL(N_("behind ")));
		color_fprintf(s->fp, branch_color_remote, "%d", num_theirs);
	} else if (!num_theirs) {
		color_fprintf(s->fp, header_color, LABEL(N_("ahead ")));
		color_fprintf(s->fp, branch_color_local, "%d", num_ours);
	} else {
		color_fprintf(s->fp, header_color, LABEL(N_("ahead ")));
		color_fprintf(s->fp, branch_color_local, "%d", num_ours);
		color_fprintf(s->fp, header_color, ", %s", LABEL(N_("behind ")));
		color_fprintf(s->fp, branch_color_remote, "%d", num_theirs);
	}

	color_fprintf(s->fp, header_color, "]");
 conclude:
	fputc(s->null_termination ? '\0' : '\n', s->fp);
}

static void wt_shortstatus_print(struct wt_status *s)
{
	int i;

	if (s->show_branch)
		wt_shortstatus_print_tracking(s);

	for (i = 0; i < s->change.nr; i++) {
		struct wt_status_change_data *d;
		struct string_list_item *it;

		it = &(s->change.items[i]);
		d = it->util;
		if (d->stagemask)
			wt_shortstatus_unmerged(it, s);
		else
			wt_shortstatus_status(it, s);
	}
	for (i = 0; i < s->untracked.nr; i++) {
		struct string_list_item *it;

		it = &(s->untracked.items[i]);
		wt_shortstatus_other(it, s, "??");
	}
	for (i = 0; i < s->ignored.nr; i++) {
		struct string_list_item *it;

		it = &(s->ignored.items[i]);
		wt_shortstatus_other(it, s, "!!");
	}
}

static void wt_porcelain_print(struct wt_status *s)
{
	s->use_color = 0;
	s->relative_paths = 0;
	s->prefix = NULL;
	s->no_gettext = 1;
	wt_shortstatus_print(s);
}

/*
 * Print branch information for porcelain v2 output.  These lines
 * are printed when the '--branch' parameter is given.
 *
 *    # branch.oid <commit><eol>
 *    # branch.head <head><eol>
 *   [# branch.upstream <upstream><eol>
 *   [# branch.ab +<ahead> -<behind><eol>]]
 *
 *      <commit> ::= the current commit hash or the the literal
 *                   "(initial)" to indicate an initialized repo
 *                   with no commits.
 *
 *        <head> ::= <branch_name> the current branch name or
 *                   "(detached)" literal when detached head or
 *                   "(unknown)" when something is wrong.
 *
 *    <upstream> ::= the upstream branch name, when set.
 *
 *       <ahead> ::= integer ahead value, when upstream set
 *                   and the commit is present (not gone).
 *
 *      <behind> ::= integer behind value, when upstream set
 *                   and commit is present.
 *
 *
 * The end-of-line is defined by the -z flag.
 *
 *                 <eol> ::= NUL when -z,
 *                           LF when NOT -z.
 *
 */
static void wt_porcelain_v2_print_tracking(struct wt_status *s)
{
	struct branch *branch;
	const char *base;
	const char *branch_name;
	struct wt_status_state state;
	int ab_info, nr_ahead, nr_behind;
	char eol = s->null_termination ? '\0' : '\n';

	memset(&state, 0, sizeof(state));
	wt_status_get_state(&state, s->branch && !strcmp(s->branch, "HEAD"));

	fprintf(s->fp, "# branch.oid %s%c",
			(s->is_initial ? "(initial)" : sha1_to_hex(s->sha1_commit)),
			eol);

	if (!s->branch)
		fprintf(s->fp, "# branch.head %s%c", "(unknown)", eol);
	else {
		if (!strcmp(s->branch, "HEAD")) {
			fprintf(s->fp, "# branch.head %s%c", "(detached)", eol);

			if (state.rebase_in_progress || state.rebase_interactive_in_progress)
				branch_name = state.onto;
			else if (state.detached_from)
				branch_name = state.detached_from;
			else
				branch_name = "";
		} else {
			branch_name = NULL;
			skip_prefix(s->branch, "refs/heads/", &branch_name);

			fprintf(s->fp, "# branch.head %s%c", branch_name, eol);
		}

		/* Lookup stats on the upstream tracking branch, if set. */
		branch = branch_get(branch_name);
		base = NULL;
		ab_info = (stat_tracking_info(branch, &nr_ahead, &nr_behind, &base) == 0);
		if (base) {
			base = shorten_unambiguous_ref(base, 0);
			fprintf(s->fp, "# branch.upstream %s%c", base, eol);
			free((char *)base);

			if (ab_info)
				fprintf(s->fp, "# branch.ab +%d -%d%c", nr_ahead, nr_behind, eol);
		}
	}

	free(state.branch);
	free(state.onto);
	free(state.detached_from);
}

/*
 * Convert various submodule status values into a
 * fixed-length string of characters in the buffer provided.
 */
static void wt_porcelain_v2_submodule_state(
	struct wt_status_change_data *d,
	char sub[5])
{
	if (S_ISGITLINK(d->mode_head) ||
		S_ISGITLINK(d->mode_index) ||
		S_ISGITLINK(d->mode_worktree)) {
		sub[0] = 'S';
		sub[1] = d->new_submodule_commits ? 'C' : '.';
		sub[2] = (d->dirty_submodule & DIRTY_SUBMODULE_MODIFIED) ? 'M' : '.';
		sub[3] = (d->dirty_submodule & DIRTY_SUBMODULE_UNTRACKED) ? 'U' : '.';
	} else {
		sub[0] = 'N';
		sub[1] = '.';
		sub[2] = '.';
		sub[3] = '.';
	}
	sub[4] = 0;
}

/*
 * Fix-up changed entries before we print them.
 */
static void wt_porcelain_v2_fix_up_changed(
	struct string_list_item *it,
	struct wt_status *s)
{
	struct wt_status_change_data *d = it->util;

	if (!d->index_status) {
		/*
		 * This entry is unchanged in the index (relative to the head).
		 * Therefore, the collect_updated_cb was never called for this
		 * entry (during the head-vs-index scan) and so the head column
		 * fields were never set.
		 *
		 * We must have data for the index column (from the
		 * index-vs-worktree scan (otherwise, this entry should not be
		 * in the list of changes)).
		 *
		 * Copy index column fields to the head column, so that our
		 * output looks complete.
		 */
		assert(d->mode_head == 0);
		d->mode_head = d->mode_index;
		oidcpy(&d->oid_head, &d->oid_index);
	}

	if (!d->worktree_status) {
		/*
		 * This entry is unchanged in the worktree (relative to the index).
		 * Therefore, the collect_changed_cb was never called for this entry
		 * (during the index-vs-worktree scan) and so the worktree column
		 * fields were never set.
		 *
		 * We must have data for the index column (from the head-vs-index
		 * scan).
		 *
		 * Copy the index column fields to the worktree column so that
		 * our output looks complete.
		 *
		 * Note that we only have a mode field in the worktree column
		 * because the scan code tries really hard to not have to compute it.
		 */
		assert(d->mode_worktree == 0);
		d->mode_worktree = d->mode_index;
	}
}

/*
 * Print porcelain v2 info for tracked entries with changes.
 */
static void wt_porcelain_v2_print_changed_entry(
	struct string_list_item *it,
	struct wt_status *s)
{
	struct wt_status_change_data *d = it->util;
	struct strbuf buf_index = STRBUF_INIT;
	struct strbuf buf_head = STRBUF_INIT;
	const char *path_index = NULL;
	const char *path_head = NULL;
	char key[3];
	char submodule_token[5];
	char sep_char, eol_char;

	wt_porcelain_v2_fix_up_changed(it, s);
	wt_porcelain_v2_submodule_state(d, submodule_token);

	key[0] = d->index_status ? d->index_status : '.';
	key[1] = d->worktree_status ? d->worktree_status : '.';
	key[2] = 0;

	if (s->null_termination) {
		/*
		 * In -z mode, we DO NOT C-quote pathnames.  Current path is ALWAYS first.
		 * A single NUL character separates them.
		 */
		sep_char = '\0';
		eol_char = '\0';
		path_index = it->string;
		path_head = d->head_path;
	} else {
		/*
		 * Path(s) are C-quoted if necessary. Current path is ALWAYS first.
		 * The source path is only present when necessary.
		 * A single TAB separates them (because paths can contain spaces
		 * which are not escaped and C-quoting does escape TAB characters).
		 */
		sep_char = '\t';
		eol_char = '\n';
		path_index = quote_path(it->string, s->prefix, &buf_index);
		if (d->head_path)
			path_head = quote_path(d->head_path, s->prefix, &buf_head);
	}

	if (path_head)
		fprintf(s->fp, "2 %s %s %06o %06o %06o %s %s %c%d %s%c%s%c",
				key, submodule_token,
				d->mode_head, d->mode_index, d->mode_worktree,
				oid_to_hex(&d->oid_head), oid_to_hex(&d->oid_index),
				key[0], d->score,
				path_index, sep_char, path_head, eol_char);
	else
		fprintf(s->fp, "1 %s %s %06o %06o %06o %s %s %s%c",
				key, submodule_token,
				d->mode_head, d->mode_index, d->mode_worktree,
				oid_to_hex(&d->oid_head), oid_to_hex(&d->oid_index),
				path_index, eol_char);

	strbuf_release(&buf_index);
	strbuf_release(&buf_head);
}

/*
 * Print porcelain v2 status info for unmerged entries.
 */
static void wt_porcelain_v2_print_unmerged_entry(
	struct string_list_item *it,
	struct wt_status *s)
{
	struct wt_status_change_data *d = it->util;
	const struct cache_entry *ce;
	struct strbuf buf_index = STRBUF_INIT;
	const char *path_index = NULL;
	int pos, stage, sum;
	struct {
		int mode;
		struct object_id oid;
	} stages[3];
	char *key;
	char submodule_token[5];
	char unmerged_prefix = 'u';
	char eol_char = s->null_termination ? '\0' : '\n';

	wt_porcelain_v2_submodule_state(d, submodule_token);

	switch (d->stagemask) {
	case 1: key = "DD"; break; /* both deleted */
	case 2: key = "AU"; break; /* added by us */
	case 3: key = "UD"; break; /* deleted by them */
	case 4: key = "UA"; break; /* added by them */
	case 5: key = "DU"; break; /* deleted by us */
	case 6: key = "AA"; break; /* both added */
	case 7: key = "UU"; break; /* both modified */
	default:
		die("BUG: unhandled unmerged status %x", d->stagemask);
	}

	/*
	 * Disregard d.aux.porcelain_v2 data that we accumulated
	 * for the head and index columns during the scans and
	 * replace with the actual stage data.
	 *
	 * Note that this is a last-one-wins for each the individual
	 * stage [123] columns in the event of multiple cache entries
	 * for same stage.
	 */
	memset(stages, 0, sizeof(stages));
	sum = 0;
	pos = cache_name_pos(it->string, strlen(it->string));
	assert(pos < 0);
	pos = -pos-1;
	while (pos < active_nr) {
		ce = active_cache[pos++];
		stage = ce_stage(ce);
		if (strcmp(ce->name, it->string) || !stage)
			break;
		stages[stage - 1].mode = ce->ce_mode;
		hashcpy(stages[stage - 1].oid.hash, ce->oid.hash);
		sum |= (1 << (stage - 1));
	}
	if (sum != d->stagemask)
		die("BUG: observed stagemask 0x%x != expected stagemask 0x%x", sum, d->stagemask);

	if (s->null_termination)
		path_index = it->string;
	else
		path_index = quote_path(it->string, s->prefix, &buf_index);

	fprintf(s->fp, "%c %s %s %06o %06o %06o %06o %s %s %s %s%c",
			unmerged_prefix, key, submodule_token,
			stages[0].mode, /* stage 1 */
			stages[1].mode, /* stage 2 */
			stages[2].mode, /* stage 3 */
			d->mode_worktree,
			oid_to_hex(&stages[0].oid), /* stage 1 */
			oid_to_hex(&stages[1].oid), /* stage 2 */
			oid_to_hex(&stages[2].oid), /* stage 3 */
			path_index,
			eol_char);

	strbuf_release(&buf_index);
}

/*
 * Print porcelain V2 status info for untracked and ignored entries.
 */
static void wt_porcelain_v2_print_other(
	struct string_list_item *it,
	struct wt_status *s,
	char prefix)
{
	struct strbuf buf = STRBUF_INIT;
	const char *path;
	char eol_char;

	if (s->null_termination) {
		path = it->string;
		eol_char = '\0';
	} else {
		path = quote_path(it->string, s->prefix, &buf);
		eol_char = '\n';
	}

	fprintf(s->fp, "%c %s%c", prefix, path, eol_char);

	strbuf_release(&buf);
}

/*
 * Print porcelain V2 status.
 *
 * [<v2_branch>]
 * [<v2_changed_items>]*
 * [<v2_unmerged_items>]*
 * [<v2_untracked_items>]*
 * [<v2_ignored_items>]*
 *
 */
static void wt_porcelain_v2_print(struct wt_status *s)
{
	struct wt_status_change_data *d;
	struct string_list_item *it;
	int i;

	if (s->show_branch)
		wt_porcelain_v2_print_tracking(s);

	for (i = 0; i < s->change.nr; i++) {
		it = &(s->change.items[i]);
		d = it->util;
		if (!d->stagemask)
			wt_porcelain_v2_print_changed_entry(it, s);
	}

	for (i = 0; i < s->change.nr; i++) {
		it = &(s->change.items[i]);
		d = it->util;
		if (d->stagemask)
			wt_porcelain_v2_print_unmerged_entry(it, s);
	}

	for (i = 0; i < s->untracked.nr; i++) {
		it = &(s->untracked.items[i]);
		wt_porcelain_v2_print_other(it, s, '?');
	}

	for (i = 0; i < s->ignored.nr; i++) {
		it = &(s->ignored.items[i]);
		wt_porcelain_v2_print_other(it, s, '!');
	}
}

void wt_status_print(struct wt_status *s)
{
	switch (s->status_format) {
	case STATUS_FORMAT_SHORT:
		wt_shortstatus_print(s);
		break;
	case STATUS_FORMAT_PORCELAIN:
		wt_porcelain_print(s);
		break;
	case STATUS_FORMAT_PORCELAIN_V2:
		wt_porcelain_v2_print(s);
		break;
	case STATUS_FORMAT_UNSPECIFIED:
		die("BUG: finalize_deferred_config() should have been called");
		break;
	case STATUS_FORMAT_NONE:
	case STATUS_FORMAT_LONG:
		wt_longstatus_print(s);
		break;
	}
}

/**
 * Returns 1 if there are unstaged changes, 0 otherwise.
 */
int has_unstaged_changes(int ignore_submodules)
{
	struct rev_info rev_info;
	int result;

	init_revisions(&rev_info, NULL);
	if (ignore_submodules)
		DIFF_OPT_SET(&rev_info.diffopt, IGNORE_SUBMODULES);
	DIFF_OPT_SET(&rev_info.diffopt, QUICK);
	diff_setup_done(&rev_info.diffopt);
	result = run_diff_files(&rev_info, 0);
	return diff_result_code(&rev_info.diffopt, result);
}

/**
 * Returns 1 if there are uncommitted changes, 0 otherwise.
 */
int has_uncommitted_changes(int ignore_submodules)
{
	struct rev_info rev_info;
	int result;

	if (is_cache_unborn())
		return 0;

	init_revisions(&rev_info, NULL);
	if (ignore_submodules)
		DIFF_OPT_SET(&rev_info.diffopt, IGNORE_SUBMODULES);
	DIFF_OPT_SET(&rev_info.diffopt, QUICK);
	add_head_to_pending(&rev_info);
	diff_setup_done(&rev_info.diffopt);
	result = run_diff_index(&rev_info, 1);
	return diff_result_code(&rev_info.diffopt, result);
}

/**
 * If the work tree has unstaged or uncommitted changes, dies with the
 * appropriate message.
 */
int require_clean_work_tree(const char *action, const char *hint, int ignore_submodules, int gently)
{
	struct lock_file *lock_file = xcalloc(1, sizeof(*lock_file));
	int err = 0;

	hold_locked_index(lock_file, 0);
	refresh_cache(REFRESH_QUIET);
	update_index_if_able(&the_index, lock_file);
	rollback_lock_file(lock_file);

	if (has_unstaged_changes(ignore_submodules)) {
		/* TRANSLATORS: the action is e.g. "pull with rebase" */
		error(_("cannot %s: You have unstaged changes."), _(action));
		err = 1;
	}

	if (has_uncommitted_changes(ignore_submodules)) {
		if (err)
			error(_("additionally, your index contains uncommitted changes."));
		else
			error(_("cannot %s: Your index contains uncommitted changes."),
			      _(action));
		err = 1;
	}

	if (err) {
		if (hint)
			error("%s", hint);
		if (!gently)
			exit(128);
	}

	return err;
}
