#pragma once

// The letters the engine can draw, in one table.
//
// The renderer, the window and the tests ask the same question - "can this
// letter be shown?" - and they have to answer it the same way. A letter the
// window accepts and the atlas cannot draw is an invisible mark in the middle
// of a name; a letter the atlas can draw and the window refuses is a letter
// that cannot be typed. So the table lives here, below both, and everybody
// reads it.
//
// THE ATLAS AND THE LETTERS ARE THE SAME TABLE.
//
// A slot is a place in the atlas texture, and the texture is read with this
// table and written with it. Two tables are two answers to the same question,
// and the day they disagree a letter is drawn as the letter after it.
//
// The order of the slots is fixed. The first slots are the Latin ones, then
// the Cyrillic, then the Bengali - the scripts a language pack is written in,
// in the order the engine learned them.
namespace s3d {
namespace text {

// The printable ASCII: U+0020..U+007E. Ninety-five slots, and the space is
// one of them - it has no ink, but it has an advance.
inline constexpr int kAsciiSlots = 95;
inline constexpr unsigned kAsciiFirstCp = 0x20;
inline constexpr unsigned kAsciiLastCp = 0x7E;

// The Cyrillic block, U+0410..U+044F, and then the letters outside it that
// an interface in Ukrainian needs: Є є, Ё ё, І і, Ї ї, Ґ ґ.
inline constexpr int kCyrillicFirstSlot = 95;
inline constexpr unsigned kCyrillicFirstCp = 0x410;
inline constexpr unsigned kCyrillicLastCp = 0x44F;
inline constexpr int kCyrillicExtraFirstSlot = 159;
inline constexpr int kCyrillicExtraCount = 10;
inline constexpr unsigned kCyrillicExtras[kCyrillicExtraCount] = {
    0x401, 0x451,  // Є є
    0x404, 0x454,  // Ё ё
    0x406, 0x456,  // І і
    0x407, 0x457,  // Ї ї
    0x490, 0x491,  // Ґ ґ
};

// Latin-1: the German umlauts and the sharp s, every French vowel and the
// cedilla, the Spanish marks. U+00C0..U+00FF is one contiguous run, so it
// costs a pair of lines instead of a hand-written list.
inline constexpr int kLatin1FirstSlot = 169;
inline constexpr unsigned kLatin1FirstCp = 0xC0;
inline constexpr unsigned kLatin1LastCp = 0xFF;
inline constexpr int kExtraLatinFirstSlot = 233;
inline constexpr int kExtraLatinCount = 6;
// Below Latin-1 and still inside what a French or a Spanish interface sets:
// the guillemets, the oe ligature in both cases, and the inverted marks.
inline constexpr unsigned kExtraLatin[kExtraLatinCount] = {0xAB, 0xBB, 0x152, 0x153, 0xA1, 0xBF};

// The Bengali block, U+0980..U+09FF: the letters, the signs that go under
// them and the marks that go above them. One block, because a Bengali letter
// is made of a letter and two signs and all three live next to each other.
//
// The block is taken whole. Some of its code points are not letters at all -
// they are reserved for a future - and a font has no glyph for them: those
// are simply not drawn, which is the one thing that is always right about a
// letter that does not exist.
inline constexpr int kBengaliFirstSlot = 239;
inline constexpr unsigned kBengaliFirstCp = 0x980;
inline constexpr unsigned kBengaliLastCp = 0x9FF;
inline constexpr int kBengaliSlots = 128;

// The danda - the stick a Bengali sentence ends with. It lives in the
// Devanagari block, but Bengali writes with it.
inline constexpr int kDandaSlot = 367;
inline constexpr unsigned kDandaCp = 0x964;

inline constexpr int kGlyphCount = 368;

// The slot a code point lives in, or -1 when the engine has no letter for it.
inline int slotOf(unsigned cp) {
    if (cp >= kAsciiFirstCp && cp <= kAsciiLastCp)
        return (int)cp - (int)kAsciiFirstCp;
    if (cp >= kCyrillicFirstCp && cp <= kCyrillicLastCp)
        return kCyrillicFirstSlot + (int)(cp - kCyrillicFirstCp);
    for (int i = 0; i < kCyrillicExtraCount; ++i)
        if (cp == kCyrillicExtras[i]) return kCyrillicExtraFirstSlot + i;
    if (cp >= kLatin1FirstCp && cp <= kLatin1LastCp)
        return kLatin1FirstSlot + (int)(cp - kLatin1FirstCp);
    for (int i = 0; i < kExtraLatinCount; ++i)
        if (cp == kExtraLatin[i]) return kExtraLatinFirstSlot + i;
    if (cp >= kBengaliFirstCp && cp <= kBengaliLastCp)
        return kBengaliFirstSlot + (int)(cp - kBengaliFirstCp);
    if (cp == kDandaCp) return kDandaSlot;
    return -1;
}

// The code point in a slot: the other direction of the same table.
inline unsigned codepointAt(int slot) {
    if (slot < 0 || slot >= kGlyphCount) return 0;
    if (slot < kAsciiSlots) return kAsciiFirstCp + (unsigned)slot;
    if (slot < kCyrillicExtraFirstSlot)
        return kCyrillicFirstCp + (unsigned)(slot - kCyrillicFirstSlot);
    if (slot < kLatin1FirstSlot)
        return kCyrillicExtras[slot - kCyrillicExtraFirstSlot];
    if (slot < kExtraLatinFirstSlot)
        return kLatin1FirstCp + (unsigned)(slot - kLatin1FirstSlot);
    if (slot < kBengaliFirstSlot) return kExtraLatin[slot - kExtraLatinFirstSlot];
    if (slot < kDandaSlot) return kBengaliFirstCp + (unsigned)(slot - kBengaliFirstSlot);
    return kDandaCp;
}

// Whether the engine has a letter for this code point at all.
inline bool covers(unsigned cp) { return slotOf(cp) >= 0; }

} // namespace text
} // namespace s3d
