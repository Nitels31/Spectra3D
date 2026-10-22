#pragma once
#include "core/Math.hpp"

namespace s3d {

// THE SETTINGS OF THE WATER.
//
// A level of water is not a colour and not a transparency. It is a surface
// with waves on it, a colour that changes with the depth under it, white
// water where the wave folds over itself, a mirror when it is seen along its
// length and the picture of the bottom bent by the waves when it is seen
// from above.
//
// Every one of those is a number, and the numbers are here so that the
// editor can change them and the shader can be given them.
//
// They live with the body and not in the renderer: the water is a property of
// the body that is water, and two sheets of water in one scene can look
// nothing alike - a mountain lake is not a puddle in a garden.
//
// The names are the names of the things, and they are the same names the
// shader uses, so that a number in this file and a number in the shader are
// the same number and not two numbers that happen to be near each other.
struct WaterSettings {
    // --- the colour ---------------------------------------------------------

    // Whether the colour of the water changes with the depth of the water
    // under it.
    //
    // Off is one colour everywhere: a sheet of coloured glass, which is what
    // a pond in a cartoon is. On is the colour of a real water: bright where
    // it is thin and dark where it is deep.
    bool depthColour = true;

    // The colour of the water where it is thin, and where it is deep.
    //
    // They are the colours of the water itself and not of the bottom: the
    // bottom has a colour of its own and is seen through the water.
    Vec3 shallowColour{0.16f, 0.55f, 0.56f};
    Vec3 deepColour{0.015f, 0.075f, 0.17f};

    // How fast the one colour becomes the other, in 1/m: how far the light
    // goes through the water before the water is the deep colour.
    //
    // A tenth is a slow change and the water is one colour for the first
    // metre; a whole one is a fast change and the colour is decided in the
    // first few centimetres.
    float depthFade = 0.30f;

    // How deep the water is, in metres: the bottom is this far under the
    // surface.
    //
    // It is the depth of the water at its deepest. The depth of the water at
    // any one place is this much less where the water thins towards its
    // shore, and this much more where a wave has risen.
    float depth = 6.0f;

    // How fast the water swallows the light of the bottom, in 1/m.
    //
    // The red goes first: water eats the red of the light and lets the blue
    // through, and a bottom that is a long way under is a bottom that is
    // blue. Zero is a window and one is a wall of water.
    float clarity = 0.32f;

    // How much of the water's own colour there is against the light of the
    // bottom, 0..1.
    //
    // One is a wall of water and zero is a window: with nothing in between,
    // the water is a tint over the bottom.
    float opacity = 0.9f;

    // How much the water thins towards its own edge, 0..1: the shore.
    //
    // The edge of the sheet is where the water ends and the ground begins,
    // and the water is shallow there and white where it breaks. Zero is a
    // sheet of water with a wall around it.
    float shoreFade = 0.55f;

    // --- the waves ----------------------------------------------------------

    // How high the waves are, in metres.
    float waveHeight = 0.40f;

    // How long they are, in metres: the distance from one crest to the next.
    float waveLength = 9.0f;

    // How fast they travel. One is the speed of the real thing for this
    // length; a half is a pond and a double is a storm.
    float waveSpeed = 1.0f;

    // How sharp the crests are, 0..1. Zero is a flat sea and one is a wave
    // that folds over itself.
    float waveSteepness = 0.55f;

    // The way the wind blows, in the plane of the water.
    //
    // The waves travel with it, and the whole set of them is turned by it:
    // the water does not care which way the first wave went, only which way
    // the wind blows.
    Vec2 windDirection{1.0f, 0.32f};

    // --- the small ripples --------------------------------------------------

    // The ripples that are too small to be in the mesh.
    //
    // The mesh carries the waves, and the mesh is a grid: the ripples that
    // are smaller than a cell of it have to be made per fragment, and they
    // are what turns a sheet of wavy glass into water.
    float detailStrength = 1.0f;

    // How big the ripples are, in metres: the length of the shortest one.
    float detailScale = 0.9f;

    // How fast they travel. They go with the wind and with the waves.
    float detailSpeed = 1.0f;

    // --- the light ----------------------------------------------------------

    // How much of the sky the water gives back, 0..1.
    float reflectivity = 1.0f;

    // How sharp the edge of the reflection is: the power of the Fresnel
    // curve. Five is the water of the real thing - two hundredths of the
    // light straight on and almost all of it at a grazing angle - and a
    // lower number is a mirror and a higher one is a sheet of plastic.
    float fresnelPower = 5.0f;

    // How much of the Fresnel there is, 0..1. This is the strength of the
    // whole effect: zero is a water that gives nothing back.
    float fresnelIntensity = 1.0f;

    // The shine of the sun on the water: how bright it is and how tight it
    // is. A tight shine is the glitter of a path of light on the water.
    float specular = 0.7f;
    float shininess = 180.0f;

    // --- the foam -----------------------------------------------------------

    // How much foam there is, 0..1.
    float foam = 0.7f;

    // The colour of the foam. It is not white: the foam is made of the
    // bubbles of the air the water threw up, and it is the colour of the
    // light that falls on them.
    Vec3 foamColour{0.92f, 0.95f, 0.97f};

    // --- the refraction -----------------------------------------------------

    // Whether the picture under the water is bent by the waves.
    //
    // It is the one thing that makes the bottom of a river look like the
    // bottom of a river and not like a picture of one.
    bool refraction = true;

    // How much the picture is bent, 0..1.
    float refractionStrength = 0.45f;

    // --- the darkening ------------------------------------------------------

    // How much the low places of the water are darkened, 0..1.
    //
    // The trough of a wave is shut in by the wave around it and sees less of
    // the sky, and the water in it is darker. It is the shading of the
    // waves on their own, and it is what makes a sheet of water look like
    // it has a shape.
    float darkening = 0.45f;
};

} // namespace s3d
