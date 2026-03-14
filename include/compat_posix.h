/* compat_posix.h — POSIX → MSVC shims for BayerFlow Windows build
 *
 * Force-included into every C/CXX translation unit via /FI in CMakeLists.txt.
 * Provides: M_PI, popen/pclose, fseeko/ftello, strcasecmp, mkdir, __builtin_clz,
 *           _CRT_SECURE_NO_WARNINGS, stdatomic shim.
 */
#ifndef BAYERFLOW_COMPAT_POSIX_H
#define BAYERFLOW_COMPAT_POSIX_H

#ifdef _WIN32

/* Suppress MSVC safe-function warnings (fopen, strncpy, etc.) */
#ifndef _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_WARNINGS
#  define _CRT_NONSTDC_NO_WARNINGS
#endif

/* M_PI and friends */
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

/* popen / pclose */
#ifndef popen
#  define popen  _popen
#  define pclose _pclose
#endif

/* fseeko / ftello — 64-bit file positions */
#ifndef fseeko
#  define fseeko _fseeki64
#endif
#ifndef ftello
#  define ftello _ftelli64
#endif

/* strcasecmp / strncasecmp */
#ifndef strcasecmp
#  define strcasecmp  _stricmp
#endif
#ifndef strncasecmp
#  define strncasecmp _strnicmp
#endif

/* mkdir — POSIX takes (path, mode), Windows _mkdir takes only path */
#ifndef mkdir
#  define mkdir(path, mode) _mkdir(path)
#endif

/* __builtin_clz — count leading zeros (undefined for 0, same as GCC) */
static __inline int __builtin_clz(unsigned int x) {
    unsigned long idx;
    if (_BitScanReverse(&idx, (unsigned long)x)) return 31 - (int)idx;
    return 32;
}

/* ssize_t */
#ifndef ssize_t
typedef long long ssize_t;
#endif

/* S_ISDIR — MSVC sys/stat.h defines S_IFDIR but not the IS* macros */
#include <sys/stat.h>
#ifndef S_ISDIR
#  define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#  define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#endif /* _WIN32 */

#endif /* BAYERFLOW_COMPAT_POSIX_H */
