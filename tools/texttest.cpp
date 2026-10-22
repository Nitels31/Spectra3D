// A picture of the letters, without an editor.
//
// The text of the interface is the one part of the picture that cannot be
// checked by reading the code: whether the letters are drawn at all, whether
// they are the right ones, and whether they land where they are supposed to
// land - those are questions about the picture. So this draws a line of text
// into a framebuffer, reads the pixels back, and says how many of them are
// the colour of the text.
//
//   texttest [file.png] [stage]
//
// The stage is how much of the frame is drawn before the letters:
//   plain  - the letters on their own
//   scene  - the world first, the way the editor draws it
//   gui    - the world and the canvas of the game first
//
// The text of the editor is drawn after the world, and a letter that is not
// there is a letter the world took with it.

#include "assets/MeshRegistry.hpp"
#include "assets/TextureRegistry.hpp"
#include "core/Log.hpp"
#include "core/Str.hpp"
#include "gui/GuiCanvas.hpp"
#include "gui/GuiRenderer.hpp"
#include "platform/Filesystem.hpp"
#include "platform/GlFunctions.hpp"
#include "platform/Screenshot.hpp"
#include "render/Camera.hpp"
#include "render/SceneRenderer.hpp"
#include "render/UiRenderer.hpp"
#include "scene/BodyFactory.hpp"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace s3d;

namespace {

// The colour of the text, and the colour of the paper it is on.
const Color kInk{1.0f, 1.0f, 1.0f, 1.0f};
const Color kPaper{0.0f, 0.0f, 0.0f, 1.0f};

bool writePixels(const std::string& path, const std::vector<unsigned char>& pixels, int width,
                 int height) {
    return screenshot::savePixels(path, pixels, width, height);
}

// How many pixels are the colour of the text.
int countInk(const std::vector<unsigned char>& pixels) {
    int ink = 0;
    for (size_t i = 0; i < pixels.size(); i += 3) {
        if (pixels[i] > 128 && pixels[i + 1] > 128 && pixels[i + 2] > 128) ++ink;
    }
    return ink;
}

} // namespace

int main(int argc, char** argv) {
    const std::string folder = screenshot::pictureFolder();
    const std::string out = argc > 1 ? argv[1] : str::join(folder, "text.png");
    const std::string stage = argc > 2 ? argv[2] : "plain";

    if (!glfwInit()) {
        std::fprintf(stderr, "texttest: no GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    const int width = 640, height = 120;
    GLFWwindow* window = glfwCreateWindow(width, height, "texttest", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "texttest: no window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    if (!loadGlFunctions()) {
        std::fprintf(stderr, "texttest: the driver is missing the functions\n");
        glfwTerminate();
        return 1;
    }

    UiRenderer ui;
    if (!ui.buildFonts()) {
        std::fprintf(stderr, "texttest: no font\n");
        glfwTerminate();
        return 1;
    }
    std::fprintf(stderr, "texttest: the font is %s\n", ui.fontsReady() ? "there" : "NOT there");
    std::fprintf(stderr, "texttest: the width of 'Hello' is %.2f\n", ui.textWidth("Hello"));

    MeshRegistry meshes;
    TextureRegistry textures;
    meshes.setTextureRegistry(&textures);
    PhysicsWorld world;

    SceneRenderer renderer;
    renderer.initialize();
    renderer.setShowShadowDebug(false);

    Camera camera;
    camera.position = Vec3{11.0f, 9.0f, 15.0f};
    camera.yaw = -0.633f;
    camera.pitch = -0.41f;
    camera.fovDeg = 60.0f;

    Viewport viewport;
    viewport.glX = 0;
    viewport.glY = 0;
    viewport.width = width;
    viewport.height = height;

    if (stage == "scene" || stage == "gui") {
        renderer.beginFrame(width, height);
        renderer.renderWorld(world, meshes, textures, camera, viewport, /*showGrid=*/true,
                             /*showVectors=*/false, 0.0f, 0.0f, -1);
    }

    if (stage == "gui") {
        // The canvas of the game: a button with a line of letters on it, the
        // same shape the editor draws over the world.
        gui::Canvas canvas;
        canvas.width = 640;
        canvas.height = 120;
        gui::Element button;
        button.kind = gui::ElementKind::Button;
        button.name = "Start";
        button.x = 20;
        button.y = 20;
        button.width = 200;
        button.height = 60;
        button.color = Color{0.22f, 0.26f, 0.34f, 1.0f};
        button.text = "Start";
        button.fontSize = 32.0f;
        canvas.elements.push_back(button);

        gui::Renderer guiRenderer;
        guiRenderer.buildFonts();
        const Rect area{0.0f, 0.0f, (float)width, (float)height};
        guiRenderer.begin(width, height, area);
        guiRenderer.draw(canvas, area, textures, false, 0.0f, 0.0f, false, false, false);
        guiRenderer.end();
    }

    ui.begin(width, height);
    ui.fillRect(0, 0, (float)width, (float)height, kPaper);
    ui.text(20.0f, 20.0f, "Hello", kInk);
    ui.text(20.0f, 50.0f, "Text", kInk);
    ui.text(20.0f, 80.0f, "Привіт", kInk);
    ui.end();

    std::vector<unsigned char> pixels((size_t)width * (size_t)height * 3);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    std::fprintf(stderr, "texttest: stage '%s' -> %d pixel(s) of text\n", stage.c_str(),
                 countInk(pixels));

    // GL counts from the bottom; a picture counts from the top.
    std::vector<unsigned char> flipped(pixels.size());
    const size_t row = (size_t)width * 3;
    for (int y = 0; y < height; ++y)
        std::copy(pixels.begin() + (size_t)(height - 1 - y) * row,
                  pixels.begin() + (size_t)(height - y) * row, flipped.begin() + (size_t)y * row);
    if (writePixels(out, flipped, width, height))
        std::fprintf(stderr, "texttest: '%s' written\n", out.c_str());

    glfwTerminate();
    return countInk(pixels) > 0 ? 0 : 1;
}
