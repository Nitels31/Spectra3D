#include "editor/Editor.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "scene/CameraEntity.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace s3d {
namespace {

constexpr float kHeaderHeight = 24.0f;
constexpr float kRowStep = 22.0f;
constexpr float kScrollWidth = 6.0f;
// How far one wheel notch moves a scrolling dock. Enough to cross a section, small
// enough to land on a row.
constexpr float kScrollStep = 26.0f;

// A dock taller than its contents shows no bar at all, and a bar that covers the
// whole height means there is nothing to scroll to. Both are drawn from the same
// two numbers.
void drawScrollbar(UiRenderer& ui, const Rect& body, float offset, float contentHeight) {
    if (contentHeight <= body.height + 0.5f) return;
    const float trackHeight = body.height;
    const float thumbHeight = std::fmax(24.0f, trackHeight * trackHeight / contentHeight);
    const float travel = trackHeight - thumbHeight;
    const float maxOffset = contentHeight - trackHeight;
    const float thumbY = body.y + travel * (maxOffset > 0.0f ? clampf(offset / maxOffset, 0.0f, 1.0f) : 0.0f);

    ui.fillRect(body.x + body.width - kScrollWidth, body.y, kScrollWidth, trackHeight,
                theme::panelDark);
    ui.fillRect(body.x + body.width - kScrollWidth, thumbY, kScrollWidth, thumbHeight,
                theme::textDim.scaled(0.55f));
}

// Case-insensitive substring match against a property label. The needle is folded
// once here instead of once per comparison; str::lowerAscii is an inline range
// test where std::tolower was an out-of-line, locale-aware call.
bool labelMatches(const char* label, const TextField& filter) {
    if (filter.empty()) return true;
    const char* needle = filter.text();
    const size_t needleLength = (size_t)filter.length;
    char lowered[64];
    for (size_t i = 0; i < needleLength; ++i) lowered[i] = str::lowerAscii(needle[i]);

    for (const char* p = label; *p; ++p) {
        size_t matched = 0;
        while (matched < needleLength && p[matched] &&
               str::lowerAscii(p[matched]) == lowered[matched])
            ++matched;
        if (matched == needleLength) return true;
    }
    return false;
}

} // namespace

