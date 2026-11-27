#pragma once

/*
 * Test-only synchronization probe used by the FX callback timing fixture.
 *
 * Prepare is called before the allocator probe so platform hooks are installed
 * with counters disabled. The begin/end pair is called by the measured thread
 * immediately around one processBlock invocation. The coverage fields are an
 * attestation of the probe itself; a zero callback count is meaningful only
 * when complete is true and the positive-control lock and wait paths passed.
 */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct VoxFxLockCoverage {
    unsigned long targetCount;
    unsigned long resolvedTargetCount;
    unsigned long loadedModuleCount;
    unsigned long matchedSlotCount;
    unsigned long patchedSlotCount;
    unsigned long patchFailureCount;
    unsigned long lockTargetCount;
    unsigned long lockResolvedTargetCount;
    unsigned long lockMatchedSlotCount;
    unsigned long lockPatchedSlotCount;
    unsigned long waitTargetCount;
    unsigned long waitResolvedTargetCount;
    unsigned long waitMatchedSlotCount;
    unsigned long waitPatchedSlotCount;
    int moduleEnumerationComplete;
    int complete;
} VoxFxLockCoverage;

int voxFxLockAvailable(void);
int voxFxLockPrepare(void);
VoxFxLockCoverage voxFxLockCoverage(void);
void voxFxLockBegin(void);
unsigned long voxFxLockEnd(void);
unsigned long voxFxWaitEnd(void);
int voxFxLockPositiveControl(void);

#ifdef __cplusplus
}
#endif
