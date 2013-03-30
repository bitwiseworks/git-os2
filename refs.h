#ifndef REFS_H
#define REFS_H

struct ref_lock {
	char *ref_name;
	char *orig_ref_name;
	struct lock_file *lk;
	unsigned char old_sha1[20];
	int lock_fd;
	int force_write;
};

#define REF_ISSYMREF 0x01
#define REF_ISPACKED 0x02
#define REF_ISBROKEN 0x04

/*
 * Calls the specified function for each ref file until it returns nonzero,
 * and returns the value
 */
typedef int each_ref_fn(const char *refname, const unsigned char *sha1, int flags, void *cb_data);
extern int head_ref(each_ref_fn, void *);
extern int for_each_ref(each_ref_fn, void *);
extern int for_each_ref_in(const char *, each_ref_fn, void *);
extern int for_each_tag_ref(each_ref_fn, void *);
extern int for_each_branch_ref(each_ref_fn, void *);
extern int for_each_remote_ref(each_ref_fn, void *);
extern int for_each_replace_ref(each_ref_fn, void *);
extern int for_each_glob_ref(each_ref_fn, const char *pattern, void *);
extern int for_each_glob_ref_in(each_ref_fn, const char *pattern, const char* prefix, void *);

extern int head_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data);
extern int for_each_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data);
extern int for_each_ref_in_submodule(const char *submodule, const char *prefix,
		each_ref_fn fn, void *cb_data);
extern int for_each_tag_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data);
extern int for_each_branch_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data);
extern int for_each_remote_ref_submodule(const char *submodule, each_ref_fn fn, void *cb_data);

extern int head_ref_namespaced(each_ref_fn fn, void *cb_data);
extern int for_each_namespaced_ref(each_ref_fn fn, void *cb_data);

static inline const char *has_glob_specials(const char *pattern)
{
	return strpbrk(pattern, "?*[");
}

/* can be used to learn about broken ref and symref */
extern int for_each_rawref(each_ref_fn, void *);

extern void warn_dangling_symref(FILE *fp, const char *msg_fmt, const char *refname);

/*
 * Extra refs will be listed by for_each_ref() before any actual refs
 * for the duration of this process or until clear_extra_refs() is
 * called. Only extra refs added before for_each_ref() is called will
 * be listed on a given call of for_each_ref().
 */
extern void add_extra_ref(const char *refname, const unsigned char *sha1, int flags);
extern void clear_extra_refs(void);
extern int ref_exists(const char *);

extern int peel_ref(const char *refname, unsigned char *sha1);

/** Locks a "refs/" ref returning the lock on success and NULL on failure. **/
extern struct ref_lock *lock_ref_sha1(const char *refname, const unsigned char *old_sha1);

/** Locks any ref (for 'HEAD' type refs). */
#define REF_NODEREF	0x01
extern struct ref_lock *lock_any_ref_for_update(const char *refname,
						const unsigned char *old_sha1,
						int flags);

/** Close the file descriptor owned by a lock and return the status */
extern int close_ref(struct ref_lock *lock);

/** Close and commit the ref locked by the lock */
extern int commit_ref(struct ref_lock *lock);

/** Release any lock taken but not written. **/
extern void unlock_ref(struct ref_lock *lock);

/** Writes sha1 into the ref specified by the lock. **/
extern int write_ref_sha1(struct ref_lock *lock, const unsigned char *sha1, const char *msg);

/*
 * Invalidate the reference cache for the specified submodule.  Use
 * submodule=NULL to invalidate the cache for the main module.  This
 * function must be called if references are changed via a mechanism
 * other than the refs API.
 */
extern void invalidate_ref_cache(const char *submodule);

/** Setup reflog before using. **/
int log_ref_setup(const char *ref_name, char *logfile, int bufsize);

/** Reads log for the value of ref during at_time. **/
extern int read_ref_at(const char *refname, unsigned long at_time, int cnt,
		       unsigned char *sha1, char **msg,
		       unsigned long *cutoff_time, int *cutoff_tz, int *cutoff_cnt);

/* iterate over reflog entries */
typedef int each_reflog_ent_fn(unsigned char *osha1, unsigned char *nsha1, const char *, unsigned long, int, const char *, void *);
int for_each_reflog_ent(const char *refname, each_reflog_ent_fn fn, void *cb_data);
int for_each_recent_reflog_ent(const char *refname, each_reflog_ent_fn fn, long, void *cb_data);

/*
 * Calls the specified function for each reflog file until it returns nonzero,
 * and returns the value
 */
extern int for_each_reflog(each_ref_fn, void *);

#define REFNAME_ALLOW_ONELEVEL 1
#define REFNAME_REFSPEC_PATTERN 2
#define REFNAME_DOT_COMPONENT 4

/*
 * Return 0 iff refname has the correct format for a refname according
 * to the rules described in Documentation/git-check-ref-format.txt.
 * If REFNAME_ALLOW_ONELEVEL is set in flags, then accept one-level
 * reference names.  If REFNAME_REFSPEC_PATTERN is set in flags, then
 * allow a "*" wildcard character in place of one of the name
 * components.  No leading or repeated slashes are accepted.  If
 * REFNAME_DOT_COMPONENT is set in flags, then allow refname
 * components to start with "." (but not a whole component equal to
 * "." or "..").
 */
extern int check_refname_format(const char *refname, int flags);

extern const char *prettify_refname(const char *refname);
extern char *shorten_unambiguous_ref(const char *refname, int strict);

/** rename ref, return 0 on success **/
extern int rename_ref(const char *oldref, const char *newref, const char *logmsg);

/**
 * Resolve refname in the nested "gitlink" repository that is located
 * at path.  If the resolution is successful, return 0 and set sha1 to
 * the name of the object; otherwise, return a non-zero value.
 */
extern int resolve_gitlink_ref(const char *path, const char *refname, unsigned char *sha1);

/** lock a ref and then write its file */
enum action_on_err { MSG_ON_ERR, DIE_ON_ERR, QUIET_ON_ERR };
int update_ref(const char *action, const char *refname,
		const unsigned char *sha1, const unsigned char *oldval,
		int flags, enum action_on_err onerr);

#endif /* REFS_H */
