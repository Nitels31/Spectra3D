#include "script/Bindings.hpp"

#include "core/Log.hpp"
#include "core/Math.hpp"
#include "physics/PhysicsWorld.hpp"
#include "render/Camera.hpp"
#include "scene/CameraEntity.hpp"

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

// --- camera entities ---------------------------------------------------------
//
// A camera in the scene is a body with Shape::Camera. It is placed like a body
// and looked through like a camera, and only one of them is the one the game
// looks through at a time.

// Creates a camera in the scene and returns its body index, or -1. It is not
// made active: a script that creates a camera usually has more than one in
// mind, and making this one active would take the view away from whatever was
// using it.
int createCamera(float x, float y, float z, float yaw, float pitch, float fovDeg) {
    ScriptState* state = activeState();
    if (!state || !state->world) return -1;

    Camera view;
    view.position = Vec3{x, y, z};
    view.yaw = yaw;
    view.pitch = clampf(pitch, -kPitchLimit, kPitchLimit);
    view.fovDeg = clampf(fovDeg, kMinFov, kMaxFov);

    RigidBody body;
    body.shape = Shape::Camera;
    body.position = view.position;
    body.orientation = orientationFromCamera(view);
    body.cameraFov = view.fovDeg;
    body.configure(1.0f);
    body.syncPrevTransform();

    state->world->bodies.push_back(body);
    return (int)state->world->bodies.size() - 1;
}

// The body index of the camera the viewport looks through, or -1 when there is
// none and the free camera is in charge.
int getActiveCamera() {
    ScriptState* state = activeState();
    if (!state || !state->world) return -1;
    return state->world->findActiveCamera();
}

// Makes `index` the camera the viewport looks through. A body that is not a
// camera is refused, and -1 hands the view back to the free camera.
void setActiveCamera(int index) {
    ScriptState* state = activeState();
    if (!state || !state->world) return;
    if (index >= 0) {
        const RigidBody* body = state->world->find(index);
        if (!body || !body->isCamera()) {
            Log::error("engine::setActiveCamera: body %d is not a camera", index);
            return;
        }
    }
    state->world->setActiveCamera(index);
}

// --- declarations -----------------------------------------------------------
//
// No namespace in the strings: the host points the engine's default namespace at
// `engine` first, so these land there and scripts spell them engine::setCamera.

const Binding kBindings[] = {
    {"void setCamera(float x, float y, float z, float yaw, float pitch)",
     asFUNCTION(setCamera)},
    {"void setCameraFov(float deg)", asFUNCTION(setCameraFov)},
    {"int createCamera(float x, float y, float z, float yaw, float pitch, "
     "float fovDeg = 70.0)", asFUNCTION(createCamera)},
    {"int getActiveCamera()", asFUNCTION(getActiveCamera)},
    {"void setActiveCamera(int index)", asFUNCTION(setActiveCamera)},
};

} // namespace

int registerCameraBindings(asIScriptEngine* engine) {
    return registerBindings(engine, kBindings, sizeof(kBindings) / sizeof(kBindings[0]));
}

} // namespace script
} // namespace s3d

#endif // SPECTRA_SCRIPTING