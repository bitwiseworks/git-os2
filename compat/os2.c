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

#ifndef NO_POLL
int poll (struct pollfd *fds, nfds_t nfds, int timeout)
{
#if defined(POLLIN)
  fd_set fd_r, *pfd_r;
#else
  fd_set *pfd_r = NULL;
#endif
#if defined(POLLOUT)
  fd_set fd_w, *pfd_w;
#else
  fd_set *pfd_w = NULL;
#endif
#if defined(POLLPRI)
  fd_set fd_x, *pfd_x;
#else
  fd_set *pfd_x = NULL;
#endif
  int fd, fd_cnt;
  int rc;
  struct timeval tv;
  nfds_t i;

#if defined(POLLIN)
  pfd_r = &fd_r;
  FD_ZERO(&fd_r);
#endif
#if defined(POLLOUT)
  pfd_w = &fd_w;
  FD_ZERO(&fd_w);
#endif
#if defined(POLLPRI)
  pfd_x = &fd_x;
  FD_ZERO(&fd_x);
#endif
  
  for(i=0, fd_cnt=0; i<nfds; i++) {
    fd = fds[i].fd;
    if (fd >= 0) {
      if (fds[i].events &
          ( 0
#if defined(POLLIN)
              | POLLIN
#endif
#if defined(POLLOUT)
              | POLLOUT
#endif
#if defined(POLLPRI)
              | POLLPRI
#endif
                       ) ) {
        assert(fd < FD_SETSIZE);
        if (fd >= fd_cnt) fd_cnt = fd + 1;
#if defined(POLLIN)
        if (fds[i].events & POLLIN) FD_SET(fd, &fd_r);
#endif
#if defined(POLLOUT)
        if (fds[i].events & POLLOUT) FD_SET(fd, fd_w);
#endif
#if defined(POLLPRI)
        if (fds[i].events & POLLPRI) FD_SET(fd, fd_x);
#endif
      }
    }
  }
  
  if (timeout >= 0) {
    if (timeout == 0) {
      tv.tv_sec = tv.tv_usec = 0;
    }
    else {
      tv.tv_sec = timeout / 1000;
      tv.tv_usec = (timeout % 1000) * 1000;
    }
    rc = select(fd_cnt, pfd_r, pfd_w, pfd_x, &tv);
  }
  else
    rc = select(fd_cnt, pfd_r, pfd_w, pfd_x, NULL);
  
  if (rc > 0) for (i=0; i<nfds; i++) {
    short rev;
    fd = fds[i].fd;
    if (fd >= 0) {
      rev = 0;
#if defined(POLLIN)
      if (FD_ISSET(fd, &fd_r)) rev |= POLLIN;
#endif
#if defined(POLLOUT)
      if (FD_ISSET(fd, &fd_w)) rev |= POLLOUT;
#endif
#if defined(POLLPRI)
      if (FD_ISSET(fd, &fd_w)) rev |= POLLPRI;
#endif
      fds[i].revents = rev;
    }
  }
  
  return rc;
}
#endif

static
int is_file(const char *name)
{
  int rc;
#if 1
  struct stat st;
  
  rc = stat(name, &st);
  if (rc == 0) {
    if (!S_ISREG(st.st_mode)) rc = -1; /* drip only normal files */
  }
#else
  rc = access(name, F_OK);
#endif
  return rc;
}

static
int searchprog_os2emx (char *dst, const char *progpathname)
{
  const size_t dst_limit = PATH_MAX;
  const char *progname;
  char *p, *p_next;
  char *path;
  size_t n, n_ppn, n_addext;
  int pn_is_abs;
  
  if (!progpathname || !*progpathname) return -1;
  n_ppn = strlen(progpathname);
  p = alloca(dst_limit +1);
  if (!p) return -1;
  
  pn_is_abs = (progpathname[0] == '\\' || progpathname[0] == '/') ||
              (progpathname[0] != '\0' && progpathname[1] == ':');
  
  progname = _getname(progpathname);
  n_addext = _getext(progname) ? 0 : 4 /* maxlen of extension '.???' */ ;
  path = (progname && !pn_is_abs) ? getenv("PATH") : NULL;
  if (!path || !*path) {
    path = ";";
    progname = progpathname;
  }
  
  while(*path != '\0') {
    int found;
    p_next = strchr(path, ';');
    if (p_next) {
      n = p_next - path;
      ++p_next;
    }
    else {
      n = strlen(path);
      p_next = path + n;
    }
    if (n + n_ppn + n_addext + 1 < dst_limit) {
      found = 0;
      memcpy(p, path, n);
      p[n] = '\0';
      _fnslashify(p);
      if (n > 0 && p[n-1] != '/') {
        p[n++] = '/';
        p[n] = '\0';
      }
      strcat(p, progname);
      if (n_addext == 0) {
        found = is_file(p) == 0;
      }
      else {
        found = is_file(p) == 0;
        if (!found) {
          n = strlen(p);
          strcpy(p + n, ".exe");
          found = is_file(p) == 0;
        }
        if (!found) {
          strcpy(p + n, ".com");
          found = is_file(p) == 0;
        }
#if 0
        if (!found) {
          strcpy(p + n, ".sh");
          found = is_file(p) == 0;
        }
        if (!found) {
          strcpy(p + n, ".cmd");
          found = is_file(p) == 0;
        }
#endif
      }
      if (found) {
        if (dst) strcpy(dst, p);
        return strlen(p) + 1;
      }
    }
    path = p_next;
  }
  return -1;
}

