#include <image/KtxBundle.h>

using namespace filament;

extern "C" void *
filament_Image_KtxBundle_CreateData(void *data, int count) {
    return (void *) new image::KtxBundle(
            reinterpret_cast<const uint8_t *>(data),
            count
    );
}
extern "C" bool
filament_Image_KtxBundle_GetSphericalHarmonics(void *nativeBundle, float *result) {
    math::float3 bands[9] = {};
    image::KtxBundle *bundle = (image::KtxBundle *) nativeBundle;

    if (!bundle->getSphericalHarmonics(bands)) {
        return false;
    }

    for (int i = 0; i < 9; i++) {
        result[i * 3] = bands[i].x;
        result[i * 3 + 1] = bands[i].y;
        result[i * 3 + 2] = bands[i].z;
    }

    return true;
}
