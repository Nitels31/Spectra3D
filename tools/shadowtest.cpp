// A picture of the shadows, without an editor.
//
// The shadows are the one part of the picture that cannot be checked by
// reading the code: whether a shadow lands where it should, whether the
// ground is striped by its own shadow, and whether the edge of it is a stair
// - those are questions about the picture. So this renders a scene with a
// ground and three boxes into a PNG, and the PNG is looked at.
//
//   shadowtest <file.png> [quality] [resolution] [cascades] [sunAzimuth]
//               [sunElevation] [bias]
//
// The numbers after the file name are the settings the picture is made with.
// Without them the defaults are used.

#include "assets/MeshRegistry.hpp"
#include "assets/TextureRegistry.hpp"
#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "platform/GlFunctions.hpp"
#include "platform/Screenshot.hpp"
#include "render/SceneRenderer.hpp"


#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace s3d;

namespace {

// A flat ground of `size` metres, as a mesh: two triangles, a normal up and
// texture coordinates over the whole of it.
//
// The ground is a mesh and not a primitive because the ground is what the
// shadow is drawn on, and a primitive is a box with sides - a box has a top
// and a shadow on the top of it is a shadow on a box.
MeshAsset makeGround(float size) {
    MeshAsset asset;
    asset.name = "ground";
    MeshPart part;
    const float h = size * 0.5f;
    // position, normal, texture coordinate - eight floats to a vertex, the
    // layout every mesh in the engine has.
    const float verts[4][8] = {
        {-h, 0.0f, -h, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
        {h, 0.0f, -h, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
        {h, 0.0f, h, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        {-h, 0.0f, h, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
    };
    for (const auto& v : verts)
        for (float f : v) part.vertices.push_back(f);
    part.indices = {0, 1, 2, 0, 2, 3};
    asset.parts.push_back(std::move(part));
    asset.boundsMin = Vec3{-h, 0.0f, -h};
    asset.boundsMax = Vec3{h, 0.0f, h};
    asset.center = Vec3{0.0f, 0.0f, 0.0f};
    asset.half = Vec3{h, 0.0f, h};
    asset.radius = asset.half.length();
    asset.scale = 1.0f;
    return asset;
}

// A body with a box of its own, standing on the ground.
void addBox(PhysicsWorld& world, const Vec3& position, const Vec3& half) {
    RigidBody body;
    body.shape = Shape::Box;
    body.halfExtents = half;
    body.position = position;
    body.material.isStatic = true;
    body.configure(1.0f);
    body.syncPrevTransform();
    world.bodies.push_back(body);
}

// The pictures are written by the engine's own writer: the tool is a tool,
// and a second copy of the PNG writer is a second thing to keep right.
bool writePixels(const std::string& path, const std::vector<unsigned char>& pixels, int width,
                 int height) {
    return screenshot::savePixels(path, pixels, width, height);
}

// The map of the shadows, as a picture of greys: the near things are black,
// the far things are white, and the middle is the middle.
bool writeMap(const std::string& path, const ShadowMap& map, int cascade) {
    std::vector<unsigned short> layer;
    if (!map.readLayer(cascade, layer)) return false;
    const int size = map.resolution();
    std::vector<unsigned char> pixels((size_t)size * (size_t)size * 3);
    for (size_t i = 0; i < layer.size(); ++i) {
        // The distance is a fraction of the depth of the box; the depth of the
        // box is in the settings, and the picture is what the map holds.
        const float value = (float)layer[i] / 65535.0f;
        const unsigned char grey = (unsigned char)(value * 255.0f);
        pixels[i * 3 + 0] = grey;
        pixels[i * 3 + 1] = grey;
        pixels[i * 3 + 2] = grey;
    }
    // GL counts from the bottom.
    std::vector<unsigned char> flipped(pixels.size());
    const size_t row = (size_t)size * 3;
    for (int y = 0; y < size; ++y)
        std::copy(pixels.begin() + (size_t)(size - 1 - y) * row,
                  pixels.begin() + (size_t)(size - y) * row,
                  flipped.begin() + (size_t)y * row);
    return writePixels(path, flipped, size, size);
}

} // namespace

int main(int argc, char** argv) {
    // The pictures go into the system's temporary folder and not into the
    // folder the program is in: the program is in a build folder, and a build
    // folder full of pictures is a build folder nobody can read.
    const std::string folder = screenshot::pictureFolder();
    const std::string out = argc > 1 ? argv[1] : str::join(folder, "shadow.png");

    ShadowSettings settings;
    settings.quality = ShadowQuality::Filtered;
    if (argc > 2) {
        const int q = std::atoi(argv[2]);
        settings.quality = q <= 0   ? ShadowQuality::Off
                           : q == 1 ? ShadowQuality::Hard
                           : q == 2 ? ShadowQuality::Filtered
                                    : ShadowQuality::Soft;
    }
    if (argc > 3) settings.resolution = std::atoi(argv[3]);
    if (argc > 4) settings.cascades = std::atoi(argv[4]);
    if (argc > 5) settings.sunAzimuth = (float)std::atof(argv[5]);
    if (argc > 6) settings.sunElevation = (float)std::atof(argv[6]);
    if (argc > 7) settings.bias = (float)std::atof(argv[7]);
    int debugMode = 0;
    if (argc > 8) debugMode = std::atoi(argv[8]);
    // With a ground mesh, or without one - the editor's ground is a grid of
    // lines, and a shadow on a line is a shadow nobody can see.
    bool withGround = false;
    if (argc > 9) withGround = std::atoi(argv[9]) != 0;

    // The window is hidden: the picture is the point, not the window.
    if (!glfwInit()) {
        std::fprintf(stderr, "shadowtest: no GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    const int width = 1400, height = 900;
    GLFWwindow* window = glfwCreateWindow(width, height, "shadowtest", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "shadowtest: no window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    if (!loadGlFunctions()) {
        std::fprintf(stderr, "shadowtest: the driver is missing the functions\n");
        glfwTerminate();
        return 1;
    }

    // The scene: a ground and three boxes on it.
    MeshRegistry meshes;
    TextureRegistry textures;
    meshes.setTextureRegistry(&textures);
    int groundMesh = -1;
    if (withGround) groundMesh = meshes.importMesh(makeGround(40.0f));

    PhysicsWorld world;
    if (groundMesh >= 0) {
        RigidBody ground;
        ground.shape = Shape::Mesh;
        ground.mesh = groundMesh;
        ground.halfExtents = Vec3{20.0f, 0.01f, 20.0f};
        ground.material.isStatic = true;
        ground.configure(1.0f);
        ground.syncPrevTransform();
        world.bodies.push_back(ground);
    }
    addBox(world, Vec3{0.0f, 1.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f});
    addBox(world, Vec3{4.0f, 0.5f, -3.0f}, Vec3{0.5f, 0.5f, 2.0f});
    addBox(world, Vec3{-5.0f, 1.5f, 3.0f}, Vec3{1.5f, 1.5f, 0.5f});

    SceneRenderer renderer;
    renderer.initialize();
    renderer.setShadowSettings(settings);
    renderer.setShowShadowDebug(false);
    renderer.setShadowDebugMode(debugMode);

    Camera camera;
    camera.position = Vec3{11.0f, 9.0f, 15.0f};
    // Looking at the middle of the scene: the engine's yaw is measured so that
    // the camera looks down -Z when the yaw is zero.
    camera.yaw = -0.633f;
    camera.pitch = -0.41f;
    camera.fovDeg = 60.0f;

    Viewport viewport;
    viewport.glX = 0;
    viewport.glY = 0;
    viewport.width = width;
    viewport.height = height;

    renderer.beginFrame(width, height);
    renderer.renderWorld(world, meshes, textures, camera, viewport, /*showGrid=*/true,
                         /*showVectors=*/false, 0.0f, -1);

    std::vector<unsigned char> pixels((size_t)width * (size_t)height * 3);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // GL counts from the bottom; a picture counts from the top.
    std::vector<unsigned char> flipped(pixels.size());
    const size_t row = (size_t)width * 3;
    for (int y = 0; y < height; ++y)
        std::copy(pixels.begin() + (size_t)(height - 1 - y) * row,
                  pixels.begin() + (size_t)(height - y) * row,
                  flipped.begin() + (size_t)y * row);

    if (!writePixels(out, flipped, width, height)) {
        std::fprintf(stderr, "shadowtest: cannot write '%s'\n", out.c_str());
        glfwTerminate();
        return 1;
    }
    std::printf("shadowtest: '%s' written\n", out.c_str());

    // And the map the picture was read from, next to it.
    for (int cascade = 0; cascade < renderer.shadowMap().layers(); ++cascade) {
        const std::string mapOut = out + ".map" + std::to_string(cascade) + ".png";
        if (writeMap(mapOut, renderer.shadowMap(), cascade))
            std::printf("shadowtest: '%s' written\n", mapOut.c_str());
    }
    {
        const ShadowMap& map = renderer.shadowMap();
        std::printf("shadowtest: centre (%.2f, %.2f, %.2f)\n", map.sceneCentre().x,
                    map.sceneCentre().y, map.sceneCentre().z);
        for (size_t i = 0; i < map.cascadeRadii().size(); ++i)
            std::printf("shadowtest: cascade %d radius %.2f depth %.2f texel %.4f\n", (int)i,
                        map.cascadeRadii()[i], map.cascadeDepthRanges()[i],
                        map.cascadeTexelSizes()[i]);
    }

    // THE PARAMETERS HAVE TO CHANGE THE PICTURE.
    //
    // The sun moves: the shadows move with it, and the map is drawn again
    // because the sun moved. The shadows go off and come back: the map is
    // freed while they are off and made again when they are on, and a map
    // that was made again holds nothing and has to be drawn again.
    //
    // Each of these is a picture of its own, and the pictures have to be
    // different from the first one - except the last, which has to be the
    // first one again.
    auto render = [&](const std::string& name) {
        renderer.beginFrame(width, height);
        renderer.renderWorld(world, meshes, textures, camera, viewport, false, false, 0.0f, -1);
        std::vector<unsigned char> shot((size_t)width * (size_t)height * 3);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, shot.data());
        std::vector<unsigned char> up(shot.size());
        for (int y = 0; y < height; ++y)
            std::copy(shot.begin() + (size_t)(height - 1 - y) * row,
                      shot.begin() + (size_t)(height - y) * row,
                      up.begin() + (size_t)y * row);
        writePixels(name, up, width, height);
        std::printf("shadowtest: '%s' written\n", name.c_str());
        return up;
    };

    // How much of the picture is different.
    auto difference = [&](const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
        size_t different = 0;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i]) ++different;
        return 100.0 * (double)different / (double)a.size();
    };

    ShadowSettings moved = settings;
    moved.sunAzimuth = settings.sunAzimuth + 40.0f;
    renderer.setShadowSettings(moved);
    const std::vector<unsigned char> afterSun = render(out + ".sun.png");

    ShadowSettings off = moved;
    off.quality = ShadowQuality::Off;
    renderer.setShadowSettings(off);
    const std::vector<unsigned char> afterOff = render(out + ".off.png");

    renderer.setShadowSettings(moved);
    const std::vector<unsigned char> afterBack = render(out + ".back.png");

    std::printf("shadowtest: the sun moved: %.1f%% of the picture changed\n",
                difference(flipped, afterSun));
    std::printf("shadowtest: the shadows off: %.1f%% of the picture changed\n",
                difference(flipped, afterOff));
    std::printf("shadowtest: the shadows on again: %.1f%% of the picture changed\n",
                difference(flipped, afterBack));
    std::printf("shadowtest: the shadows on again are the same picture: %.3f%% of it differs\n",
                difference(afterSun, afterBack));

    glfwTerminate();
    return 0;
}
