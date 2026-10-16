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
    // Paints the application icon into the title bar and the task bar. Drawn from
    // code instead of shipped as a file: one mark, two sizes, no asset to go
    // missing from a build. A no-op where the window system has no use for it.
    void applyIcon(const char* iconName = nullptr);
    void setSizeLimits(int minW, int minH, int maxW, int maxH);
    bool pumpEvents();
    void present();

    // True when anything happened since the last call, which means the next frame
    // has to be drawn: an event, a held key or button, a cursor move or a resize.
    // The loop uses it to skip drawing a window nobody is touching, which on a
    // laptop is the difference between a warm machine and a hot one.
    //
    // Peeks only. Every edge flag stays exactly as it was, so the widgets that
    // consume them still see the event this frame. The cursor position is what
    // this call latches, which is why there are two of them: `peekActivity()`
    // asks the same question without latching, so a wait that woke up on a mouse
    // move does not eat the movement that woke it up.
    bool consumeActivity();
    bool peekActivity() const;

    // Drops every input edge nobody read during the frame that was just drawn.
    //
    // Edges live for exactly one frame; the end of that frame is the only place
    // where "nobody wanted this" is knowable. Without it a single keypress in a
    // window with no shortcut for that key - Enter opening a project, a letter
    // typed with no text field focused - stays latched in `keyEdges_` forever,
    // `consumeActivity()` answers "yes, something happened" on every frame from
    // then on, and the editor redraws at the frame cap with nothing new to show.
    // Measured on the editor: 0.9 s of CPU per 8 s of doing nothing, against
    // 0.06 s once the frame that had its chance to read the press drops it.
    void dropUnreadInput();

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
    // Cursor position as of the last consumeActivity(), to spot a move.
    double activityX_ = -1.0, activityY_ = -1.0;
    int fbW_ = 0, fbH_ = 0;
};

} // namespace s3d