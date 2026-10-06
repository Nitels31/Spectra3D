#pragma once
#include <array>
#include <vector>

#include <GL/gl.h>

namespace s3d {

// FreeType-backed glyph atlas for the editor UI. Covers printable ASCII, the
// Cyrillic range the localisations need, and Latin-1 for German, French, Italian
// and Spanish - umlauts, accents, cedilla, guillemets, oe ligature, inverted
// marks - which keeps the atlas at a fixed 1024x1024 single page.
class TextAtlas {
public:
    static constexpr int kGlyphCount = 240;
    static constexpr int kPixelSize = 18;

    struct Glyph {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        float width = 0, height = 0;
        float offsetX = 0, offsetY = 0;
        float advance = 0;
    };

    TextAtlas() = default;
    ~TextAtlas();

    TextAtlas(const TextAtlas&) = delete;
    TextAtlas& operator=(const TextAtlas&) = delete;

    // Bakes the atlas and uploads it as an alpha texture. Returns false when no
    // usable system font was found; `textWidth` then falls back to an estimate.
    bool build();
    void release();

    bool ready() const { return ready_; }
    unsigned texture() const { return texture_; }
    float ascender() const { return ascender_; }

    const Glyph& glyph(int index) const { return glyphs_[index]; }

    // Maps a code point to a slot in the atlas, or -1 when unsupported.
    static int glyphIndex(unsigned codePoint);

    float textWidth(const char* utf8) const;
    // Appends the textured quads for `utf8` into `batch` (x, y, u, v, r, g, b).
    // `scale` shrinks or grows the glyphs and their advances around the anchor at
    // (x, baselineY); the atlas itself is untouched, so one texture serves every
    // size the UI asks for.
    void appendQuads(std::vector<float>& batch, float x, float baselineY,
                     const char* utf8, float r, float g, float b, float scale = 1.0f) const;

private:
    unsigned codepointAt(int index) const;

    std::array<Glyph, kGlyphCount> glyphs_{};
    unsigned texture_ = 0;
    bool ready_ = false;
    float ascender_ = 15.0f;
};

} // namespace s3d