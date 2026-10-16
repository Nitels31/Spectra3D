#include "platform/Window.hpp"

#include "core/Log.hpp"
#include "core/Project.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
// windows.h trimmed here rather than from the command line: rapidobj.hpp defines
// both macros unconditionally, and a -DNOMINMAX from the build is a different
// definition of the same name - the build then warns about a header that is not
// ours. Filesystem.cpp does the same around its own windows.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Ask for the native handle: glfw3native.h only declares glfwGetWin32Window - and
// only pulls in windows.h - when this is defined before the include.
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace s3d {
namespace {

void glfwErrorCallback(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc ? desc : "(null)");
}

#if defined(_WIN32)

// --- the application mark ---------------------------------------------------
//
// Two sources, one result: a file out of assets/image/icons, and - when there is no
// readable file - a small prism drawn from code. The drawn one is the fallback the
// editor sketch calls for: a 32x32 mark in a file is one more thing a build can
// lose, and this costs eight lines of arithmetic once per launch.
//
// Both go through iconFromPixels, which is forward declared because the two are
// mutually recursive in reading order: the mark is written first and the helper it
// hands its pixels to is defined with the other icon loaders.
HICON iconFromPixels(const unsigned char* pixels, int size);

struct Rgb {
    float r, g, b;
};

// Signed area of the triangle (a, b, c) at p, doubled. Its sign tells which side
// of the edge p is on, which is all a point-in-triangle test needs.
float edge(const float ax, const float ay, const float bx, const float by, const float px,
           const float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

// Coverage of one pixel by the mark, 2x2 supersampled: a 16 pixel icon drawn
// with a hard test looks like a staircase, and the title bar is the one place
// the mark is always on screen.
float markCoverage(float u, float v) {
    // Outer triangle: apex up top, base across the bottom.
    const float a1 = edge(0.50f, 0.10f, 0.90f, 0.86f, u, v);
    const float a2 = edge(0.90f, 0.86f, 0.10f, 0.86f, u, v);
    const float a3 = edge(0.10f, 0.86f, 0.50f, 0.10f, u, v);
    const float outer = (a1 >= 0.0f && a2 >= 0.0f && a3 >= 0.0f) ? 1.0f : 0.0f;

    // Inner triangle: the lit core, a third of the way in on every side.
    const float b1 = edge(0.50f, 0.36f, 0.71f, 0.70f, u, v);
    const float b2 = edge(0.71f, 0.70f, 0.29f, 0.70f, u, v);
    const float b3 = edge(0.29f, 0.70f, 0.50f, 0.36f, u, v);
    const float core = (b1 >= 0.0f && b2 >= 0.0f && b3 >= 0.0f) ? 1.0f : 0.0f;
    return outer + core;  // 1 for the shell, 2 for the core
}

HICON makeMarkIcon(int size) {
    std::vector<unsigned char> pixels((size_t)size * size * 4, 0);

    const Rgb shell{0.30f, 0.55f, 0.97f};
    const Rgb core{0.91f, 0.95f, 1.0f};

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float coverage = 0.0f;
            for (int sy = 0; sy < 2; ++sy)
                for (int sx = 0; sx < 2; ++sx) {
                    const float u = ((float)x + 0.25f + 0.5f * (float)sx) / (float)size;
                    const float v = ((float)y + 0.25f + 0.5f * (float)sy) / (float)size;
                    coverage += markCoverage(u, v) * 0.25f;
                }

            // coverage 0..2: 1 is the edge of the shell, 2 the middle of the core.
            const float inCore = coverage > 1.5f ? 1.0f : 0.0f;
            const float alpha = coverage < 1.5f ? coverage - inCore : 1.0f;
            if (alpha <= 0.0f) continue;

            const float mix = inCore > 0.5f ? 1.0f : (coverage - 1.0f) * 0.5f + 0.5f;
            unsigned char* out = pixels.data() + ((size_t)y * size + x) * 4;
            // 32bpp BI_RGB icons are read as BGRA.
            out[0] = (unsigned char)(255.0f * (shell.b + (core.b - shell.b) * mix));
            out[1] = (unsigned char)(255.0f * (shell.g + (core.g - shell.g) * mix));
            out[2] = (unsigned char)(255.0f * (shell.r + (core.r - shell.r) * mix));
            out[3] = (unsigned char)(255.0f * (alpha > 1.0f ? 1.0f : alpha));
        }
    }
    return iconFromPixels(pixels.data(), size);
}

