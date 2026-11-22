/* GNU linker probe for the FX callback fixture. The wrappers observe calls
 * from the linked executable and static JUCE/product objects, not locks hidden
 * inside a dynamically loaded library. The counters are thread-local and
 * only the measured thread is enabled. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

static _Thread_local unsigned long lockCount;
static _Thread_local unsigned long waitCount;
static _Thread_local int active;

int voxFxLockAvailable(void) { return 1; }
void voxFxLockPrepare(void) { active = 0; }
void voxFxLockBegin(void) { lockCount = 0; waitCount = 0; active = 1; }
unsigned long voxFxLockEnd(void) { active = 0; return lockCount; }
unsigned long voxFxWaitEnd(void) { active = 0; return waitCount; }

#define LOCK_PROBE(name, type)                                      \
  int __real_##name(type*);                                        \
  int __wrap_##name(type* lock) {                                  \
    if (active) ++lockCount;                                       \
    return __real_##name(lock);                                    \
  }

LOCK_PROBE(pthread_mutex_lock, pthread_mutex_t)
LOCK_PROBE(pthread_mutex_trylock, pthread_mutex_t)

int __real_pthread_mutex_timedlock(pthread_mutex_t*, const struct timespec*);
int __wrap_pthread_mutex_timedlock(pthread_mutex_t* lock, const struct timespec* until) {
  if (active) ++lockCount;
  return __real_pthread_mutex_timedlock(lock, until);
}

LOCK_PROBE(pthread_rwlock_rdlock, pthread_rwlock_t)
LOCK_PROBE(pthread_rwlock_tryrdlock, pthread_rwlock_t)
LOCK_PROBE(pthread_rwlock_wrlock, pthread_rwlock_t)
LOCK_PROBE(pthread_rwlock_trywrlock, pthread_rwlock_t)
LOCK_PROBE(pthread_spin_lock, pthread_spinlock_t)

#define WAIT_PROBE(name, declaration, args)                          \
  int __real_##name declaration;                                     \
  int __wrap_##name declaration {                                    \
    if (active) ++waitCount;                                         \
    return __real_##name args;                                       \
  }

WAIT_PROBE(pthread_cond_wait, (pthread_cond_t* condition, pthread_mutex_t* lock),
           (condition, lock))
WAIT_PROBE(pthread_cond_timedwait,
           (pthread_cond_t* condition, pthread_mutex_t* lock, const struct timespec* until),
           (condition, lock, until))
WAIT_PROBE(sem_wait, (sem_t* semaphore), (semaphore))
WAIT_PROBE(sem_timedwait, (sem_t* semaphore, const struct timespec* until),
           (semaphore, until))