void Editor::drawInspectorDock(UiRenderer& ui, PhysicsWorld& world, Window& window,
                               const Rect& dock) {
    // One header, one filter, one column of properties. The old second tab held
    // three static strings; it is a section at the bottom now instead of a tab
    // that hid the whole inspector behind it.
    ui.fillRect(dock.x, dock.y, dock.width, kHeaderHeight, theme::panelDark);
    ui.text(dock.x + 18.0f, dock.y + 5.0f, text("tab.inspector"), theme::text);
    ui.fillRect(dock.x, dock.y + kHeaderHeight - 1.0f, dock.width, 1.0f, theme::separator);

    float y = dock.y + kHeaderHeight + 4.0f;
    const Rect filterRect{dock.x + 6.0f, y, dock.width - 12.0f, theme::fieldHeight};
    ui_.pumpTextField(window, filter_, ui_.hovered(filterRect));
    ui.fillRect(filterRect.x, filterRect.y, filterRect.width, filterRect.height,
                filter_.editing ? theme::accent.scaled(0.35f) : theme::field);
    ui.text(filterRect.x + 6.0f,
            filterRect.y + (filterRect.height - theme::textLineHeight) * 0.5f,
            filter_.empty() && !filter_.editing ? text("filter.placeholder") : filter_.text(),
            filter_.empty() ? theme::textDim : theme::text);
    if (filter_.editing)
        ui.fillRect(filterRect.x + 7.0f + ui.textWidth(filter_.text()), filterRect.y + 3.0f, 1.5f,
                    filterRect.height - 6.0f, theme::accent);
    y += filterRect.height + 6.0f;

    // --- body: clipped to the dock and scrollable -------------------------
    //
    // The dock ends where the console begins, and a body with every section on it
    // is taller than that. Without a clip the last sections were drawn straight
    // over the console's level filters; without a scroll they were drawn but never
    // reachable. So the body is a scissored region that follows the pointer's
    // wheel, and a bar on the right edge shows how much of it is off screen.
    const float bodyTop = y;
    const Rect bodyRect{dock.x, bodyTop, dock.width,
                        std::fmax(0.0f, dock.y + dock.height - bodyTop)};

    const float maxScroll =
        std::fmax(0.0f, inspectorContentHeight_ - bodyRect.height);
    // Clamped against the height measured while drawing the previous frame: the
    // total is only known once the sections have been laid out, and measuring
    // twice would double the work of the one panel that redraws on every frame.
    inspectorScroll_ = clampf(inspectorScroll_, 0.0f, maxScroll);
    if (ui_.hovered(bodyRect) && ui_.scrollDelta() != 0.0)
        inspectorScroll_ = clampf(inspectorScroll_ - (float)ui_.scrollDelta() * kScrollStep,
                                  0.0f, maxScroll);

    const RigidBody* found = world.find(activeBody_);
    if (!found) {
        // Nothing selected: there is nothing long to scroll through.
        inspectorScroll_ = 0.0f;
        ui.pushClip(bodyRect);
        drawEmptyInspector(ui, dock, bodyTop);
        ui.popClip();
        return;
    }

    // The bar takes its width out of the panel, so the sections are laid out in
    // what is left - otherwise a numeric field ends up under the bar and its text
    // stops one character early for no visible reason.
    const bool needsBar = maxScroll > 0.5f;
    const Rect innerDock{dock.x, dock.y, dock.width - (needsBar ? kScrollWidth : 0.0f),
                         dock.height};

    // Clipped twice on purpose: once for the drawing, so the sections cannot paint
    // over the panel's own header, and once for the hit test, so a property that
    // has been scrolled out of the body is not still a draggable field - it is
    // underneath the console, and a click there belongs to the console.
    ui.pushClip(bodyRect);
    ui_.pushClip(bodyRect);
    float y2 = bodyTop - inspectorScroll_;
    RigidBody& body = world.bodies[(size_t)activeBody_];
    drawTransformSection(ui, body, innerDock, y2);
    // A camera has no volume and no material, so it gets its own section
    // instead of the shape and material ones - which would offer fields that
    // mean nothing for it.
    if (body.isCamera())
        drawCameraSection(ui, world, body, innerDock, y2);
    else {
        drawShapeSection(ui, body, innerDock, y2);
        drawMaterialSection(ui, body, innerDock, y2);
    }
    drawFlagsSection(ui, body, innerDock, y2);
    drawAnchorSection(ui, body, innerDock, y2);
    drawStateSection(ui, world, body, innerDock, y2);
    drawNodeSection(ui, innerDock, y2);
    ui_.popClip();
    ui.popClip();

    // A couple of pixels so the last row is not flush against the bottom edge.
    inspectorContentHeight_ = y2 + inspectorScroll_ - bodyTop + 8.0f;
    if (needsBar) drawScrollbar(ui, bodyRect, inspectorScroll_, inspectorContentHeight_);
}

void Editor::drawEmptyInspector(UiRenderer& ui, const Rect& dock, float y) {
    // Centred in the dock, and nothing that looks like a card floating in the
    // corner: an empty inspector should read as empty space.
    const char* title = text("inspector.nothing");
    const char* hint = text("inspector.click");
    const float centreY = y + std::fmax(0.0f, (dock.height - (y - dock.y) - 40.0f) * 0.35f);
    ui.text(dock.x + (dock.width - ui.textWidth(title)) * 0.5f, centreY, title, theme::textDim);
    ui.textFit(dock.x + 8.0f, centreY + 20.0f, dock.width - 16.0f, hint, theme::textDim);
}

void Editor::drawTransformSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y) {
    char label[64];
    formatIndexed(label, sizeof(label), shapeName(body.shape), '_', activeBody_);
    ui.textFit(dock.x + 8.0f, y + 2.0f, dock.width - 16.0f, label, theme::accent);
    y += 22.0f;

    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.transform"));
    y += 26.0f;

    float* position[3] = {&body.position.x, &body.position.y, &body.position.z};
    const char* keys[3] = {"Position X", "Position Y", "Position Z"};
    const char* labels[3] = {"prop.posX", "prop.posY", "prop.posZ"};
    for (int i = 0; i < 3; ++i) {
        if (labelMatches(keys[i], filter_))
            ui_.propertyFloat(ui, kInspectorProperty + i, dock.x, y, dock.width, text(labels[i]),
                              *position[i], 0.05f, -99999.0f, 99999.0f);
        y += i == 2 ? 24.0f : kRowStep;
    }
}

