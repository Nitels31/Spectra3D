#include "Editor.hpp"

#include "../core/Log.hpp"
#include "../scene/SceneSerializer.hpp"
#include "../ui/Theme.hpp"
#include "Picking.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace s3d {
namespace {

constexpr float kMinViewportSize = 120.0f;
// "Scene" strip above the viewport, and the vertical tool column inside it.
constexpr float kSceneHeaderHeight = 26.0f;
constexpr float kToolStripWidth = 34.0f;
// Console height under the viewport and the inspector.
constexpr float kConsoleHeight = 156.0f;
// Fallback when no project is open: next to the binary.
const char* const kDefaultSceneFile = "scene.txt";

// Navigation balls: camera orientation presets, offsets from the widget centre.
struct NavBall {
    float dx, dy;
    float yaw, pitch;
};

const NavBall kNavBalls[6] = {
    {0.0f, -30.0f, 0.0f, -1.55f},      // look down +Y
    {0.0f, 30.0f, 0.0f, 1.55f},       // look up -Y
    {30.0f, 8.0f, -kPi * 0.5f, 0.0f}, // +X
    {-30.0f, 8.0f, kPi * 0.5f, 0.0f}, // -X
    {-26.0f, 26.0f, kPi, 0.0f},       // +Z
    {26.0f, -26.0f, 0.0f, 0.0f},      // -Z
};

} // namespace

Editor::Editor(MeshRegistry& meshes, i18n::Localizer& localizer)
    : meshes_(meshes), loc_(localizer) {
    filter_.id = kFilterField;
    toolbarSearch_.id = kToolbarSearch;
}

bool Editor::consumeSingleStep() {
    const bool requested = singleStepRequested_;
    singleStepRequested_ = false;
    return requested;
}

void Editor::recomputeLayout(int fbWidth, int fbHeight) {
    const float width = (float)fbWidth;
    const float height = (float)fbHeight;

    const float consoleH = std::fmin(kConsoleHeight, height * 0.34f);

    layout_.menuBar = Rect{0.0f, 0.0f, width, theme::menuBarHeight};

    // The console spans the viewport and the inspector, not the whole window: the
    // left column keeps its full height, which is what the sketch shows, and the
    // console reaches the bottom of the window because there is no status bar under
    // it any more.
    const float consoleY = height - consoleH;
    layout_.console = Rect{theme::leftDockWidth, consoleY,
                           std::fmax(0.0f, width - theme::leftDockWidth), consoleH};

    const float columnTop = theme::menuBarHeight;
    const float columnH = std::fmax(0.0f, consoleY - columnTop);

    layout_.leftDock = Rect{0.0f, columnTop, theme::leftDockWidth, columnH};
    layout_.rightDock = Rect{std::fmax(0.0f, width - theme::rightDockWidth), columnTop,
                             theme::rightDockWidth, columnH};

    const float viewX = theme::leftDockWidth;
    const float viewW = std::fmax(kMinViewportSize,
                                  width - theme::leftDockWidth - theme::rightDockWidth);
    const float headerH = kSceneHeaderHeight;
    layout_.sceneHeader = Rect{viewX, columnTop, viewW, headerH};
    layout_.viewport =
        Rect{viewX, columnTop + headerH, viewW,
             std::fmax(kMinViewportSize, consoleY - columnTop - headerH)};
    layout_.viewportTools = Rect{layout_.viewport.x + 4.0f, layout_.viewport.y + 4.0f,
                                 kToolStripWidth, 0.0f};
    layout_.viewportTools.height = std::max(0.0f, layout_.viewport.height - 8.0f);
}

void Editor::pruneSelection(const PhysicsWorld& world) {
    const int count = (int)world.bodies.size();
    if (activeBody_ >= count) activeBody_ = -1;
    selectedBodies_.erase(
        std::remove_if(selectedBodies_.begin(), selectedBodies_.end(),
                       [&](int index) { return index < 0 || index >= count; }),
        selectedBodies_.end());
}

void Editor::deleteSelection(PhysicsWorld& world) {
    std::sort(selectedBodies_.rbegin(), selectedBodies_.rend());
    for (int index : selectedBodies_) {
        if (index < 0 || index >= (int)world.bodies.size()) continue;
        // A locked body stays. "Locked" has to mean something the editor enforces,
        // and the one thing a lock is for is that a stray Delete cannot throw work
        // away.
        if (world.bodies[(size_t)index].locked) {
            Log::info("Body %d is locked", index);
            continue;
        }
        Log::info("Deleted body %d", index);
        world.remove(index);
    }
    selectedBodies_.clear();
    activeBody_ = -1;
}

