#pragma once
#include <GLFW/glfw3.h>

#include "platform/InputEvents.hpp"

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

    // The pointer's position, in the framebuffer's pixels - the same pixels the
    // engine draws in. On a scaled display the window is smaller than its
    // framebuffer, and the cursor comes in the window's pixels; the two are
    // reconciled here so that nothing downstream has to know about it.
    void cursorPos(double& x, double& y) const;
    bool mouseDown(int button) const;
    bool mousePressed(int button);
    bool mouseReleased(int button);
    double consumeScroll();
    int consumeTextInput(char* out, int maxChars);
    // Throws the characters that have arrived away.
    //
    // They are kept until somebody reads them, and a character that nobody
    // read is a character that lands in whatever is opened next: a digit typed
    // with no field in sight turns up in the number in the field that is
    // opened a moment later. A field that is given the keyboard throws the
    // old characters away, because they were typed at something else.
    void discardTextInput();

    // Whether a character may be put in a text field: the ones the engine can
    // draw.
    //
    // Printable ASCII, Latin-1 and the Cyrillic the interface is written in.
    // A character the engine cannot draw is a character that would sit in the
    // field as an invisible mark, so it is not put there at all.
    //
    // The list is here and not in the font atlas because the window is below
    // the renderer - but a test asks the two whether they still agree.
    static bool textCodepointSupported(unsigned codepoint);

    // Whether a field has the keyboard.
    //
    // While it does, the characters that arrive are KEPT until somebody reads
    // them - even the ones that arrive after the frame has been drawn. The
    // frame drops what nobody read, and a character that arrives between the
    // drawing and the end of the frame was read by nobody: a fast typist
    // loses every second letter, and a letter that is lost is a name that is
    // spelled wrong.
    void setTextCaptured(bool captured) { textCaptured_ = captured; }
    bool textCaptured() const { return textCaptured_; }

    bool keyDown(int key) const;
    bool takeKeyPress(int key);

    // The frame's edges, oldest first. Filled in the GLFW callbacks and independent
    // of everything above: takeKeyPress() clears keyEdges_ and not this, so a script
    // reading the queue still sees a press the editor already acted on. That is
    // deliberate - a game wants the key the editor used for a shortcut too - and it
    // is also why there is no "claim" or "consume" here. The queue is read-only to
    // a reader and cleared once per frame by dropUnreadInput().
    //
    // Mouse movement is not an event. One pixel of movement is one GLFW callback and
    // a cursor crossing the window is hundreds, which would make the queue grow with
    // distance instead of with intent; mouseDelta() is the aggregate for that.
    int eventCount() const { return events_.count(); }
    bool eventAt(int index, InputEvent& out) const { return events_.at(index, out); }

    double time() const;
    void setTitle(const char* title);
    void requestClose() const;
    GLFWwindow* handle() const { return window_; }
    int framebufferWidth() const { return fbW_; }
    int framebufferHeight() const { return fbH_; }

private:
    // How many framebuffer pixels there are in one window pixel. One on a
    // display that is not scaled, more on one that is.
    double cursorScale() const;
    static void onKey(GLFWwindow*, int, int, int, int);
    static void onMouseButton(GLFWwindow*, int, int, int);
    static void onScroll(GLFWwindow*, double, double);
    static void onChar(GLFWwindow*, unsigned int);
    static void onFramebufferSize(GLFWwindow*, int, int);

    static Window* of(GLFWwindow* w);
    void bindCallbacks();

    GLFWwindow* window_ = nullptr;
    // See InputEvents.hpp. Kept next to the state arrays below rather than
    // replacing them: both are asked different questions.
    InputEvents events_;
    bool keys_[GLFW_KEY_LAST + 1] = {};
    bool keyEdges_[GLFW_KEY_LAST + 1] = {};
    bool mouseDown_[8] = {};
    bool mousePressed_[8] = {};
    bool mouseReleased_[8] = {};
    double scrollAcc_ = 0.0;
    char textBuf_[64] = {};
    int textLen_ = 0;
    // True while a text field has the keyboard. The characters that arrive
    // then are kept until somebody reads them - see setTextCaptured().
    bool textCaptured_ = false;
    bool captured_ = false;
    bool deltaPrimed_ = false;
    double lastX_ = 0.0, lastY_ = 0.0;
    // Cursor position as of the last consumeActivity(), to spot a move.
    double activityX_ = -1.0, activityY_ = -1.0;
    int fbW_ = 0, fbH_ = 0;
};

} // namespace s3d