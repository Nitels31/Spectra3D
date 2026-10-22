#pragma once
#include <string>

namespace s3d {

// THE DESCRIPTION OF A CLOUD.
//
// These are the settings of a piece of sky, and they live here rather than
// with the cloud itself because a cloud is a mesh: a mesh the engine made
// instead of read from a file, and it carries the settings it was made of
// (see `MeshAsset::cloud`). A scene that is saved has to say how the cloud
// was made - the same seed is the same cloud on every machine - and the only
// place that can say it is the mesh itself.

// HOW A CLOUD IS DRAWN
//
// Two ways, and they are not two settings of one thing. They are two
// different things.
//
//   Flat  - the cloud is a picture on a plane: a cutout with a cloudy edge,
//           seen from the front, and as thin as a piece of paper. This is the
//           cloud of a 2D game - it is a picture, it costs almost nothing,
//           and it is seen from one side only.
//
//   Puffy - the cloud is a volume: a heap of puffs, each one a little ball
//           of water, and the heap has a shape of its own. This is the cloud
//           of a 3D world: it is seen from every side, and the sun lights it
//           and the shadow of it falls on the ground.
enum class CloudMode {
    Flat,
    Puffy,
};

// The shapes a cloud takes.
//
// They are not six different noises: they are one noise and four different
// answers to "what shape is the envelope of it".
enum class CloudShape {
    Cumulus,      // a heap of puffs, wide at the bottom and rounded on top
    Stratus,      // a flat layer, wide and low - the sheet of cloud of a dull day
    Cirrus,       // thin streaks, drawn out along one axis
    Cumulonimbus, // a tower: narrow at the bottom, wide at the top, very tall
};

// How many shapes there are, and the name of each. The panel asks for the
// name and the index, so the two cannot get out of step.
inline constexpr int kCloudShapeCount = 4;

// The settings are what a person sets before the cloud is made: how big it is,
// how many puffs it is made of, how much of the sky it takes and what colour
// it comes out.
struct CloudSettings {
    // Which of the two clouds this is.
    CloudMode mode = CloudMode::Puffy;

    // The shape it takes.
    CloudShape shape = CloudShape::Cumulus;

    // The seed of the noise. The same seed is the same cloud on every
    // machine.
    unsigned seed = 1;

    // How many puffs the cloud is made of. More is a puffier cloud and a
    // bigger mesh: every puff is a little ball of geometry.
    int puffs = 28;

    // The radius of one puff, in metres. The puffs of a cloud are all of
    // roughly one size, and this is that size.
    float puffSize = 3.0f;

    // How wide the cloud is, along X, in metres. How deep it is along Z is a
    // part of this, and the shape decides how much of it.
    float spread = 24.0f;

    // How tall the cloud is, in metres.
    float height = 10.0f;

    // How much of the sky the cloud takes, 0..1. A cloud that covers
    // everything is a fog, and a cloud that covers nothing is a smudge.
    float coverage = 0.55f;

    // How much the puffs differ from one another, 0..1. Zero is a cloud of
    // identical balls, and one is a cloud with a ragged edge.
    float softness = 0.5f;

    // How many octaves the noise is made of. More is more detail in the
    // shape of the cloud.
    int detail = 4;

    // How finely a puff is made: how many pieces go around it. Four is a
    // ball of eight triangles, and twenty-four is a ball of a thousand.
    //
    // This is the number the cost of the cloud is made of, and it is here
    // because a cloud is made of hundreds of puffs: the difference between
    // four and twenty-four is the difference between a cloud that a machine
    // can draw and one it cannot.
    int resolution = 10;

    // The colour of the cloud where the sun reaches it, and the colour of it
    // where the sun does not. The bottom of a cloud is grey and the top is
    // white, and this is where that comes from.
    float color[3] = {1.0f, 1.0f, 1.0f};
    float shade[3] = {0.62f, 0.68f, 0.78f};

    // How many threads the making may use. The puffs are independent of one
    // another, so the work spreads over the threads and nothing is shared.
    //
    // Not written into the scene: it is a machine's business and not the
    // cloud's, and the same cloud is the same cloud on a machine with one
    // thread as on a machine with six.
    int threads = 4;
};

} // namespace s3d