void Editor::updateViewRay(const Camera& camera) {
    if (viewRay_.valid) return;
    camera.basis(viewRay_.forward, viewRay_.right, viewRay_.up);
    const float tanY = std::tan(camera.fovDeg * kPi / 360.0f);
    viewRay_.tanY = tanY;
    viewRay_.tanX = tanY * layout_.viewport.width / layout_.viewport.height;
    viewRay_.valid = true;
}

Vec3 Editor::viewportRay(const Camera& camera) {
    updateViewRay(camera);
    return picking::rayFromViewport(ui_.pointerX(), ui_.pointerY(), layout_.viewport,
                                    viewRay_.forward, viewRay_.right, viewRay_.up,
                                    viewRay_.tanX, viewRay_.tanY);
}

void Editor::appendDecimal(char* out, int& at, unsigned long long value, int minDigits) {
    char digits[24];
    int count = 0;
    do {
        digits[count++] = (char)('0' + (int)(value % 10ull));
        value /= 10ull;
    } while (value != 0);

    char padded[24];
    const int pad = minDigits > count ? minDigits - count : 0;
    for (int i = 0; i < pad; ++i) padded[i] = '0';
    for (int i = 0; i < count; ++i) padded[pad + i] = digits[count - 1 - i];
    for (int i = 0; i < pad + count; ++i) out[at++] = padded[i];
}

void Editor::formatIndexed(char* out, size_t size, const char* name, char separator,
                           int index) {
    const size_t nameLength = std::strlen(name);
    // Longest possible output is name + separator + sign + 10 digits + NUL.
    if (size < nameLength + 13u) {
        if (separator)
            std::snprintf(out, size, "%s%c%02d", name, separator, index);
        else
            std::snprintf(out, size, "%s%02d", name, index);
        return;
    }

    std::memcpy(out, name, nameLength);
    int at = (int)nameLength;
    if (separator) out[at++] = separator;
    // The '0' flag pads between the sign and the digits, so a negative value
    // never reaches the two digit minimum.
    if (index < 0) {
        out[at++] = '-';
        appendDecimal(out, at, (unsigned long long)(-(long long)index), 1);
    } else {
        appendDecimal(out, at, (unsigned long long)index, 2);
    }
    out[at] = '\0';
}

void Editor::formatPrefixed(char* out, size_t size, const char* prefix, const char* value) {
    const size_t prefixLength = std::strlen(prefix);
    const size_t valueLength = std::strlen(value);
    if (size < prefixLength + valueLength + 1u) {
        std::snprintf(out, size, "%s%s", prefix, value);
        return;
    }
    std::memcpy(out, prefix, prefixLength);
    std::memcpy(out + prefixLength, value, valueLength);
    out[prefixLength + valueLength] = '\0';
}

void Editor::handleInput(PhysicsWorld& world, Camera& camera, Window& window, int fbWidth,
                         int fbHeight) {
    ui_.beginInput(window);
    viewRay_.valid = false;
    recomputeLayout(fbWidth, fbHeight);
    handleHotkeys(world, window);
    handleViewport(world, camera, window);
}

void Editor::handleHotkeys(PhysicsWorld& world, Window& window) {
    // Deliberately empty of state-changing shortcuts.
    //
    // Play / Pause / Stop and save / load used to be bound to keys. Every one of
    // them mutates or discards the scene, and a stray keypress while typing or
    // working silently threw work away - stopping play mode restores the snapshot
    // taken when play began, so anything built during the run is gone.
    // Mode changes now only happen through the buttons in the menu bar.
    (void)world;
    (void)window;
}

bool Editor::tryNavigationGizmo(Camera& camera) {
    const Rect viewport = layout_.viewport;
    const float centerX = viewport.x + viewport.width - 52.0f;
    const float centerY = viewport.y + 52.0f;
    if (!ui_.pointerPressed()) return false;

    for (const NavBall& ball : kNavBalls) {
        const float dx = ui_.pointerX() - (centerX + ball.dx);
        const float dy = ui_.pointerY() - (centerY + ball.dy);
        if (dx * dx + dy * dy >= 144.0f) continue;
        camera.yaw = ball.yaw;
        camera.pitch = ball.pitch;
        viewRay_.valid = false;
        return true;
    }
    return false;
}

