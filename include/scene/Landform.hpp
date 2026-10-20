#pragma once
#include "assets/MeshAsset.hpp"

namespace s3d {

// A LANDFORM
//
// The shape of the ground, made of noise.
//
// The settings are what a person sets before the ground is made: how low and
// how high it goes, how big it is, and how finely it is cut into squares.
struct LandformSettings {
    // The height of the lowest and the highest square, in the world's units.
    float minHeight = 0.0f;
    float maxHeight = 12.0f;

    // The size of the ground, along X and along Z.
    float width = 40.0f;
    float depth = 40.0f;

    // How many squares across and down. More squares is a finer ground and a
    // slower one to make.
    int cells = 64;

    // The seed of the noise. The same seed is the same landform on every
    // machine.
    unsigned seed = 1;

    // How many threads the making may use. The squares are cut in rows and a
    // row does not depend on the next, so the work spreads.
    int threads = 4;
};

// Makes the ground.
//
// The model it returns is an ordinary model: one part, no texture, the same
// shape an imported one has, so the renderer does not know or care that this
// one was not read from a file.
MeshAsset generateLandform(const LandformSettings& settings);

} // namespace s3d
