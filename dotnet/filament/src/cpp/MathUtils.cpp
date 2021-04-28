#include <math/mat3.h>
#include <math/quat.h>

using namespace filament::math;

// FIXME
//extern "C" void filament_MathUtils_nPackTangentFrame(float tangentX, float tangentY, float tangentZ,
//                                                     float bitangentX, float bitangentY, float bitangentZ,
//                                                     float normalX, float normalY, float normalZ,
//                                                     floatArray quaternion_, int offset) {
//
//    float3 tangent{tangentX, tangentY, tangentZ};
//    float3 bitangent{bitangentX, bitangentY, bitangentZ};
//    float3 normal{normalX, normalY, normalZ};
//    quatf q = mat3f::packTangentFrame({tangent, bitangent, normal});
//
//    env->SetFloatArrayRegion(quaternion_, offset, 4,
//                             reinterpret_cast<float *>(&q));
//}