// A 32bpp icon out of a straight BGRA buffer, `size` square. The drawn mark above
// is the only source that needs this: a file goes straight to LoadImageW, which
// hands back every size a .ico carries already scaled. CreateIconIndirect copies
// what it needs, so the source bitmap is ours to drop and only the HICON outlives
// this call.
HICON iconFromPixels(const unsigned char* pixels, int size) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    // Negative height: the rows are stored top-down, which is the order both
    // sources above write them in.
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!bitmap || !bits) return nullptr;
    std::memcpy(bits, pixels, (size_t)size * size * 4);

    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = nullptr;  // a 32bpp icon carries its own mask in the alpha
    iconInfo.hbmColor = bitmap;
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(bitmap);
    return icon;
}

// The icon the build ships: one of the files in assets/image/icons, next to the
// binary, for the window, the task bar and the exe.
//
// Built wide, from the running module's own path. LoadImageW is the one icon
// loader whose prototype is the same everywhere, and the wide path is what keeps a
// project folder with a non-ASCII name from being the reason the window has no
// icon.
//
// Returns null when the file is missing, unreadable or not an icon. That is not an
// error: makeMarkIcon() below is the fallback, which is why the mark is still drawn
// here instead of the file being the only path.
//
// `iconName` is a bare file name, never a path: everything below this function is
// appending it to a directory the engine owns, and a name with a ".." in it would
// turn that into a read of somewhere else entirely.
HICON loadShippedIcon(int width, int height, const char* iconName) {
    std::string name = (iconName && iconName[0]) ? iconName : kAppIconFile;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
        name.find("..") != std::string::npos)
        return nullptr;

    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return nullptr;

    // Cut the file name off the module path, then append the icons folder and the
    // file. Both literals are ASCII, which is what makes the widening below a cast
    // instead of a conversion.
    size_t at = 0;
    for (size_t i = length; i > 0; --i) {
        const wchar_t c = path[i - 1];
        if (c != L'\\' && c != L'/') continue;
        at = i;
        break;
    }
    if (at == 0) return nullptr;  // no directory to put the icon in

    for (const char* c = kAppIconsDir; *c; ++c) {
        if (at >= MAX_PATH - 2) return nullptr;
        path[at++] = (*c == '/') ? L'\\' : (wchar_t)(unsigned char)*c;
    }
    path[at++] = L'\\';
    for (const char* c = name.c_str(); *c; ++c) {
        if (at >= MAX_PATH - 1) return nullptr;
        path[at++] = (wchar_t)(unsigned char)*c;
    }
    path[at] = L'\0';

    // width and height are the size being asked for, and the loader hands back the
    // entry closest to it - a shipped .ico carries 16, 32, 64, 128 and 256, and the
    // shell scales rather than our code doing it. Null means the file is missing,
    // unreadable, or not an icon, which is not an error: makeMarkIcon() above is
    // the fallback and is what a missing file leaves behind.
    return static_cast<HICON>(
        LoadImageW(nullptr, path, IMAGE_ICON, width, height, LR_LOADFROMFILE));
}
#endif  // _WIN32

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
    // 3.3 compatibility, not 3.3 core. The renderer is written against the fixed
    // pipeline (glOrtho, glEnableClientState, glTexCoord2f), which a core profile
    // removes entirely; the compatibility profile keeps all of it and still pins
    // the context to a modern version instead of the 2.1 the driver used to hand
    // out, which made a lot of hardware spin up far more of the GPU than the
    // engine ever asked for.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kGlVersionMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, kGlVersionMinor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) {
        // A driver without a 3.3 compatibility profile still runs the 2.1 fixed
        // pipeline the renderer is written against, so falling back is correct
        // rather than fatal.
        Log::warn("GL: no 3.3 compatibility context, falling back to 2.1");
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    }
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