bool Editor::gizmoModeFor(Tool tool, gizmo::Mode& modeOut) const {
    switch (tool) {
    case Tool::Move: modeOut = gizmo::Mode::Move; return true;
    case Tool::Rotate: modeOut = gizmo::Mode::Rotate; return true;
    case Tool::Scale: modeOut = gizmo::Mode::Scale; return true;
    // Select picks bodies and Pan moves the camera; neither has handles.
    case Tool::Select:
    case Tool::Pan: return false;
    }
    return false;
}

bool Editor::tryGizmoGrip(PhysicsWorld& world, Camera& camera) {
    gizmo::Mode mode = gizmo::Mode::Move;
    if (!gizmoModeFor(tool_, mode)) return false;
    const RigidBody* body = world.find(activeBody_);
    if (!body) return false;
    // Same rule as Delete: a locked body shows no handles to grab, so the lock
    // cannot be argued with by dragging.
    if (body->locked) return false;

    const Rect viewport = layout_.viewport;
    const Vec3 rayOrigin = camera.position;
    const Vec3 rayDir = viewportRay(camera);
    const float pointerX = ui_.pointerX() - viewport.x;
    const float pointerY = ui_.pointerY() - viewport.y;

    dragAxis_ = mode == gizmo::Mode::Rotate
                    ? gizmo::pickRing(pointerX, pointerY, rayOrigin, rayDir, camera.position,
                                      viewRay_.forward, viewRay_.right, viewRay_.up,
                                      viewRay_.tanX, viewRay_.tanY, viewport, *body)
                    : gizmo::pickAxis(pointerX, pointerY, rayOrigin, rayDir, camera.position,
                                      viewRay_.forward, viewRay_.right, viewRay_.up,
                                      viewRay_.tanX, viewRay_.tanY, viewport, *body, mode);
    if (dragAxis_ < 0) return false;

    dragOrigin_ = body->position;
    Vec3 axes[3];
    gizmo::axesFor(*body, mode, axes);

    if (mode == gizmo::Mode::Rotate) {
        // Remember where the pointer sat on the ring, in the ring's own frame, so
        // the drag is a delta from that and not an absolute angle.
        dragStartOrientation_ = body->orientation;
        rotateStartAngle_ = pointerAngleOnRing(rayOrigin, rayDir, *body, axes[dragAxis_],
                                               pointerX, pointerY, camera);
        return true;
    }

    float rayT = 0.0f;
    picking::closestOnAxis(rayOrigin, rayDir, dragOrigin_, axes[dragAxis_],
                           dragStartAxisT_, rayT);
    return true;
}

// Where the pointer ray crosses the plane the ring lies in, as an angle around
// `axis`. Returns 0 when the ray is too close to parallel to that plane to give a
// meaningful answer, which leaves the body where it was instead of spinning it.
float Editor::pointerAngleOnRing(const Vec3& rayOrigin, const Vec3& rayDir,
                                 const RigidBody& body, const Vec3& axis, float pointerX,
                                 float pointerY, const Camera& camera) {
    const float denominator = rayDir.x * axis.x + rayDir.y * axis.y + rayDir.z * axis.z;
    if (std::fabs(denominator) < 1e-3f) return 0.0f;

    const Vec3 toPlane = body.position - rayOrigin;
    const float t = (toPlane.x * axis.x + toPlane.y * axis.y + toPlane.z * axis.z) / denominator;
    if (t <= 0.0f) return 0.0f;

    const Vec3 hit = rayOrigin + rayDir * t - body.position;
    Vec3 u, v;
    gizmo::ringFrame(axis, u, v);
    return std::atan2(hit.x * v.x + hit.y * v.y + hit.z * v.z,
                      hit.x * u.x + hit.y * u.y + hit.z * u.z);
}

