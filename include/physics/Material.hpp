#pragma once
#include <algorithm>
#include <cmath>
#include <string>

#include "core/Math.hpp"

namespace s3d {

// Surface response of a body. `isStatic` turns the body into an immovable
// collider that still pushes dynamic bodies.
//
// The numbers here are what the simulation reads and nothing else reads them:
// the friction of a body is the friction of the contact it makes, and the
// restitution of a body is how hard it hits back. A body whose friction is
// 0.1 slides ten times as easily as a body whose friction is 1.
//
// `preset` is the name of the material this body was given out of the library
// - "Concrete", "Ice", "Metal" - and it is empty for a material that was
// written by hand. It is a name and not a number because the numbers of a
// preset may change with the engine while its name does not: a scene that
// says "Concrete" keeps being concrete even when the table of materials
// grows a row under it.
struct Material {
    // How much of it there is in a cubic metre, in kilograms. It is what the
    // mass of a body is made of: the mass is this number times the volume of
    // the body, so a bigger box of the same stuff is heavier.
    float density = 1.0f;
    // How much it slides against something else.
    float friction = 0.5f;
    // How much of a hit it gives back.
    float restitution = 0.3f;
    // How fast the air takes the speed out of it, and how fast it takes the
    // spin out. Both per second: 0.5 means half of what there was is left
    // after a second. Zero is nothing at all, and that is the default,
    // because a world where everything slows down on its own is a world
    // where a ball never rolls as far as it should.
    float linearDamping = 0.0f;
    float angularDamping = 0.0f;
    // An immovable collider: it is never pushed, and it pushes everything
    // that touches it.
    bool isStatic = false;
    // The name of the material this came out of the library, or empty when
    // the numbers were written by hand.
    std::string preset;

    Vec3 color{0.7f, 0.7f, 0.7f};
};

// How two surfaces slide against each other.
//
// The geometric mean, and not the average: the average of 0 and 1 is 0.5, so
// a thing that grips like rubber would let a thing that slides like ice slide
// half as much as it should. The geometric mean of 0 and 1 is 0 - one of the
// two grips and the pair grips as much as the grippy one - and the mean of
// 0.5 and 0.5 is still 0.5, so nothing that was right before is wrong now.
inline float combineFriction(const Material& a, const Material& b) {
    const float fa = std::clamp(a.friction, 0.0f, 2.0f);
    const float fb = std::clamp(b.friction, 0.0f, 2.0f);
    return std::sqrt(fa * fb);
}

// How hard a pair of surfaces hits back.
//
// The bouncier one wins, because that is what happens: a ball of rubber on a
// floor of concrete leaves the floor at nearly the speed it arrived with, and
// the floor does not get any bouncier because it is the heavier of the two.
inline float combineRestitution(const Material& a, const Material& b) {
    return std::clamp(std::max(a.restitution, b.restitution), 0.0f, 1.0f);
}

} // namespace s3d
