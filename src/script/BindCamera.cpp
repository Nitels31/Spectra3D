#include "Bindings.hpp"

#include "../core/Math.hpp"
#include "../render/Camera.hpp"

#if SPECTRA_SCRIPTING

namespace s3d {
namespace script {
namespace {

// Pitch is clamped just short of straight up/down. The basis is rebuilt from
// yaw and pitch alone, and at exactly +/-90 degrees forward and up collapse onto
// the same line, which makes the orbit gizmo and the picking ray disagree.
constexpr float kPitchLimit = kPi * 0.5f - 0.001f;
constexpr float kMinFov = 10.0f;
constexpr float kMaxFov = 170.0f;

void setCamera(float x, float y, float z, float yaw, float pitch) {
    ScriptState* state = activeState();
    if (!state || !state->camera) return;

    // Camera keys its basis cache on yaw and pitch and revalidates on every
    // read, so assigning the fields directly is safe - no refresh needed here.
    state->camera->position = Vec3{x, y, z};
    state->camera->yaw = yaw;
    state->camera->pitch = clampf(pitch, -kPitchLimit, kPitchLimit);
}

void setCameraFov(float degrees) {
    ScriptState* state = activeState();
    if (!state || !state->camera) return;
    state->camera->fovDeg = clampf(degrees, kMinFov, kMaxFov);
}

// --- declarations -----------------------------------------------------------
//
// No namespace in the strings: the host points the engine's default namespace at
// `engine` first, so these land there and scripts spell them engine::setCamera.

const Binding kBindings[] = {
    {"void setCamera(float x, float y, float z, float yaw, float pitch)",
     asFUNCTION(setCamera)},
    {"void setCameraFov(float deg)", asFUNCTION(setCameraFov)},
};

} // namespace

int registerCameraBindings(asIScriptEngine* engine) {
    return registerBindings(engine, kBindings, sizeof(kBindings) / sizeof(kBindings[0]));
}

} // namespace script
} // namespace s3d

#endif // SPECTRA_SCRIPTING