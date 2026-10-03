#include "../Editor.hpp"

#include "../../core/Log.hpp"
#include "../../ui/Theme.hpp"

#include <cctype>

namespace s3d {
namespace {

// Filter matching is case insensitive and substring based, matching how editors
// of this kind usually behave.
bool matchesFilter(const char* label, const TextField& filter) {
    if (filter.empty()) return true;
    const char* needle = filter.text();
    const size_t needleLength = (size_t)filter.length;
    for (const char* p = label; *p; ++p) {
        size_t matched = 0;
        while (matched < needleLength && p[matched] &&
               std::tolower((unsigned char)p[matched]) ==
                   std::tolower((unsigned char)needle[matched]))
            ++matched;
        if (matched == needleLength) return true;
    }
    return false;
}

constexpr float kRowStep = 22.0f;

} // namespace

void Editor::drawInspectorDock(UiRenderer& ui, PhysicsWorld& world, Window& window,
                               int fbWidth, int fbHeight) {
    const Rect dock = layout_.rightDock;
    ui.fillRect(dock.x, dock.y, dock.width, dock.height, theme::panel);

    const float halfWidth = dock.width * 0.5f;
    if (ui_.tab(ui, dock.x, dock.y, halfWidth, text("tab.inspector"), inspectorTab_ == 0))
        inspectorTab_ = 0;
    if (ui_.tab(ui, dock.x + halfWidth, dock.y, halfWidth, text("tab.node"),
                inspectorTab_ == 1))
        inspectorTab_ = 1;
    ui.fillRect(dock.x, dock.y + 24.0f, dock.width, 1.0f, theme::separator);

    float y = dock.y + 30.0f;

    // Filter field (inspector tab only).
    if (inspectorTab_ == 0) {
        const float fieldX = dock.x + 6.0f;
        const float fieldW = dock.width - 12.0f;
        ui_.pumpTextField(window, filter_, ui_.hovered(Rect{fieldX, y, fieldW, 22.0f}));
        ui.fillRect(fieldX, y, fieldW, 22.0f, theme::field);
        if (filter_.empty() && !filter_.editing)
            ui.text(fieldX + 6.0f, y + 3.0f, text("filter.placeholder"), theme::textDim);
        else
            ui.text(fieldX + 6.0f, y + 3.0f, filter_.text(), theme::text);
        if (filter_.editing)
            ui.fillRect(fieldX + 7.0f + ui.textWidth(filter_.text()), y + 4.0f, 1.5f, 14.0f,
                        theme::accent.scaled(0.8f));
        y += 28.0f;
    }

    const RigidBody* body = world.find(activeBody_);

    if (inspectorTab_ == 1) {
        if (body) {
            char label[48];
            std::snprintf(label, sizeof(label), "Body_%02d", activeBody_);
            ui.text(dock.x + 10.0f, dock.y + 38.0f, label, theme::text);
            ui.text(dock.x + 10.0f, dock.y + 60.0f, text("node.signals"), theme::textDim);
            ui.text(dock.x + 20.0f, dock.y + 78.0f, text("node.onBodyEntered"),
                    theme::textDim);
            ui.text(dock.x + 20.0f, dock.y + 96.0f, text("node.onSleepChanged"),
                    theme::textDim);
        } else {
            ui.text(dock.x + 10.0f, dock.y + 38.0f, text("node.noNode"), theme::textDim);
        }
        return;
    }

    if (!body) {
        drawEmptyInspectorCard(ui, dock, y);
        return;
    }

    RigidBody& editable = world.bodies[activeBody_];
    drawTransformSection(ui, editable, dock, y);
    drawShapeSection(ui, editable, dock, y);
    drawMaterialSection(ui, editable, dock, y);
    drawFlagsSection(ui, editable, dock, y);
    drawAnchorSection(ui, editable, dock, y);
    drawStateSection(ui, world, editable, dock, y);
}

void Editor::drawEmptyInspectorCard(UiRenderer& ui, const Rect& dock, float y) {
    const float cardX = dock.x + 12.0f;
    const float cardY = y + 30.0f;
    const float cardW = dock.width - 24.0f;
    const float cardH = 74.0f;

    ui.fillRect(cardX, cardY, cardW, cardH, theme::panelDark);
    ui.fillRect(cardX, cardY, cardW, 2.0f, theme::accent.scaled(0.9f));

    const char* title = text("inspector.nothing");
    const char* hint = text("inspector.click");
    ui.text(cardX + (cardW - ui.textWidth(title)) * 0.5f, cardY + 16.0f, title, theme::text);
    ui.text(cardX + (cardW - ui.textWidth(hint)) * 0.5f, cardY + 38.0f, hint,
            theme::textDim);
    ui.text(cardX + (cardW - 92.0f) * 0.5f, cardY + 56.0f, "[ LMB ]  select",
            theme::textDim.scaled(0.9f));
}

void Editor::drawTransformSection(UiRenderer& ui, RigidBody& body, const Rect& dock,
                                  float& y) {
    char label[64];
    std::snprintf(label, sizeof(label), "%s_%02d", shapeName(body.shape), activeBody_);
    ui.text(dock.x + 8.0f, y + 2.0f, label, theme::accent);
    y += 22.0f;

    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.transform"));
    y += 24.0f;

    float* position[3] = {&body.position.x, &body.position.y, &body.position.z};
    const char* keys[3] = {"Position X", "Position Y", "Position Z"};
    const char* labels[3] = {"prop.posX", "prop.posY", "prop.posZ"};
    for (int i = 0; i < 3; ++i) {
        if (matchesFilter(keys[i], filter_))
            ui_.propertyFloat(ui, kInspectorProperty + i, dock.x, y, dock.width,
                              text(labels[i]), *position[i], 0.05f, -99999.0f, 99999.0f);
        y += i == 2 ? 24.0f : kRowStep;
    }
}

void Editor::drawShapeSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.shape"));
    y += 24.0f;

