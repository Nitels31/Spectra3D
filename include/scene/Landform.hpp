#pragma once
#include <string>
#include <vector>

#include "assets/MeshAsset.hpp"

namespace s3d {

// The shape the ground takes.
//
// The noise under every one of them is the same; what differs is what is done
// to the noise afterwards. That is why the shapes are here and not inside the
// noise: the noise knows how to make hills, and it does not know what a
// plateau is.
enum class LandformShape {
    Hills,     // rolling ground, the plainest of them
    Mountains, // ridges and peaks, with the ground falling away at the sides
    Island,    // ground that sinks into the water at the edges
    Plateau,   // flat tops with steps between them
    Canyon,    // a river cut through the ground
    Dunes,     // round hills, stretched along one axis
};

// How many shapes there are, and the name of each. The panel asks for the
// name and the index, so the two cannot get out of step.
inline constexpr int kLandformShapeCount = 6;
const char* landformShapeName(LandformShape shape);

// A LANDFORM
//
// The shape of the ground, made of noise.
//
// The settings are what a person sets before the ground is made: how low and
// how high it goes, how big it is, how finely it is cut into squares, and
// what the noise is made of.
struct LandformSettings {
    // The height of the lowest and the highest square, in the world's units.
    float minHeight = 0.0f;
    float maxHeight = 12.0f;

    // The size of the ground, along X and along Z.
    float width = 40.0f;
    float depth = 40.0f;

    // How many squares across and down. More squares is a finer ground and a
    // slower one to make.
    int cells = 96;

    // The seed of the noise. The same seed is the same landform on every
    // machine.
    unsigned seed = 1;

    // How many threads the making may use. The squares are cut in rows and a
    // row does not depend on the next, so the work spreads.
    int threads = 4;

    // What shape the ground takes.
    LandformShape shape = LandformShape::Hills;

    // --- the shape ---------------------------------------------------------
    //
    // The numbers the shapes are made of. They were written down once and
    // were the same for every ground, which is a way of saying that every
    // ground looked the same; they are asked for now, and the ground comes
    // out the way the one who asks for it wants it.
    //
    // How many steps the plateau is cut into.
    int levels = 6;

    // How wide the step between one step and the next is, as a part of the
    // step itself. A narrow step is a wall and a wide one is a slope.
    float roundness = 0.16f;

    // How deep the canyon is cut into the ground, 0..1.
    float cut = 0.80f;

    // How wide the bed of the river is, as a part of the width of the ground.
    float river = 0.07f;

    // How far the river wanders from the middle, as a part of the depth of
    // the ground. A river that runs straight is a ditch.
    float meander = 0.90f;

    // How far from the middle the island starts to sink into the water, as a
    // part of the way to the corner.
    float shore = 0.35f;

    // How much of the mountain range is lowland rather than mountain, 0..1.
    float lowland = 0.30f;

    // How much the dunes are stretched along one axis.
    float stretch = 1.30f;

    // How large the features are: the size in world units of the biggest
    // shape on the ground. A hill is a few metres across, a mountain range
    // is a few hundred.
    float frequency = 0.055f;

    // How many octaves the noise is made of. More is more detail, and a
    // slower ground to make.
    int octaves = 6;

    // How much each octave is larger than the one before it, and how much
    // weaker. Two is the usual answer; a little under two gives softer
    // shapes.
    float lacunarity = 2.0f;
    float gain = 0.5f;

    // How much the shapes are bent by another noise. Zero is straight and
    // regular, one is ground that has been folded about.
    float warp = 0.8f;

    // How sharp the ridges are: zero is round hills, one is mountains.
    float ridge = 0.0f;

    // How many times the ground is let slide downhill. Material above the
    // angle of repose falls until it lies flat, and that is what takes the
    // spikes off a field of noise and leaves ridges and valleys.
    int erosion = 4;

    // How much the ground is smoothed afterwards, 0..1.
    float smoothing = 0.5f;

    // The colour of the ground when the biomes are off, as the three numbers
    // of a colour. The picture is multiplied by it, so a white ground is the
    // picture as it is and a green one is the picture in green.
    float color[3] = {0.55f, 0.72f, 0.42f};

    // The picture on the ground, relative to the assets. The grass that ships
    // with the engine is "image/textures/grass.jpg", and it tiles: a hundred
    // metres of ground is not one picture stretched over it.
    std::string texture = "image/textures/grass.jpg";

    // Whether the ground is coloured by its height and its steepness - sand
    // at the bottom, grass on the slopes, stone on what is too steep to hold
    // anything, snow at the top.
    bool biomes = true;
};

// A BUILT LANDFORM
//
// The heightfield and the mesh made from it.
//
// The heightfield is kept, and not just the mesh, because the editor asks
// the ground how high it is at a point: a body dropped on a new piece of
// ground has to land ON it, and the only honest answer is the ground itself
// and not a formula that only looks like it.
class Landform {
public:
    // Makes the ground. The settings are copied, so the panel can go on
    // changing them without changing the ground that is already there.
    void build(const LandformSettings& settings);

    // True when there is a ground to ask about.
    bool ready() const { return side_ > 1; }

    const LandformSettings& settings() const { return settings_; }

    // The height of the ground at a point in the world. The same ground the
    // mesh was made from, so a body put on it stands on it.
    float heightAt(float x, float z) const;

    // The mesh. One part, with the colours of the biomes in it - or the flat
    // colour of the settings, when the biomes are off.
    MeshAsset toMesh() const;

    // The lowest and the highest point of the ground that was made.
    float lowest() const { return lowest_; }
    float highest() const { return highest_; }

private:
    // The heights, one per vertex of the grid, in 0..1. The world heights come
    // from them, so the shape of the ground does not change when the ground is
    // made bigger: a hill is a hill, it is just further away.
    std::vector<float> heights_;
    int side_ = 0;
    LandformSettings settings_;
    float lowest_ = 0.0f;
    float highest_ = 0.0f;
};

} // namespace s3d
