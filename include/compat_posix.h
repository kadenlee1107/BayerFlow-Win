/* compat_posix.h -- POSIX to MSVC shims for BayerFlow Windows build
 * Force-included via /FI in CMakeLists.txt.
 */
#ifndef BAYERFLOW_COMPAT_POSIX_H
#define BAYERFLOW_COMPAT_POSIX_H

#ifdef _WIN32

#ifndef _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_WARNINGS
#  define _CRT_NONSTDC_NO_WARNINGS
#endif
#ifndef _USE_MATH_DEFINES
#  define _USE_MATH_DEFINES
#endif

#include <windows.h>
#include <io.h>
#include <direct.h>
#include <intrin.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef popen
#  define popen  _popen
#  define pclose _pclose
#endif

#ifndef fseeko
#  define fseeko _fseeki64
#endif
#ifndef ftello
#  define ftello _ftelli64
#endif

#ifndef strcasecmp
#  define strcasecmp  _stricmp
#endif
#ifndef strncasecmp
#  define strncasecmp _strnicmp
#endif

#ifndef mkdir
#  define mkdir(path, mode) _mkdir(path)
#endif

static __inline int __builtin_clz(unsigned int x) {
    unsigned long idx;
    if (_BitScanReverse(&idx, (unsigned long)x)) return 31 - (int)idx;
    return 32;
}

#ifndef ssize_t
typedef long long ssize_t;
#endif

/* stat/struct stat -- 64-bit for large file support (>2GB) */
#include <sys/stat.h>
#define stat _stat64
#define fstat _fstat64

#ifndef S_ISDIR
#  define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#  define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#endif /* _WIN32 */
#endif /* BAYERFLOW_COMPAT_POSIX_H */