    bool changed = false;
    if (body.hasMesh()) {
        char meshLabel[96];
        std::snprintf(meshLabel, sizeof(meshLabel), "MESH: %s",
                      meshes_.nameOf(body.mesh).c_str());
        ui.text(dock.x + 8.0f, y + 2.0f, meshLabel, theme::textDim);
        y += 22.0f;
        if (matchesFilter("SCALE", filter_)) {
            changed |= ui_.propertyFloat(ui, kInspectorProperty, dock.x, y, dock.width,
                                         "Scale", body.meshScale, 0.01f, 0.001f, 99999.0f);
            if (changed) {
                const MeshAsset* mesh = meshes_.at(body.mesh);
                if (mesh) body.halfExtents = mesh->half * body.meshScale;
                body.configure(body.mass);
            }
        }
        y += 22.0f;
        return;
    }

    // Radius / height / sides per primitive, plus the derived hull.
    float* radius = nullptr;
    float* height = nullptr;
    int* sides = nullptr;
    const char* filterKeys[3] = {nullptr, nullptr, nullptr};
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
            filterKeys[0] = "SX";
            filterKeys[1] = "SY";
            filterKeys[2] = "SZ";
            break;
    }

    if (body.shape == Shape::Sphere) {
        if (matchesFilter("RADIUS", filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty, dock.x, y, dock.width,
                                         text("prop.radius"), body.halfExtents.x, 0.02f,
                                         0.001f, 99999.0f);
        if (changed) {
            body.halfExtents.y = body.halfExtents.z = body.halfExtents.x;
            body.configure(body.mass);
        }
        y += 22.0f;
    } else if (radius) {
        if (matchesFilter(filterKeys[0], filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty, dock.x, y, dock.width,
                                         text("prop.radius"), *radius, 0.02f, 0.001f,
                                         99999.0f);
        y += 22.0f;
        if (height && matchesFilter(filterKeys[1], filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty + 1, dock.x, y, dock.width,
                                         text("prop.height"), *height, 0.02f, 0.001f,
                                         99999.0f);
        y += 22.0f;
        if (sides && matchesFilter(filterKeys[2], filter_))
            changed |= ui_.propertyInt(ui, kInspectorProperty + 2, dock.x, y, dock.width,
                                      text("prop.sides"), *sides, 3, 64);
        y += 22.0f;
        if (changed) {
            if (body.shape == Shape::Cylinder)
                body.halfExtents = {body.cylinderRadius, body.halfExtents.y,
                                    body.cylinderRadius};
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
            if (matchesFilter(filterKeys[i], filter_))
                changed |= ui_.propertyFloat(ui, kInspectorProperty + i, dock.x, y,
                                             dock.width, text(labels[i]), *extents[i], 0.02f,
                                             0.001f, 99999.0f);
            y += 22.0f;
        }
        if (changed) body.configure(body.mass);
    }
    y += 2.0f;
}

void Editor::drawMaterialSection(UiRenderer& ui, RigidBody& body, const Rect& dock,
                                 float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.material"));
    y += 24.0f;

    bool changed = false;
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
    for (int i = 0; i < 4; ++i) {
        if (matchesFilter(fields[i].filterKey, filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty + i, dock.x, y, dock.width,
                                         text(fields[i].labelKey), *fields[i].value,
                                         fields[i].step, 0.001f, 999999.0f);
        y += i == 3 ? 24.0f : kRowStep;
    }
    if (changed) body.configure(body.mass);
}

void Editor::drawFlagsSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.flags"));
    y += 24.0f;

    ui_.checkbox(ui, kInspectorProperty, dock.x, y, dock.width, text("flag.static_"),
                 body.material.isStatic);
    y += kRowStep;
    ui_.checkbox(ui, kInspectorProperty + 1, dock.x, y, dock.width, text("flag.visible"),
                 body.visible);
    y += 24.0f;
    ui_.checkbox(ui, kInspectorProperty + 2, dock.x, y, dock.width, text("flag.locked"),
                 body.locked);
    y += 24.0f;
}

void Editor::drawAnchorSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.anchor"));
    y += 24.0f;

    if (ui_.checkbox(ui, kInspectorProperty + 3, dock.x, y, dock.width,
                     text("anchor.pinPosition"), body.anchorPosition)) {
        if (body.anchorPosition) body.anchorPoint = body.position;
    }
    y += kRowStep;
    if (ui_.checkbox(ui, kInspectorProperty + 4, dock.x, y, dock.width,
                     text("anchor.pinRotation"), body.anchorRotation)) {
        if (body.anchorRotation) body.anchorOrientation = body.orientation;
    }
    y += kRowStep;

    const float buttonX = dock.x + 6.0f;
    const float buttonW = dock.width - 12.0f;
    if (!body.anchorPosition && !body.anchorRotation) {
        y += 24.0f;
        return;
    }

    ui_.propertyFloat(ui, kInspectorProperty + 5, dock.x, y, dock.width,
                      text("anchor.spring"), body.anchorSpring, 1.0f, 0.0f, 999999.0f);
    y += kRowStep;
    if (ui_.button(ui, kInspectorProperty + 6, buttonX, y, buttonW, 22.0f,
                   text("anchor.setAtBody"))) {
        body.anchorPoint = body.position;
        body.anchorOrientation = body.orientation;
    }
    y += 24.0f;
}

void Editor::drawStateSection(UiRenderer& ui, PhysicsWorld& world, RigidBody& body,
                              const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.state"));
    y += 24.0f;

    char line[64];
    std::snprintf(line, sizeof(line), "%s: %.1f m/s", text("state.velocity"),
                  body.velocity.length());
    ui.text(dock.x + 8.0f, y + 2.0f, line, Color{0.60f, 1.0f, 0.65f});
    ui.text(dock.x + dock.width * 0.5f, y + 2.0f,
            body.asleep ? text("state.sleeping") : text("state.active"),
            body.asleep ? Color{0.60f, 0.70f, 0.80f} : Color{0.55f, 1.0f, 0.60f});
    y += 26.0f;

    const float buttonX = dock.x + 6.0f;
    if (ui_.button(ui, kInspectorProperty + 7, buttonX, y, 70.0f, 24.0f, text("btn.wake"))) {
        body.wake();
        Log::info("Body %d woken", activeBody_);
    }
    if (ui_.button(ui, kInspectorProperty + 8, buttonX + 78.0f, y, 76.0f, 24.0f,
                   text("btn.delete_")))
        deleteSelection(world);
}

} // namespace s3d