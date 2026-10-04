#pragma once

#include <GL/gl.h>

#include <string>

#include "../platform/Window.hpp"

namespace s3d {
namespace gl {

// The context version the engine asks for, e.g. "3.3". This is what the window
// title and the editor's status label show.
//
// It is NOT what glGetString(GL_VERSION) reports. Several drivers - NVIDIA among
// them - print their highest supported version for any compatibility context, so a
// 3.3 context on such a driver still answers "4.6.0 NVIDIA ...". Showing that
// number as "the OpenGL version" is how a 2.1 request ends up looking like a 4.6
// one. The driver string still goes to the log, where it belongs.
inline const char* requestedVersion() {
    static const std::string cached = std::to_string(kGlVersionMajor) + "." +
                                      std::to_string(kGlVersionMinor);
    return cached.c_str();
}

// Short form of the driver-reported version: "4.6.0 NVIDIA 391.35" becomes
// "4.6". Diagnostics only - the launcher banner and the startup log.
inline const char* driverVersion() {
    static const std::string cached = [] {
        const char* raw = (const char*)glGetString(GL_VERSION);
        std::string text = (raw && *raw) ? raw : "?";
        const size_t stop = text.find_first_of(" \t");
        if (stop != std::string::npos) text.resize(stop);
        const size_t firstDot = text.find('.');
        if (firstDot != std::string::npos) {
            const size_t secondDot = text.find('.', firstDot + 1);
            if (secondDot != std::string::npos) text.resize(secondDot);
        }
        return text;
    }();
    return cached.c_str();
}

// Full driver string, unmodified, for the log.
inline const char* driverVersionString() {
    const char* raw = (const char*)glGetString(GL_VERSION);
    return (raw && *raw) ? raw : "(none)";
}

} // namespace gl
} // namespace s3d