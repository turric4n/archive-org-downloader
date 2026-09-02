#define _CRT_SECURE_NO_WARNINGS
#include "thrd.h"

#include <stdlib.h>

#ifdef _WIN32
#include <process.h>
#endif

#ifdef _WIN32
typedef struct { thrd_fn fn; void *arg; } thrd_start_args;

static unsigned __stdcall thrd_win_entry(void *raw) {
    thrd_start_args *a = (thrd_start_args *)raw;
    thrd_fn fn = a->fn;
    void *arg = a->arg;
    free(a);
    fn(arg);
    return 0;
}

int thrd_create(thrd_t *t, thrd_fn fn, void *arg) {
    thrd_start_args *a = (thrd_start_args *)malloc(sizeof(*a));
    if (!a) return -1;
    a->fn = fn;
    a->arg = arg;
    uintptr_t h = _beginthreadex(NULL, 0, thrd_win_entry, a, 0, NULL);
    if (h == 0) {
        free(a);
        return -1;
    }
    *t = (HANDLE)h;
    return 0;
}

void thrd_join(thrd_t t) {
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}

void thrd_mutex_init(thrd_mutex *m) { InitializeCriticalSection(m); }
void thrd_mutex_destroy(thrd_mutex *m) { DeleteCriticalSection(m); }
void thrd_mutex_lock(thrd_mutex *m) { EnterCriticalSection(m); }
void thrd_mutex_unlock(thrd_mutex *m) { LeaveCriticalSection(m); }

#else

typedef struct { thrd_fn fn; void *arg; } thrd_start_args;

static void *thrd_posix_entry(void *raw) {
    thrd_start_args *a = (thrd_start_args *)raw;
    thrd_fn fn = a->fn;
    void *arg = a->arg;
    free(a);
    fn(arg);
    return NULL;
}

int thrd_create(thrd_t *t, thrd_fn fn, void *arg) {
    thrd_start_args *a = (thrd_start_args *)malloc(sizeof(*a));
    if (!a) return -1;
    a->fn = fn;
    a->arg = arg;
    if (pthread_create(t, NULL, thrd_posix_entry, a) != 0) {
        free(a);
        return -1;
    }
    return 0;
}

void thrd_join(thrd_t t) { pthread_join(t, NULL); }

void thrd_mutex_init(thrd_mutex *m) { pthread_mutex_init(m, NULL); }
void thrd_mutex_destroy(thrd_mutex *m) { pthread_mutex_destroy(m); }
void thrd_mutex_lock(thrd_mutex *m) { pthread_mutex_lock(m); }
void thrd_mutex_unlock(thrd_mutex *m) { pthread_mutex_unlock(m); }

#endif