void Window::applyIcon(const char* iconName) {
#if defined(_WIN32)
    if (!window_) return;
    HWND handle = glfwGetWin32Window(window_);
    if (!handle) return;

    // Both sizes: the title bar asks for 16, the task bar button and Alt-Tab for
    // 32, and letting the shell stretch one bitmap into the other is what makes a
    // mark look blurry at one size and blocky at the other.
    const int smallSize =
        GetSystemMetrics(SM_CXSMICON) > 0 ? GetSystemMetrics(SM_CXSMICON) : 16;
    const int bigSize = GetSystemMetrics(SM_CXICON) > 0 ? GetSystemMetrics(SM_CXICON) : 32;

    // The shipped file first, the drawn mark as the fallback, per size: a file
    // that is there but unreadable leaves the window with the mark instead of no
    // icon at all, which is the whole reason both paths exist.
    HICON small = loadShippedIcon(smallSize, smallSize, iconName);
    HICON big = loadShippedIcon(bigSize, bigSize, iconName);
    if (!small) small = makeMarkIcon(smallSize);
    if (!big) big = makeMarkIcon(bigSize);

    if (small) {
        SendMessageW(handle, WM_SETICON, ICON_SMALL, (LPARAM)small);
        // Deliberately never destroyed: the shell keeps drawing these for the life
        // of the window, and freeing one leaves a hole in the title bar rather than
        // a memory leak anyone would notice - two icons for one process.
    }
    if (big) SendMessageW(handle, WM_SETICON, ICON_BIG, (LPARAM)big);
#else
    (void)0;
#endif
}

bool Window::pumpEvents() {
    glfwPollEvents();
    return window_ && !glfwWindowShouldClose(window_);
}

void Window::present() { glfwSwapBuffers(window_); }

bool Window::peekActivity() const {
    // A frame is worth drawing if something happened, if a control is being held
    // (camera flight, a gizmo drag) or if the pointer moved, because hover
    // highlights are part of the picture.
    bool busy = scrollAcc_ != 0.0 || textLen_ > 0 || captured_;
    for (int key = 0; key <= GLFW_KEY_LAST && !busy; ++key)
        busy = keys_[key] || keyEdges_[key];
    for (int button = 0; button < 8 && !busy; ++button)
        busy = mouseDown_[button] || mousePressed_[button] || mouseReleased_[button];

    double x = 0.0, y = 0.0;
    cursorPos(x, y);
    if (activityX_ >= 0.0 && (x != activityX_ || y != activityY_)) busy = true;

    if (fbW_ != framebufferWidth() || fbH_ != framebufferHeight()) busy = true;
    return busy;
}

bool Window::consumeActivity() {
    const bool busy = peekActivity();
    double x = 0.0, y = 0.0;
    cursorPos(x, y);
    activityX_ = x;
    activityY_ = y;
    return busy;
}

void Window::dropUnreadInput() {
    for (int key = 0; key <= GLFW_KEY_LAST; ++key) keyEdges_[key] = false;
    for (int button = 0; button < 8; ++button) {
        mousePressed_[button] = false;
        mouseReleased_[button] = false;
    }
    // Typed characters nobody asked for: a field that was not focused when they
    // arrived. Left in the buffer they keep `peekActivity()` true just as a stray
    // key press does.
    textLen_ = 0;
    scrollAcc_ = 0.0;
}

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
