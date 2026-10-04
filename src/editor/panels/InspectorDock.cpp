#include "../Editor.hpp"

#include "../../core/Log.hpp"
#include "../../core/Str.hpp"
#include "../../ui/Theme.hpp"

#include <cstdio>

namespace s3d {
namespace {

constexpr float kHeaderHeight = 24.0f;
constexpr float kRowStep = 22.0f;

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
                               int fbWidth, int fbHeight) {
    const Rect dock = layout_.rightDock;
    ui.fillRect(dock.x, dock.y, dock.width, dock.height, theme::panel);

    // One header, one filter, one column of properties. The old second tab held
    // three static strings; it is a section at the bottom now instead of a tab
    // that hid the whole inspector behind it.
    ui.fillRect(dock.x, dock.y, dock.width, kHeaderHeight, theme::panelDark);
    ui.text(dock.x + 8.0f, dock.y + 5.0f, text("tab.inspector"), theme::text);
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

    const RigidBody* found = world.find(activeBody_);
    if (!found) {
        drawEmptyInspector(ui, dock, y, fbWidth);
        return;
    }

    RigidBody& body = world.bodies[(size_t)activeBody_];
    drawTransformSection(ui, body, dock, y);
    drawShapeSection(ui, body, dock, y);
    drawMaterialSection(ui, body, dock, y);
    drawFlagsSection(ui, body, dock, y);
    drawAnchorSection(ui, body, dock, y);
    drawStateSection(ui, world, body, dock, y);
    drawNodeSection(ui, dock, y);
    (void)fbHeight;
}

void Editor::drawEmptyInspector(UiRenderer& ui, const Rect& dock, float y, int fbWidth) {
    // Centred in the dock, and nothing that looks like a card floating in the
    // corner: an empty inspector should read as empty space.
    const char* title = text("inspector.nothing");
    const char* hint = text("inspector.click");
    const float centreY = y + std::fmax(0.0f, (dock.height - (y - dock.y) - 40.0f) * 0.35f);
    ui.text(dock.x + (dock.width - ui.textWidth(title)) * 0.5f, centreY, title, theme::textDim);
    ui.textFit(dock.x + 8.0f, centreY + 20.0f, dock.width - 16.0f, hint, theme::textDim);
    (void)fbWidth;
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