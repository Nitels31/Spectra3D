#include "render/TextAtlas.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "render/FontFile.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstring>

namespace s3d {
namespace {

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_ALPHA
#define GL_ALPHA 0x1906
#endif

constexpr int kAtlasSize = 1024;

// The printable ASCII is 95 characters, U+0020..U+007E, and the slot table
// counted 96 of them: the last slot was U+007F, which no font draws and
// nothing ever asked for, and the table was one longer than the letters in it.
// The number of slots is the number of letters now.
constexpr int kAsciiSlots = 95;

// Cyrillic extras beyond the contiguous U+0410..U+044F block.
const unsigned kExtraCodePoints[8] = {0x404, 0x454, 0x406, 0x456,
                                      0x407, 0x457, 0x490, 0x491};
constexpr int kFirstExtraSlot = 161;

// Latin-1, appended after the Cyrillic blocks: German umlauts and the sharp s,
// every French vowel and the cedilla. U+00C0..U+00FF is one contiguous run, so
// it costs a pair of lines instead of another hand-written list.
constexpr unsigned kLatin1FirstCp = 0xC0;
constexpr unsigned kLatin1LastCp = 0xFF;
constexpr int kLatin1FirstSlot = 169;

// Outside Latin-1 and still inside what a UI sets in those languages: the French
// guillemets, the oe ligature (both cases), and the Spanish opening marks - all
// of which live below U+00C0 and would otherwise be one more range each.
const unsigned kExtraLatin[6] = {0xAB, 0xBB, 0x152, 0x153, 0xA1, 0xBF};
constexpr int kExtraLatinSlot = 233;

} // namespace

int TextAtlas::glyphIndex(unsigned cp) {
    if (cp >= 32 && cp <= 126) return (int)cp - 32;
    // The Cyrillic block, and then the two letters outside it.
    //
    // The slots have to be the ones `codepointAt` bakes into, and they used to
    // be one higher than they are now: every Cyrillic letter was drawn as the
    // one after it, and a word in Ukrainian came out as a word in a language
    // nobody speaks. The atlas is baked with one table and read with the
    // other, so the two have to be the same table - and the only way to be
    // sure of that is to have them agree by construction.
    if (cp >= 0x410 && cp <= 0x44F) return 95 + (int)(cp - 0x410);
    if (cp == 0x401) return 159; // Є
    if (cp == 0x451) return 160; // є
    for (int i = 0; i < 8; ++i)
        if (cp == kExtraCodePoints[i]) return kFirstExtraSlot + i;
    if (cp >= kLatin1FirstCp && cp <= kLatin1LastCp)
        return kLatin1FirstSlot + (int)(cp - kLatin1FirstCp);
    for (int i = 0; i < 6; ++i)
        if (cp == kExtraLatin[i]) return kExtraLatinSlot + i;
    return -1;
}

unsigned TextAtlas::codepointAt(int index) {
    if (index < kAsciiSlots) return (unsigned)(32 + index);
    if (index == 159) return 0x401;
    if (index == 160) return 0x451;
    if (index >= kFirstExtraSlot && index < kLatin1FirstSlot)
        return kExtraCodePoints[index - kFirstExtraSlot];
    if (index >= kExtraLatinSlot) return kExtraLatin[index - kExtraLatinSlot];
    if (index >= kLatin1FirstSlot) return kLatin1FirstCp + (unsigned)(index - kLatin1FirstSlot);
    return 0x410u + (unsigned)(index - 95);
}

float TextAtlas::baselineFor(float lineHeight, float capHeight, float descender) {
    // The middle of the line, with the cap height above the baseline and the
    // descender below it. A font whose letters are taller than the line sticks
    // out of it a little at the top and the same little at the bottom, which is
    // what a line that is too tight for its font looks like.
    return (lineHeight - (capHeight + descender)) * 0.5f + capHeight;
}

TextAtlas::~TextAtlas() { release(); }

void TextAtlas::release() {
    if (texture_) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    ready_ = false;
}

bool TextAtlas::build() {
    release();

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library)) return false;

    FT_Face face = nullptr;
    std::string used;

    // The bundled font first, then the system ones. Every candidate is tried
    // before the next one is: a font that cannot be read is not a font.
    for (const std::string& path : fontFileCandidates()) {
        // The font library can only be given a narrow path, so the path is
        // handed over in the machine's own code page.
        if (FT_New_Face(library, fs::systemPath(path).c_str(), 0, &face) == 0) {
            used = path;
            break;
        }
    }
    if (!face) {
        FT_Done_FreeType(library);
        Log::warn("Text: no usable font (none in '%s/fonts' and none on this system), "
                  "falling back to metrics estimate",
                  fs::assetRoot().c_str());
        return false;
    }
    FT_Set_Pixel_Sizes(face, 0, kPixelSize);

    std::vector<unsigned char> atlas((size_t)kAtlasSize * kAtlasSize, 0);
    int penX = 1, penY = 1, rowHeight = 0, fitted = 0;

    for (int i = 0; i < kGlyphCount; ++i) {
        const unsigned cp = codepointAt(i);
        if (FT_Load_Char(face, (FT_ULong)cp, FT_LOAD_RENDER)) continue;

        FT_GlyphSlot slot = face->glyph;
        Glyph& g = glyphs_[i];
        g.advance = (float)slot->advance.x / 64.0f;
        g.offsetX = (float)slot->bitmap_left;
        g.offsetY = (float)slot->bitmap_top;
        g.width = (float)slot->bitmap.width;
        g.height = (float)slot->bitmap.rows;

        if (penX + (int)g.width + 1 >= kAtlasSize) {
            penX = 1;
            penY += rowHeight + 1;
            rowHeight = 0;
        }
        if (penY + (int)g.height + 1 >= kAtlasSize) {
            Log::warn("Text: atlas full, glyph U+%04X skipped", cp);
            g.width = g.height = 0.0f;
            continue;
        }

        if (g.width > 0.0f && g.height > 0.0f) {
            for (int row = 0; row < (int)g.height; ++row)
                std::memcpy(&atlas[(size_t)(penY + row) * kAtlasSize + penX],
                            slot->bitmap.buffer + (size_t)row * slot->bitmap.width,
                            (size_t)g.width);
            ++fitted;
        }

        g.u0 = (float)penX / (float)kAtlasSize;
        g.v0 = (float)penY / (float)kAtlasSize;
        g.u1 = (float)(penX + (int)g.width) / (float)kAtlasSize;
        g.v1 = (float)(penY + (int)g.height) / (float)kAtlasSize;
        penX += (int)g.width + 1;
        rowHeight = std::max(rowHeight, (int)g.height);
    }

    // Where the baseline sits in a line, measured from the font itself.
    //
    // The ascent - how far the font reaches above the baseline - is not the
    // answer: JetBrains Mono's ascent is taller than its own em, so a line
    // placed by the ascent hangs below the box it is drawn in, and the
    // descenders go off the bottom of the panel they are in.
    //
    // What the interface wants is the VISUAL box of the text centred in the
    // line: the capitals above the baseline, the descenders below it. The cap
    // height comes from the 'H' and the descender from the deepest glyph in the
    // atlas, and the baseline is what puts the two in the middle of the line.
    float capHeight = 0.0f;
    float descender = 0.0f;
    const int capIndex = glyphIndex('H');
    if (capIndex >= 0) capHeight = glyphs_[capIndex].offsetY;
    for (int i = 0; i < kGlyphCount; ++i) {
        const float below = glyphs_[i].height - glyphs_[i].offsetY;
        if (below > descender) descender = below;
    }
    baseline_ = baselineFor((float)kLineHeight, capHeight, descender);

    Log::info("Text: atlas %d/%d glyphs from '%s' (%s)", fitted, kGlyphCount, face->family_name,
              used.c_str());
    Log::info("Text: line %dpx, baseline %.1f, capitals %.1f, descender %.1f", kLineHeight,
              baseline_, capHeight, descender);

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, kAtlasSize, kAtlasSize, 0, GL_ALPHA,
                 GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    FT_Done_Face(face);
    FT_Done_FreeType(library);
    ready_ = true;
    return true;
}

