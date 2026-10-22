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

} // namespace

int TextAtlas::glyphIndex(unsigned cp) { return text::slotOf(cp); }

unsigned TextAtlas::codepointAt(int index) { return text::codepointAt(index); }

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

    // The faces the letters come from, in the order they are asked.
    //
    // The first one is the font the interface is drawn with - the bundled one,
    // so that the editor looks the same on every machine. The ones after it
    // are the fallbacks: a letter the first font does not have is taken from
    // the first font that has it. That is how the Bengali letters are drawn
    // by a font that has Bengali letters, and how a letter of a language that
    // is added tomorrow is drawn by the font of that script.
    std::vector<FT_Face> faces;
    std::vector<std::string> faceNames;

    for (const std::string& path : fontFileCandidates()) {
        // The font library can only be given a narrow path, so the path is
        // handed over in the machine's own code page.
        FT_Face face = nullptr;
        if (FT_New_Face(library, fs::systemPath(path).c_str(), 0, &face) == 0) {
            faces.push_back(face);
            faceNames.push_back(path);
            break;
        }
    }
    if (faces.empty()) {
        FT_Done_FreeType(library);
        Log::warn("Text: no usable font (none in '%s/fonts' and none on this system), "
                  "falling back to metrics estimate",
                  fs::assetRoot().c_str());
        return false;
    }

    for (const std::string& path : fallbackFontCandidates()) {
        FT_Face face = nullptr;
        if (FT_New_Face(library, fs::systemPath(path).c_str(), 0, &face) == 0) {
            faces.push_back(face);
            faceNames.push_back(path);
        }
    }
    Log::info("Text: %d fonts in the chain, the first is '%s'", (int)faces.size(),
              faceNames[0].c_str());

    std::vector<unsigned char> atlas((size_t)kAtlasSize * kAtlasSize, 0);
    int penX = 1, penY = 1, rowHeight = 0, fitted = 0, missing = 0;
    // Which letters came from the font the interface is drawn with. The line
    // of text is measured with those and not with the fallbacks: a Bengali
    // letter is taller than a Latin one, and the line of every other language
    // must not move because of it.
    std::vector<bool> fromPrimary((size_t)kGlyphCount, false);

    for (int i = 0; i < kGlyphCount; ++i) {
        const unsigned cp = codepointAt(i);

        // The first font that has this letter. A font that does not have it
        // says so with a zero index, and the letter is asked of the next one.
        FT_Face face = nullptr;
        for (FT_Face candidate : faces) {
            if (FT_Get_Char_Index(candidate, cp) != 0) {
                face = candidate;
                break;
            }
        }
        if (!face) {
            // No font of the chain has this letter. It is left empty and it is
            // not drawn - see `appendQuads`. The count is reported at the end
            // of the build, because a language whose letters are all missing
            // is a language that is not there.
            ++missing;
            continue;
        }

        FT_Set_Pixel_Sizes(face, 0, kPixelSize);
        if (FT_Load_Char(face, (FT_ULong)cp, FT_LOAD_RENDER)) continue;

        FT_GlyphSlot slot = face->glyph;
        Glyph& g = glyphs_[i];
        g.advance = (float)slot->advance.x / 64.0f;
        g.offsetX = (float)slot->bitmap_left;
        g.offsetY = (float)slot->bitmap_top;
        g.width = (float)slot->bitmap.width;
        g.height = (float)slot->bitmap.rows;
        g.hasGlyph = true;
        fromPrimary[(size_t)i] = (face == faces[0]);

        if (penX + (int)g.width + 1 >= kAtlasSize) {
            penX = 1;
            penY += rowHeight + 1;
            rowHeight = 0;
        }
        if (penY + (int)g.height + 1 >= kAtlasSize) {
            Log::warn("Text: atlas full, glyph U+%04X skipped", cp);
            g.width = g.height = 0.0f;
            g.hasGlyph = false;
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
    // height comes from the 'H' and the descender from the deepest glyph in
    // the atlas, and the baseline is what puts the two in the middle of the line.
    float capHeight = 0.0f;
    float descender = 0.0f;
    const int capIndex = glyphIndex('H');
    if (capIndex >= 0) capHeight = glyphs_[capIndex].offsetY;
    for (int i = 0; i < kGlyphCount; ++i) {
        if (!fromPrimary[(size_t)i]) continue;
        const float below = glyphs_[i].height - glyphs_[i].offsetY;
        if (below > descender) descender = below;
    }
    baseline_ = baselineFor((float)kLineHeight, capHeight, descender);

    Log::info("Text: atlas %d/%d glyphs from '%s' (%s)", fitted, kGlyphCount,
              faces[0]->family_name, faceNames[0].c_str());
    // The letters no font has are the ones the table has and no font does:
    // the reserved code points of a block, mostly. They are not drawn, and the
    // number is here so that a font that did not come with the engine - the
    // Bengali one, say - is visible in the log instead of in the interface.
    Log::info("Text: %d letters have no font in the chain and are not drawn", missing);
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

    for (FT_Face face : faces) FT_Done_Face(face);
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
        const unsigned cp = str::nextCodepoint(p);
        const int index = glyphIndex(cp);
        // A letter of a script the engine has no slot for is drawn as one
        // question mark, and it takes the room of that mark.
        if (index < 0) {
            width += glyphs_[glyphIndex('?')].advance;
            continue;
        }
        // A letter the table has and no font has is not drawn at all, so it
        // takes no room either. The two cases have to agree, or the text is
        // measured with one width and drawn with another.
        if (!glyphs_[index].hasGlyph) continue;
        width += glyphs_[index].advance;
    }
    return width;
}

void TextAtlas::appendQuads(std::vector<float>& batch, float x, float topY,
                            const char* utf8, float r, float g, float b, float scale) const {
    if (!ready_) {
        // A silent "no text" is how a whole editor loses its letters and nobody
        // notices until someone looks at the picture. Warn once so the failure
        // cannot be missed.
        static bool warned = false;
        if (!warned) {
            Log::warn("Text: the font atlas is not ready; no text will be drawn");
            warned = true;
        }
        return;
    }

    // Glyph metrics are baked at one size, so shrinking means scaling the box and
    // the pen advance by the same factor. Doing it here rather than in each caller
    // is what keeps a scaled string the same width as textWidth() * scale.
    const float s = scale > 0.0f ? scale : 1.0f;
    float penX = x;
    const float baselineY = topY + baseline_ * s;
    const int questionIndex = glyphIndex('?');
    for (const char* p = utf8; *p;) {
        const unsigned cp = str::nextCodepoint(p);
        int index = glyphIndex(cp);
        if (index < 0) {
            // A letter of a script the engine has no slot for. ONE question
            // mark stands for it - one for the letter, not one for each of its
            // bytes. The decoder used to turn a three-byte letter into three
            // question marks, and a Bengali interface came out as a row of
            // them; that cannot happen now, and it must not come back.
            index = questionIndex;
        } else if (!glyphs_[index].hasGlyph) {
            // A letter the table has and no font has: it is not drawn. An empty
            // box in the middle of a word is worse than no letter at all.
            continue;
        }
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