/* compat_threads.h — pthread compatibility for Windows
 *
 * On Windows: uses native Win32 threads behind a pthread-compatible API.
 * On Mac/Linux: just includes <pthread.h> directly.
 *
 * Only the subset used by BayerFlow is implemented:
 *   pthread_t, pthread_mutex_t, pthread_cond_t
 *   pthread_create, pthread_join
 *   pthread_mutex_init/lock/unlock/destroy
 *   pthread_cond_init/wait/signal/broadcast/destroy
 */

#pragma once

#ifdef _WIN32
#include <windows.h>
#include <stdint.h>

/* ---- pthread_t ---- */
typedef HANDLE pthread_t;
typedef void   pthread_attr_t;

typedef struct {
    DWORD  (*fn)(LPVOID);
    void   *arg;
    void   *(*pfn)(void *);
} _bf_thread_ctx;

static DWORD WINAPI _bf_thread_trampoline(LPVOID p) {
    _bf_thread_ctx *ctx = (_bf_thread_ctx *)p;
    ctx->pfn(ctx->arg);
    free(ctx);
    return 0;
}

static inline int pthread_create(pthread_t *t, pthread_attr_t *attr,
                                  void *(*fn)(void *), void *arg) {
    (void)attr;
    _bf_thread_ctx *ctx = (_bf_thread_ctx *)malloc(sizeof(_bf_thread_ctx));
    ctx->pfn = fn; ctx->arg = arg;
    *t = CreateThread(NULL, 0, _bf_thread_trampoline, ctx, 0, NULL);
    return *t ? 0 : -1;
}

static inline int pthread_join(pthread_t t, void **retval) {
    (void)retval;
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    return 0;
}

/* ---- pthread_mutex_t ---- */
typedef CRITICAL_SECTION pthread_mutex_t;
typedef void              pthread_mutexattr_t;

static inline int pthread_mutex_init(pthread_mutex_t *m, pthread_mutexattr_t *a)
{ (void)a; InitializeCriticalSection(m); return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *m)
{ EnterCriticalSection(m); return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m)
{ LeaveCriticalSection(m); return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *m)
{ DeleteCriticalSection(m); return 0; }

/* ---- pthread_cond_t ---- */
typedef CONDITION_VARIABLE pthread_cond_t;
typedef void               pthread_condattr_t;

static inline int pthread_cond_init(pthread_cond_t *c, pthread_condattr_t *a)
{ (void)a; InitializeConditionVariable(c); return 0; }
static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{ return SleepConditionVariableCS(c, m, INFINITE) ? 0 : -1; }
static inline int pthread_cond_signal(pthread_cond_t *c)
{ WakeConditionVariable(c); return 0; }
static inline int pthread_cond_broadcast(pthread_cond_t *c)
{ WakeAllConditionVariable(c); return 0; }
static inline int pthread_cond_destroy(pthread_cond_t *c)
{ (void)c; return 0; }

#else
#include <pthread.h>
#endif
