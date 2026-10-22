// A picture of the clouds, without an editor.
//
// The clouds are the one part of the picture that cannot be checked by
// reading the code: whether a cloud looks like a cloud is a question about
// the picture and not about the arithmetic. So this renders the cloud's own
// shaders - the box of a cloud, the weather in it and the light through it -
// and writes a PNG.
//
//   cloudtest <file.png> [view]
//
// The view is what the camera does:
//
//   0  a heap of puffs, from the side
//   1  a heap of puffs, straight at it
//   2  a heap of puffs, from above
//   3  the sun behind the cloud: the silver lining
//   4  streaks of cirrus
//   5  a layer
//   6  a field of cloud, from above
//   7  a field of cloud, from the ground
//
// The program is the engine's own: the engine's loader reads the shaders,
// with the includes put in place the way the engine puts them in place, and
// the engine's own program holds the uniforms.

#include "platform/GlFunctions.hpp"
#include "platform/Screenshot.hpp"
#include "render/ShaderProgram.hpp"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

using namespace s3d;

namespace {

struct V3 {
    float x, y, z;
};
static V3 sub(V3 a, V3 b) { return V3{a.x - b.x, a.y - b.y, a.z - b.z}; }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 norm(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 0.0f ? V3{a.x / l, a.y / l, a.z / l} : a;
}
static V3 cross(V3 a, V3 b) {
    return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static void lookAt(float* out, V3 eye, V3 centre, V3 up) {
    const V3 f = norm(sub(centre, eye));
    const V3 s = norm(cross(f, up));
    const V3 u = cross(s, f);
    out[0] = s.x; out[4] = s.y; out[8] = s.z; out[12] = -dot(s, eye);
    out[1] = u.x; out[5] = u.y; out[9] = u.z; out[13] = -dot(u, eye);
    out[2] = -f.x; out[6] = -f.y; out[10] = -f.z; out[14] = dot(f, eye);
    out[3] = 0; out[7] = 0; out[11] = 0; out[15] = 1;
}
static void perspective(float* out, float fovY, float aspect, float nearZ, float farZ) {
    const float f = 1.0f / std::tan(fovY * 0.5f);
    for (int i = 0; i < 16; ++i) out[i] = 0.0f;
    out[0] = f / aspect;
    out[5] = f;
    out[10] = (farZ + nearZ) / (nearZ - farZ);
    out[11] = -1.0f;
    out[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
}

// One cloud of the picture.
struct Cloud {
    V3 centre;
    unsigned seed;
};

} // namespace

int main(int argc, char** argv) {
    const std::string outFile = argc > 1 ? argv[1] : "cloudtest.png";
    const int which = argc > 2 ? std::atoi(argv[2]) : 0;

    if (!glfwInit()) {
        std::printf("no GLFW\n");
        return 1;
    }
    // 3.3 compatibility, like the engine: the renderer is written against the
    // fixed pipeline, and the compatibility profile keeps all of it.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    const int W = 800, H = 500;
    GLFWwindow* window = glfwCreateWindow(W, H, "cloudtest", nullptr, nullptr);
    if (!window) {
        std::printf("no window: the clouds need a 3.3 context\n");
        return 1;
    }
    glfwMakeContextCurrent(window);
    std::printf("GL: %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    std::printf("card: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    loadGlFunctions();

    // The cloud's own program, read by the engine's own loader.
    ShaderProgram program;
    if (!program.load(shaderPath("clouds/clouds.vert"), shaderPath("clouds/clouds.frag"))) {
        std::printf("the cloud's program did not build\n");
        return 1;
    }
    program.use();

    // THE PICTURE
    V3 centre{0.0f, 58.0f, 0.0f};
    V3 boxMin{-34.0f, -30.0f, -30.0f};
    V3 boxMax{34.0f, 30.0f, 30.0f};
    int shape = 0;
    float coverage = 0.68f;
    float softness = 0.62f;
    int detail = 4;
    float weatherScale = 3.4f;
    float detailScale = 13.0f;
    float density = 1.0f;
    float extinction = 7.5f;
    V3 eye{62.0f, 58.0f, 84.0f};
    V3 look{0.0f, 54.0f, 0.0f};
    V3 sun = norm(V3{0.58f, 0.62f, 0.53f});

    std::vector<Cloud> field;
    if (which == 1) {
        eye = V3{0.0f, 58.0f, 150.0f};
        look = V3{0.0f, 58.0f, 0.0f};
        sun = norm(V3{0.55f, 0.35f, 0.76f});
    } else if (which == 2) {
        eye = V3{0.0f, 150.0f, 0.5f};
        look = V3{0.0f, 58.0f, 0.0f};
    } else if (which == 3) {
        eye = V3{0.0f, 58.0f, 150.0f};
        look = V3{0.0f, 58.0f, 0.0f};
        sun = norm(V3{0.06f, 0.14f, -0.99f});
    } else if (which == 4) {
        shape = 2;
        boxMin = V3{-420.0f, -45.0f, -420.0f};
        boxMax = V3{420.0f, 45.0f, 420.0f};
        weatherScale = 9.0f;
        detailScale = 90.0f;
        softness = 0.5f;
        extinction = 5.0f;
        centre = V3{0.0f, 320.0f, 0.0f};
        eye = V3{0.0f, 10.0f, 120.0f};
        look = V3{60.0f, 420.0f, -260.0f};
        sun = norm(V3{0.30f, 0.55f, 0.78f});
    } else if (which == 5) {
        shape = 1;
        boxMin = V3{-70.0f, -12.0f, -70.0f};
        boxMax = V3{70.0f, 12.0f, 70.0f};
        eye = V3{80.0f, 62.0f, 100.0f};
        look = V3{0.0f, 56.0f, 0.0f};
    } else if (which == 6) {
        // A field of cloud, seen from above: the sea of cloud that an
        // aeroplane flies over.
        for (int gy = -4; gy <= 4; ++gy)
            for (int gx = -4; gx <= 4; ++gx)
                field.push_back(Cloud{V3{(float)gx * 105.0f + 40.0f, 40.0f, (float)gy * 105.0f - 30.0f},
                                      (unsigned)(1 + gx * 7 + gy * 13 + 91)});
        boxMin = V3{-46.0f, -26.0f, -46.0f};
        boxMax = V3{46.0f, 26.0f, 46.0f};
        eye = V3{40.0f, 620.0f, 320.0f};
        look = V3{40.0f, 40.0f, -30.0f};
        sun = norm(V3{0.36f, 0.55f, 0.75f});
    } else if (which == 7) {
        // A field of cloud seen from the ground.
        for (int gy = -2; gy <= 2; ++gy)
            for (int gx = -2; gx <= 2; ++gx)
                field.push_back(Cloud{V3{(float)gx * 190.0f + 30.0f, 165.0f, (float)gy * 190.0f - 80.0f},
                                      (unsigned)(3 + gx * 5 + gy * 11 + 41)});
        boxMin = V3{-55.0f, -40.0f, -55.0f};
        boxMax = V3{55.0f, 40.0f, 55.0f};
        eye = V3{0.0f, 4.0f, 260.0f};
        look = V3{20.0f, 130.0f, -40.0f};
        sun = norm(V3{0.34f, 0.62f, 0.71f});
        weatherScale = 4.0f;
        coverage = 0.40f;
    }

    float viewMatrix[16], projectionMatrix[16];
    lookAt(viewMatrix, eye, look, V3{0, 1, 0});
    perspective(projectionMatrix, 55.0f * 3.14159265f / 180.0f, (float)W / (float)H, 0.5f, 3000.0f);

    program.setMat4("uView", viewMatrix);
    program.setMat4("uProjection", projectionMatrix);
    program.setVec3("uCameraPosition", eye.x, eye.y, eye.z);
    program.setVec3("uSunDirection", sun.x, sun.y, sun.z);
    program.setVec3("uSunColour", 1.30f, 1.20f, 1.05f);
    program.setVec3("uSkyColour", 0.34f, 0.45f, 0.62f);
    program.setVec3("uGroundColour", 0.16f, 0.15f, 0.13f);
    program.setFloat("uAmbient", 0.80f);
    program.setVec3("uCloudColour", 1.0f, 0.99f, 0.96f);
    program.setVec3("uCloudShade", 0.66f, 0.72f, 0.82f);
    program.setVec3("uBoxMin", boxMin.x, boxMin.y, boxMin.z);
    program.setVec3("uBoxMax", boxMax.x, boxMax.y, boxMax.z);
    program.setInt("uShape", shape);
    program.setFloat("uCloudCoverage", coverage);
    program.setFloat("uCloudSoftness", softness);
    program.setInt("uCloudDetail", detail);
    program.setFloat("uCloudDetailScale", detailScale);
    program.setFloat("uCloudDensity", density);
    program.setFloat("uExtinction", extinction);
    program.setFloat("uSeed", 2024.0f);
    program.setFloat("uWeatherScale", weatherScale);
    program.setVec2("uWindOffset", 0.0f, 0.0f);
    program.setVec2("uWindDirection", 0.82f, 0.57f);
    program.setInt("uVolumeEnabled", 0);
    program.setInt("uSteps", 96);
    program.setInt("uLightSteps", 6);
    program.setFloat("uLightStep", 0.12f);
    program.setFloat("uScatter", 1.0f);
    program.setVec3("uFogColour", 0.70f, 0.78f, 0.88f);
    program.setFloat("uFogDensity", 0.0012f);
    program.setFloat("uFogFalloff", 0.05f);
    program.setFloat("uFogHeight", 0.0f);
    program.setFloat("uFogScatter", 0.4f);
    program.setFloat("uFogForward", 0.7f);

    // The shadow map: an empty one, so the sun's light is everywhere.
    {
        const int kMap = 64;
        GLuint shadowMap = 0;
        glGenTextures(1, &shadowMap);
        glBindTexture(GL_TEXTURE_2D_ARRAY, shadowMap);
        std::vector<float> layers((size_t)kMap * kMap * 4, 1.0f);
        gl::TexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R32F, kMap, kMap, 4, 0, GL_RED, GL_FLOAT,
                       layers.data());
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl::ActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D_ARRAY, shadowMap);
        program.setInt("uShadowMap", 1);
        gl::ActiveTexture(GL_TEXTURE0);
    }
    {
        float lights[64] = {0};
        for (int i = 0; i < 4; ++i) lights[i * 16 + 0] = 1.0f;
        program.setMat4Array("uLightViewProjection[0]", lights, 4);
        float params[16] = {0};
        for (int i = 0; i < 4; ++i) {
            params[i * 4 + 0] = 1000.0f;
            params[i * 4 + 1] = 500.0f;
            params[i * 4 + 2] = 2.0f;
        }
        program.setVec4Array("uCascadeParams[0]", params, 4);
    }
    program.setVec3("uSceneCentre", 0.0f, 0.0f, 0.0f);
    program.setInt("uCascadeCount", 4);
    program.setInt("uShadowEnabled", 1);
    program.setFloat("uShadowBias", 0.05f);

    // THE BOX OF THE CLOUD, in the cloud's own numbers.
    const V3 corner[8] = {{boxMin.x, boxMin.y, boxMin.z}, {boxMax.x, boxMin.y, boxMin.z},
                           {boxMax.x, boxMax.y, boxMin.z}, {boxMin.x, boxMax.y, boxMin.z},
                           {boxMin.x, boxMin.y, boxMax.z}, {boxMax.x, boxMin.y, boxMax.z},
                           {boxMax.x, boxMax.y, boxMax.z}, {boxMin.x, boxMax.y, boxMax.z}};
    const int face[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                             {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
    std::vector<float> box;
    for (const auto& f : face)
        for (const int index : {0, 1, 2, 0, 2, 3}) {
            const V3& c = corner[f[index]];
            box.push_back(c.x);
            box.push_back(c.y);
            box.push_back(c.z);
        }
    GLuint buffer = 0;
    gl::GenBuffers(1, &buffer);
    gl::BindBuffer(GL_ARRAY_BUFFER, buffer);
    gl::BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(box.size() * sizeof(float)), box.data(),
                    GL_STATIC_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    glViewport(0, 0, W, H);
    glClearColor(0.22f, 0.44f, 0.78f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    // The front faces of the box are taken away, so that the back faces are
    // the ones on the screen: the back faces are there even when the eye is
    // inside the cloud.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    // The premultiplied alpha: the colour is added to the picture and the
    // bottom comes through what is left of the transparency.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    if (field.empty()) {
        float model[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        model[12] = centre.x;
        model[13] = centre.y;
        model[14] = centre.z;
        program.setMat4("uModel", model);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(box.size() / 3));
    } else {
        // The far ones first: the cloud in front is the one that is put over
        // them.
        for (const Cloud& cloud : field) {
            float model[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            model[12] = cloud.centre.x;
            model[13] = cloud.centre.y;
            model[14] = cloud.centre.z;
            program.setMat4("uModel", model);
            program.setFloat("uSeed", (float)cloud.seed);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(box.size() / 3));
        }
    }
    glDisable(GL_BLEND);
    glFinish();

    std::vector<unsigned char> pixels((size_t)W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    // The picture of the framebuffer is upside down: the first row of it is
    // the last row of the picture.
    std::vector<unsigned char> flipped(pixels.size());
    for (int y = 0; y < H; ++y)
        std::memcpy(&flipped[(size_t)y * W * 4], &pixels[(size_t)(H - 1 - y) * W * 4], (size_t)W * 4);

    if (!screenshot::savePixels(outFile, flipped, W, H)) {
        std::printf("cannot write the picture\n");
        return 1;
    }
    std::printf("OK: %s\n", outFile.c_str());
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
