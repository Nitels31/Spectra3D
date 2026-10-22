#pragma once
#include <string>

#include "render/Camera.hpp"
#include "render/ShaderProgram.hpp"

namespace s3d {

// The sky the world is seen against.
//
// A cube: six faces, drawn on a cube around the camera, so the horizon is the
// world and not a colour. There are two ways to get one:
//
//  - a folder of six images, named after the direction each one looks at;
//  - a single equirectangular picture, the way a panoramic skybox is a picture
//    rather than six. The picture is turned into the six faces when it is
//    loaded, so the drawing is the same either way.
//
// Where the sky comes from is a file. The engine makes no sky of its own: a
// scene with no skybox has no sky, and the world behind it is the clear
// colour.
//
// The six faces are six ordinary textures and not one cube map, because a
// cube map is looked up through the fixed function texture coordinate
// generation and that is not something every driver still has: on a machine
// without it the sky comes out as a sphere map - a picture wrapped around the
// camera, which turns with the player. Six quads with the texture
// coordinates written out by hand look the same everywhere.
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

    // Loads one equirectangular picture - the sky in a single image, wide
    // twice as much as it is tall. It is turned into the six faces on the way
    // in, so the drawing is the same either way.
    //
    // Returns false and keeps the sky it has when the file is not there or is
    // not a picture.
    bool loadImage(const std::string& path);

    // Draws the sky around the camera. Does nothing when there is no sky, which
    // leaves the clear colour showing.
    //
    // The program is the scene's own, and the sky is drawn with it so that the
    // fog reaches the horizon: the sky is the far distance, and the air in
    // front of it is the same air as the air in front of everything else. A sky
    // drawn without it stands in front of the fog like a wall.
    //
    // A program that is not ready is not a reason to draw no sky: the fixed
    // pipeline draws it without the fog, which is worse and not nothing.
    void draw(const Camera& camera, const ShaderProgram& program) const;

    bool valid() const {
        for (unsigned face : faces_)
            if (!face) return false;
        return true;
    }
    // Where it was loaded from, for the log.
    const std::string& source() const { return source_; }

private:
    void release();
    // Uploads the six faces built from an equirectangular picture.
    bool buildFromEquirect(const unsigned char* pixels, int width, int height);
    // The sky without a program: six quads through the fixed pipeline.
    void drawWithFixedPipeline(const Camera& camera, float size) const;

    unsigned faces_[6] = {0, 0, 0, 0, 0, 0};
    // The corners of the six faces, on the card.
    //
    // The cube follows the camera, so the corners move every frame and the
    // buffer is written again every frame - it is thirty-six vertices, and a
    // picture of the sky is worth a small copy.
    //
    // They are on the card and not in the memory of the program because the
    // program reads its attributes from the generic arrays: a texture
    // coordinate handed over with glTexCoordPointer does not reach a program
    // on every driver, and a sky whose texture coordinates never arrive is a
    // sky of one flat colour - a wall instead of a sky.
    mutable unsigned vertexBuffer_ = 0;
    std::string source_;
};

} // namespace s3d
