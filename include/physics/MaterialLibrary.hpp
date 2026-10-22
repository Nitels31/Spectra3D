#pragma once
#include "physics/Material.hpp"

namespace s3d {

// A NAMED MATERIAL
//
// A material is how a thing is made of, and the answer to that is a set of
// numbers: how dense it is, how much it slides, how much it bounces. Those
// numbers are the same for every wooden thing in the world, and writing them
// out again for every wooden thing is how a scene ends up with a crate of ice
// and a plank of rubber.
//
// A preset is those numbers with a name on them. The name is the identity of
// the preset and not its position in this table: the table may grow, and a
// scene that says "Concrete" must mean concrete in every version of the
// engine that reads it. So the name is what goes into the scene file, and
// this table is only the place where the numbers behind a name are kept.
struct MaterialPreset {
    // The name of the preset, in English, and the key the scene file uses.
    const char* name;
    // A key for the localisation table, so a preset can be shown in the
    // language of the editor. Empty when the name is used as it is.
    const char* labelKey;
    // How much of it there is in a cubic metre, in kilograms. Ice is lighter
    // than water and a lot lighter than concrete, and that is what makes a
    // block of it feel different in the hand.
    float density;
    // How much it slides against something else, from 0 (a grip that holds on
    // to everything) to 1 (a surface nothing sticks to).
    float friction;
    // How much of a hit it gives back, from 0 (dead) to 1 (a ball of rubber).
    float restitution;
    // How fast the air takes the speed out of a body of it, and how fast it
    // takes the spin out. Both are per second: 0.5 means half of what there
    // is left after a second.
    float linearDamping;
    float angularDamping;
    // The colour of it, so a thing made of it can be told from a thing made
    // of something else at a glance.
    Vec3 color;
};

// How many presets there are. The count is a function and not a constant so
// that the table can grow without every caller having to be told.
int materialPresetCount();

// The preset at `index`, or the first one when the index is out of range.
const MaterialPreset& materialPreset(int index);

// The index of the preset with this name, or -1 when there is no such preset.
// The comparison is exact: the name is an identity and not a word to be
// guessed at.
int materialPresetIndex(const char* name);

// The name of the preset at `index`, or an empty string.
const char* materialPresetName(int index);

// Puts the preset into the material: its numbers and its colour.
//
// The colour comes with it because a colour is what a thing made of ice looks
// like, and a thing that slides like ice and is coloured like a brick is a
// thing whose numbers were changed on purpose. The caller that wants a
// different colour changes it afterwards.
void applyMaterialPreset(Material& material, int index);

// The same, by name. A name that is not in the table changes nothing and says
// so by returning false - a scene that names a material this engine does not
// have keeps the numbers it was saved with.
bool applyMaterialPreset(Material& material, const char* name);

} // namespace s3d
