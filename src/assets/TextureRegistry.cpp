#include "assets/TextureRegistry.hpp"

#include "core/Log.hpp"

#include <GL/gl.h>

#if defined(_WIN32)
// wglGetProcAddress, for the two entry points the 1.1 header does not declare.
#include <windows.h>
#endif

// stb_image is a single-header library: exactly one translation unit defines the
// implementation, and it has to be this one. Without a decoder every textured
// model drew in its flat material colour, and a glTF material with no
// baseColorFactor has a flat colour of white - so the model came out entirely
// white, which is exactly what a missing texture looks like.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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
// letting the driver pick the internal format per image is how two textures that
// look the same in an art tool end up different on screen.
unsigned upload(const unsigned char* pixels, int width, int height) {
    unsigned tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels);

    // Mipmaps: a minified texture with none aliases into shimmer, which is the
    // single most obvious "this render is cheap" tell. Generated on the CPU side
    // by the driver.
    //
    // Only for a power-of-two image, where the mip chain is complete. A texture
    // that asks for mipmaps it cannot have is an incomplete texture in GL, and an
    // incomplete texture samples as solid black - a worse outcome than the shimmer
    // the mipmaps were there to remove. The engine runs a 3.3 compatibility
    // context, where a non-power-of-two texture is legal on its own and simply
    // does without a chain.
    const bool powerOfTwo = width > 0 && height > 0 && (width & (width - 1)) == 0 &&
                            (height & (height - 1)) == 0;
    if (powerOfTwo) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        generateMipmap();
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // CLAMP rather than REPEAT: a default texture is clamped on every edge, and a
    // model whose UVs leave 0..1 then shows the border texel instead of a seam of
    // wrapped image. Repeat is opt-in per texture later, not the global default.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// Decodes image bytes to an RGBA8 texture. Returns 0 when the bytes are not an
// image this build can read, which the caller records as an untextured entry
// rather than an error: the model still draws, in its flat material colour.
//
// Always four channels, so upload() has one layout to deal with and a greyscale
// PNG does not arrive as three bytes per texel that GL would read as RGBA.
unsigned decodeToTexture(const unsigned char* bytes, size_t size, const char* label,
                          bool quiet) {
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels =
        stbi_load_from_memory(bytes, (int)size, &width, &height, &channels, 4);
    if (!pixels) {
        if (!quiet)
            Log::error("Texture: cannot decode '%s': %s", label, stbi_failure_reason());
        return 0;
    }
    const unsigned tex = upload(pixels, width, height);
    stbi_image_free(pixels);
    if (!tex && !quiet) Log::error("Texture: GL refused '%s'", label);
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

    unsigned tex = 0;
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (pixels) {
        tex = upload(pixels, width, height);
        stbi_image_free(pixels);
        if (!tex && !quiet) Log::error("Texture: GL refused '%s'", path.c_str());
    } else if (!quiet) {
        Log::error("Texture: cannot decode '%s': %s", path.c_str(), stbi_failure_reason());
    }

    // Recorded even when the load failed, rather than left out: a model that asks
    // for this texture again gets the same answer without the file being looked at
    // twice, and 0 is GL's "no texture", so the part draws in its flat colour
    // instead of disappearing.
    textures_.push_back(tex);
    names_.push_back(path);
    const int index = (int)textures_.size() - 1;
    byPath_.emplace(path, index);
    return index;
}

int TextureRegistry::loadMemory(const std::vector<unsigned char>& bytes, bool quiet) {
    // No path to key on, so no cache to consult: the loader resolves one glTF
    // image to one call, so two calls mean two different images.
    const unsigned tex = decodeToTexture(bytes.data(), bytes.size(), "embedded image", quiet);

    textures_.push_back(tex);
    names_.push_back("<embedded>");
    return (int)textures_.size() - 1;
}

} // namespace s3d