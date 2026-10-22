#pragma once
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include <GL/gl.h>

namespace s3d {

// THE LETTERS OF A GAME, BAKED AT THE SIZE THEY ARE DRAWN AT.
//
// The editor's own interface is one size, and its atlas is baked once for it
// (TextAtlas). The interface of a game is not: a label can be any size its
// author asks for, and letters taken out of a bitmap of the wrong size are
// the soft, smeared letters everybody knows - the shape is right and the
// edges are not.
//
// So the letters of a game are baked at the size they are asked for, and only
// the ones that are used: a canvas with two labels has two labels' worth of
// letters in it and not the whole alphabet.
//
// ONE SIZE IS ONE PICTURE, AND THE PICTURES ARE FEW. A picture is made when
// a size is asked for the first time, and it is let go of when there are too
// many of them: the letters of a game are a handful of sizes, and the sizes
// of a window being resized are a lot of them.
class GlyphCache {
public:
    GlyphCache();
    ~GlyphCache();
    GlyphCache(const GlyphCache&) = delete;
    GlyphCache& operator=(const GlyphCache&) = delete;

    // Opens the font the engine ships. False when there is none to be had.
    bool open();
    bool ready() const { return ready_; }
    // Frees every picture and closes the font.
    void close();

    // The picture the letters of `size` are in. The size is made if it is new.
    unsigned textureFor(float size);
    // The width of a string, in pixels, at `size`.
    float measure(const char* utf8, float size);
    // The distance from the top of a line to its baseline, in pixels, at `size`.
    float baselineFor(float size);
    // The height of a line of letters, in pixels, at `size`.
    float lineHeightFor(float size);
    // The quads for a string at `size`, with the top of the line at (x, topY).
    // The letters are (x, y, u, v, r, g, b): seven floats to a vertex, the
    // same shape the atlas of the editor fills.
    void appendQuads(std::vector<float>& batch, float x, float topY, const char* utf8,
                     float r, float g, float b, float size);

    // The number of pixels the letters of `size` are baked at: the nearest
    // whole pixel, and never less than one.
    static int pixelsFor(float size);

private:
    struct Glyph {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        float width = 0, height = 0;
        float offsetX = 0, offsetY = 0;
        float advance = 0;
        // False when no font of the chain has this letter: the letter is not
        // drawn and takes no room.
        bool hasGlyph = false;
    };
    // One size: one picture, and the letters in it.
    struct Picture {
        int pixels = 0;
        int side = 0;
        unsigned texture = 0;
        float baseline = 0;
        float lineHeight = 0;
        std::unordered_map<unsigned, Glyph> glyphs;
        // Where the next letter goes in the picture.
        int penX = 1, penY = 1, rowHeight = 0;
        // When it was last asked for, so that the oldest picture can be let go
        // of. The picture in use is the one that was asked for last, and the
        // picture in use is never the one that is let go of.
        unsigned long used = 0;
    };
    // The FreeType library and the face in it. Their types cannot be named in
    // a header without dragging the font library into every file that so much
    // as mentions a letter, so they live behind a pointer.
    struct Impl;

    // The picture for a size, made if it is not there yet.
    Picture& pictureFor(float size);
    // The letter in a picture, baked if it is not there yet.
    const Glyph* glyphFor(Picture& picture, unsigned codepoint);

    std::unique_ptr<Impl> impl_;
    std::map<int, Picture> pictures_;
    unsigned long clock_ = 0;
    bool ready_ = false;
};

} // namespace s3d
