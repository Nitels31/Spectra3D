// A picture of the water, without an editor.
//
// The water is the one part of the picture that cannot be checked by reading
// the code: whether the waves are waves, whether the colour of the water
// changes with the depth, whether the foam is where the wave breaks and
// whether the bottom shows through - those are questions about the picture.
// So this renders a scene with a ground and a sheet of water over it into a
// PNG, and the PNG is looked at.
//
//   watertest <file.png> [time] [waveHeight] [waveLength] [depth] [clarity]
//             [foam] [refraction] [darkening] [camera]
//
// The numbers after the file name are the settings the picture is made with.
// Without them the defaults are used. The camera is 0 for the one that looks
// at the water from above, 1 for the one that looks along it, and 2 for the
// one that is under the water.

#include "assets/MeshRegistry.hpp"
#include "assets/TextureRegistry.hpp"
#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "platform/GlFunctions.hpp"
#include "platform/Screenshot.hpp"
#include "render/SceneRenderer.hpp"
#include "scene/BodyFactory.hpp"

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
// The ground is a mesh and not a primitive because the water needs
// something under it to be seen through.
MeshAsset makeGround(float size, float y) {
    MeshAsset asset;
    asset.name = "ground";
    MeshPart part;
    const float h = size * 0.5f;
    // position, normal, texture coordinate - eight floats to a vertex, the
    // layout every mesh in the engine has.
    const float verts[4][8] = {
        {-h, y, -h, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
        {h, y, -h, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
        {h, y, h, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        {-h, y, h, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
    };
    for (const auto& v : verts)
        for (float f : v) part.vertices.push_back(f);
    part.indices = {0, 1, 2, 0, 2, 3};
    // The colour of the ground: a part with no colour of its own is white,
    // and a white bottom under the water is a white bottom under the water.
    part.color[0] = 0.55f;
    part.color[1] = 0.48f;
    part.color[2] = 0.36f;
    asset.parts.push_back(std::move(part));
    asset.boundsMin = Vec3{-h, y, -h};
    asset.boundsMax = Vec3{h, y, h};
    asset.center = Vec3{0.0f, y, 0.0f};
    asset.half = Vec3{h, 0.0f, h};
    asset.radius = asset.half.length();
    asset.scale = 1.0f;
    return asset;
}

// A body with a box of its own, standing in the water.
void addBox(PhysicsWorld& world, const Vec3& position, const Vec3& half) {
    RigidBody body;
    body.shape = Shape::Box;
    body.halfExtents = half;
    body.position = position;
    body.material.isStatic = true;
    body.material.color = Vec3{0.65f, 0.55f, 0.40f};
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

} // namespace

int main(int argc, char** argv) {
    const std::string folder = screenshot::pictureFolder();
    const std::string out = argc > 1 ? argv[1] : str::join(folder, "water.png");

    float time = 3.0f;
    float waveHeight = 0.40f;
    float waveLength = 9.0f;
    float depth = 5.0f;
    float clarity = 0.32f;
    float foam = 0.7f;
    float refraction = 1.0f;
    float darkening = 0.45f;
    float specular = 1.0f;
    float reflectivity = 1.0f;
    float detail = 1.0f;
    int camera = 0;

    if (argc > 2) time = (float)std::atof(argv[2]);
    if (argc > 3) waveHeight = (float)std::atof(argv[3]);
    if (argc > 4) waveLength = (float)std::atof(argv[4]);
    if (argc > 5) depth = (float)std::atof(argv[5]);
    if (argc > 6) clarity = (float)std::atof(argv[6]);
    if (argc > 7) foam = (float)std::atof(argv[7]);
    if (argc > 8) refraction = (float)std::atof(argv[8]);
    if (argc > 9) darkening = (float)std::atof(argv[9]);
    if (argc > 10) camera = std::atoi(argv[10]);
    if (argc > 11) specular = (float)std::atof(argv[11]);
    if (argc > 12) reflectivity = (float)std::atof(argv[12]);
    if (argc > 13) detail = (float)std::atof(argv[13]);

    // The window is hidden: the picture is the point, not the window.
    if (!glfwInit()) {
        std::fprintf(stderr, "watertest: no GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    const int width = 1400, height = 900;
    GLFWwindow* window = glfwCreateWindow(width, height, "watertest", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "watertest: no window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    if (!loadGlFunctions()) {
        std::fprintf(stderr, "watertest: the driver is missing the functions\n");
        glfwTerminate();
        return 1;
    }

    // The scene: a ground under the water and two boxes standing in it.
    MeshRegistry meshes;
    TextureRegistry textures;
    meshes.setTextureRegistry(&textures);
    const int groundMesh = meshes.importMesh(makeGround(60.0f, -1.5f));

    PhysicsWorld world;
    {
        RigidBody ground;
        ground.shape = Shape::Mesh;
        ground.mesh = groundMesh;
        ground.halfExtents = Vec3{30.0f, 0.01f, 30.0f};
        ground.position = Vec3{0.0f, -1.5f, 0.0f};
        ground.material.isStatic = true;
        ground.material.color = Vec3{0.55f, 0.48f, 0.36f};
        ground.configure(1.0f);
        ground.syncPrevTransform();
        world.bodies.push_back(ground);
    }
    addBox(world, Vec3{3.0f, 0.5f, -2.0f}, Vec3{0.6f, 2.0f, 0.6f});
    addBox(world, Vec3{-6.0f, 0.2f, 4.0f}, Vec3{1.2f, 1.7f, 1.2f});

    // The water: a level of it over the ground.
    RigidBody water = BodyFactory::makeWater(Vec3{0.0f, 0.0f, 0.0f}, 60.0f, 60.0f,
                                              Vec3{0.10f, 0.32f, 0.45f}, 0.65f);
    water.water.depth = depth;
    water.water.clarity = clarity;
    water.water.foam = foam;
    water.water.waveHeight = waveHeight;
    water.water.waveLength = waveLength;
    water.water.refraction = refraction != 0.0f;
    water.water.darkening = darkening;
    water.water.specular = specular;
    water.water.reflectivity = reflectivity;
    water.water.detailStrength = detail;
    world.bodies.push_back(water);

    SceneRenderer renderer;
    renderer.initialize();
    renderer.setShadowSettings(ShadowSettings{});

    Camera cam;
    if (camera == 1) {
        // Along the water: the mirror at a grazing angle.
        cam.position = Vec3{0.0f, 1.6f, 22.0f};
        cam.yaw = 0.0f;
        cam.pitch = -0.06f;
    } else if (camera == 2) {
        // Under the water: the bottom through the surface.
        cam.position = Vec3{6.0f, -0.9f, 8.0f};
        cam.yaw = -0.6f;
        cam.pitch = 0.12f;
    } else {
        // From above: the colour of the water and the bottom through it.
        cam.position = Vec3{14.0f, 12.0f, 18.0f};
        cam.yaw = -0.63f;
        cam.pitch = -0.45f;
    }
    cam.fovDeg = 60.0f;

    Viewport viewport;
    viewport.glX = 0;
    viewport.glY = 0;
    viewport.width = width;
    viewport.height = height;

    renderer.beginFrame(width, height);
    renderer.renderWorld(world, meshes, textures, cam, viewport, /*showGrid=*/false,
                         /*showVectors=*/false, 0.0f, time, -1);

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
        std::fprintf(stderr, "watertest: cannot write '%s'\n", out.c_str());
        glfwTerminate();
        return 1;
    }
    std::printf("watertest: '%s' written\n", out.c_str());

    glfwTerminate();
    return 0;
}
