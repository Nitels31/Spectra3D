#include "editor/Editor.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "physics/MaterialLibrary.hpp"
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

// The most parameters one body can have: a hard cap so the inspector never
// grows without bound.
constexpr int kMaxParams = 64;

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

// A COLOUR OF THE WATER
//
// A swatch of the colour and the three numbers of it.
//
// The swatch is the colour itself, so that the eye can see what the numbers
// are without reading them - and the three numbers are there because a
// colour cannot be typed into a swatch.
//
// The three numbers share the row: a colour takes three of them and the
// panel is narrow, and a colour with a row of its own for every channel is
// a colour that takes the whole dock.
//
// Says whether one of the numbers was changed.
bool drawColourRow(UiRenderer& ui, UiContext& context, const Rect& dock, float& y, int id,
                   const char* label, Vec3& colour) {
    ui.fillRect(dock.x + 8.0f, y + 1.0f, 14.0f, 14.0f,
                Color{colour.x, colour.y, colour.z});
    ui.textFit(dock.x + 28.0f, y + 2.0f, dock.width - 36.0f, label, theme::textDim);
    y += 19.0f;

    const float third = (dock.width - 12.0f) / 3.0f;
    float* channels[3] = {&colour.x, &colour.y, &colour.z};
    const char* names[3] = {"R", "G", "B"};
    bool changed = false;
    for (int i = 0; i < 3; ++i)
        changed |= context.propertyFine(ui, id + i, dock.x + 6.0f + third * (float)i, y, third,
                                        names[i], *channels[i], 0.002f, 0.0f, 1.0f);
    y += 24.0f;
    return changed;
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
    // Fitted to the field: a placeholder or a typed name longer than the box
    // runs out of it and lands on the panel next to it.
    const float filterScale = ui.textFit(
        filterRect.x + 6.0f, filterRect.y + (filterRect.height - theme::textLineHeight) * 0.5f,
        filterRect.width - 12.0f,
        filter_.empty() && !filter_.editing ? text("filter.placeholder") : filter_.text(),
        filter_.empty() ? theme::textDim : theme::text);
    if (filter_.editing)
        ui.fillRect(filterRect.x + 7.0f + ui.textWidthScaled(filter_.text(), filterScale),
                    filterRect.y + 3.0f, 1.5f, filterRect.height - 6.0f, theme::accent);
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
        // The interface of the game is edited in the same panel, and it is
        // clipped the same way: a field that is scrolled out of the dock is
        // not a field anybody can drag.
        ui_.pushClip(bodyRect);
        if (guiMode_ && selectedGui_ >= 0) {
            float guiY = bodyTop;
            drawGuiInspector(ui, window, dock, guiY);
            inspectorContentHeight_ = guiY - bodyTop + 8.0f;
        } else {
            drawEmptyInspector(ui, dock, bodyTop);
        }
        ui_.popClip();
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
    // The body as it was before the fields below are drawn. The inspector
    // edits the body in place, and this is where the state that a change goes
    // back to comes from.
    const RigidBody before = body;
    drawTransformSection(ui, body, innerDock, y2);
    // A camera has no volume and no material, so it gets its own section
    // instead of the shape and material ones - which would offer fields that
    // mean nothing for it.
if (body.isCamera())
        drawCameraSection(ui, world, body, innerDock, y2);
    else {
        drawShapeSection(ui, body, innerDock, y2);
        // The water of a body that is water: the waves, the colour, the foam
        // and the light. It is a section of its own because there is a great
        // deal of it, and because a box has none of it.
        if (body.isWater()) drawWaterSection(ui, body, innerDock, y2);
        drawMaterialSection(ui, world, body, innerDock, y2);
    }
    // The parameters of the thing: the values a person put on it. Every body
    // has them - a camera has no material and a camera can still have a name.
    drawParamsSection(ui, window, body, innerDock, y2);
    drawFlagsSection(ui, body, innerDock, y2);

    drawSoundSection(ui, body, innerDock, y2);
    drawAnchorSection(ui, body, innerDock, y2);
    drawStateSection(ui, world, body, innerDock, y2);
    drawNodeSection(ui, innerDock, y2);
    // A number that was changed is a change that can be taken back.
    if (!sameBodyState(before, body)) noteBodyEdited(world, activeBody_);
    ui_.popClip();
    ui.popClip();

    // A couple of pixels so the last row is not flush against the bottom edge.
    inspectorContentHeight_ = y2 + inspectorScroll_ - bodyTop + 8.0f;
    if (needsBar) drawScrollbar(ui, bodyRect, inspectorScroll_, inspectorContentHeight_);
}

