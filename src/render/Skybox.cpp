#include "render/Skybox.hpp"

#include "core/Log.hpp"
#include "core/Math.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"

#include <GL/gl.h>

// stb_image is a single-header library and its implementation lives in
// TextureRegistry.cpp - exactly one translation unit may have it, and that is
// not this one. Only the declarations are needed here, and the linker finds
// the implementation in the same library.
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <vector>

// GL_CLAMP_TO_EDGE is not in the 1.1 header Windows ships; it is in the
// extension-era one. The engine runs a 3.3 compatibility context, where it
// exists, so it is named here rather than pulling in a header the rest of the
// renderer does not use.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace s3d {
namespace {

// The six faces of a cube, with the name each one is looked for under. The
// names are the direction the face looks at, which is the only naming a
// person can get right without a diagram.
//
// `forward` is where the face looks, `right` the way u grows across the face
// and `down` the way v grows down it. The texel at (u, v) of a face looks
// along
//
//     forward + right * (2u - 1) + down * (2v - 1)
//
// which is the cube map selection rule of the OpenGL specification, written
// out per face instead of left to the driver. The same rule is what the six
// quads of the cube are given as texture coordinates, so a face looks the
// same whether it came from a file or from a panorama.
struct Face {
    const char* name;
    Vec3 forward;
    Vec3 right;
    Vec3 down;
};

const Face kFaces[6] = {
    {"px", {1, 0, 0}, {0, 0, -1}, {0, -1, 0}},
    {"nx", {-1, 0, 0}, {0, 0, 1}, {0, -1, 0}},
    {"py", {0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
    {"ny", {0, -1, 0}, {1, 0, 0}, {0, 0, -1}},
    {"pz", {0, 0, 1}, {1, 0, 0}, {0, -1, 0}},
    {"nz", {0, 0, -1}, {-1, 0, 0}, {0, -1, 0}},
};

// The image extensions a face may have. A folder of six PNGs and a folder of
// six JPGs are both a skybox, and the person who made it should not have to
// care which.
const char* const kFaceExtensions[] = {".png", ".jpg", ".jpeg", ".bmp"};

// Where a direction sits in an equirectangular picture: u across (0..1, the
// longitude) and v down (0..1, the top row is the zenith).
//
// The longitude is atan2(z, x), so the picture starts at -X and runs through
// -Z, +X and +Z before it comes back around. The generator that makes such a
// picture (tools/skygen.cpp) uses the same two formulas, and the two have to
// agree to the letter: when they do not, the sun ends up in the wrong quarter
// of the sky and nobody can say why.
void equirectUv(const Vec3& direction, float& u, float& v) {
    u = std::atan2(direction.z, direction.x) * 0.5f * (1.0f / kPi) + 0.5f;
    v = 0.5f - std::asin(clampf(direction.y, -1.0f, 1.0f)) * (1.0f / kPi);
}

// One texel of an equirectangular picture, in RGBA.
//
// The longitude wraps - the picture's left edge and its right edge are the same
// direction - and the latitude does not: above the zenith and below the nadir
// there is nothing to sample, and the last row is held.
void sampleEquirect(const unsigned char* pixels, int width, int height, const Vec3& direction,
                    unsigned char* out) {
    float u = 0.0f, v = 0.0f;
    equirectUv(direction.normalized(), u, v);

    // The texel centres sit at (x + 0.5) / width, so the coordinate of a texel
    // is a quarter of a texel off its centre and the two are put back together.
    const float fx = u * (float)width - 0.5f;
    const float fy = v * (float)height - 0.5f;
    const int x0 = (int)std::floor(fx);
    const int y0 = (int)std::floor(fy);
    const float tx = fx - (float)x0;
    const float ty = fy - (float)y0;

    // Longitude wraps, latitude clamps.
    auto wrapX = [&](int x) {
        x %= width;
        return x < 0 ? x + width : x;
    };
    auto clampY = [&](int y) { return y < 0 ? 0 : (y > height - 1 ? height - 1 : y); };

    const int xs[2] = {wrapX(x0), wrapX(x0 + 1)};
    const int ys[2] = {clampY(y0), clampY(y0 + 1)};

    for (int channel = 0; channel < 4; ++channel) {
        const float top = (float)pixels[((size_t)ys[0] * (size_t)width + (size_t)xs[0]) * 4 + channel] *
                               (1.0f - tx) +
                           (float)pixels[((size_t)ys[0] * (size_t)width + (size_t)xs[1]) * 4 + channel] * tx;
        const float bottom = (float)pixels[((size_t)ys[1] * (size_t)width + (size_t)xs[0]) * 4 + channel] *
                                 (1.0f - tx) +
                             (float)pixels[((size_t)ys[1] * (size_t)width + (size_t)xs[1]) * 4 + channel] * tx;
        const float value = top * (1.0f - ty) + bottom * ty;
        out[channel] = (unsigned char)clampf(value + 0.5f, 0.0f, 255.0f);
    }
}

// Uploads one face as an ordinary two dimensional texture.
//
// The faces are six textures and not one cube map, and the reason is in
// Skybox::draw: a cube map is looked up through the fixed function texture
// coordinate generation, and that is not something every driver still has.
// Six textures with the texture coordinates written out by hand work
// everywhere, and they look exactly the same.
unsigned uploadFace(const unsigned char* pixels, int width, int height) {
    unsigned texture = 0;
    glGenTextures(1, &texture);
    if (!texture) return 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Clamped, not repeated: the edge of a face is the edge of the picture, and
    // a face that wrapped into its neighbour would show a stripe of sky along
    // the seam.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

// The first face file in `directory` with the given name, or an empty string.
std::string findFace(const std::string& directory, const char* name) {
    for (const char* extension : kFaceExtensions) {
        const std::string path = str::join(directory, std::string(name) + extension);
        if (fs::exists(path)) return path;
    }
    return std::string();
}

} // namespace

Skybox::~Skybox() { release(); }

void Skybox::release() {
    for (unsigned& texture : faces_)
        if (texture) glDeleteTextures(1, &texture);
    for (unsigned& texture : faces_) texture = 0;
    source_.clear();
}

bool Skybox::load(const std::string& directory) {
    if (!fs::isDirectory(directory)) return false;

    // A folder with no faces in it is not a sky that is broken - it is a
    // folder that has nothing in it yet, and the sky that is there stays.
    const std::string first = findFace(directory, kFaces[0].name);
    if (first.empty()) return false;

    // Every face has to be there. A skybox with five faces is not a skybox, and
    // half of one is worse than none: the missing side would be black.
    std::string paths[6];
    for (int i = 0; i < 6; ++i) {
        paths[i] = findFace(directory, kFaces[i].name);
        if (paths[i].empty()) {
            Log::warn("Sky: '%s' has no '%s' face, keeping the sky it has",
                      directory.c_str(), kFaces[i].name);
            return false;
        }
    }

    // The faces are decoded first and uploaded afterwards: a picture that is
    // not a picture has to be found out before half a sky is on the screen.
    unsigned textures[6] = {0, 0, 0, 0, 0, 0};
    // Whatever was uploaded before a face turned out to be unusable is thrown
    // away: a half sky is not a sky, and the textures belong to the GL context.
    auto discard = [&]() {
        for (int f = 0; f < 6; ++f) {
            if (!textures[f]) continue;
            glDeleteTextures(1, &textures[f]);
            textures[f] = 0;
        }
    };

    for (int i = 0; i < 6; ++i) {
        std::string bytes;
        if (!fs::readFile(paths[i], bytes)) {
            Log::warn("Sky: cannot read '%s'", paths[i].c_str());
            discard();
            return false;
        }

        int width = 0, height = 0, channels = 0;
        unsigned char* pixels =
            stbi_load_from_memory((const unsigned char*)bytes.data(), (int)bytes.size(),
                                  &width, &height, &channels, 4);
        if (!pixels) {
            Log::warn("Sky: cannot decode '%s': %s", paths[i].c_str(), stbi_failure_reason());
            discard();
            return false;
        }

        // The cube's faces are square, and a face that is not square is a
        // picture that is not a face.
        if (width != height) {
            Log::warn("Sky: '%s' is %dx%d, a face has to be square", paths[i].c_str(), width,
                      height);
            stbi_image_free(pixels);
            discard();
            return false;
        }

        textures[i] = uploadFace(pixels, width, height);
        stbi_image_free(pixels);
        if (!textures[i]) {
            Log::warn("Sky: GL refused '%s'", paths[i].c_str());
            discard();
            return false;
        }
    }

    // Only when every face is there is the new sky better than the old one.
    for (int i = 0; i < 6; ++i) {
        if (faces_[i]) glDeleteTextures(1, &faces_[i]);
        faces_[i] = textures[i];
    }
    source_ = directory;
    Log::info("Sky: loaded from '%s'", directory.c_str());
    return true;
}

bool Skybox::loadImage(const std::string& path) {
    std::string bytes;
    if (!fs::readFile(path, bytes)) {
        Log::warn("Sky: cannot read '%s'", path.c_str());
        return false;
    }

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels =
        stbi_load_from_memory((const unsigned char*)bytes.data(), (int)bytes.size(),
                              &width, &height, &channels, 4);
    if (!pixels) {
        Log::warn("Sky: cannot decode '%s': %s", path.c_str(), stbi_failure_reason());
        return false;
    }

    // An equirectangular picture is twice as wide as it is tall. One that is
    // not is still drawn - stretched, not refused - but it is worth a line in
    // the log, because a picture that is not 2:1 is nearly always a mistake.
    if (width != height * 2)
        Log::warn("Sky: '%s' is %dx%d, an equirectangular sky is twice as wide as it is "
                  "tall",
                  path.c_str(), width, height);

    const bool built = buildFromEquirect(pixels, width, height);
    stbi_image_free(pixels);
    if (!built) return false;

    source_ = path;
    Log::info("Sky: loaded from '%s' (%dx%d)", path.c_str(), width, height);
    return true;
}

bool Skybox::buildFromEquirect(const unsigned char* pixels, int width, int height) {
    if (!pixels || width < 8 || height < 4) return false;

    // A face of the cube covers 90 degrees, which is a quarter of the picture's
    // width: a 4096x2048 picture gives 1024x1024 faces. The face is smaller
    // than the screen it is drawn on, so it is magnified - with GL_LINEAR that
    // is a soft sky rather than a blocky one.
    const int size = std::max(16, width / 4);

    unsigned textures[6] = {0, 0, 0, 0, 0, 0};
    std::vector<unsigned char> face((size_t)size * (size_t)size * 4);
    for (int f = 0; f < 6; ++f) {
        const Face& spec = kFaces[f];
        for (int y = 0; y < size; ++y) {
            // The first row of a face is the face's v = 0 edge, which is the
            // same edge the texture coordinates of the quad below start at.
            const float v = ((float)y + 0.5f) / (float)size;
            for (int x = 0; x < size; ++x) {
                const float u = ((float)x + 0.5f) / (float)size;
                const Vec3 direction =
                    (spec.forward + spec.right * (2.0f * u - 1.0f) +
                     spec.down * (2.0f * v - 1.0f))
                        .normalized();
                const size_t at = ((size_t)y * (size_t)size + (size_t)x) * 4;
                sampleEquirect(pixels, width, height, direction, &face[at]);
            }
        }
        textures[f] = uploadFace(face.data(), size, size);
        if (!textures[f]) {
            for (int i = 0; i < 6; ++i)
                if (textures[i]) glDeleteTextures(1, &textures[i]);
            return false;
        }
    }

    for (int i = 0; i < 6; ++i) {
        if (faces_[i]) glDeleteTextures(1, &faces_[i]);
        faces_[i] = textures[i];
    }
    return true;
}

void Skybox::draw(const Camera& camera) const {
    if (!valid()) return;

    // The state the sky touches is put back the way it was found, because the
    // renderer draws the world straight after and the world is not a sky.
    const bool culling = glIsEnabled(GL_CULL_FACE) != 0;
    const bool lighting = glIsEnabled(GL_LIGHTING) != 0;
    const bool texture2d = glIsEnabled(GL_TEXTURE_2D) != 0;

    // The sky writes no depth. A depth of its own would put a wall in front of
    // everything that is further away than the cube, and the cube is far.
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    // The texture is modulated by the current colour, and the current colour is
    // whatever the last thing in the frame left behind - which is usually a
    // label's colour. The sky is the picture, not a tint of it.
    glColor3f(1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);

    glPushMatrix();
    // The cube is put where the camera is and scaled up, and it is not turned:
    // the model matrix is a translation and a scale and nothing else, so the
    // cube's own frame is the world's frame. That is what makes the sky stand
    // still while the camera turns - the direction a texel is looked up in is a
    // world direction, not a direction relative to the camera.
    glTranslatef(camera.position.x, camera.position.y, camera.position.z);
    // Big enough to be past everything in the arena, small enough to stay
    // inside the far plane: the far plane is at 900 and the corner of the cube
    // is at 400 * sqrt(3).
    const float size = 400.0f;
    glScalef(size, size, size);

    for (int f = 0; f < 6; ++f) {
        if (!faces_[f]) continue;
        const Face& spec = kFaces[f];

        // The four corners of the face, with the texture coordinates the cube
        // map rules would have produced for them: (0,0) at -right -down, then
        // right, then down. The hardware interpolates them across the quad
        // with the perspective, which is exactly the mapping a cube map lookup
        // does - only that one does not need the texture coordinate generation
        // to be there.
        const Vec3 corners[4] = {
            spec.forward - spec.right - spec.down,
            spec.forward + spec.right - spec.down,
            spec.forward + spec.right + spec.down,
            spec.forward - spec.right + spec.down,
        };
        const float us[4] = {0.0f, 1.0f, 1.0f, 0.0f};
        const float vs[4] = {0.0f, 0.0f, 1.0f, 1.0f};

        glBindTexture(GL_TEXTURE_2D, faces_[f]);
        glBegin(GL_QUADS);
        for (int c = 0; c < 4; ++c) {
            glTexCoord2f(us[c], vs[c]);
            glVertex3f(corners[c].x, corners[c].y, corners[c].z);
        }
        glEnd();
    }
    glPopMatrix();

    glBindTexture(GL_TEXTURE_2D, 0);
    if (!texture2d) glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    if (lighting) glEnable(GL_LIGHTING);
    if (culling) glEnable(GL_CULL_FACE);
}

} // namespace s3d