void Editor::applyGizmoDrag(PhysicsWorld& world, Camera& camera) {
    RigidBody* body = world.find(activeBody_);
    if (!body) return;

    const Rect viewport = layout_.viewport;
    const Vec3 rayOrigin = camera.position;
    const Vec3 rayDir = viewportRay(camera);

    gizmo::Mode mode = gizmo::Mode::Move;
    if (!gizmoModeFor(tool_, mode)) return;

    Vec3 axes[3];
    gizmo::axesFor(*body, mode, axes);

    if (mode == gizmo::Mode::Rotate) {
        // The pointer's angle around the axis now, minus where it started: a delta
        // applied to the orientation the body had when the ring was grabbed.
        const float angle = pointerAngleOnRing(rayOrigin, rayDir, *body, axes[dragAxis_],
                                               ui_.pointerX() - viewport.x,
                                               ui_.pointerY() - viewport.y, camera);
        const float delta = angle - rotateStartAngle_;
        if (std::fabs(delta) < 1e-4f) return;
        body->orientation =
            (Quat::axisAngle(axes[dragAxis_], delta) * dragStartOrientation_).normalized();
        body->wake();
        return;
    }

    float axisT = 0.0f, rayT = 0.0f;
    picking::closestOnAxis(rayOrigin, rayDir, dragOrigin_, axes[dragAxis_], axisT, rayT);

    if (mode == gizmo::Mode::Move) {
        body->position = dragOrigin_ + axes[dragAxis_] * (axisT - dragStartAxisT_);
    } else {
        float factor =
            std::fabs(dragStartAxisT_) < 1e-3f ? 1.0f : axisT / dragStartAxisT_;
        factor = clampf(factor, 0.05f, 20.0f);
        const float scaled = dragStartScale_ * factor;

        if (body->hasMesh()) {
            body->meshScale = clampf(scaled, 0.05f, 10.0f);
            const MeshAsset* mesh = meshes_.at(body->mesh);
            if (mesh) body->halfExtents = mesh->half * body->meshScale;
            body->configure(body->mass);
        } else if (body->shape == Shape::Sphere) {
            const float radius = clampf(scaled, 0.05f, 10.0f);
            body->halfExtents = {radius, radius, radius};
            body->configure(body->mass);
        } else {
            (&body->halfExtents.x)[dragAxis_] = clampf(scaled, 0.05f, 10.0f);
            body->configure(body->mass);
        }
    }
    body->wake();
}

void Editor::handleViewport(PhysicsWorld& world, Camera& camera, Window& window) {
    if (ui_.cameraCaptured()) {
        dragAxis_ = -1;
        return;
    }

const Rect viewport = layout_.viewport;
    // A press on the tool column or the "Scene" strip is a UI press, not a drag in
    // the scene: without this, clicking GIZ or M would also grab whatever is under
    // the pointer in the world.
    const Rect tools = layout_.viewportTools;
    const bool onChrome = ui_.hovered(tools) ||
                          ui_.hovered(layout_.sceneHeader);
    if (!ui_.hovered(viewport) || onChrome) {
        if (ui_.pointerReleased()) dragAxis_ = -1;
        return;
    }

    if (tryNavigationGizmo(camera)) return;

    // Pan is a viewport drag, not a UI press: start it here and keep feeding it
    // below, so the camera tracks the pointer for as long as the button is down.
    if (tool_ == Tool::Pan) {
        if (ui_.pointerPressed()) {
            panStartPointer_ = {ui_.pointerX(), ui_.pointerY(), 0.0f};
            panStartCamera_ = camera.position;
            // One screen pixel of drag per world unit at the focal plane, so the
            // scene follows the pointer at any zoom or field of view. The plane is
            // taken at whatever the user is looking at: the selection if there is
            // one, otherwise the origin.
            Vec3 reference{0.0f, 0.0f, 0.0f};
            if (const RigidBody* body = world.find(activeBody_)) reference = body->position;
            const float distance = (camera.position - reference).length();
            panPixelsPerUnit_ = std::fmax(0.001f, distance * 2.0f *
                                                        std::tan(camera.fovDeg * 0.5f * kPi / 180.0f) /
                                                        std::fmax(1.0f, viewport.height));
            viewRay_.valid = false;
            return;
        }
        if (ui_.pointerDown()) {
            Vec3 forward, right, up;
            camera.basis(forward, right, up);
            const float dx = ui_.pointerX() - panStartPointer_.x;
            const float dy = ui_.pointerY() - panStartPointer_.y;
            camera.position =
                panStartCamera_ - right * (dx * panPixelsPerUnit_) + up * (dy * panPixelsPerUnit_);
            viewRay_.valid = false;
            return;
        }
        return;
    }

    if (ui_.pointerPressed()) {
        if (tryGizmoGrip(world, camera)) {
            const RigidBody* body = world.find(activeBody_);
            gizmo::Mode mode = gizmo::Mode::Move;
            if (body && gizmoModeFor(tool_, mode) && mode == gizmo::Mode::Scale) {
                if (body->hasMesh()) dragStartScale_ = body->meshScale;
                else if (body->shape == Shape::Sphere) dragStartScale_ = body->halfExtents.x;
                else dragStartScale_ = (&body->halfExtents.x)[dragAxis_];
            }
            return;
        }
    }

    if (ui_.pointerDown() && dragAxis_ >= 0 && world.find(activeBody_)) {
        applyGizmoDrag(world, camera);
        if (ui_.pointerReleased()) dragAxis_ = -1;
        return;
    }
    if (ui_.pointerReleased() && dragAxis_ >= 0) {
        dragAxis_ = -1;
        return;
    }
    if (ui_.pointerPressed())
        handleSelectionClick(world, camera.position, viewportRay(camera));
}

