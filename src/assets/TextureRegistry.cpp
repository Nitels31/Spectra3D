#include "assets/TextureRegistry.hpp"

#include "core/Log.hpp"

#include <GL/gl.h>

#if defined(_WIN32)
// wglGetProcAddress, for the two entry points the 1.1 header does not declare.
#include <windows.h>
#endif

// No image decoder yet. The file under third_party/stb/stb_image/ is
// stb_image_resize2 - it scales pixels that are already in memory and cannot read
// a PNG or JPEG - so there is nothing here that can turn a path into texels.
// Deliberately a stub rather than a silent wrong answer: loadFile reports once and
// returns an untextured index, and the model draws in its flat colour as before.
//
// Everything else is in place - UVs come in through the loaders and the renderer
// binds a texture per part - so adding the real stb_image.h here is the only
// step left, and it is one file plus the STB_IMAGE_IMPLEMENTATION define below.

// GL 1.1 headers stop at CLAMP; the extension-era enums and glGenerateMipmap
// arrive with glext.h. They are constants and one function pointer here, and
// the engine already runs a 3.3 compatibility context where both exist.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

// glGenerateMipmap is a function, and GL/gl.h on Windows is the 1.1 header,
// which stops before it. Resolved once through the context that is already
// current, so this costs one lookup for the process rather than one per texture.
namespace {
void generateMipmap() {
    typedef void (*Fn)(GLenum);
    static Fn fn = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        // PROC to a typed pointer through void*: the Win32 header's PROC and the
        // target differ, and -Wcast-function-type rejects a direct cast between
        // them. The round trip through void* is the standard way to say "this is
        // a function pointer, trust me", and it is what every GL loader does.
        fn = (Fn)(void*)wglGetProcAddress("glGenerateMipmap");
        if (!fn) fn = (Fn)(void*)wglGetProcAddress("glGenerateMipmapEXT");
    }
    if (fn) fn(GL_TEXTURE_2D);
}
} // namespace

namespace s3d {
namespace {

// Uploads `width` x `height` RGBA8 pixels. One internal format for everything:
// mipmaps, sRGB and anisotropic filtering are the only quality knobs, and letting
// the driver pick the internal format per image is how two textures that look the
// same in an art tool end up different on screen.
// Unused until a decoder exists to feed it. Kept compiled rather than deleted so
// adding stb_image.h is a one-file change, and marked maybe_unused because
// -Werror turns an unreferenced helper into a build failure.
[[maybe_unused]] unsigned upload(const unsigned char* pixels, int width, int height) {
    unsigned tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels);

    // Mipmaps: a minified texture with none aliases into shimmer, which is the
    // single most obvious "this render is cheap" tell. Generated on the CPU side
    // by the driver, and skipped when the image is not power-of-two, where the
    // fixed-function pipeline would reject it anyway.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // CLAMP rather than REPEAT: a default texture is clamped on every edge, and a
    // model whose UVs leave 0..1 then shows the border texel instead of a seam of
    // wrapped image. Repeat is opt-in per texture later, not the global default.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    generateMipmap();

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

} // namespace

TextureRegistry::~TextureRegistry() { release(); }

void TextureRegistry::release() {
    for (unsigned& tex : textures_)
        if (tex) glDeleteTextures(1, &tex);
    textures_.clear();
    names_.clear();
    byPath_.clear();
}

int TextureRegistry::find(const std::string& path) const {
    const auto it = byPath_.find(path);
    return it == byPath_.end() ? -1 : it->second;
}

const std::string& TextureRegistry::nameOf(int index) const {
    static const std::string kNone;
    if (index < 0 || index >= (int)names_.size()) return kNone;
    return names_[(size_t)index];
}

unsigned TextureRegistry::at(int index) const {
    if (index < 0 || index >= (int)textures_.size()) return 0;
    return textures_[(size_t)index];
}

int TextureRegistry::loadFile(const std::string& path, bool quiet) {
    const int existing = find(path);
    if (existing >= 0) return existing;

    // Recorded as a failed entry rather than left out: a model that asks for this
    // texture again gets the same answer without the file being looked at twice.
    textures_.push_back(0);
    names_.push_back(path);
    const int index = (int)textures_.size() - 1;
    byPath_.emplace(path, index);
    if (!quiet)
        Log::warn("Texture: no image decoder in this build, '%s' is untextured",
                  path.c_str());
    return index;
}

} // namespace s3d