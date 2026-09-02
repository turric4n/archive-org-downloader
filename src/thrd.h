#ifndef THRD_H
#define THRD_H

#include <stddef.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef void (*thrd_fn)(void *arg);

#ifdef _WIN32
typedef HANDLE thrd_t;
typedef CRITICAL_SECTION thrd_mutex;
#else
typedef pthread_t thrd_t;
typedef pthread_mutex_t thrd_mutex;
#endif

int thrd_create(thrd_t *t, thrd_fn fn, void *arg);
void thrd_join(thrd_t t);

void thrd_mutex_init(thrd_mutex *m);
void thrd_mutex_destroy(thrd_mutex *m);
void thrd_mutex_lock(thrd_mutex *m);
void thrd_mutex_unlock(thrd_mutex *m);

#endif /* THRD_H */