void Editor::drawShapeSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.shape"));
    y += 26.0f;

    bool changed = false;
    if (body.hasMesh()) {
        char meshLabel[96];
        formatPrefixed(meshLabel, sizeof(meshLabel), "MESH: ", meshes_.nameOf(body.mesh).c_str());
        ui.textFit(dock.x + 8.0f, y + 2.0f, dock.width - 16.0f, meshLabel, theme::textDim);
        y += 22.0f;
        if (labelMatches("SCALE", filter_)) {
            changed |= ui_.propertyFloat(ui, kInspectorProperty, dock.x, y, dock.width, "Scale",
                                         body.meshScale, 0.01f, 0.001f, 99999.0f);
            if (changed) {
                const MeshAsset* mesh = meshes_.at(body.mesh);
                if (mesh) body.halfExtents = mesh->half * body.meshScale;
                body.configure(body.mass);
            }
        }
        y += 24.0f;
        return;
    }

    // Radius / height / sides per primitive, plus the hull derived from them.
    float* radius = nullptr;
    float* height = nullptr;
    int* sides = nullptr;
    const char* filterKeys[3] = {"SX", "SY", "SZ"};
    switch (body.shape) {
        case Shape::Sphere:
            radius = &body.halfExtents.x;
            filterKeys[0] = "RADIUS";
            break;
        case Shape::Cylinder:
            radius = &body.cylinderRadius;
            height = &body.halfExtents.y;
            filterKeys[0] = "RADIUS";
            filterKeys[1] = "HEIGHT";
            break;
        case Shape::Capsule:
            radius = &body.capsuleRadius;
            height = &body.capsuleHeight;
            filterKeys[0] = "RADIUS";
            filterKeys[1] = "HEIGHT";
            break;
        case Shape::Cone:
            radius = &body.coneRadius;
            height = &body.coneHeight;
            filterKeys[0] = "RADIUS";
            filterKeys[1] = "HEIGHT";
            break;
        case Shape::Pyramid:
            radius = &body.pyramidBaseRadius;
            height = &body.pyramidHeight;
            filterKeys[0] = "RADIUS";
            filterKeys[1] = "HEIGHT";
            break;
        case Shape::Prism:
            radius = &body.prismRadius;
            height = &body.prismHeight;
            sides = &body.prismSides;
            filterKeys[0] = "RADIUS";
            filterKeys[1] = "HEIGHT";
            filterKeys[2] = "SIDES";
            break;
        default:
            break;
    }

    if (body.shape == Shape::Sphere) {
        if (labelMatches("RADIUS", filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty, dock.x, y, dock.width,
                                         text("prop.radius"), body.halfExtents.x, 0.02f, 0.001f,
                                         99999.0f);
        if (changed) {
            body.halfExtents.y = body.halfExtents.z = body.halfExtents.x;
            body.configure(body.mass);
        }
        y += 24.0f;
    } else if (radius) {
        if (labelMatches(filterKeys[0], filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty, dock.x, y, dock.width,
                                         text("prop.radius"), *radius, 0.02f, 0.001f, 99999.0f);
        y += 22.0f;
        if (height && labelMatches(filterKeys[1], filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty + 1, dock.x, y, dock.width,
                                         text("prop.height"), *height, 0.02f, 0.001f, 99999.0f);
        y += 22.0f;
        if (sides && labelMatches(filterKeys[2], filter_))
            changed |= ui_.propertyInt(ui, kInspectorProperty + 2, dock.x, y, dock.width,
                                       text("prop.sides"), *sides, 3, 64);
        y += 24.0f;
        if (changed) {
            if (body.shape == Shape::Cylinder)
                body.halfExtents = {body.cylinderRadius, body.halfExtents.y, body.cylinderRadius};
            else if (body.shape == Shape::Capsule)
                body.halfExtents = {body.capsuleRadius,
                                    body.capsuleHeight * 0.5f + body.capsuleRadius,
                                    body.capsuleRadius};
            else if (body.shape == Shape::Cone)
                body.halfExtents = {body.coneRadius, body.coneHeight * 0.5f, body.coneRadius};
            else if (body.shape == Shape::Pyramid)
                body.halfExtents = {body.pyramidBaseRadius, body.pyramidHeight * 0.5f,
                                    body.pyramidBaseRadius};
            else if (body.shape == Shape::Prism)
                body.halfExtents = {body.prismRadius, body.prismHeight * 0.5f, body.prismRadius};
            body.configure(body.mass);
        }
    } else {
        float* extents[3] = {&body.halfExtents.x, &body.halfExtents.y, &body.halfExtents.z};
        const char* labels[3] = {"prop.sx", "prop.sy", "prop.sz"};
        for (int i = 0; i < 3; ++i) {
            if (labelMatches(filterKeys[i], filter_))
                changed |= ui_.propertyFloat(ui, kInspectorProperty + i, dock.x, y, dock.width,
                                             text(labels[i]), *extents[i], 0.02f, 0.001f, 99999.0f);
            y += 22.0f;
        }
        if (changed) body.configure(body.mass);
        y += 2.0f;
    }
}

void Editor::drawCameraSection(UiRenderer& ui, PhysicsWorld& world, RigidBody& body,
                                const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.camera"));
    y += 26.0f;

    if (labelMatches("FOV", filter_))
        ui_.propertyFloat(ui, kInspectorProperty + 9, dock.x, y, dock.width, text("prop.fov"),
                          body.cameraFov, 1.0f, 10.0f, 170.0f);
    y += kRowStep;

    // Yaw and pitch, because the camera is aimed with them and a body carries a
    // quaternion - which no field can be typed into. This is also the only way
    // to aim a camera that is the active one: the viewport is looking out of
    // it, so its gizmo is inside the eye and is not drawn.
    //
    // In degrees, because the engine keeps them in radians and nobody types
    // radians into a field.
    if (labelMatches("YAW", filter_) || labelMatches("PITCH", filter_)) {
        const Camera view = cameraFromEntity(body);
        const float toDegrees = 180.0f / kPi;
        float yawDeg = view.yaw * toDegrees;
        float pitchDeg = view.pitch * toDegrees;
        bool changed = false;
        if (labelMatches("YAW", filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty + 11, dock.x, y, dock.width,
                                         text("prop.yaw"), yawDeg, 1.0f, -360.0f, 360.0f);
        y += kRowStep;
        if (labelMatches("PITCH", filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty + 12, dock.x, y, dock.width,
                                         text("prop.pitch"), pitchDeg, 1.0f, -89.0f, 89.0f);
        if (changed) {
            Camera aimed = view;
            aimed.yaw = yawDeg / toDegrees;
            aimed.pitch = pitchDeg / toDegrees;
            body.orientation = orientationFromCamera(aimed);
            body.syncPrevTransform();
            markSceneDirty();
        }
    }
    y += kRowStep;

    // The active camera is the one the viewport looks through. Only one body
    // can hold it, so setting it takes the flag off every other camera - which
    // is what setActiveCamera does.
    if (ui_.checkbox(ui, kInspectorProperty + 10, dock.x, y, dock.width, text("cam.active"),
                     body.activeCamera)) {
        world.setActiveCamera(body.activeCamera ? activeBody_ : -1);
        markSceneDirty();
    }
    y += 24.0f;
}

void Editor::drawMaterialSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.material"));
    y += 26.0f;

    struct Field {
        const char* filterKey;
        const char* labelKey;
        float* value;
        float step;
    };
    const Field fields[4] = {
        {"MASS", "prop.mass", &body.mass, 1.0f},
        {"FRICTION", "prop.friction", &body.material.friction, 0.005f},
        {"RESTITUTION", "prop.bounce", &body.material.restitution, 0.005f},
        {"DENSITY", "prop.density", &body.material.density, 0.01f},
    };
    bool changed = false;
    for (int i = 0; i < 4; ++i) {
        if (labelMatches(fields[i].filterKey, filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty + i, dock.x, y, dock.width,
                                         text(fields[i].labelKey), *fields[i].value,
                                         fields[i].step, 0.001f, 999999.0f);
        y += i == 3 ? 24.0f : kRowStep;
    }
    if (changed) body.configure(body.mass);
}

void Editor::drawFlagsSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.flags"));
    y += 26.0f;
    ui_.checkbox(ui, kInspectorProperty, dock.x, y, dock.width, text("flag.static_"),
                 body.material.isStatic);
    y += kRowStep;
    ui_.checkbox(ui, kInspectorProperty + 1, dock.x, y, dock.width, text("flag.visible"),
                 body.visible);
    y += kRowStep;
    ui_.checkbox(ui, kInspectorProperty + 2, dock.x, y, dock.width, text("flag.locked"),
                 body.locked);
    y += 24.0f;
}

void Editor::drawAnchorSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.anchor"));
    y += 26.0f;

    if (ui_.checkbox(ui, kInspectorProperty + 3, dock.x, y, dock.width, text("anchor.pinPosition"),
                     body.anchorPosition)) {
        if (body.anchorPosition) body.anchorPoint = body.position;
    }
    y += kRowStep;
    if (ui_.checkbox(ui, kInspectorProperty + 4, dock.x, y, dock.width, text("anchor.pinRotation"),
                     body.anchorRotation)) {
        if (body.anchorRotation) body.anchorOrientation = body.orientation;
    }
    y += kRowStep;

    if (!body.anchorPosition && !body.anchorRotation) {
        y += 24.0f;
        return;
    }
    ui_.propertyFloat(ui, kInspectorProperty + 5, dock.x, y, dock.width, text("anchor.spring"),
                      body.anchorSpring, 1.0f, 0.0f, 999999.0f);
    y += kRowStep;
    if (ui_.button(ui, kInspectorProperty + 6, dock.x + 6.0f, y, dock.width - 12.0f, 22.0f,
                   text("anchor.setAtBody"))) {
        body.anchorPoint = body.position;
        body.anchorOrientation = body.orientation;
    }
    y += 24.0f;
}

void Editor::drawStateSection(UiRenderer& ui, PhysicsWorld& world, RigidBody& body,
                              const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.state"));
    y += 26.0f;

    char line[64];
    std::snprintf(line, sizeof(line), "%s: %.1f m/s", text("state.velocity"),
                  body.velocity.length());
    ui.textFit(dock.x + 8.0f, y + 2.0f, dock.width * 0.55f, line, Color{0.60f, 1.0f, 0.65f});
    const char* state = body.asleep ? text("state.sleeping") : text("state.active");
    const Color stateColor = body.asleep ? Color{0.60f, 0.70f, 0.80f} : Color{0.55f, 1.0f, 0.60f};
    ui.textFit(dock.x + dock.width * 0.55f, y + 2.0f, dock.width * 0.42f, state, stateColor);
    y += 26.0f;

    // Buttons sized to their labels: a fixed 70 pixel button clips a longer word
    // in one language and looks empty in the other.
    const float wakeW = ui.textWidth(text("btn.wake")) + 20.0f;
    const float deleteW = ui.textWidth(text("btn.delete_")) + 20.0f;
    if (ui_.button(ui, kInspectorProperty + 7, dock.x + 6.0f, y, wakeW, 24.0f, text("btn.wake"))) {
        body.wake();
        Log::info("Body %d woken", activeBody_);
    }
    if (ui_.button(ui, kInspectorProperty + 8, dock.x + 12.0f + wakeW, y, deleteW, 24.0f,
                   text("btn.delete_")))
        deleteSelection(world);
    y += 28.0f;
}

void Editor::drawNodeSection(UiRenderer& ui, const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("tab.node"));
    y += 26.0f;
    ui.text(dock.x + 10.0f, y, text("node.onBodyEntered"), theme::textDim);
    y += kRowStep;
    ui.text(dock.x + 10.0f, y, text("node.onSleepChanged"), theme::textDim);
    y += kRowStep;
}

} // namespace s3d