static char * wrapped_get_gitshell(int *warned)
{
  static char dummy[PATH_MAX + 1];
  char *p, *altsh;
  int rc_gitsh = -1, rc = -1;
  
  if (dummy[0])
    return dummy;
  
  altsh = "cmd.exe";
  p = getenv("GIT_SHELL");
  if (p) {
    rc_gitsh  = searchprog_os2emx(dummy, p);
    
    if (rc >= 0)
      altsh = "$GIT_SHELL";
    else {
      if (warned && !*warned)
        warning("$GIT_SHELL (%s) is not found.", p);
    }
  }
  if (rc_gitsh < 0) {
    char *p_rt;
    size_t n;
    p_rt = getenv("UNIXROOT");
    if (!p_rt) p_rt = getenv("ROOT");
    if (!p_rt) p_rt = "";
    n = strlen(p_rt);
    if (n > 0 && (p_rt[n-1] == '/' || p_rt[n-1] == '\\')) {
      --n;
      p_rt[n] = '\0';
    }
    p = alloca(n + sizeof( "/usr/bin/sh" ) + 1);
    strcpy(p, p_rt);
    _fnslashify(p);
    if (n > 0 && p[n-1] == '/') {
      --n;
      p[n] = '\0';
    }
    strcat(p, "/usr/bin/sh");
    rc_gitsh = searchprog_os2emx(dummy, p);
    if (rc_gitsh >= 0)
      altsh = "$UNIXROOT/usr/bin/sh";
    else {
      p = getenv("PERL_SH_DIR");
      if (p && *p) {
        char *p_psh;
        p_psh = alloca(strlen(p) + 4);
        strcpy(p_psh, p);
        strcat(p_psh, "/sh");
        rc = searchprog_os2emx(dummy, p_psh);
      }
      if (rc >= 0)
        altsh = "$PERL_SH/sh";
      else {
        p = getenv("SHELL");
        if (p && *p)
          rc = searchprog_os2emx(dummy, p);
      }
      if (rc >= 0)
        altsh = "$SHELL";
      else {
        p = getenv("OS2SHELL");
        if (p && *p)
          rc = searchprog_os2emx(dummy, p);
      }
      if (rc >= 0)
        altsh = "$OS2SHELL";
      else {
        p = getenv("EMXSHELL");
        if (p && *p)
          rc = searchprog_os2emx(dummy, p);
      }
      if (rc >= 0)
        altsh = "$EMXSHELL";
      else {
        strcpy(dummy, "cmd.exe");
      }
    }
  }

  if (rc_gitsh < 0) {
    if (warned && !*warned) {
      warning("/bin/sh is not found. %s will be used alternatively.", altsh);
    }
  }
  _fnslashify(dummy);
  
  return dummy[0] ? dummy : NULL;
}
static char * wrapped_getenv_shell (const char *name, int *warned)
{
  return wrapped_get_gitshell(warned);
}

static char * wrapped_getenv_user (const char *name, int *warned)
{
  static char dummy_noname[] = "NONAME";
  char *p, *pu;
  
  p = getenv(name);
  if (!p) {
    pu = getenv("USERNAME");
    if (pu) {
      if (!*warned) warning("$%s is not set. $USERNAME (%s) will be used alternatively.", name, pu);
    }
    else {
      pu = dummy_noname;
      if (!*warned) warning("$%s is not set. NONAME will be used alternatively.", name);
    }
    return pu;
  }
  return p;
}

