#pragma once

#include "core/Math.hpp"

namespace s3d {

// THE FOG
//
// The fog is the air between the eye and the thing. It is the one part of the
// picture that is not a thing: there is nothing to point at and nothing to
// move, and it is what tells the eye how far away everything is.
//
// It is two numbers and not one, and they answer different questions:
//
//  - how much of it is on the way - the density, and how fast it thins as it
//    goes up;
//  - what colour it is - the colour of the air, and how much of the sun's
//    light it carries.
//
// The first is measured in metres and the second in light, and the two are
// mixed per fragment: the far things lose their colour and gain the colour of
// the air, and the air near the sun is brighter than the air away from it.
//
// The fog lives here and not with the shadows because it is not a shadow: it
// has no map, no cascade and no bias, and it is not a comparison of two
// numbers. It is the air, and the renderer is what draws it.

struct FogSettings {
    // The colour of the fog: what the far things fade towards.
    //
    // It is the colour of the air and not the colour of the sky, and the two
    // are usually close: a fog that is a different colour from the sky it
    // stands against draws a line along the horizon, and the line is the one
    // thing a fog must not have.
    Vec3 colour{0.62f, 0.72f, 0.86f};

    // How thick the fog is where it is thickest, in 1/m.
    //
    // It is read as a distance: the fog halves itself every 0.69 / density
    // metres. 0.004 is a quarter of a kilometre of visibility, 0.02 is a
    // hundred metres, and zero is no fog at all - which is what a scene that
    // was made before the fog came has.
    float density = 0.0f;

    // How fast the fog thins as it goes up, in 1/m.
    //
    // The fog is thickest at `height` and thins as it goes up, and this is how
    // fast: the air is half as thick every 0.69 / falloff metres of height.
    // 0.04 is twenty-five metres, which is the height of a house and about the
    // height of a mist on a still morning. Zero is a fog that is the same
    // everywhere: plain distance haze, with no ground to it.
    float falloff = 0.04f;

    // The height at which the fog is thickest, in metres.
    //
    // The ground of a valley collects it and the top of a hill stands out of
    // it, and this is the height of the bottom of the valley.
    float height = 0.0f;

    // How much of the sun's light the fog scatters into the eye, 0..1.
    //
    // It is what makes the fog glow when the sun is behind it: the drops of
    // water throw the light of the sun forward, and a fog that does not do
    // that is a flat grey sheet that hides the sun instead of carrying it.
    //
    // A quarter of it is a glow that can be looked at: the light of the sun
    // comes forward, and the air in front of the sun is bright without being
    // white all the way to the edge of it.
    float scatter = 0.25f;

    // How forward the fog throws the light of the sun, 0..0.95.
    //
    // It is the shape of the glow around the sun: zero is a fog that scatters
    // equally in every direction - a sun behind it is as bright as a sun to
    // the side of it - and 0.8 is a fog that throws the light of the sun
    // straight back at whoever looks at it, so that the sun in it is a lamp
    // with a halo and not a hole in the sky.
    //
    // The fog is made of small drops of water, and the drops of a real mist
    // throw the light forward hard: 0.7 is the look of a morning mist and 0.4
    // is the look of a haze that is more air than water.
    float forward = 0.7f;
};

} // namespace s3d
