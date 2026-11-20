/* GNU linker --wrap observes linked FX/JUCE calls, not locks hidden inside
 * dynamic libraries. Scope is thread-local; worker threads are excluded. */
#include <pthread.h>
static _Thread_local unsigned long count;
static _Thread_local int active;
void voxFxLockBegin(void){count=0;active=1;}
unsigned long voxFxLockEnd(void){active=0;return count;}
#define PROBE(name,type) int __real_##name(type*); \
    int __wrap_##name(type* lock){if(active)++count;return __real_##name(lock);}
PROBE(pthread_mutex_lock,pthread_mutex_t)
PROBE(pthread_mutex_trylock,pthread_mutex_t)
PROBE(pthread_rwlock_rdlock,pthread_rwlock_t)
PROBE(pthread_rwlock_wrlock,pthread_rwlock_t)