void Editor::handleSelectionClick(PhysicsWorld& world, const Vec3& rayOrigin,
                                  const Vec3& rayDir) {
    const int hit = picking::pickBody(rayOrigin, rayDir, world);

    if (!ui_.ctrlHeld()) selectedBodies_.clear();
    if (hit < 0) {
        activeBody_ = -1;
        return;
    }

    if (ui_.ctrlHeld()) {
        const auto it = std::find(selectedBodies_.begin(), selectedBodies_.end(), hit);
        if (it != selectedBodies_.end()) selectedBodies_.erase(it);
        else selectedBodies_.push_back(hit);
    } else {
        selectedBodies_.assign(1, hit);
    }
    activeBody_ = hit;
}

void Editor::enterPlayMode(PhysicsWorld& world) {
    editSnapshot_ = world.bodies;
    snapshotGravity_ = world.gravityAxis;
    playMode_ = true;
    playing_ = true;
    playStarted_ = true;
    selectedBodies_.clear();
    world.wakeAll();
    Log::info(text("log.play"), (int)editSnapshot_.size());
}

void Editor::exitPlayMode(PhysicsWorld& world) {
    world.bodies = editSnapshot_;
    world.setGravity(snapshotGravity_);
    playMode_ = false;
    playing_ = false;
    timeResetRequested_ = true;
    dragAxis_ = -1;
    Log::info(text("log.stop"));
}

void Editor::saveScene(const PhysicsWorld& world) {
    if (scenes::save(world, meshes_, scenePath()))
        Log::info(text("log.saved"), (int)world.bodies.size());
}

void Editor::loadScene(PhysicsWorld& world) {
    if (!scenes::load(world, meshes_, scenePath()))
        return;
    activeBody_ = -1;
    selectedBodies_.clear();
    Log::info(text("log.loaded"), (int)world.bodies.size());
}

void Editor::renderOverlay(SceneRenderer& scene, PhysicsWorld& world) {
    for (int index : selectedBodies_) {
        const RigidBody* body = world.find(index);
        if (body) scene.renderSelection(*body);
    }
    const RigidBody* body = world.find(activeBody_);
    if (!body) return;

    // Only the tool that is actually active draws handles: Select and Pan show
    // none, and a gizmo left over from the previous tool would be a lie about
    // what a drag would do.
    gizmo::Mode mode = gizmo::Mode::Move;
    if (!gizmoModeFor(tool_, mode)) return;

    Vec3 axes[3];
    gizmo::axesFor(*body, mode, axes);

    Vec3 ringU[3], ringV[3];
    for (int i = 0; i < 3; ++i) gizmo::ringFrame(axes[i], ringU[i], ringV[i]);

    SceneRenderer::GizmoShape shape = SceneRenderer::GizmoShape::Arrows;
    if (mode == gizmo::Mode::Rotate) shape = SceneRenderer::GizmoShape::Rings;
    else if (mode == gizmo::Mode::Scale) shape = SceneRenderer::GizmoShape::Cubes;

    scene.renderGizmo(body->position, axes, gizmo::handleLength(*body),
                      gizmo::ringRadius(*body), ringU, ringV, dragAxis_, shape);
}

void Editor::renderUi(UiRenderer& ui, PhysicsWorld& world, Camera& camera, Window& window,
                      int fbWidth, int fbHeight) {
    ui.begin(fbWidth, fbHeight);

    drawTopBar(ui, world, camera, window, fbWidth);
    drawLeftColumn(ui, world, camera, window, fbWidth);
    drawInspectorDock(ui, world, window, fbWidth, fbHeight);
    drawSceneHeader(ui, world, camera);
    drawViewportTools(ui, camera);
    drawConsolePanel(ui);
    // Overlays last, and after a flush: the dropdowns hang over the panels below
    // the top bar, and the batcher emits every rectangle before any text, so
    // without the flush their background would land under their neighbours' labels.
    ui.flush();
    drawTopBarOverlay(ui, world, camera, window, fbWidth, fbHeight);

    ui.end();
    pruneSelection(world);
}

} // namespace s3d
