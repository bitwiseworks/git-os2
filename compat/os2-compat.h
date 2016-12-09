#define GIT_OS2_NATIVE

#include <ctype.h>
#ifndef NO_ICONV
# include <iconv.h>
# if defined(__INNOTEK_LIBC__) && defined(__ICONV_H__)
  /* override klibc's builtin iconv_open */
   extern iconv_t wrapped_iconv_open_for_klibc (const char *, const char *);
#  ifndef BUILDING_COMPAT_OS2
#   define iconv_open(t,f) wrapped_iconv_open_for_klibc (t,f)
#  endif
# endif
#endif

extern struct passwd * wrapped_getpwuid_for_klibc (uid_t);
extern int wrapped_unlink_for_dosish_system (const char *);
extern char * wrapped_getenv_for_os2 (const char *);

#ifndef NO_POLL
#ifndef _NFDS_T_DECLARED
typedef unsigned int  nfds_t;
# define _NFDS_T_DECLARED
#endif

struct pollfd {
  int fd;
  short events;
  short revents;
};
#define POLLIN 1
#define POLLHUP 2

extern int poll (struct pollfd *, nfds_t, int);
#endif

#define SHUT_RD         0               /* shut down the reading side */
#define SHUT_WR         1               /* shut down the writing side */
#define SHUT_RDWR       2               /* shut down both sides */

extern int wrapped_execl_for_os2 (const char *, char *, ...);
extern int wrapped_execlp_for_os2 (const char *, char *, ...);
extern int wrapped_execv_for_os2 (const char *, char * const *);
extern int wrapped_execvp_for_os2 (const char *, char **);

#ifndef BUILDING_COMPAT_OS2
# ifdef __EMX__
#  define chdir(d) _chdir2(d)
#  define getcwd(d,n) _getcwd2(d,n)
# endif
# define getenv(e) wrapped_getenv_for_os2 (e)
# define getpwuid(u) wrapped_getpwuid_for_klibc (u)
# define unlink(f) wrapped_unlink_for_dosish_system (f)
# define execl wrapped_execl_for_os2
# define execlp wrapped_execlp_for_os2
# define execv wrapped_execv_for_os2
# define execvp wrapped_execvp_for_os2
#endif

/*
 * git specific compatibility
 */

#define has_dos_drive_prefix(path) \
	(isalpha(*(path)) && (path)[1] == ':' ? 2 : 0)
static inline int os2_skip_dos_drive_prefix(char **path)
{
	int ret = has_dos_drive_prefix(*path);
	*path += ret;
	return ret;
}
#define skip_dos_drive_prefix os2_skip_dos_drive_prefix
#define is_dir_sep(c) ((c) == '/' || (c) == '\\')
static inline char *os2_find_last_dir_sep(const char *path)
{
	char *ret = NULL;
	for (; *path; ++path)
		if (is_dir_sep(*path))
			ret = (char *)path;
	return ret;
}
static inline void convert_slashes(char *path)
{
	for (; *path; path++)
		if (*path == '\\')
			*path = '/';
}
#define find_last_dir_sep os2_find_last_dir_sep
int os2_offset_1st_component(const char *path);
#define offset_1st_component os2_offset_1st_component
#define PATH_SEP ';'

#define main(c,v) dummy_decl_git_os2_main(); \
extern int git_os2_main_prepare(int *, char ** *); \
extern int git_os2_main(c,v); \
int main(int argc, char **argv) \
{ \
  git_os2_main_prepare(&argc,&argv); \
  return git_os2_main(argc,argv); \
} \
int git_os2_main(c,v)
