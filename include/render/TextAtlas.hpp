#pragma once
#include <array>
#include <vector>

#include <GL/gl.h>

#include "core/TextCoverage.hpp"

namespace s3d {

// FreeType-backed glyph atlas for the editor UI. The letters it holds are the
// ones the table in core/TextCoverage.hpp names: printable ASCII, the Cyrillic
// the localisations need, Latin-1 for German, French, Italian and Spanish, and
// the Bengali block for the Bengali pack - which keeps the atlas at a fixed
// 1024x1024 single page.
//
// A letter that no font has is not drawn. It is not drawn as a box either: an
// empty box in the middle of a word is worse than no letter at all, and a
// question mark in the middle of a word is worse still. The atlas says so, and
// the test that reads the language files says it out loud.
class TextAtlas {
public:
    static constexpr int kGlyphCount = text::kGlyphCount;
    static constexpr int kPixelSize = 18;
    // The height of one line of text, in pixels. The interface lays its text out
    // in lines of this height, and the font is baked bigger than that - the
    // letters are the size they should be, and the line is tight.
    //
    // The baseline is placed by the font's own metrics, not by this number: see
    // `baseline()`.
    static constexpr int kLineHeight = 15;

    struct Glyph {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        float width = 0, height = 0;
        float offsetX = 0, offsetY = 0;
        float advance = 0;
        // False when no font of the chain has this letter. The letter is then
        // not drawn and takes no room: a space has no ink but has an advance,
        // and a letter that does not exist has neither.
        bool hasGlyph = false;
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
    // The distance from the top of a line to the baseline, in pixels. It is the
    // font's own metrics and not the ascent: the ascent is how far the font
    // reaches above the baseline, and a font whose ascent is taller than its
    // own em - JetBrains Mono's is - would push the whole line down and out of
    // the box it is drawn in.
    float baseline() const { return baseline_; }

    const Glyph& glyph(int index) const { return glyphs_[index]; }

    // Maps a code point to a slot in the atlas, or -1 when unsupported.
    // The slot is where the letter would live; whether a font actually has it
    // is asked of the glyph itself (`hasGlyph`).
    static int glyphIndex(unsigned codePoint);
    // The code point in a slot: the other direction of the same table.
    static unsigned codepointAt(int index);

    // The baseline in a line of `lineHeight` pixels, from the font's own
    // metrics: the cap height above it and the descender below it.
    //
    // The ascent is not the answer - a font whose ascent is taller than its
    // own em (JetBrains Mono's is) would push the whole line down and out of
    // the box it is drawn in. The visible box of the text is what has to sit in
    // the middle of the line, and that is the cap height and the descender.
    static float baselineFor(float lineHeight, float capHeight, float descender);

    float textWidth(const char* utf8) const;
    // Appends the textured quads for `utf8` into `batch` (x, y, u, v, r, g, b).
    // `scale` shrinks or grows the glyphs and their advances around the anchor at
    // (x, baselineY); the atlas itself is untouched, so one texture serves every
    // size the UI asks for.
    void appendQuads(std::vector<float>& batch, float x, float baselineY,
                     const char* utf8, float r, float g, float b, float scale = 1.0f) const;

private:
    std::array<Glyph, kGlyphCount> glyphs_{};
    unsigned texture_ = 0;
    bool ready_ = false;
    float baseline_ = (float)kLineHeight * 0.5f;
};

} // namespace s3d