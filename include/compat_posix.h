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

/* popen replacement that hides the console window */
static __inline FILE *bf_popen_hidden(const char *cmd, const char *mode) {
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return NULL;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    char cmdBuf[4096];
    strncpy(cmdBuf, cmd, sizeof(cmdBuf) - 1);
    cmdBuf[sizeof(cmdBuf) - 1] = '\0';

    if (!CreateProcessA(NULL, cmdBuf, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return NULL;
    }
    CloseHandle(hWrite);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    int fd = _open_osfhandle((intptr_t)hRead, 0);
    if (fd < 0) { CloseHandle(hRead); return NULL; }
    return _fdopen(fd, mode);
}

#ifndef popen
#  define popen  bf_popen_hidden
#  define pclose fclose
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
