#include "render/GlyphCache.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "render/FontFile.hpp"
#include "render/TextAtlas.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>

namespace s3d {
namespace {

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_ALPHA
#define GL_ALPHA 0x1906
#endif

// The picture the letters of one size are drawn into. A thousand and twenty
// four is enough for the letters of a game at the sizes a game is played at.
constexpr int kPictureSide = 1024;
// The letters of a game at a size bigger than this get a bigger picture: a
// letter of two hundred pixels does not fit in a row of a thousand.
constexpr int kLargePictureSide = 2048;
constexpr int kLargePictureFrom = 96;
// How many pictures are kept around. The letters of a game are a handful of
// sizes, and the sizes of a window being resized are a lot of them.
constexpr size_t kMaxPictures = 6;
// How far off the size of a picture may be and still be good enough: the
// letters are then drawn a fraction of a pixel off, and a picture is not made
// again for every pixel of a window being resized.
constexpr float kSizeTolerance = 0.04f;

} // namespace

struct GlyphCache::Impl {
    FT_Library library = nullptr;
    FT_Face face = nullptr;
};

GlyphCache::GlyphCache() = default;

GlyphCache::~GlyphCache() { close(); }

bool GlyphCache::open() {
    close();

    // The bundled font first, then the system ones. Every candidate is tried
    // before the next one is: a file that cannot be read is not a font.
    std::string used;
    for (const std::string& path : fontFileCandidates()) {
        FT_Library library = nullptr;
        if (FT_Init_FreeType(&library)) continue;
        FT_Face face = nullptr;
        // The font library can only be given a narrow path, so the path is
        // handed over in the machine's own code page.
        if (FT_New_Face(library, fs::systemPath(path).c_str(), 0, &face) == 0) {
            impl_ = std::make_unique<Impl>();
            impl_->library = library;
            impl_->face = face;
            used = path;
            break;
        }
        FT_Done_FreeType(library);
    }
    if (!impl_) {
        Log::warn("Text: no usable font (none in the assets and none on this system), "
                  "the letters of a game cannot be drawn");
        return false;
    }

    Log::info("Text: the letters of a game come from '%s'", used.c_str());
    ready_ = true;
    return true;
}

void GlyphCache::close() {
    for (auto& entry : pictures_)
        if (entry.second.texture) glDeleteTextures(1, &entry.second.texture);
    pictures_.clear();
    if (impl_) {
        if (impl_->face) FT_Done_Face(impl_->face);
        if (impl_->library) FT_Done_FreeType(impl_->library);
        impl_.reset();
    }
    ready_ = false;
}

int GlyphCache::pixelsFor(float size) {
    if (size < 1.0f) return 1;
    return (int)std::lround(size);
}

GlyphCache::Picture& GlyphCache::pictureFor(float size) {
    const int pixels = pixelsFor(size);

    // The size that is exactly there.
    auto exact = pictures_.find(pixels);
    if (exact != pictures_.end()) {
        exact->second.used = ++clock_;
        return exact->second;
    }

    // A picture whose letters are a hair off is good enough: the letters are
    // drawn a fraction of a pixel off, and a picture is not made again for
    // every pixel of a window being resized.
    Picture* found = nullptr;
    float best = kSizeTolerance;
    for (auto& entry : pictures_) {
        const float difference = std::fabs((float)entry.first - size) / std::fmax(1.0f, size);
        if (difference <= best) {
            best = difference;
            found = &entry.second;
        }
    }
    if (found) {
        found->used = ++clock_;
        return *found;
    }

    // The picture is made. The letters go into it one at a time, as they are
    // asked for: a canvas has a handful of letters in it and not an alphabet.
    Picture picture;
    picture.pixels = pixels;
    picture.side = pixels > kLargePictureFrom ? kLargePictureSide : kPictureSide;
    picture.used = ++clock_;

    glGenTextures(1, &picture.texture);
    glBindTexture(GL_TEXTURE_2D, picture.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, picture.side, picture.side, 0, GL_ALPHA,
                 GL_UNSIGNED_BYTE, nullptr);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // The box the letters of a line stand in: the capitals above the baseline
    // and the descenders below it. The ascent is not the answer - the ascent
    // of JetBrains Mono is taller than its own em, and a line placed by the
    // ascent hangs below the box it is drawn in - so the capitals are measured
    // from the letter H itself.
    float capHeight = 0.0f;
    if (impl_ && impl_->face) {
        FT_Set_Pixel_Sizes(impl_->face, 0, pixels);
        const Glyph* cap = glyphFor(picture, 'H');
        if (cap) capHeight = cap->offsetY;
    }
    float descender = 0.0f;
    if (impl_ && impl_->face && impl_->face->size)
        descender = (float)(-impl_->face->size->metrics.descender) / 64.0f;

    // The line is as tall as the editor's line is at its own size: the same
    // letters, the same proportions, a different size.
    picture.lineHeight = (float)TextAtlas::kLineHeight * (float)pixels / (float)TextAtlas::kPixelSize;
    picture.baseline = TextAtlas::baselineFor(picture.lineHeight, capHeight, descender);

    // Too many pictures: the oldest one is let go of. It is not the one in
    // use - the picture in use was asked for last, and the clock says so.
    if (pictures_.size() >= kMaxPictures) {
        auto oldest = pictures_.begin();
        for (auto it = pictures_.begin(); it != pictures_.end(); ++it)
            if (it->second.used < oldest->second.used) oldest = it;
        if (oldest->second.texture) glDeleteTextures(1, &oldest->second.texture);
        pictures_.erase(oldest);
    }

    auto inserted = pictures_.emplace(pixels, std::move(picture));
    return inserted.first->second;
}

const GlyphCache::Glyph* GlyphCache::glyphFor(Picture& picture, unsigned codepoint) {
    auto found = picture.glyphs.find(codepoint);
    if (found != picture.glyphs.end()) return &found->second;
    if (!impl_ || !impl_->face) return nullptr;

    FT_Set_Pixel_Sizes(impl_->face, 0, picture.pixels);
    if (FT_Load_Char(impl_->face, (FT_ULong)codepoint, FT_LOAD_RENDER)) return nullptr;

    FT_GlyphSlot slot = impl_->face->glyph;
    Glyph glyph;
    glyph.advance = (float)slot->advance.x / 64.0f;
    glyph.offsetX = (float)slot->bitmap_left;
    glyph.offsetY = (float)slot->bitmap_top;
    glyph.width = (float)slot->bitmap.width;
    glyph.height = (float)slot->bitmap.rows;

    const int width = (int)glyph.width;
    const int height = (int)glyph.height;
    if (width > 0 && height > 0) {
        // Where the letter goes in the picture.
        if (picture.penX + width + 1 >= picture.side) {
            picture.penX = 1;
            picture.penY += picture.rowHeight + 1;
            picture.rowHeight = 0;
        }
        if (picture.penY + height + 1 >= picture.side) {
            // The picture is full. The letter is left out: a letter that is
            // there and wrong is worse than a letter that is not there.
            Log::warn("Text: the picture of the %d-pixel letters is full", picture.pixels);
            return nullptr;
        }

        // The letter goes into the picture, a row at a time: the rows of a
        // letter are not a multiple of four, and the picture is a row of bytes.
        glBindTexture(GL_TEXTURE_2D, picture.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (int row = 0; row < height; ++row)
            glTexSubImage2D(GL_TEXTURE_2D, 0, picture.penX, picture.penY + row, width, 1,
                            GL_ALPHA, GL_UNSIGNED_BYTE,
                            slot->bitmap.buffer + (size_t)row * slot->bitmap.width);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);

        glyph.u0 = (float)picture.penX / (float)picture.side;
        glyph.v0 = (float)picture.penY / (float)picture.side;
        glyph.u1 = (float)(picture.penX + width) / (float)picture.side;
        glyph.v1 = (float)(picture.penY + height) / (float)picture.side;
        picture.penX += width + 1;
        picture.rowHeight = std::max(picture.rowHeight, height);
    }

    auto inserted = picture.glyphs.emplace(codepoint, glyph);
    return &inserted.first->second;
}

unsigned GlyphCache::textureFor(float size) {
    if (!ready_) return 0;
    return pictureFor(size).texture;
}

float GlyphCache::measure(const char* utf8, float size) {
    if (!utf8 || !*utf8 || !ready_) return 0.0f;
    Picture& picture = pictureFor(size);
    // The width of the letters as they are drawn: the picture holds them at
    // their own size, and they are drawn at that size - so the width is the
    // sum of the advances and not a fraction of it.
    float width = 0.0f;
    for (const char* p = utf8; *p;) {
        unsigned codepoint = str::nextCodepoint(p);
        // The letters the editor's atlas has are the letters a game has: a
        // name that can be typed in the editor can be written on a label.
        if (TextAtlas::glyphIndex(codepoint) < 0) codepoint = (unsigned)'?';
        const Glyph* glyph = glyphFor(picture, codepoint);
        if (glyph) width += glyph->advance;
    }
    return width;
}

float GlyphCache::baselineFor(float size) {
    if (!ready_) return 0.0f;
    return pictureFor(size).baseline;
}

float GlyphCache::lineHeightFor(float size) {
    if (!ready_) return 0.0f;
    return pictureFor(size).lineHeight;
}

void GlyphCache::appendQuads(std::vector<float>& batch, float x, float topY, const char* utf8,
                             float r, float g, float b, float size) {
    if (!utf8 || !*utf8 || !ready_) return;
    Picture& picture = pictureFor(size);

    // THE LETTERS ARE DRAWN AT THEIR OWN SIZE AND ON WHOLE PIXELS.
    //
    // The picture holds the letters at the size they were baked at, and they
    // are drawn at that size: a letter taken out of a picture of one size and
    // drawn at another is a soft letter, and the whole point of baking a
    // picture per size is that the letters are not soft.
    //
    // And every letter stands on a whole pixel. The texture filter blends the
    // texel it is given with the one next to it, so a letter that is a
    // fraction of a pixel off its own grid is a letter that is half a texel
    // soft everywhere - which is exactly what a blurry text looks like.
    //
    // The pen moves by the advance of the letter, which is not a whole pixel,
    // so the position of every letter is rounded on its own.
    const float baselineY = std::round(topY + picture.baseline);
    float penX = x;
    for (const char* p = utf8; *p;) {
        unsigned codepoint = str::nextCodepoint(p);
        if (TextAtlas::glyphIndex(codepoint) < 0) codepoint = (unsigned)'?';
        const Glyph* glyph = glyphFor(picture, codepoint);
        if (!glyph) continue;

        const float x0 = std::round(penX) + glyph->offsetX;
        const float y0 = baselineY - glyph->offsetY;
        const float x1 = x0 + glyph->width;
        const float y1 = y0 + glyph->height;

        const size_t base = batch.size();
        batch.resize(base + 42);
        float* out = batch.data() + base;
        const auto put = [&out, r, g, b](float vx, float vy, float vu, float vv) {
            *out++ = vx;
            *out++ = vy;
            *out++ = vu;
            *out++ = vv;
            *out++ = r;
            *out++ = g;
            *out++ = b;
        };
        put(x0, y0, glyph->u0, glyph->v0);
        put(x1, y0, glyph->u1, glyph->v0);
        put(x1, y1, glyph->u1, glyph->v1);
        put(x0, y0, glyph->u0, glyph->v0);
        put(x1, y1, glyph->u1, glyph->v1);
        put(x0, y1, glyph->u0, glyph->v1);
        penX += glyph->advance;
    }
}

} // namespace s3d
