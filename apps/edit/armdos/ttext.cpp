/*
 * armdos/ttext.cpp - TText for ARM-DOS: single-byte code page 437.
 *
 * Replaces magiblot's source/platform/ttext.cpp, which treats text as UTF-8
 * (with invalid bytes shown as CP437 glyphs). On ARM-DOS every byte is one
 * character one column wide, and a screen cell holds that byte: the text
 * buffer at 0xB8000 shows it with the VGA's code page 437 font. So a DOS
 * text file edits byte for byte, like MS-DOS EDIT.
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License (as Turbo Vision).
 */
#define Uses_TText
#include <tvision/tv.h>

#include <ctype.h>

size_t TText::width(TStringView text) noexcept
{
    return text.size();
}

TTextMetrics TText::measure(TStringView text) noexcept
{
    TTextMetrics m {};
    m.width = (uint) text.size();
    m.characterCount = (uint) text.size();
    m.graphemeCount = (uint) text.size();
    return m;
}

size_t TText::next(TStringView text) noexcept
{
    return text.size() ? 1 : 0;
}

TText::Lw TText::nextImpl(TStringView text) noexcept
{
    if (text.size())
        return {1, 1};
    return {0, 0};
}

TText::Lw TText::nextImpl(TSpan<const uint32_t> text) noexcept
{
    if (text.size())
        return {1, 1};
    return {0, 0};
}

size_t TText::prev(TStringView, size_t index) noexcept
{
    return index ? 1 : 0;
}

/* Upper case in code page 437 (as DOS's country case map does for 437). */
static uchar cpUpper(uchar c)
{
    static const uchar hi[128] = {
        0x80,0x9A,0x90,0x41,0x8E,0x41,0x8F,0x80,0x45,0x45,0x45,0x49,0x49,0x49,0x8E,0x8F,
        0x90,0x92,0x92,0x4F,0x99,0x4F,0x55,0x55,0x59,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F,
        0x41,0x49,0x4F,0x55,0xA5,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
    };
    if (c < 0x80)
        return (uchar) toupper(c);
    if (c < 0xB0)
        return hi[c - 0x80];
    return c;
}

Boolean TText::equalsIgnoreCase(TStringView a, TStringView b) noexcept
{
    if (a.size() != b.size())
        return False;
    for (size_t i = 0; i < a.size(); ++i)
        if (cpUpper((uchar) a[i]) != cpUpper((uchar) b[i]))
            return False;
    return True;
}

char TText::toCodePage(TStringView text) noexcept
{
    return text.size() ? text[0] : '\0';
}

TStringView TText::fromCodePage(char c) noexcept
{
    static char all[256];
    static bool init = false;
    if (!init)
    {
        for (int i = 0; i < 256; ++i)
            all[i] = (char) i;
        init = true;
    }
    return TStringView(&all[(uchar) c], 1);
}

void TText::setCodePageTranslation(const char (*)[256][4]) noexcept
{
}

TText::Lw TText::scrollImpl(TStringView text, int count, Boolean) noexcept
{
    if (count <= 0)
        return {0, 0};
    size_t n = (size_t) count < text.size() ? (size_t) count : text.size();
    return {n, n};
}

TText::Lw TText::scrollImpl(TSpan<const uint32_t> text, int count, Boolean) noexcept
{
    if (count <= 0)
        return {0, 0};
    size_t n = (size_t) count < text.size() ? (size_t) count : text.size();
    return {n, n};
}

TText::Lw TText::drawOneImpl( TSpan<TScreenCell> cells, size_t i,
                              TStringView text, size_t j ) noexcept
{
    if (j < text.size() && i < cells.size())
    {
        cells[i].character.initWithChar(text[j]);
        return {1, 1};
    }
    return {0, 0};
}

TText::Lw TText::drawOneImpl( TSpan<TScreenCell> cells, size_t i,
                              TSpan<const uint32_t> text, size_t j ) noexcept
{
    if (j < text.size() && i < cells.size())
    {
        uint32_t c = text[j];
        cells[i].character.initWithChar(c < 256 ? (char) c : '?');
        return {1, 1};
    }
    return {0, 0};
}
