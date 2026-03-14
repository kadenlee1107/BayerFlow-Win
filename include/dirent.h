/* dirent.h — minimal POSIX directory iteration shim for Windows (MSVC/MinGW)
 * Wraps Win32 FindFirstFile / FindNextFile to match the POSIX DIR/dirent API.
 * Only covers what BayerFlow actually uses: opendir / readdir / closedir / d_name.
 */
#ifndef BAYERFLOW_DIRENT_H
#define BAYERFLOW_DIRENT_H

#ifdef _WIN32

#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#define NAME_MAX 260

struct dirent {
    char d_name[NAME_MAX + 1];
};

typedef struct _DIR {
    HANDLE           hFind;
    WIN32_FIND_DATAA findData;
    struct dirent    ent;
    int              firstRead; /* 1 = FindFirstFile result already in findData */
} DIR;

static inline DIR *opendir(const char *path) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    DIR *d = (DIR *)calloc(1, sizeof(DIR));
    if (!d) { errno = ENOMEM; return NULL; }

    d->hFind = FindFirstFileA(pattern, &d->findData);
    if (d->hFind == INVALID_HANDLE_VALUE) {
        free(d);
        errno = ENOENT;
        return NULL;
    }
    d->firstRead = 1;
    return d;
}

static inline struct dirent *readdir(DIR *d) {
    if (!d) return NULL;

    if (d->firstRead) {
        d->firstRead = 0;
    } else {
        if (!FindNextFileA(d->hFind, &d->findData))
            return NULL; /* end of directory */
    }

    strncpy(d->ent.d_name, d->findData.cFileName, NAME_MAX);
    d->ent.d_name[NAME_MAX] = '\0';
    return &d->ent;
}

static inline int closedir(DIR *d) {
    if (!d) return -1;
    if (d->hFind != INVALID_HANDLE_VALUE)
        FindClose(d->hFind);
    free(d);
    return 0;
}

#else  /* not Windows — use the system header */
#include_next <dirent.h>
#endif /* _WIN32 */

#endif /* BAYERFLOW_DIRENT_H */