static char * wrapped_getenv_home (const char *name, int *warned)
{
  static char dummy[PATH_MAX + 1];
  char *p;
  
  p = getenv(name);
  if (p) {
    strncpy(dummy, p, PATH_MAX);
  }
  if (!dummy[0]) {
#ifdef __EMX__
    _getcwd2(dummy, PATH_MAX);
#else
    getcwd(dummy, PATH_MAX);
#endif
    if (warned && !*warned)
      warning("$%s is not set. Current directory (%s) will be used.", name, dummy);
  }
  _fnslashify(dummy);
  
  return dummy;
}

static char * wrapped_getenv_tmpdir (const char *name, int *warned)
{
  static char dummy[PATH_MAX + 1];
  char *p;
  
  p = getenv(name);
  if (p) {
    strncpy(dummy, p, PATH_MAX);
  }
  else {
    char *pt;
    pt = getenv("TMP");
    if (pt) {
      strncpy(dummy, pt, PATH_MAX);
      if (!*warned) warning("$%s is not set. $TMP will be used.", name);
    }
    else {
#ifdef __EMX__
      _getcwd2(dummy, PATH_MAX);
#else
      getcwd(dummy, PATH_MAX);
#endif
      if (!*warned) warning("$%s is not set. Current directory will be used.", name);
    }
  }
  _fnslashify(dummy);
  return &(dummy[0]);
}

static char * wrapped_getenv_email (const char *name, int *warned)
{
  static char dummy[128];
  char *p;
  
  p = getenv(name);
  if (!p || !*p) {
    if (warned && !*warned) warning("$%s is not set.", name);
#if 0
    strcpy(dummy, "i_have_no_email@nowhere.org");
#endif
    p = dummy; /* zero-length string "" */
  }
  
  return p;
}


char *
wrapped_getenv_for_os2 (const char *name)
{
  struct altered_env_table {
    char *envname;
    int warned_unset;
    char * (*altered_getenv)(const char *, int *);
  };
  
  static struct altered_env_table envtbl[] = \
    {
      { "HOME", 0, wrapped_getenv_home },
      { "USER", 0, wrapped_getenv_user },
      { "TMPDIR", 0, wrapped_getenv_tmpdir },
      { "EMAIL", 0, wrapped_getenv_email },
      { "GIT_SHELL", 0 ,wrapped_getenv_shell },
      { "SHELL", 0 ,wrapped_getenv_shell },
      { NULL, 0, NULL }
    };
  
  if (name && *name) {
    int i;
    char *p;
    for(i=0; envtbl[i].envname && envtbl[i].altered_getenv; i++) {
      if (strcmp(name, envtbl[i].envname) == 0) {
        p = (*(envtbl[i].altered_getenv))(name, &(envtbl[i].warned_unset));
        envtbl[i].warned_unset ++;
        return p;
      }
    }
  }
  return getenv(name);
}


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

/*
  unlink (for DOSish system )
*/

int wrapped_unlink_for_dosish_system (const char *pn)
{
  int rc;
  int errno_bak;
  
  rc = unlink(pn);
  errno_bak = errno;
  if (rc) {
    if (chmod(pn, 666) == 0) {
      rc = unlink(pn);
    }
    else {
      errno = errno_bak;
    }
  }
  return rc;
}

/*
  getpwuid
*/


struct passwd * wrapped_getpwuid_for_klibc (uid_t uid)
{
  static struct passwd pw_dummy;
  struct passwd *pw;
  
