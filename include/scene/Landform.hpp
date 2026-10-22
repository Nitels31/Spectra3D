#pragma once
#include <string>
#include <vector>

#include "assets/LandformSettings.hpp"
#include "assets/MeshAsset.hpp"

namespace s3d {

// The shape of the ground, and the noise it is made of.
//
// The description of the ground - what shape it takes and the numbers it is
// made of - is in `assets/LandformSettings.hpp`, because a ground is a mesh
// and the mesh carries the settings it was made of: a scene that is saved
// says how the ground was made, and the ground is made again when the scene
// is opened.
//
// What is here is the making of it: the heightfield, and the mesh that is
// cut from it.
const char* landformShapeName(LandformShape shape);

// The name of a biome, as the localisation table spells it. The panel shows
// the name and the colours of the biome, and the two come from the same
// place.
const char* landformBiomeName(LandformBiome biome);

// A RANDOM NUMBER FOR THE VARIATION OF A GROUND.
//
// The number is small - between a half of a thousandth and two hundredths -
// and it is added to the frequency of the noise, so the features of the
// ground come out a little bigger or a little smaller than they were the
// last time. Two grounds made one after the other are not the same ground
// twice, and a ground made with the same number is the same ground.
float landformVariation();

// The brush: what a stroke of the pointer does to the ground.
//
// The ground is a heightfield, so everything the brush can do is done to the
// heights.
enum class LandformBrush {
    Raise,   // the ground goes up
    Lower,   // and down
    Smooth,  // and the bumps are taken off it
    Flatten, // and it is made level with the height the stroke began at
};

// The name of a brush, as the localisation table spells it.
const char* landformBrushName(LandformBrush brush);

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

    // --- the brush ----------------------------------------------------------

    // Paints the ground. `x` and `z` are in the ground's own frame: the
    // middle of the ground is 0,0. `radius` is how wide the stroke is and
    // `amount` is how much it does, both in the world's units.
    //
    // Returns true when the ground changed.
    bool paint(float x, float z, float radius, float amount, LandformBrush brush);

    // Starts a stroke. The height the flatten brush levels the ground to is
    // the height the ground has where the stroke began - and it is the height
    // it had then, not the height it has now, or the ground would follow the
    // brush as it rises.
    void beginStroke(float x, float z);

    // Where a ray meets the ground, if it meets it. The ground is a height
    // field, so the ray is walked along until it goes under the ground, and
    // the crossing is found between the two steps it fell between.
    //
    // The ray is in the ground's own frame, like the brush.
    bool rayHit(const Vec3& origin, const Vec3& direction, float& outDistance) const;

    // The heightfield: one height per vertex, 0..1.
    const std::vector<float>& heights() const { return heights_; }

    // Puts a ground that was painted back into the landform, so that it can
    // be painted again. The heights are the ones of the mesh: they live in
    // the mesh because the mesh is what the scene shows.
    //
    // False when the heights are not the ones of a ground of this size.
    bool setHeights(const std::vector<float>& heights);

    // True when the ground is not the ground the settings make: it has been
    // painted, and its heights are its own.
    bool painted() const { return painted_; }

private:
    // How much of the ground around each square stands above it, 0..1. The
    // valleys are the ones that are surrounded, and they are darker because
    // the sky does not reach them.
    std::vector<float> shade_;
    // Stretches the heights so that the lowest of them is 0 and the highest
    // is 1.
    void normalizeHeights();
    // Measures the shade of every square. Done before the detail is put on,
    // so that a bump does not make every square look like a hole.
    void measureShade();
    // Puts the little bumps on the surface of the ground.
    void addDetail(float halfWidth, float halfDepth, float stepX, float stepZ);

    // The heights, one per vertex of the grid, in 0..1. The world heights come
    // from them, so the shape of the ground does not change when the ground is
    // made bigger: a hill is a hill, it is just further away.
    std::vector<float> heights_;
    // The heights as they were before the stroke that is being made now: the
    // brush smooths towards the average of the neighbours, and the average
    // must be the one of the ground before the brush touched it.
    std::vector<float> scratch_;
    // The height the flatten brush levels the ground to.
    float strokeHeight_ = 0.0f;
    // True when the ground has been painted.
    bool painted_ = false;
    int side_ = 0;
    LandformSettings settings_;
    float lowest_ = 0.0f;
    float highest_ = 0.0f;
};

} // namespace s3d
