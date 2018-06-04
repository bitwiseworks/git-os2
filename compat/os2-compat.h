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

#define SHUT_RD         0               /* shut down the reading side */
#define SHUT_WR         1               /* shut down the writing side */
#define SHUT_RDWR       2               /* shut down both sides */

#if !defined O_CLOEXEC && defined O_NOINHERIT
#define O_CLOEXEC	O_NOINHERIT
#endif

#ifdef __EMX__
# define chdir(d) _chdir2(d)
# define getcwd(d,n) _getcwd2(d,n)
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

/*
 * A replacement of main() that adds OS/2 specific initialization.
 */

void os2_startup(void);
#define main(c,v) dummy_decl_os2_main(void); \
static int os2_main(c,v); \
int main(int argc, const char **argv) \
{ \
	os2_startup(); \
	return os2_main(argc, argv); \
} \
static int os2_main(c,v)