void Editor::drawEmptyInspector(UiRenderer& ui, const Rect& dock, float y) {
    // Centred in the dock, and nothing that looks like a card floating in the
    // corner: an empty inspector should read as empty space.
    //
    // The title is fitted to the dock like everything else. A title wider than
    // the panel and centred anyway starts to the left of the panel, and the
    // words run off the edge of the window - a message that is cut in half is
    // a message nobody reads.
    const char* title = text("inspector.nothing");
    const char* hint = text("inspector.click");
    const float centreY = y + std::fmax(0.0f, (dock.height - (y - dock.y) - 40.0f) * 0.35f);
    ui.textFit(dock.x + 8.0f, centreY, dock.width - 16.0f, title, theme::textDim);
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
    } else if (body.shape == Shape::Quad) {
        // A square in the XY plane: two numbers, because the thickness is the
        // shape's own business and not the user's.
        if (labelMatches("WIDTH", filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty, dock.x, y, dock.width,
                                         text("prop.sx"), body.halfExtents.x, 0.02f, 0.001f,
                                         99999.0f);
        y += 22.0f;
        if (labelMatches("HEIGHT", filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty + 1, dock.x, y, dock.width,
                                         text("prop.sy"), body.halfExtents.y, 0.02f, 0.001f,
                                         99999.0f);
        y += 24.0f;
        if (changed) body.configure(body.mass);
    } else if (body.shape == Shape::Water) {
        // The water: how wide it is, how deep, and how much of what is under
        // it shows through it. The thickness is the shape's own business, and
        // the level it stands at is its position.
        if (labelMatches("WIDTH", filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty, dock.x, y, dock.width,
                                         text("prop.sx"), body.halfExtents.x, 0.02f, 0.001f,
                                         99999.0f);
        y += 22.0f;
        if (labelMatches("DEPTH", filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty + 1, dock.x, y, dock.width,
                                         text("prop.sz"), body.halfExtents.z, 0.02f, 0.001f,
                                         99999.0f);
        y += 22.0f;
        if (labelMatches("ALPHA", filter_))
            changed |= ui_.propertyFloat(ui, kInspectorProperty + 2, dock.x, y, dock.width,
                                         text("prop.alpha"), body.waterAlpha, 0.005f, 0.0f, 1.0f);
        y += 24.0f;
        if (changed) body.configure(body.mass);
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

// THE WATER
//
// Everything about a body that is water: the colour of it and how the
// colour changes with the depth, the waves and the small ripples on them,
// the light on the surface, the foam, the bend of what is under it and the
// darkening of the low places.
//
// It is a long list, and it is a long list because a sheet of water is a
// great many things at once: it is a mirror at a grazing angle, a window
// from above, a white thing where it breaks and a dark thing where it is
// deep.
void Editor::drawWaterSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y) {
    WaterSettings& water = body.water;
    bool changed = false;

    // --- the colour ---------------------------------------------------------
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.water"));
    y += 26.0f;

    if (ui_.checkbox(ui, kWaterDepthColour, dock.x, y, dock.width, text("water.depthColour"),
                     water.depthColour))
        changed = true;
    y += kRowStep;

    if (labelMatches("SHALLOW", filter_))
        changed |= drawColourRow(ui, ui_, dock, y, kWaterShallow, text("water.shallow"),
                                  water.shallowColour);
    else
        y += 43.0f;
    if (labelMatches("DEEP", filter_))
        changed |= drawColourRow(ui, ui_, dock, y, kWaterDeep, text("water.deep"),
                                  water.deepColour);
    else
        y += 43.0f;

    struct Field {
        const char* filterKey;
        const char* labelKey;
        float* value;
        float step;
        float lo;
        float hi;
    };
    const Field fields[] = {
        {"DEPTH FADE", "water.depthFade", &water.depthFade, 0.005f, 0.0f, 10.0f},
        {"DEPTH", "water.depth", &water.depth, 0.05f, 0.0f, 500.0f},
        {"CLARITY", "water.clarity", &water.clarity, 0.005f, 0.0f, 5.0f},
        {"OPACITY", "water.opacity", &water.opacity, 0.005f, 0.0f, 1.0f},
        {"SHORE", "water.shoreFade", &water.shoreFade, 0.005f, 0.0f, 1.0f},
    };
    const int fieldCount = (int)(sizeof(fields) / sizeof(fields[0]));
    for (int i = 0; i < fieldCount; ++i) {
        if (labelMatches(fields[i].filterKey, filter_))
            changed |= ui_.propertyFloat(ui, kWaterDepthFade + i, dock.x, y, dock.width,
                                         text(fields[i].labelKey), *fields[i].value,
                                         fields[i].step, fields[i].lo, fields[i].hi);
        y += kRowStep;
    }
    y += 4.0f;

    // --- the waves ----------------------------------------------------------
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.waves"));
    y += 26.0f;

    const Field waves[] = {
        {"HEIGHT", "water.waveHeight", &water.waveHeight, 0.01f, 0.0f, 50.0f},
        {"LENGTH", "water.waveLength", &water.waveLength, 0.05f, 0.05f, 500.0f},
        {"SPEED", "water.waveSpeed", &water.waveSpeed, 0.01f, 0.0f, 10.0f},
        {"STEEPNESS", "water.waveSteepness", &water.waveSteepness, 0.005f, 0.0f, 1.0f},
        {"RIPPLE STRENGTH", "water.detailStrength", &water.detailStrength, 0.01f, 0.0f, 10.0f},
        {"RIPPLE SIZE", "water.detailScale", &water.detailScale, 0.01f, 0.05f, 50.0f},
        {"RIPPLE SPEED", "water.detailSpeed", &water.detailSpeed, 0.01f, 0.0f, 10.0f},
    };
    const int waveCount = (int)(sizeof(waves) / sizeof(waves[0]));
    for (int i = 0; i < waveCount; ++i) {
        if (labelMatches(waves[i].filterKey, filter_))
            changed |= ui_.propertyFloat(ui, kWaterWaveHeight + i, dock.x, y, dock.width,
                                         text(waves[i].labelKey), *waves[i].value, waves[i].step,
                                         waves[i].lo, waves[i].hi);
        y += kRowStep;
    }

    // The wind: the way it blows, in the plane of the water. Two numbers,
    // because the plane has two directions and not three.
    {
        const float half = (dock.width - 12.0f) * 0.5f;
        if (labelMatches("WIND", filter_)) {
            changed |= ui_.propertyFloat(ui, kWaterWindX, dock.x + 6.0f, y, half, "X",
                                         water.windDirection.x, 0.01f, -10.0f, 10.0f);
            changed |= ui_.propertyFloat(ui, kWaterWindZ, dock.x + 6.0f + half + 6.0f, y, half,
                                         "Z", water.windDirection.y, 0.01f, -10.0f, 10.0f);
        }
        y += kRowStep;
    }
    y += 4.0f;

    // --- the light ----------------------------------------------------------
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.light"));
    y += 26.0f;

    const Field lights[] = {
        {"REFLECTIVITY", "water.reflectivity", &water.reflectivity, 0.005f, 0.0f, 1.0f},
        {"FRESNEL POWER", "water.fresnelPower", &water.fresnelPower, 0.05f, 0.5f, 12.0f},
        {"FRESNEL AMOUNT", "water.fresnelIntensity", &water.fresnelIntensity, 0.005f, 0.0f, 1.0f},
        {"SUN SHINE", "water.specular", &water.specular, 0.005f, 0.0f, 4.0f},
        {"SHINE TIGHTNESS", "water.shininess", &water.shininess, 1.0f, 1.0f, 2000.0f},
    };
    const int lightCount = (int)(sizeof(lights) / sizeof(lights[0]));
    for (int i = 0; i < lightCount; ++i) {
        if (labelMatches(lights[i].filterKey, filter_))
            changed |= ui_.propertyFloat(ui, kWaterReflectivity + i, dock.x, y, dock.width,
                                         text(lights[i].labelKey), *lights[i].value, lights[i].step,
                                         lights[i].lo, lights[i].hi);
        y += kRowStep;
    }
    y += 4.0f;

    // --- the foam -----------------------------------------------------------
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.foam"));
    y += 26.0f;

    if (labelMatches("FOAM", filter_))
        changed |= ui_.propertyFloat(ui, kWaterFoam, dock.x, y, dock.width, text("water.foam"),
                                     water.foam, 0.005f, 0.0f, 1.0f);
    y += kRowStep;
    if (labelMatches("FOAM COLOUR", filter_))
        changed |= drawColourRow(ui, ui_, dock, y, kWaterFoamColour, text("water.foamColour"),
                                  water.foamColour);
    else
        y += 43.0f;

    // --- the bend of the light ----------------------------------------------
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.refraction"));
    y += 26.0f;

    if (ui_.checkbox(ui, kWaterRefraction, dock.x, y, dock.width, text("water.refraction"),
                     water.refraction))
        changed = true;
    y += kRowStep;
    if (labelMatches("BEND STRENGTH", filter_))
        changed |= ui_.propertyFloat(ui, kWaterRefractionStrength, dock.x, y, dock.width,
                                     text("water.refractionStrength"), water.refractionStrength,
                                     0.005f, 0.0f, 2.0f);
    y += kRowStep;
    y += 4.0f;

    // --- the darkening ------------------------------------------------------
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.darkening"));
    y += 26.0f;

    if (labelMatches("DARKENING", filter_))
        changed |= ui_.propertyFloat(ui, kWaterDarkening, dock.x, y, dock.width,
                                     text("water.darkening"), water.darkening, 0.005f, 0.0f, 1.0f);
    y += kRowStep;

    // The colour of the water is the colour of the body as well, so that the
    // flat water of a flat world is the same water.
    if (changed) markSceneDirty();
}

void Editor::drawCameraSection(UiRenderer& ui, PhysicsWorld& world, RigidBody& body,
                                const Rect& dock, float& y) {    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.camera"));
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

// THE PALETTE OF THE MATERIALS
//
// The materials the engine knows, as a grid of chips: a swatch of the colour
// and the name of it.
//
// The swatch is what makes the grid readable at a glance - sixteen names in a
// column is sixteen rows nobody can hold in their head, and sixteen little
// squares of ice-blue and wood-brown and steel-grey are a picture of the
// materials. The chip of the material that is on the body is the one that is
// lit, so the answer to "what is this made of" is a thing that can be seen
// rather than a thing that has to be read.
void Editor::drawMaterialPalette(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y) {
    const int count = materialPresetCount();
    const float cellWidth = (dock.width - 20.0f) * 0.5f;
    const float cellHeight = 19.0f;
    const float swatch = 13.0f;
    const int chosen = materialPresetIndex(body.material.preset.c_str());

    for (int i = 0; i < count; ++i) {
        const int column = i % 2;
        const int row = i / 2;
        const float x = dock.x + 8.0f + (float)column * (cellWidth + 4.0f);
        const float rowY = y + (float)row * cellHeight;
        const Rect chip{x, rowY, cellWidth, cellHeight - 2.0f};

        bool hovered = false;
        const bool pressed = ui_.chip(ui, kMaterialChip + i, chip, hovered);

        const MaterialPreset& preset = materialPreset(i);
        const bool on = i == chosen;
        if (on) {
            // The chip that is on: a frame of the accent colour around it.
            ui.fillRect(chip.x - 1.0f, chip.y - 1.0f, chip.width + 2.0f, chip.height + 2.0f,
                        theme::accent.scaled(0.55f));
        } else if (hovered) {
            ui.fillRect(chip.x, chip.y, chip.width, chip.height, theme::buttonHover);
        }
        ui.fillRect(chip.x + 2.0f, chip.y + 2.0f, swatch, swatch,
                    Color{preset.color.x, preset.color.y, preset.color.z});
        ui.textFit(chip.x + swatch + 6.0f, chip.y + 2.0f, chip.width - swatch - 10.0f,
                   text(preset.labelKey), on ? theme::text : theme::textDim);

        if (pressed) {
            // The material is put on the body: its numbers and its colour.
            applyMaterialPreset(body.material, i);
            // And the mass follows the density of the new material: the same
            // box of concrete is heavier than the same box of foam.
            body.setMass(body.material.density * body.volume());
            markSceneDirty();
        }
    }

    y += ((float)(count + 1) / 2.0f) * cellHeight + 4.0f;

    // What the material is called, and whether it is one of the library's or
    // one that was written by hand.
    ui.textFit(dock.x + 8.0f, y + 2.0f, dock.width * 0.45f, text("prop.preset"), theme::textDim);
    ui.textFit(dock.x + dock.width * 0.45f, y + 2.0f, dock.width * 0.52f,
               text(materialLabelKey(body)), theme::accent);
    y += kRowStep + 4.0f;
}

// The key of the localisation table for the material that is on the body.
//
// The name of a preset is its identity and the label of it is its
// translation, so the two are two things: a scene file says "Concrete" in
// every language, and the inspector says "Бетон" to a person who reads
// Ukrainian.
const char* Editor::materialLabelKey(const RigidBody& body) const {
    const int index = materialPresetIndex(body.material.preset.c_str());
    if (index < 0) return "material.custom";
    return materialPreset(index).labelKey;
}

void Editor::drawMaterialSection(UiRenderer& ui, PhysicsWorld& world, RigidBody& body,
                                  const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.material"));
    y += 26.0f;

    // The palette of the materials, and the name of the one that is on.
    drawMaterialPalette(ui, body, dock, y);

    // The numbers of the material.
    //
    // Every one of them is read and written through a copy, so that a change
    // can be seen: the density and the mass are two ways of saying the same
    // thing, and one of them has to be able to say "and now the other one
    // has changed".
    bool changed = false;

    // The density: how much of it there is in a cubic metre. It is what the
    // mass is made of, so the two move together.
    float density = body.material.density;
    if (labelMatches("DENSITY", filter_))
        changed |= ui_.propertyFloat(ui, kMaterialNumber, dock.x, y, dock.width,
                                     text("prop.density"), density, 1.0f, 0.0f, 999999.0f);
    if (density != body.material.density) {
        body.setDensity(density);
        // A number that was written by hand is not the number of any of the
        // materials of the library any more.
        body.material.preset.clear();
    }
    y += kRowStep;

    // The friction: how much it slides against something else.
    float friction = body.material.friction;
    if (labelMatches("FRICTION", filter_))
        changed |= ui_.propertyFine(ui, kMaterialNumber + 1, dock.x, y, dock.width,
                                    text("prop.friction"), friction, 0.005f, 0.0f, 2.0f);
    if (friction != body.material.friction) {
        body.material.friction = friction;
        body.material.preset.clear();
    }
    y += kRowStep;

    // The bounce: how much of a hit it gives back.
    float bounce = body.material.restitution;
    if (labelMatches("BOUNCE", filter_) || labelMatches("RESTITUTION", filter_))
        changed |= ui_.propertyFine(ui, kMaterialNumber + 2, dock.x, y, dock.width,
                                    text("prop.bounce"), bounce, 0.005f, 0.0f, 1.0f);
    if (bounce != body.material.restitution) {
        body.material.restitution = bounce;
        body.material.preset.clear();
    }
    y += kRowStep;

    // The mass: how heavy it is. It is the density times the volume, and
    // typing a number here changes the density to match.
    float mass = body.mass;
    if (labelMatches("MASS", filter_))
        changed |= ui_.propertyFloat(ui, kMaterialNumber + 3, dock.x, y, dock.width,
                                     text("prop.mass"), mass, 0.5f, 0.001f, 9999999.0f);
    if (mass != body.mass) {
        body.setMass(mass);
        body.material.preset.clear();
    }
    y += kRowStep;

    // The weight: the force the world puts on it. It is not a number that can
    // be typed into - it is what the mass and the gravity of the world make
    // of each other, and it is here because a thing that weighs four hundred
    // kilograms should be able to be read as four hundred kilograms.
    char weight[64];
    std::snprintf(weight, sizeof(weight), "%.1f N", body.mass * world.gravityMagnitude);
    ui.textFit(dock.x + 8.0f, y + 2.0f, dock.width * 0.55f, text("prop.weight"), theme::textDim);
    ui.textFit(dock.x + dock.width * 0.55f, y + 2.0f, dock.width * 0.42f, weight, theme::text);
    y += kRowStep;

    // The air: how fast the speed is taken out of it, and how fast the spin
    // dies. Both are a fraction per second, and both are zero in a world
    // where nothing slows down on its own.
    float damping = body.material.linearDamping;
    if (labelMatches("DAMPING", filter_) || labelMatches("DRAG", filter_))
        changed |= ui_.propertyFine(ui, kMaterialNumber + 4, dock.x, y, dock.width,
                                    text("prop.damping"), damping, 0.005f, 0.0f, 20.0f);
    if (damping != body.material.linearDamping) {
        body.material.linearDamping = damping;
        body.material.preset.clear();
    }
    y += kRowStep;

    float spin = body.material.angularDamping;
    if (labelMatches("SPIN", filter_) || labelMatches("ANGULAR", filter_))
        changed |= ui_.propertyFine(ui, kMaterialNumber + 5, dock.x, y, dock.width,
                                    text("prop.spin"), spin, 0.005f, 0.0f, 20.0f);
    if (spin != body.material.angularDamping) {
        body.material.angularDamping = spin;
        body.material.preset.clear();
    }
    y += 24.0f;

    if (changed) markSceneDirty();
}

void Editor::drawFlagsSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.flags"));
    y += 26.0f;
    ui_.checkbox(ui, kInspectorProperty + 20, dock.x, y, dock.width, text("flag.static_"),
                 body.material.isStatic);
    y += kRowStep;
    ui_.checkbox(ui, kInspectorProperty + 21, dock.x, y, dock.width, text("flag.visible"),
                 body.visible);
    y += kRowStep;
    ui_.checkbox(ui, kInspectorProperty + 22, dock.x, y, dock.width, text("flag.locked"),
                 body.locked);
    y += kRowStep;
    // Whether the body casts a shadow. A body that does not is still drawn and
    // still catches a shadow - it is only the shadow it casts that is not
    // there, which is what a flat ground plane wants.
    ui_.checkbox(ui, kInspectorProperty + 23, dock.x, y, dock.width, text("flag.castsShadow"),
                 body.castsShadow);
    y += 24.0f;
}

void Editor::drawSoundSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.sound"));
    y += 26.0f;

    if (body.sound.empty()) {
        // No sound: the row says so and the button that would play one is
        // absent rather than dead.
        ui.textFit(dock.x + 8.0f, y + 2.0f, dock.width - 16.0f, text("sound.none"),
                   theme::textDim);
        y += kRowStep;
        return;
    }

    // The name of the sound, a button that plays it, and a button that takes
    // it away. The sound is chosen in the Sounds panel; this is where you
    // hear what you chose and where you undo it.
    ui.textFit(dock.x + 8.0f, y + 2.0f, dock.width - 70.0f, body.sound.c_str(),
               theme::text);
    if (ui_.button(ui, kInspectorProperty + 24, dock.x + dock.width - 56.0f, y, 22.0f, 22.0f, ">"))
        sound_.play(body.sound);
    if (ui_.button(ui, kInspectorProperty + 25, dock.x + dock.width - 30.0f, y, 22.0f, 22.0f, "x")) {
        body.sound.clear();
        markSceneDirty();
    }
    y += kRowStep;

    // How loud the body makes it.
    if (ui_.propertyFloat(ui, kInspectorProperty + 26, dock.x, y, dock.width, text("sound.volume"),
                          body.soundVolume, 0.01f, 0.0f, 1.0f))
        markSceneDirty();
    y += kRowStep;
}

void Editor::drawAnchorSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.anchor"));
    y += 26.0f;

    if (ui_.checkbox(ui, kAnchorField, dock.x, y, dock.width, text("anchor.pinPosition"),
                     body.anchorPosition)) {
        if (body.anchorPosition) body.anchorPoint = body.position;
    }
    y += kRowStep;
    if (ui_.checkbox(ui, kAnchorField + 1, dock.x, y, dock.width, text("anchor.pinRotation"),
                     body.anchorRotation)) {
        if (body.anchorRotation) body.anchorOrientation = body.orientation;
    }
    y += kRowStep;

    if (!body.anchorPosition && !body.anchorRotation) {
        y += 24.0f;
        return;
    }
    ui_.propertyFloat(ui, kAnchorField + 2, dock.x, y, dock.width, text("anchor.spring"),
                      body.anchorSpring, 1.0f, 0.0f, 999999.0f);
    y += kRowStep;
    if (ui_.button(ui, kAnchorField + 3, dock.x + 6.0f, y, dock.width - 12.0f, 22.0f,
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
    if (ui_.button(ui, kInspectorProperty + 27, dock.x + 6.0f, y, wakeW, 24.0f, text("btn.wake"))) {
        body.wake();
        Log::info("Body %d woken", activeBody_);
    }
    if (ui_.button(ui, kInspectorProperty + 28, dock.x + 12.0f + wakeW, y, deleteW, 24.0f,
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

// THE PARAMETERS OF A THING
//
// The values a person put on a body: the health of a crate, the name of the
// player, whether a door is open. The engine knows nothing about what they
// mean - it carries them, saves them with the body and hands them to the
// scripts - and the inspector is where they are written.
//
// A row is four things: the name of the parameter, what kind of value it
// holds, the value itself, and the button that takes the row away. The kind
// is a button and not a dropdown because a dropdown needs a popup and a popup
// in a list of rows is a popup that has to be drawn over the panel below it -
// while a kind that is clicked through is a kind that is seen changing.
void Editor::drawParamsSection(UiRenderer& ui, Window& window, RigidBody& body,
                                const Rect& dock, float& y) {
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("sec.params"));
    y += 26.0f;

    // The fields follow the body that is selected, and they are made again
    // when the list of the parameters changes - and at no other time, because
    // a field that was made again while it was being typed into would throw
    // the typing away.
    if (paramFieldsFor_ != activeBody_ || paramFieldsCount_ != body.params.size()) {
        paramNameFields_.clear();
        paramTextFields_.clear();
        paramNameFields_.reserve(body.params.size());
        paramTextFields_.reserve(body.params.size());
        for (size_t i = 0; i < body.params.size(); ++i) {
            TextField nameField;
            nameField.id = kParamName + (int)i;
            nameField.set(body.params[i].name.c_str());
            paramNameFields_.push_back(nameField);

            TextField textField;
            textField.id = kParamValue + (int)i;
            textField.set(body.params[i].text.c_str());
            paramTextFields_.push_back(textField);
        }
        paramFieldsFor_ = activeBody_;
        paramFieldsCount_ = body.params.size();
    }

    const float rowHeight = theme::fieldHeight + 4.0f;
    const float removeWidth = 22.0f;
    const float kindWidth = 34.0f;
    const float nameWidth = std::fmax(60.0f, dock.width * 0.34f);
    const float valueX = dock.x + 8.0f + nameWidth + kindWidth + 8.0f;
    const float valueWidth = std::fmax(40.0f, dock.width - 16.0f - nameWidth - kindWidth - 8.0f -
                                                   removeWidth - 6.0f);

    bool changed = false;

    for (size_t i = 0; i < body.params.size(); ++i) {
        CustomParam& param = body.params[i];
        const int index = (int)i;
        const float rowY = y;

        // The name of it.
        TextField& nameField = paramNameFields_[i];
        const Rect nameRect{dock.x + 8.0f, rowY, nameWidth, theme::fieldHeight};
        ui_.pumpTextField(window, nameField, ui_.hovered(nameRect));
        ui.fillRect(nameRect.x, nameRect.y, nameRect.width, nameRect.height,
                    nameField.editing ? theme::accent.scaled(0.35f) : theme::field);
        ui.textFit(nameRect.x + 4.0f,
                   nameRect.y + (nameRect.height - theme::textLineHeight) * 0.5f,
                   nameRect.width - 8.0f,
                   nameField.empty() ? text("params.namePlaceholder") : nameField.text(),
                   nameField.empty() ? theme::textDim : theme::text);
        if (param.name != nameField.text()) {
            param.name = nameField.text();
            changed = true;
        }

        // The kind of it: a button that goes round to the next kind.
        const char* kindLabel = text(paramTypeName(param.type));
        if (ui_.button(ui, kParamKind + index, dock.x + 8.0f + nameWidth + 4.0f, rowY, kindWidth,
                       theme::fieldHeight, kindLabel)) {
            param.type = (ParamType)(((int)param.type + 1) % kParamTypeCount);
            changed = true;
        }

        // The value of it.
        switch (param.type) {
            case ParamType::Number: {
                float value = param.number;
                if (ui_.propertyFine(ui, kParamValue + index, valueX, rowY, valueWidth, "",
                                     value, 0.05f, -1e9f, 1e9f))
                    changed = true;
                param.number = value;
                break;
            }
            case ParamType::Integer: {
                int value = (int)std::lround(param.number);
                if (ui_.propertyInt(ui, kParamValue + index, valueX, rowY, valueWidth, "", value,
                                    -1000000, 1000000))
                    changed = true;
                param.number = (float)value;
                break;
            }
            case ParamType::Flag: {
                if (ui_.checkbox(ui, kParamValue + index, valueX, rowY, valueWidth, "",
                                 param.flag))
                    changed = true;
                break;
            }
            case ParamType::Text: {
                TextField& textField = paramTextFields_[i];
                const Rect textRect{valueX, rowY, valueWidth, theme::fieldHeight};
                ui_.pumpTextField(window, textField, ui_.hovered(textRect));
                ui.fillRect(textRect.x, textRect.y, textRect.width, textRect.height,
                            textField.editing ? theme::accent.scaled(0.35f) : theme::field);
                ui.textFit(textRect.x + 4.0f,
                           textRect.y + (textRect.height - theme::textLineHeight) * 0.5f,
                           textRect.width - 8.0f, textField.text(), theme::text);
                if (param.text != textField.text()) {
                    param.text = textField.text();
                    changed = true;
                }
                break;
            }
        }

        // The button that takes the row away.
        if (ui_.button(ui, kParamRemove + index, dock.x + dock.width - removeWidth - 8.0f, rowY,
                       removeWidth, theme::fieldHeight, "x")) {
            // The row goes, and the fields go with it: they are made again
            // for the rows that are left.
            body.params.erase(body.params.begin() + (std::ptrdiff_t)i);
            paramNameFields_.erase(paramNameFields_.begin() + (std::ptrdiff_t)i);
            paramTextFields_.erase(paramTextFields_.begin() + (std::ptrdiff_t)i);
            paramFieldsCount_ = body.params.size();
            changed = true;
            y += rowHeight;
            break;
        }

        y += rowHeight;
    }

    // The button that adds a row.
    if (ui_.button(ui, kParamAdd, dock.x + 8.0f, y, dock.width - 16.0f, 22.0f,
                   text("params.add"))) {
        if (body.params.size() < kMaxParams) {
            CustomParam param;
            // The name is not empty: a parameter without a name is a value
            // that nobody can ask for, and a row that says "new" is a row
            // that says what it is for.
            param.name = "param" + std::to_string(body.params.size() + 1);
            param.type = ParamType::Number;
            param.number = 0.0f;
            body.params.push_back(param);
            changed = true;
        }
    }
    y += 26.0f;

    if (changed) markSceneDirty();
}

} // namespace s3d
