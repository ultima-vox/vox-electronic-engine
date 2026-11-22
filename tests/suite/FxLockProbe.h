#pragma once

/*
 * Test-only synchronization probe used by the FX callback timing fixture.
 *
 * Prepare is called before the allocator probe so platform hooks are installed
 * with both counters disabled. The begin/end pair is called by the measured
 * thread immediately around one processBlock invocation. Implementations count
 * synchronization entry points for that thread only. This is evidence about
 * the fixture's linked calls, not a replacement for a system trace of every
 * kernel wait in a DAW.
 */
#ifdef __cplusplus
extern "C" {
#endif

int voxFxLockAvailable(void);
void voxFxLockPrepare(void);
void voxFxLockBegin(void);
unsigned long voxFxLockEnd(void);
unsigned long voxFxWaitEnd(void);

#ifdef __cplusplus
}
#endif
