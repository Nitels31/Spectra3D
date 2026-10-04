#pragma once
#include <GLFW/glfw3.h>

namespace s3d {

// The OpenGL context the engine asks for: 3.3 in the COMPATIBILITY profile, not
// core. Core would remove the fixed pipeline the renderer is written against
// (glOrtho, glEnableClientState, glTexCoord2f); compatibility keeps all of it.
//
// Note that a driver's GL_VERSION string is not this number. NVIDIA, for one,
// reports its highest supported version for any compatibility context, so a 3.3
// context on that driver still prints "4.6.0 ...". The requested version is what
// the UI shows; the driver string goes to the log.
inline constexpr int kGlVersionMajor = 3;
inline constexpr int kGlVersionMinor = 3;

// GLFW window + input state. Edge-triggered flags (`mousePressed`,
// `takePressed`) are consumed by the reader, level-triggered flags (`mouseDown`,
// `keyPressed`) describe the current state.
class Window {
public:
    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(const char* title, int width, int height);
    void setSizeLimits(int minW, int minH, int maxW, int maxH);
    bool pumpEvents();
    void present();


    void setMouseCaptured(bool capture);
    bool mouseCaptured() const { return captured_; }
    void mouseDelta(double& dx, double& dy);

    void cursorPos(double& x, double& y) const;
    bool mouseDown(int button) const;
    bool mousePressed(int button);
    bool mouseReleased(int button);
    double consumeScroll();
    int consumeTextInput(char* out, int maxChars);

    bool keyDown(int key) const;
    bool takeKeyPress(int key);

    double time() const;
    void setTitle(const char* title);
    void requestClose() const;
    GLFWwindow* handle() const { return window_; }
    int framebufferWidth() const { return fbW_; }
    int framebufferHeight() const { return fbH_; }

private:
    static void onKey(GLFWwindow*, int, int, int, int);
    static void onMouseButton(GLFWwindow*, int, int, int);
    static void onScroll(GLFWwindow*, double, double);
    static void onChar(GLFWwindow*, unsigned int);
    static void onFramebufferSize(GLFWwindow*, int, int);

    static Window* of(GLFWwindow* w);
    void bindCallbacks();

    GLFWwindow* window_ = nullptr;
    bool keys_[GLFW_KEY_LAST + 1] = {};
    bool keyEdges_[GLFW_KEY_LAST + 1] = {};
    bool mouseDown_[8] = {};
    bool mousePressed_[8] = {};
    bool mouseReleased_[8] = {};
    double scrollAcc_ = 0.0;
    char textBuf_[64] = {};
    int textLen_ = 0;
    bool captured_ = false;
    bool deltaPrimed_ = false;
    double lastX_ = 0.0, lastY_ = 0.0;
    int fbW_ = 0, fbH_ = 0;
};

} // namespace s3d