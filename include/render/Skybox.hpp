#pragma once
#include <string>

#include "render/Camera.hpp"

namespace s3d {

// The sky the world is seen against.
//
// A cubemap: six faces, drawn on a cube around the camera, so the horizon is
// the world and not a colour. Where the faces come from is a folder of six
// images named after the direction each one looks at; where they do not come
// from is a gradient the engine makes itself, so the sky is never nothing at
// all.
//
// The cube is drawn with the depth test off and before anything else, which is
// what makes it the background rather than an object in the world.
class Skybox {
public:
    ~Skybox();

    Skybox() = default;
    Skybox(const Skybox&) = delete;
    Skybox& operator=(const Skybox&) = delete;

    // Loads a folder of six faces. The names are the direction each face looks
    // at - px, nx, py, ny, pz, nz - with .png, .jpg or .bmp.
    //
    // Returns false and keeps the sky it has when the folder is not there or
    // does not hold six images: a missing sky is not a reason to have none.
    bool load(const std::string& directory);

    // The sky the engine makes: a blue gradient above the horizon and a muted
    // ground below it, baked into a cubemap so that the sky and a loaded one
    // are drawn the same way.
    void buildDefault();

    // Draws the sky around the camera. Does nothing when there is no sky, which
    // leaves the clear colour showing.
    void draw(const Camera& camera) const;

    bool valid() const { return cube_ != 0; }
    // Where it was loaded from, for the log.
    const std::string& source() const { return source_; }

private:
    void release();
    // Uploads one face of the cube. `face` is a GL_TEXTURE_CUBE_MAP_* constant.
    bool uploadFace(int face, const std::string& path);

    unsigned cube_ = 0;
    std::string source_;
};

} // namespace s3d
