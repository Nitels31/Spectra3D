#pragma once
#include <vector>

#include "assets/CloudSettings.hpp"
#include "assets/MeshAsset.hpp"
#include "core/Math.hpp"

namespace s3d {

// The shape of a cloud, as the localisation table spells it. The panel shows
// the name and the settings behind it, and the two come from the same place.
const char* cloudShapeName(CloudShape shape);

// The way a cloud is drawn, as the localisation table spells it.
const char* cloudModeName(CloudMode mode);

// A BUILT CLOUD
//
// The puffs and the mesh made from them.
//
// The cloud is not a picture of a cloud and not a texture of one: it is
// geometry - a heap of little balls, or a heap of little discs - and the
// shape of it comes out of the noise the same way the shape of the ground
// does.
//
// **THE WHOLE CLOUD IS ONE MESH AND ONE DRAW CALL.** A hundred puffs are a
// hundred little balls, and they are made into one part, so the picture of a
// cloud is one call and not a hundred. The physics does not know that a cloud
// is made of puffs: it is one body with one box around it, and the frustum
// is asked about that box and not about every ball in it.
class Cloud {
public:
    // Makes the cloud. The settings are copied, so the panel can go on
    // changing them without changing the cloud that is already there.
    void build(const CloudSettings& settings);

    // True when there is a cloud to look at.
    bool ready() const { return !puffs_.empty(); }

    const CloudSettings& settings() const { return settings_; }

    // The puffs it is made of. How many of them there are is worth knowing:
    // a cloud asked for thirty puffs and given twelve is a cloud that did
    // not fill the sky it was given.
    int puffCount() const { return (int)puffs_.size(); }

    // The mesh. One part, with the colours of the cloud in it - the sun on
    // the top of it and the grey of the shadow underneath.
    MeshAsset toMesh() const;

    // ONE PUFF
    //
    // A little ball of cloud: where it is, how big it is, and how deep in
    // the cloud it lies. The last number is what makes the inside of a cloud
    // darker than the outside of it.
    struct Puff {
        Vec3 centre;
        // The half sizes of it. A ball is a ball, but a cloud is made of
        // things that are flatter than they are wide, and the shape of the
        // cloud says which.
        Vec3 radius{1.0f, 1.0f, 1.0f};
        float buried = 0.0f;
    };

private:
    std::vector<Puff> puffs_;
    CloudSettings settings_;
};

} // namespace s3d
