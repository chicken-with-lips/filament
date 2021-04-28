#include <filament/Color.h>

using namespace filament;

extern "C" LinearColor filament_Color_ToLinearAccurate(sRGBColor color) {
    return Color::toLinear<ACCURATE>(color);
}