#include "Window.hpp"

#include "../core/Log.hpp"

#include <cstdio>

namespace s3d {
namespace {

void glfwErrorCallback(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc ? desc : "(null)");
}

} // namespace

Window* Window::of(GLFWwindow* w) {
    return static_cast<Window*>(glfwGetWindowUserPointer(w));
}

void Window::onKey(GLFWwindow* w, int key, int, int action, int) {
    Window* self = of(w);
    if (!self || key < 0 || key > GLFW_KEY_LAST) return;
    if (action == GLFW_PRESS) {
        self->keys_[key] = true;
        self->keyEdges_[key] = true;
    } else if (action == GLFW_RELEASE) {
        self->keys_[key] = false;
    }
}

void Window::onMouseButton(GLFWwindow* w, int button, int action, int) {
    Window* self = of(w);
    if (!self || button < 0 || button >= 8) return;
    if (action == GLFW_PRESS) {
        self->mouseDown_[button] = true;
        self->mousePressed_[button] = true;
    } else if (action == GLFW_RELEASE) {
        self->mouseDown_[button] = false;
        self->mouseReleased_[button] = true;
    }
}

void Window::onScroll(GLFWwindow* w, double, double yoff) {
    if (Window* self = of(w)) self->scrollAcc_ += yoff;
}

void Window::onChar(GLFWwindow* w, unsigned int cp) {
    Window* self = of(w);
    if (!self || cp < 32 || cp > 126) return;
    if (self->textLen_ < (int)sizeof(self->textBuf_) - 1)
        self->textBuf_[self->textLen_++] = (char)cp;
}

void Window::onFramebufferSize(GLFWwindow* w, int width, int height) {
    if (Window* self = of(w)) {
        self->fbW_ = width;
        self->fbH_ = height;
    }
}

Window::~Window() {
    if (!window_) return;
    glfwDestroyWindow(window_);
    window_ = nullptr;
    glfwTerminate();
}

bool Window::create(const char* title, int width, int height) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        return false;
    }
    // Instance recovery through the window handle removes the need for a
    // file-scope "current window" pointer.
    glfwSetWindowUserPointer(window_, this);
    bindCallbacks();
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    glfwGetFramebufferSize(window_, &fbW_, &fbH_);
    return true;
}

void Window::bindCallbacks() {
    glfwSetKeyCallback(window_, &Window::onKey);
    glfwSetMouseButtonCallback(window_, &Window::onMouseButton);
    glfwSetScrollCallback(window_, &Window::onScroll);
    glfwSetCharCallback(window_, &Window::onChar);
    glfwSetFramebufferSizeCallback(window_, &Window::onFramebufferSize);
}

void Window::setSizeLimits(int minW, int minH, int maxW, int maxH) {
    if (window_) glfwSetWindowSizeLimits(window_, minW, minH, maxW, maxH);
}

bool Window::pumpEvents() {
    glfwPollEvents();
    return window_ && !glfwWindowShouldClose(window_);
}

void Window::present() { glfwSwapBuffers(window_); }

void Window::setMouseCaptured(bool capture) {
    if (!window_ || capture == captured_) return;
    captured_ = capture;
    glfwSetInputMode(window_, GLFW_CURSOR, capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    deltaPrimed_ = false;
}

void Window::mouseDelta(double& dx, double& dy) {
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window_, &x, &y);
    if (!deltaPrimed_) {
        lastX_ = x;
        lastY_ = y;
        deltaPrimed_ = true;
    }
    dx = x - lastX_;
    dy = y - lastY_;
    lastX_ = x;
    lastY_ = y;
}

void Window::cursorPos(double& x, double& y) const { glfwGetCursorPos(window_, &x, &y); }

bool Window::mouseDown(int button) const {
    return button >= 0 && button < 8 && mouseDown_[button];
}

bool Window::mousePressed(int button) {
    if (button < 0 || button >= 8) return false;
    const bool v = mousePressed_[button];
    mousePressed_[button] = false;
    return v;
}

bool Window::mouseReleased(int button) {
    if (button < 0 || button >= 8) return false;
    const bool v = mouseReleased_[button];
    mouseReleased_[button] = false;
    return v;
}

double Window::consumeScroll() {
    const double v = scrollAcc_;
    scrollAcc_ = 0.0;
    return v;
}

int Window::consumeTextInput(char* out, int maxChars) {
    const int n = textLen_ < maxChars ? textLen_ : maxChars;
    for (int i = 0; i < n; ++i) out[i] = textBuf_[i];
    textLen_ = 0;
    return n;
}

bool Window::keyDown(int key) const {
    return key >= 0 && key <= GLFW_KEY_LAST && keys_[key];
}

bool Window::takeKeyPress(int key) {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    const bool v = keyEdges_[key];
    keyEdges_[key] = false;
    return v;
}

double Window::time() const { return glfwGetTime(); }

void Window::setTitle(const char* title) {
    if (window_) glfwSetWindowTitle(window_, title);
}

void Window::requestClose() const {
    if (window_) glfwSetWindowShouldClose(window_, GLFW_TRUE);
}

} // namespace s3d