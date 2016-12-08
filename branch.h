#ifndef BRANCH_H
#define BRANCH_H

/* Functions for acting on the information about branches. */

/*
 * Creates a new branch, where:
 *
 *   - name is the new branch name
 *
 *   - start_name is the name of the existing branch that the new branch should
 *     start from
 *
 *   - force enables overwriting an existing (non-head) branch
 *
 *   - reflog creates a reflog for the branch
 *
 *   - track causes the new branch to be configured to merge the remote branch
 *     that start_name is a tracking branch for (if any).
 */
void create_branch(const char *name, const char *start_name,
		   int force, int reflog,
		   int clobber_head, int quiet, enum branch_track track);

/*
 * Validates that the requested branch may be created, returning the
 * interpreted ref in ref, force indicates whether (non-head) branches
 * may be overwritten. A non-zero return value indicates that the force
 * parameter was non-zero and the branch already exists.
 *
 * Contrary to all of the above, when attr_only is 1, the caller is
 * not interested in verifying if it is Ok to update the named
 * branch to point at a potentially different commit. It is merely
 * asking if it is OK to change some attribute for the named branch
 * (e.g. tracking upstream).
 *
 * NEEDSWORK: This needs to be split into two separate functions in the
 * longer run for sanity.
 *
 */
int validate_new_branchname(const char *name, struct strbuf *ref, int force, int attr_only);

/*
 * Remove information about the state of working on the current
 * branch. (E.g., MERGE_HEAD)
 */
void remove_branch_state(void);

/*
 * Configure local branch "local" as downstream to branch "remote"
 * from remote "origin".  Used by git branch --set-upstream.
 * Returns 0 on success.
 */
#define BRANCH_CONFIG_VERBOSE 01
extern int install_branch_config(int flag, const char *local, const char *origin, const char *remote);

/*
 * Read branch description
 */
extern int read_branch_desc(struct strbuf *, const char *branch_name);

/*
 * Check if a branch is checked out in the main worktree or any linked
 * worktree and die (with a message describing its checkout location) if
 * it is.
 */
extern void die_if_checked_out(const char *branch, int ignore_current_worktree);

/*
 * Update all per-worktree HEADs pointing at the old ref to point the new ref.
 * This will be used when renaming a branch. Returns 0 if successful, non-zero
 * otherwise.
 */
extern int replace_each_worktree_head_symref(const char *oldref, const char *newref);

#endif
