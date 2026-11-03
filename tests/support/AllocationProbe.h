#pragma once
/* Realtime allocation probe for tests (Linux: tests/abi/AllocationProbeLinux.c,
 * Windows: tests/support/AllocationProbeWindows.c). Built into a test when
 * VOX_TEST_INTERPOSE_MALLOC is defined.
 *
 * voxProbeBegin() starts counting heap and virtual-memory allocation calls
 * made by the calling thread only; voxProbeEnd() stops and returns that
 * thread's count. Other threads (sample loader, collectors, the message
 * thread) are never counted into it. A count is the number of allocation
 * entry points entered, so one malloc can count more than once (CRT and OS
 * heap); a probed audio path must reach exactly 0. */
#ifdef __cplusplus
extern "C" {
#endif
int voxProbeAvailable(void);
void voxProbeBegin(void);
unsigned long voxProbeEnd(void);
#ifdef __cplusplus
}
#endif
