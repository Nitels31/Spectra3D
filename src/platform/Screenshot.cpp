#include "platform/Screenshot.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"

#include <GL/gl.h>

// The PNG writer. One header, one implementation, and only this file ever
// sees it - the same rule the sound device follows with miniaudio.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdio>
#include <vector>

namespace s3d {
namespace screenshot {
namespace {

// The picture is read out of the framebuffer and turned over: OpenGL counts
// from the bottom of the picture and a file counts from the top.
std::vector<unsigned char> readFramebuffer(int width, int height) {
    std::vector<unsigned char> pixels((size_t)width * (size_t)height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    std::vector<unsigned char> flipped(pixels.size());
    const size_t row = (size_t)width * 3;
    for (int y = 0; y < height; ++y) {
        const unsigned char* from = pixels.data() + (size_t)(height - 1 - y) * row;
        std::copy(from, from + row, flipped.begin() + (size_t)y * row);
    }
    return flipped;
}

} // namespace

bool save(const std::string& path, int width, int height) {
    if (width < 1 || height < 1) {
        Log::error("Screenshot: the picture is empty");
        return false;
    }

    const std::vector<unsigned char> pixels = readFramebuffer(width, height);
    if (!savePixels(path, pixels, width, height)) return false;
    Log::info("Screenshot: '%s' written", path.c_str());
    return true;
}

bool savePixels(const std::string& path, const std::vector<unsigned char>& pixels, int width,
                int height) {
    if (width < 1 || height < 1 || pixels.size() < (size_t)width * (size_t)height * 3) {
        Log::error("Screenshot: the picture is empty");
        return false;
    }
    // The folder of the picture is made if it is not there: a path that was
    // asked for by name is a path somebody means to be used, and a picture
    // that could not be written because a folder was missing is a picture
    // that is not there.
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) fs::ensureDirectory(path.substr(0, slash));

    // The writer can only be given a narrow path, so the path is handed over in
    // the machine's own code page.
    if (!stbi_write_png(fs::systemPath(path).c_str(), width, height, 3, pixels.data(),
                        width * 3)) {
        Log::error("Screenshot: cannot write '%s'", path.c_str());
        return false;
    }
    return true;
}

std::string nextPath(const std::string& folder) {
    // The pictures that are already there, so that the new one does not take
    // the place of an old one.
    int highest = 0;
    std::vector<std::string> files;
    if (fs::listDirectory(folder, files)) {
        for (const std::string& name : files) {
            const std::string prefix = "screenshot-";
            if (name.size() <= prefix.size() ||
                name.compare(0, prefix.size(), prefix) != 0)
                continue;
            const size_t dot = name.find('.');
            if (dot == std::string::npos) continue;
            const std::string number = name.substr(prefix.size(), dot - prefix.size());
            if (number.empty()) continue;
            int value = 0;
            if (std::sscanf(number.c_str(), "%d", &value) == 1 && value > highest)
                highest = value;
        }
    }
    char name[64];
    std::snprintf(name, sizeof(name), "screenshot-%03d.png", highest + 1);
    return str::join(folder, name);
}

std::string pictureFolder() {
    // The folder of the program, and the folder above it.
    //
    // The program sits in a build folder, and a build folder full of pictures
    // is a build folder nobody can read. The pictures go one level up, in a
    // folder of their own, so that they are easy to find and easy to throw
    // away, and so that nothing lands in the build itself.
    const std::string exeDir = fs::executableDir();
    const size_t slash = exeDir.find_last_of("/\\");
    const std::string above = slash == std::string::npos ? exeDir : exeDir.substr(0, slash);
    const std::string folder = str::join(above, "test_screenshot");
    fs::ensureDirectory(folder);
    return folder;
}

} // namespace screenshot
} // namespace s3d