float TextAtlas::textWidth(const char* utf8) const {
    if (!ready_) {
        // Rough fallback: assume half-em per byte, good enough for layout.
        return (float)strlen(utf8) * (kPixelSize * 0.5f);
    }
    float width = 0.0f;
    for (const char* p = utf8; *p;) {
        const int index = glyphIndex(str::nextCodepoint(p));
        if (index >= 0) width += glyphs_[index].advance;
    }
    return width;
}

void TextAtlas::appendQuads(std::vector<float>& batch, float x, float topY,
                            const char* utf8, float r, float g, float b, float scale) const {
    if (!ready_) return;

    // Glyph metrics are baked at one size, so shrinking means scaling the box and
    // the pen advance by the same factor. Doing it here rather than in each caller
    // is what keeps a scaled string the same width as textWidth() * scale.
    const float s = scale > 0.0f ? scale : 1.0f;
    float penX = x;
    const float baselineY = topY + baseline_ * s;
    for (const char* p = utf8; *p;) {
        int index = glyphIndex(str::nextCodepoint(p));
        if (index < 0) index = glyphIndex('?');
        const Glyph& glyph = glyphs_[index];

        const float x0 = penX + glyph.offsetX * s;
        const float y0 = baselineY - glyph.offsetY * s;
        const float x1 = x0 + glyph.width * s;
        const float y1 = y0 + glyph.height * s;

        // One grow + one straight-line fill instead of six
        // vector::insert(end, ptr, ptr + 7) calls per glyph, each of which
        // re-checked capacity and ran a generic 7-element copy. Same 42 floats
        // in the same order.
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
        put(x0, y0, glyph.u0, glyph.v0);
        put(x1, y0, glyph.u1, glyph.v0);
        put(x1, y1, glyph.u1, glyph.v1);
        put(x0, y0, glyph.u0, glyph.v0);
        put(x1, y1, glyph.u1, glyph.v1);
        put(x0, y1, glyph.u0, glyph.v1);
        penX += glyph.advance * s;
    }
}

} // namespace s3d