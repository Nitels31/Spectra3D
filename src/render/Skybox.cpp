#include "render/Skybox.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "render/Color.hpp"

#include <GL/gl.h>

// stb_image is a single-header library and its implementation lives in
// TextureRegistry.cpp - exactly one translation unit may have it, and that is
// not this one. Only the declarations are needed here, and the linker finds
// the implementation in the same library.
#include <stb_image.h>

#include <cmath>
#include <vector>

// The cube map and the edge clamp are not in the 1.1 header Windows ships; they
// are in the extension-era one. The engine runs a 3.3 compatibility context,
// where both exist, so they are named here rather than pulling in a header the
// rest of the renderer does not use.
#ifndef GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP 0x8513
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#endif
#ifndef GL_TEXTURE_CUBE_MAP_NEGATIVE_X
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X 0x8516
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_Y
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y 0x8517
#endif
#ifndef GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y 0x8518
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_Z
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z 0x8519
#endif
#ifndef GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 0x851A
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace s3d {
namespace {

// The six faces of a cube, in the order OpenGL wants them, with the name each
// one is looked for under. The names are the direction the face looks at, which
// is the only naming a person can get right without a diagram.
struct Face {
    const char* name;
    int glFace;
    // The direction of the face's centre, and the two axes its texels run
    // along. A texel at (s, t), both in -1..1, looks along
    // forward + right * s + up * t.
    Vec3 forward;
    Vec3 right;
    Vec3 up;
};

const Face kFaces[6] = {
    {"px", GL_TEXTURE_CUBE_MAP_POSITIVE_X, {1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
    {"nx", GL_TEXTURE_CUBE_MAP_NEGATIVE_X, {-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
    {"py", GL_TEXTURE_CUBE_MAP_POSITIVE_Y, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
    {"ny", GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, {0, -1, 0}, {1, 0, 0}, {0, 0, -1}},
    {"pz", GL_TEXTURE_CUBE_MAP_POSITIVE_Z, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
    {"nz", GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, {0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
};

// The image extensions a face may have. A folder of six PNGs and a folder of
// six JPGs are both a skybox, and the person who made it should not have to
// care which.
const char* const kFaceExtensions[] = {".png", ".jpg", ".jpeg", ".bmp"};

// The colour the sky is made of when there are no images: a deep blue above,
// a white band at the horizon, and a dark ground below it.
//
// The white band is what makes a sky look like a sky rather than a wash of
// blue, and the dark ground is what keeps the world's own colours - the grid,
// the bodies - from being read against a bright background. A pale sky swallows
// everything in front of it, and at night it is a lamp in the face.
constexpr Color kZenith{0.16f, 0.27f, 0.55f};
constexpr Color kHorizon{1.00f, 1.00f, 1.00f};
constexpr Color kGround{0.19f, 0.19f, 0.23f};

// The colour of the sky in one direction.
Color skyColour(const Vec3& direction) {
    const float y = direction.y;
    if (y >= 0.0f) {
        // Above the horizon: the horizon colour up to the zenith colour. The
        // exponent is what puts the blue where a person looks rather than
        // right at the edge of it: the blue starts a few degrees above the
        // horizon, and the white is the last thin band before it.
        const float t = std::pow(y, 0.5f);
        return Color{kHorizon.r + (kZenith.r - kHorizon.r) * t,
                     kHorizon.g + (kZenith.g - kHorizon.g) * t,
                     kHorizon.b + (kZenith.b - kHorizon.b) * t};
    }
    // Below: the ground, darkening away from the horizon.
    const float t = std::pow(-y, 0.3f);
    return Color{kHorizon.r + (kGround.r - kHorizon.r) * t,
                 kHorizon.g + (kGround.g - kHorizon.g) * t,
                 kHorizon.b + (kGround.b - kHorizon.b) * t};
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
    if (cube_) glDeleteTextures(1, &cube_);
    cube_ = 0;
}

bool Skybox::uploadFace(int glFace, const std::string& path) {
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

    // The cube's faces are square, and a face that is not square is a picture
    // that is not a face.
    if (width != height) {
        Log::warn("Sky: '%s' is %dx%d, a face has to be square", path.c_str(), width,
                  height);
        stbi_image_free(pixels);
        return false;
    }

    glTexImage2D(glFace, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    return true;
}

bool Skybox::load(const std::string& directory) {
    if (!fs::isDirectory(directory)) return false;

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

    unsigned cube = 0;
    glGenTextures(1, &cube);
    if (!cube) return false;
    glBindTexture(GL_TEXTURE_CUBE_MAP, cube);

    for (int i = 0; i < 6; ++i)
        if (!uploadFace(kFaces[i].glFace, paths[i])) {
            glDeleteTextures(1, &cube);
            return false;
        }

    // A cube map is sampled with the coordinates of the fragment, so the edges
    // have to be clamped or the seam between two faces shows.
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    // Only when every face is there is the new sky better than the old one.
    if (cube_) glDeleteTextures(1, &cube_);
    cube_ = cube;
    source_ = directory;
    Log::info("Sky: loaded from '%s'", directory.c_str());
    return true;
}

void Skybox::buildDefault() {
    // Small on purpose: a gradient does not need more, and a sky that is
    // generated does not need to be big.
    constexpr int kSize = 64;

    unsigned cube = 0;
    glGenTextures(1, &cube);
    if (!cube) return;
    glBindTexture(GL_TEXTURE_CUBE_MAP, cube);

    for (int f = 0; f < 6; ++f) {
        const Face& face = kFaces[f];
        std::vector<unsigned char> pixels((size_t)kSize * kSize * 4);
        for (int y = 0; y < kSize; ++y) {
            // The texel row is t, from the top of the face down.
            const float t = 1.0f - 2.0f * ((float)y + 0.5f) / (float)kSize;
            for (int x = 0; x < kSize; ++x) {
                const float s = 2.0f * ((float)x + 0.5f) / (float)kSize - 1.0f;
                const Vec3 direction = (face.forward + face.right * s + face.up * t).normalized();
                const Color colour = skyColour(direction);
                const size_t at = ((size_t)y * (size_t)kSize + (size_t)x) * 4;
                pixels[at + 0] = (unsigned char)(clampf(colour.r, 0.0f, 1.0f) * 255.0f);
                pixels[at + 1] = (unsigned char)(clampf(colour.g, 0.0f, 1.0f) * 255.0f);
                pixels[at + 2] = (unsigned char)(clampf(colour.b, 0.0f, 1.0f) * 255.0f);
                pixels[at + 3] = 255;
            }
        }
        glTexImage2D(face.glFace, 0, GL_RGBA, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     pixels.data());
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    if (cube_) glDeleteTextures(1, &cube_);
    cube_ = cube;
    source_ = "built-in";
}

void Skybox::draw(const Camera& camera) const {
    if (!cube_) return;

    // The cube is drawn around the camera with the depth test off: the sky is
    // behind everything, and there is nothing in the world to be behind.
    //
    // The state the sky touches is put back the way it was found, because the
    // renderer draws the world straight after and the world is not a sky.
    const bool culling = glIsEnabled(GL_CULL_FACE) != 0;
    const bool lighting = glIsEnabled(GL_LIGHTING) != 0;
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
    glEnable(GL_TEXTURE_CUBE_MAP);
    // The texture coordinate is generated from the cube's own vertices, which is
    // what puts the right part of the sky on the right part of the cube.
    //
    // The three generators have to be enabled as well: setting the mode is
    // only saying what the coordinate would be, and without them there is no
    // coordinate at all - the whole cube samples one texel and the sky is a
    // flat colour.
    glTexGenf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
    glEnable(GL_TEXTURE_GEN_R);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cube_);

    glPushMatrix();
    glTranslatef(camera.position.x, camera.position.y, camera.position.z);
    // Big enough to be past everything in the arena, small enough to stay
    // inside the far plane.
    const float size = 400.0f;
    glScalef(size, size, size);

    // The cube primitive is a unit cube, so this is a cube of the size above.
    // Immediate mode rather than a display list: the texture coordinate
    // generation has to be in effect when it is drawn, and the primitive's
    // display list is shared with the bodies.
    glBegin(GL_QUADS);
    const float h = 1.0f;
    // Six faces, wound so that they are seen from the inside.
    const float v[6][4][3] = {
        {{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}},       // +Z
        {{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}},   // -Z
        {{-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}},       // +Y
        {{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}},   // -Y
        {{h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}},       // +X
        {{-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}},   // -X
    };
    for (int f = 0; f < 6; ++f)
        for (int c = 0; c < 4; ++c) {
            glVertex3f(v[f][c][0], v[f][c][1], v[f][c][2]);
        }
    glEnd();
    glPopMatrix();

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glDisable(GL_TEXTURE_GEN_R);
    glTexGenf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    glDisable(GL_TEXTURE_CUBE_MAP);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    if (lighting) glEnable(GL_LIGHTING);
    if (culling) glEnable(GL_CULL_FACE);
}

} // namespace s3d
