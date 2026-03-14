/* unistd.h — stub for Windows (MSVC)
 * Provides the subset of POSIX unistd.h that BayerFlow uses.
 * On Windows, getenv() lives in stdlib.h (already included elsewhere).
 */
#ifndef BAYERFLOW_UNISTD_H
#define BAYERFLOW_UNISTD_H

#ifdef _WIN32
#include <stdlib.h>
#include <io.h>
#include <process.h>
typedef int pid_t;
static inline pid_t getpid(void) { return (pid_t)_getpid(); }
#else
#include_next <unistd.h>
#endif

#endif /* BAYERFLOW_UNISTD_H */
