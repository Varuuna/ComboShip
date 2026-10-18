#pragma once
// ComboShip: file-name glyph codes. MM keeps digits at 0x00-0x09, A-Z at 0x0A, a-z at 0x24, space 0x3E, '-' 0x3F and
// '.' 0x40 (OOT's PAL order). OOT on an NTSC ROM keeps A-Z at 0xAB, space at 0xDF, '-' at 0xE4 and '.' at 0xEA, with
// Japanese glyphs below 0xAB. Names cross between the games through these two converters. C-compatible.

// One OOT glyph -> MM's code. ntsc = the OOT name uses NTSC codes (its filenameLanguage is not PAL); JP glyphs become
// a space.
static inline unsigned char ComboNameCharOotToMm(unsigned char c, int ntsc) {
    if (!ntsc) {
        return (c <= 0x40) ? c : 0x3E;
    }
    if (c <= 0x09) {
        return c;
    }
    if (c >= 0xAB && c <= 0xDF) {
        return (unsigned char)(c - 0xA1);
    }
    if (c == 0xE4) {
        return 0x3F;
    }
    if (c == 0xEA) {
        return 0x40;
    }
    return 0x3E;
}

// One MM glyph -> OOT's code (the inverse).
static inline unsigned char ComboNameCharMmToOot(unsigned char c, int ntsc) {
    if (c > 0x40) {
        c = 0x3E;
    }
    if (!ntsc || c <= 0x09) {
        return c;
    }
    if (c == 0x3F) {
        return 0xE4;
    }
    if (c == 0x40) {
        return 0xEA;
    }
    return (unsigned char)(c + 0xA1);
}
