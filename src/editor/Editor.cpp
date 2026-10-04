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
constexpr float kViewportTopInset = 28.0f;
constexpr float kChromeHeight = 26.0f;
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
}

bool Editor::consumeSingleStep() {
    const bool requested = singleStepRequested_;
    singleStepRequested_ = false;
    return requested;
}

void Editor::recomputeLayout(int fbWidth, int fbHeight) {
    const float width = (float)fbWidth;
    const float height = (float)fbHeight;

    layout_.menuBar = Rect{0.0f, 0.0f, width, theme::menuBarHeight};
    layout_.leftDock = Rect{0.0f, theme::menuBarHeight, theme::leftDockWidth,
                            height - theme::menuBarHeight - theme::statusBarHeight};
    layout_.rightDock = Rect{width - theme::rightDockWidth, theme::menuBarHeight,
                             theme::rightDockWidth,
                             height - theme::menuBarHeight - theme::statusBarHeight};

    const float viewY = theme::menuBarHeight + kViewportTopInset;
    layout_.viewport = Rect{theme::leftDockWidth, viewY,
                            std::fmax(kMinViewportSize, width - theme::leftDockWidth -
                                                              theme::rightDockWidth),
                            std::fmax(kMinViewportSize, height - viewY -
                                                              theme::statusBarHeight)};
    layout_.viewportChrome =
        Rect{layout_.viewport.x, layout_.viewport.y, layout_.viewport.width, kChromeHeight};
    layout_.statusBar = Rect{0.0f, height - theme::statusBarHeight, width,
                             theme::statusBarHeight};
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

bool Editor::tryGizmoGrip(PhysicsWorld& world, Camera& camera) {
    if (!gizmoEnabled_) return false;
    const RigidBody* body = world.find(activeBody_);
    if (!body) return false;

    const Rect viewport = layout_.viewport;
    const Vec3 rayOrigin = camera.position;
    const Vec3 rayDir = viewportRay(camera);
    dragAxis_ = gizmo::pickAxis(ui_.pointerX() - viewport.x, ui_.pointerY() - viewport.y,
                                rayOrigin, rayDir, camera.position, viewRay_.forward,
                                viewRay_.right, viewRay_.up, viewRay_.tanX, viewRay_.tanY,
                                viewport, *body, gizmoMode_);
    if (dragAxis_ < 0) return false;

    dragOrigin_ = body->position;
    Vec3 axes[3];
    gizmo::axesFor(*body, gizmoMode_, axes);
    float rayT = 0.0f;
    picking::closestOnAxis(rayOrigin, rayDir, dragOrigin_, axes[dragAxis_],
                           dragStartAxisT_, rayT);
    return true;
}

void Editor::applyGizmoDrag(PhysicsWorld& world, Camera& camera) {
    RigidBody* body = world.find(activeBody_);
    if (!body) return;

    const Vec3 rayOrigin = camera.position;
    const Vec3 rayDir = viewportRay(camera);

    Vec3 axes[3];
    gizmo::axesFor(*body, gizmoMode_, axes);
    float axisT = 0.0f, rayT = 0.0f;
    picking::closestOnAxis(rayOrigin, rayDir, dragOrigin_, axes[dragAxis_], axisT, rayT);

    if (gizmoMode_ == gizmo::Mode::Move) {
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
    const bool onChrome = ui_.pointerY() >= layout_.viewportChrome.y &&
                          ui_.pointerY() < layout_.viewportChrome.y +
                                                 layout_.viewportChrome.height;
    if (!ui_.hovered(viewport) || onChrome) {
        if (ui_.pointerReleased()) dragAxis_ = -1;
        return;
    }

    if (tryNavigationGizmo(camera)) return;

    if (ui_.pointerPressed()) {
        if (tryGizmoGrip(world, camera)) {
            const RigidBody* body = world.find(activeBody_);
            if (body && gizmoMode_ == gizmo::Mode::Scale) {
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
    if (!gizmoEnabled_) return;
    const RigidBody* body = world.find(activeBody_);
    if (!body) return;

    Vec3 axes[3];
    gizmo::axesFor(*body, gizmoMode_, axes);
    scene.renderGizmo(body->position, axes, gizmo::handleLength(*body), dragAxis_,
                      gizmoMode_ == gizmo::Mode::Scale);
}

void Editor::renderUi(UiRenderer& ui, PhysicsWorld& world, Camera& camera, Window& window,
                      int fbWidth, int fbHeight) {
    ui.begin(fbWidth, fbHeight);

    drawMenuBar(ui, world, fbWidth);
    drawSceneDock(ui, world, camera, fbHeight);
    drawInspectorDock(ui, world, window, fbWidth, fbHeight);
    drawViewportChrome(ui, camera);
    drawStatusBar(ui, world, fbWidth, fbHeight);

    ui.end();
    pruneSelection(world);
}

} // namespace s3d