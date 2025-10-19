#define INCL_DOS
#define INCL_DOSERRORS
#include <os2.h>
#include <uconv.h>

#include <sys/types.h>
#include <assert.h>
#include <float.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <pwd.h>
#include <unistd.h>
#define BUILDING_COMPAT_OS2
#include "../git-compat-util.h"

#ifdef __EMX__
extern int _fmode_bin;
#endif

#ifndef PATH_MAX
#define PATH_MAX 260
#endif

static const char *
remap_charset(const char *org)
{
  size_t i, i_s, n_org;
  char c, *s, *s_up;

  enum {
    CMP_END = 0,
    CMP_CASE = 1,
    CMP_NOCASE
  };
  struct remap_table {
    int flag;
    const char *map_from;
    const char *map_to;
  } rmt[] = {
    { CMP_NOCASE, "SJIS", "IBM-932" },
    { CMP_NOCASE, "SHIFTJIS", "IBM-932" },
    { CMP_NOCASE, "UJIS", "IBM-eucJP" },
    { CMP_NOCASE, "EUCCN", "IBM-eucCN" },
    { CMP_NOCASE, "EUCJP", "IBM-eucJP" },
    { CMP_NOCASE, "EUCKR", "IBM-eucKR" },
    { CMP_NOCASE, "EUCTW", "IBM-eucTW" },
    { CMP_NOCASE, "BIG5", "IBM-950" }, /* workaround */
    { CMP_NOCASE, "ISO88591", "ISO-8859-1" }, /* workaround */
    { CMP_NOCASE, "ISO88592", "ISO-8859-2" }, /* workaround */
    { CMP_NOCASE, "ISO88593", "ISO-8859-3" }, /* workaround */
    { CMP_NOCASE, "ISO88594", "ISO-8859-4" }, /* workaround */
    { CMP_NOCASE, "ISO88595", "ISO-8859-5" }, /* workaround */
    { CMP_NOCASE, "ISO88596", "ISO-8859-6" }, /* workaround */
    { CMP_NOCASE, "ISO88597", "ISO-8859-7" }, /* workaround */
    { CMP_NOCASE, "ISO88598", "ISO-8859-8" }, /* workaround */
    { CMP_NOCASE, "ISO88599", "ISO-8859-9" }, /* workaround */
    { CMP_NOCASE, "ASCII", "IBM-367" }, /* workaround */
    { CMP_NOCASE, "ANSIX3.4", "IBM-367" }, /* workaround */
    { CMP_NOCASE, "USASCII", "IBM-367" }, /* workaround */
    { CMP_NOCASE, "ISO646", "IBM-367" }, /* workaround */
    { CMP_NOCASE, "ISO646US", "IBM-367" }, /* workaround */
    { CMP_NOCASE, "UTF8", "UTF-8" }, /* workaround */
    { CMP_NOCASE, "UCS2", "UCS-2" }, /* workaround */
    { CMP_NOCASE, "UTF16", "UCS-2" }, /* workaround */
    { CMP_NOCASE, "UTF16LE", "UCS-2@endian=little" }, /* workaround */
    { CMP_NOCASE, "UTF16BE", "UCS-2@endian=big" }, /* workaround */
    { CMP_NOCASE, "UCS2INTERNAL", "UCS-2" }, /* gnu libiconv extension */
    { CMP_NOCASE, "WCHART", "UCS-2" }, /* gnu libiconv extension */
#ifdef MS932_IS_AVAILABLE
    { CMP_NOCASE, "CP932", "MS-932" },
    { CMP_NOCASE, "WINDOWS31J", "MS-932" },
#else
    { CMP_NOCASE, "MS932", "IBM-932" }, /* better than nothing, maybe... */
    { CMP_NOCASE, "WINDOWS31J", "IBM-932" },
#endif

    { CMP_END, NULL, NULL }
  };

  if (!org || !*org) return org;
  n_org = strlen(org);
  s = alloca(n_org + 1);
  s_up = alloca(n_org + 1);
  for(i=0, i_s=0 ; i<n_org; i++) {
    c = org[i];
    if ((unsigned char)c < 0x20) break;
    if (c == '-' || c == '_') continue;
    s[i_s] = c;
    s_up[i_s] = isalpha(c) ? toupper(c) : c;
    ++i_s;
  }
  s[i_s] = '\0';
  s_up[i_s] = '\0';

  for(i=0; rmt[i].flag != CMP_END; i++) {
    if (strcmp(rmt[i].map_from, rmt[i].flag == CMP_CASE ? s : s_up) == 0)
      return rmt[i].map_to;
  }

  return org;
}


/*
  iconv_open wrapper (for klibc's builtin iconv)
  treat \ and ~ characters as path separater under DBCS codepage.
*/

iconv_t wrapped_iconv_open_for_klibc (const char *cp_to, const char *cp_from)
{
  struct k_iconv_t {
    UconvObject uo_from;
    UconvObject uo_to;
    UniChar *ucp_to;
    UniChar *ucp_from;
  };
  iconv_t ic;

  ic = iconv_open(remap_charset(cp_to), remap_charset(cp_from));
  if (ic != (iconv_t)-1) {
    struct k_iconv_t *kic = (struct k_iconv_t *)ic;
    uconv_attribute_t ua;
    APIRET rc;
    rc = UniQueryUconvObject(kic->uo_from, &ua, sizeof(ua), 0, 0, 0);
    if (rc == 0) {
      ua.converttype |= CVTTYPE_PATH;
      UniSetUconvObject(kic->uo_from, &ua);
    }
    rc = UniQueryUconvObject(kic->uo_to, &ua, sizeof(ua), 0, 0, 0);
    if (rc == 0) {
      ua.converttype |= CVTTYPE_PATH;
      UniSetUconvObject(kic->uo_to, &ua);
    }
  }

  return ic;
}

int os2_offset_1st_component(const char *path)
{
	char *pos = (char *)path;

	/* unc paths */
	if (!skip_dos_drive_prefix(&pos) &&
			is_dir_sep(pos[0]) && is_dir_sep(pos[1])) {
		/* skip server name */
		pos = strpbrk(pos + 2, "\\/");
		if (!pos)
			return 0; /* Error: malformed unc path */

		do {
			pos++;
		} while (*pos && !is_dir_sep(*pos));
	}

	return pos + is_dir_sep(*pos) - path;
}

void os2_startup(void)
{
  /* mask all FPEs */
  _control87(MCW_EM, MCW_EM);

  /* switch stdin/out/err to binary if they are files (kLIBC doesn't do that) */
  if (!isatty(fileno(stdin))) setmode( fileno(stdin), O_BINARY);
  if (!isatty(fileno(stdout))) setmode( fileno(stdout), O_BINARY);
  if (!isatty(fileno(stderr))) setmode( fileno(stderr), O_BINARY);

  /* HOME and TMPDIR are critical for git functionality */
  if (!getenv ("HOME"))
    die("Put your home directory to the environment variable `HOME'.\n");
  if (!getenv ("TMPDIR"))
    die("Put your temporary directory to the environment variable `TMPDIR'.\n");
}

int os2_pipe(int filedes[2])
{
  /* Use socketpair() instead of pipe() because select() does not work on pipes */
  int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, filedes);
  /*
   * Make pipes non inheritable for spawn2 to work properly without the
   * expensive P_2_NOINHERIT flag (note that MinGW does the same).
   */
  if (rc == 0) {
    fcntl(filedes[0], F_SETFD, FD_CLOEXEC);
    fcntl(filedes[1], F_SETFD, FD_CLOEXEC);
  }
  return rc;
}
