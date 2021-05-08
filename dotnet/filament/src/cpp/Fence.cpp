#include <filament/Fence.h>

using namespace filament;

extern "C" int filament_Fence_nWait(void *nativeFence, int mode, uint64_t timeoutNanoSeconds) {
    Fence *fence = (Fence *) nativeFence;
    return (int) fence->wait((Fence::Mode) mode, (uint64_t) timeoutNanoSeconds);
}

extern "C" int filament_Fence_nWaitAndDestroy(void *nativeFence, int mode) {
    Fence *fence = (Fence *) nativeFence;
    return (int) Fence::waitAndDestroy(fence, (Fence::Mode) mode);
}

