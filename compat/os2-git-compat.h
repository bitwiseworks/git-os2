
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

#define SHUT_RD         0               /* shut down the reading side */
#define SHUT_WR         1               /* shut down the writing side */
#define SHUT_RDWR       2               /* shut down both sides */

extern int poll (struct pollfd *, nfds_t, int);

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


//#ifndef _SOCKLEN_T_DECLARED
//typedef int socklen_t;
//# define _SOCKLEN_T_DECLARED
//#endif

#define has_dos_drive_prefix(path) (isalpha(*(path)) && (path)[1] == ':')
#define is_dir_sep(c) ((c) == '/' || (c) == '\\')
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

