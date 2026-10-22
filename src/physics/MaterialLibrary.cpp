#include "physics/MaterialLibrary.hpp"

namespace s3d {
namespace {

// THE MATERIALS OF THE WORLD
//
// The numbers are the real ones, in the units the real world uses: the
// density is in kilograms per cubic metre, the friction is the friction of
// the surface against something like it, and the restitution is how much of a
// hit comes back.
//
// The densities are the reason the engine can be honest about weight. A
// cubic metre of concrete is 2400 kilograms, and a cubic metre of ice is 917
// of them, and a crate that weighs more than a person is a crate that has to
// be pushed harder. The numbers here are the ones a game uses to decide
// that.
//
// The order is the order of the table in the inspector, and the names are
// what a scene file stores, so neither of them may be changed without
// thinking about the scenes that were written before.
const MaterialPreset kPresets[] = {
    // name        label key              density  friction  bounce  linDamp angDamp  colour
    {"Ice",       "material.ice",           917.0f,  0.05f,  0.10f, 0.02f, 0.05f, {0.72f, 0.88f, 0.96f}},
    {"Wood",      "material.wood",          700.0f,  0.55f,  0.20f, 0.05f, 0.10f, {0.62f, 0.42f, 0.24f}},
    {"Concrete",  "material.concrete",     2400.0f,  0.70f,  0.05f, 0.00f, 0.05f, {0.66f, 0.66f, 0.64f}},
    {"Metal",     "material.metal",        7800.0f,  0.45f,  0.25f, 0.00f, 0.02f, {0.70f, 0.74f, 0.80f}},
    {"Stone",     "material.stone",        2600.0f,  0.75f,  0.05f, 0.00f, 0.05f, {0.55f, 0.53f, 0.50f}},
    {"Rubber",    "material.rubber",       1100.0f,  0.95f,  0.85f, 0.10f, 0.20f, {0.20f, 0.20f, 0.22f}},
    {"Glass",     "material.glass",        2500.0f,  0.20f,  0.35f, 0.02f, 0.03f, {0.80f, 0.90f, 0.92f}},
    {"Plastic",   "material.plastic",      1200.0f,  0.40f,  0.55f, 0.03f, 0.08f, {0.30f, 0.55f, 0.85f}},
    {"Sand",      "material.sand",         1600.0f,  0.85f,  0.02f, 0.30f, 0.40f, {0.85f, 0.75f, 0.52f}},
    {"Snow",      "material.snow",          300.0f,  0.60f,  0.05f, 0.15f, 0.20f, {0.94f, 0.96f, 0.99f}},
    {"Leather",   "material.leather",       900.0f,  0.65f,  0.15f, 0.08f, 0.15f, {0.44f, 0.26f, 0.14f}},
    {"Clay",      "material.clay",         1700.0f,  0.80f,  0.03f, 0.20f, 0.30f, {0.72f, 0.48f, 0.36f}},
    {"Grass",     "material.grass",         300.0f,  0.70f,  0.05f, 0.25f, 0.35f, {0.42f, 0.70f, 0.32f}},
    {"Gold",      "material.gold",        19300.0f,  0.35f,  0.30f, 0.00f, 0.01f, {0.95f, 0.78f, 0.28f}},
    {"Foam",      "material.foam",          100.0f,  0.80f,  0.15f, 0.60f, 0.60f, {0.94f, 0.94f, 0.90f}},
    {"Water",     "material.water",        1000.0f,  0.05f,  0.10f, 0.40f, 0.50f, {0.20f, 0.45f, 0.80f}},
};

const int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

} // namespace

int materialPresetCount() { return kPresetCount; }

const MaterialPreset& materialPreset(int index) {
    if (index < 0 || index >= kPresetCount) index = 0;
    return kPresets[index];
}

int materialPresetIndex(const char* name) {
    if (!name || !*name) return -1;
    for (int i = 0; i < kPresetCount; ++i)
        if (std::string(kPresets[i].name) == name) return i;
    return -1;
}

const char* materialPresetName(int index) {
    if (index < 0 || index >= kPresetCount) return "";
    return kPresets[index].name;
}

void applyMaterialPreset(Material& material, int index) {
    if (index < 0 || index >= kPresetCount) return;
    const MaterialPreset& preset = kPresets[index];
    material.density = preset.density;
    material.friction = preset.friction;
    material.restitution = preset.restitution;
    material.linearDamping = preset.linearDamping;
    material.angularDamping = preset.angularDamping;
    material.color = preset.color;
    material.preset = preset.name;
}

bool applyMaterialPreset(Material& material, const char* name) {
    const int index = materialPresetIndex(name);
    if (index < 0) return false;
    applyMaterialPreset(material, index);
    return true;
}

} // namespace s3d