  pw = getpwuid(uid);
  if (pw) {
    memcpy(&pw_dummy, pw, sizeof(struct passwd));
  }
  pw = &pw_dummy;
  
#if 1
  if (!pw->pw_name || !pw->pw_name[0])
    pw->pw_name = wrapped_getenv_for_os2("USER");
  if (!pw->pw_gecos || !pw->pw_gecos[0])
    pw->pw_gecos = wrapped_getenv_for_os2("EMAIL");
  if (!pw->pw_dir || !pw->pw_dir[0])
    pw->pw_dir = wrapped_getenv_for_os2("HOME");
  if (!pw->pw_shell || ! pw->pw_shell[0])
    pw->pw_shell = wrapped_getenv_for_os2("SHELL");
#else
  {
  static char myhome[PATH_MAX];
  char *p;

  p = pw->pw_name;
  if (!p || !*p) {
    p = getenv("USERNAME");
    if (!p || !*p) p = getenv("USER");
    if (!p || !*p) {
      warning ("Your $USERNAME (or $USER) is not defined.");
      p = "NONAME";
    }
    pw->pw_name = p;
  }
  p = pw->pw_gecos;
  if (!p || !*p) {
    if (!p || !*p) p = getenv("EMAIL");
    if (!p || !*p) {
      warning ("Your $EMAIL is not defined.");
      p = "(no email)";
    }
    pw->pw_gecos = p;
  }
  p = pw->pw_dir;
  if (!p || !*p) {
    p = getenv("HOME");
    if (p && *p) {
      strncpy(myhome, p, sizeof(myhome));
      if (myhome[sizeof(myhome)-1] != '\0') {
        myhome[0] = myhome[sizeof(myhome)-1] = '\0';
        p = NULL;
      }
    }
    if (!p && !myhome[0]) {
      warning ("Home directory $HOME is not defined.");
      getcwd(myhome, sizeof(myhome)-1);
    }
    _fnslashify(myhome);
    pw->pw_dir = myhome;
  }
  p = pw->pw_shell;
  if (!p || !*p) {
    p = getenv("SHELL");
    if (!p || !*p) p = getenv("OS2SHELL");
    if (!p || !*p) p = getenv("EMXSHELL");
    if (!p || !*p) {
      warning ("Default shell $SHELL is not defined.");
      /* p = getenv("COMSPEC"); */
      p = "CMD.EXE";
    }
    pw->pw_shell = p;
  }
  }
#endif

  return pw;
}


static
int wrapped_exec_2(int with_path, const char *progname, char **argv)
{
  if (!progname && !*progname) return -1;
  
  /* treat special case '/bin/sh' */
  if (strcmp(progname, "/bin/sh") == 0) {
    char *sh;
    progname = wrapped_get_gitshell(NULL);
    sh = _getname(progname);
    if (argv[0] && argv[1] && strcmp(argv[1], "-c")==0 &&
        (stricmp(sh, "cmd.exe")==0 || stricmp(sh, "cmd")==0 ||
         stricmp(sh, "4os2.exe")==0 || stricmp(sh, "4os2")==0 ) )
    {
      argv[1] = "/c";
    }
  }
  return with_path ? execvp(progname, argv) : execv(progname, argv);
}

int wrapped_execv_for_os2 (const char *progname, char * const *argv)
{
  return wrapped_exec_2(0, progname, argv);
}
int wrapped_execvp_for_os2 (const char *progname, char **argv)
{
  return wrapped_exec_2(1, progname, argv);
}

int wrapped_execl_for_os2 (const char *progname, char *argv0, ...)
{
  int rc;
  va_list args;
  va_start (args, argv0);
  rc = wrapped_exec_2(0, progname, (char**)args);
  va_end (args);
  return rc;
}
int wrapped_execlp_for_os2 (const char *progname, char *argv0, ...)
{
  int rc;
  va_list args;
  va_start (args, argv0);
  rc = wrapped_exec_2(1, progname, (char**)args);
  va_end (args);
  return rc;
}




static
int setbinmode_to_file(int fd)
{
  struct stat st;
  int rc;
  
  rc = fstat(fd, &st);
  if (rc == 0) {
    rc = setmode(fd, S_ISCHR(st.st_mode) ? O_TEXT : O_BINARY);
  }
  return rc;
}

int git_os2_main_prepare (int * p_argc, char ** * p_argv)
{
#ifdef __EMX__
  char *p;
  
  _fmode_bin = 1; /* -Zbin-files */
  _wildcard(p_argc, p_argv);
  _response(p_argc, p_argv);
  p = (*p_argv)[0];
  _fnslashify(p);
  p = _getname(p);
  if (p) {
    char *prog, *pext;
    prog = strdup(p);
    pext = _getext(prog);
    if (pext) *pext = '\0';
    (*p_argv)[0] = prog;
  }
#endif
  _control87(MCW_EM, MCW_EM); /* mask all FPEs */
  if (!isatty(fileno(stdin))) setmode( fileno(stdin), O_BINARY);
  if (!isatty(fileno(stdout))) setmode( fileno(stdout), O_BINARY);
  if (!isatty(fileno(stderr))) setmode( fileno(stderr), O_BINARY);

#if 1
  if (!getenv ("HOME"))
    die("Set your home directory to the environment variable `HOME'.\n");
#else
  wrapped_getenv_for_os2 ("HOME"); /* warn if $HOME is not set */
#endif
  wrapped_getenv_for_os2 ("GIT_SHELL"); /* warn if /bin/sh or $GIT_SHELL not exist */
  
  return 0;
}

