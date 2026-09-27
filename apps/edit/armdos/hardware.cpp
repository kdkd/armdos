/*
 * armdos/hardware.cpp - Turbo Vision's THardwareInfo for ARM-DOS.
 *
 * Replaces magiblot's source/platform/hardware.cpp (and the whole
 * Platform/ConsoleAdapter machinery behind it) with the way Turbo Vision
 * talked to a PC under DOS:
 *
 *   screen    the colour text buffer at 0xB8000 (BDA 0x44A columns, 0x484
 *             rows); TV's cells are converted to char/attribute words in a
 *             shadow buffer, and flushScreen() copies the changed rows to
 *             video memory (with the mouse cursor hidden meanwhile)
 *   caret     INT 10h AH=01h/02h
 *   keyboard  INT 16h AH=11h/10h (enhanced keys), AH=12h (shift states)
 *   mouse     INT 33h (MOUSE.COM): an event handler (AX=000Ch) queues the
 *             button/motion events from the mouse interrupt
 *   timer     the BIOS tick count at 0x46C (18.2 Hz)
 *   idle      CP15 wait-for-interrupt until a key, mouse event or tick
 *   Ctrl-C    INT 23h handler that ignores it; INT 24h (critical error)
 *             handler that fails the call, so a missing floppy becomes an
 *             error message box instead of "Abort, Retry, Fail?" over the
 *             editor's screen
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License (as Turbo Vision).
 */
#define Uses_TKeys
#define Uses_TEvent
#define Uses_TScreen
#define Uses_THardwareInfo
#include <tvision/tv.h>

#include <armdos.h>
#include <stdlib.h>
#include <string.h>

#include "edplat.h"

TEvent THardwareInfo::eventQ[];
size_t THardwareInfo::eventCount = 0;
BOOL THardwareInfo::insertState = True;
DWORD THardwareInfo::pendingEvent = 0;

/* ------------------------------------------------------------------ state */

static int scrCols = 80, scrRows = 25;
static uint16_t *shadow;            /* scrCols*scrRows char/attribute words */
static int dirtyLo[64], dirtyHi[64];/* per row: changed columns [lo, hi) */
static bool anyDirty;
static int caretX, caretY, caretSize = 0;       /* what TV wants */
static int hwCaretX = -1, hwCaretY = -1, hwCaretSize = -1;  /* what is set */
static int cursorLinesTop = 14, cursorLinesBottom = 15;  /* underline, 16-line font */

/* the DOS screen we started on (restored at exit) */
static uint16_t *savedScreen;
static int savedCols, savedRows, savedCurPos, savedCurShape;

static bool mousePresent;
static bool altDown, altAlone, altPending;     /* see pollAlt() */
static bool mouseShown;             /* INT 33h cursor state as TV sees it */

static armdos_vect_t oldInt23, oldInt24;
static bool consoleUp;

int armdosCritError = -1;           /* last INT 24h error code, -1 = none */

static inline uint16_t bdaW(unsigned off) { return *(volatile uint16_t *)(0x400 + off); }
static inline uint8_t bdaB(unsigned off) { return *(volatile uint8_t *)(0x400 + off); }

static unsigned int10(unsigned ax, unsigned bx = 0, unsigned cx = 0, unsigned dx = 0)
{
    struct armregs r = {};
    r.r0 = ax; r.r1 = bx; r.r2 = cx; r.r3 = dx;
    _armdos_int10(&r);
    return r.r0;
}

static struct armregs int33(unsigned ax, unsigned bx = 0, unsigned cx = 0, unsigned dx = 0)
{
    struct armregs r = {};
    r.r0 = ax; r.r1 = bx; r.r2 = cx; r.r3 = dx;
    _armdos_int33(&r);
    return r;
}

/* ------------------------------------------------------------------ mouse */

/* Events from the INT 33h handler (mouse interrupt) to getMouseEvent(). */
struct MouseRec { uint16_t x, y; uint8_t buttons; };
enum { mouseQSize = 64 };
static volatile MouseRec mouseQ[mouseQSize];
static volatile unsigned mouseHead, mouseTail;   /* head: next to read */
static int lastMouseX, lastMouseY;
static uint8_t lastButtons;

/* Called by the mouse driver as an ARM far call (IRQs off):
 * r0 = events, r1 = buttons, r2 = x, r3 = y (virtual 640 x lines*8). */
extern "C" void armdos_mouse_event(unsigned events, unsigned buttons, unsigned x, unsigned y)
{
    (void) events;
    MouseRec r;
    r.x = (uint16_t) (x >> 3);
    r.y = (uint16_t) (y >> 3);
    r.buttons = (uint8_t) (buttons & 3);
    unsigned tail = mouseTail;
    if (tail != mouseHead)
    {
        /* coalesce: a pure move replaces a pending entry with the same buttons */
        volatile MouseRec &prev = mouseQ[(tail - 1) % mouseQSize];
        if (prev.buttons == r.buttons && (events & ~1u) == 0)
        {
            prev.x = r.x; prev.y = r.y;
            return;
        }
    }
    if (tail - mouseHead >= mouseQSize)
        return;                         /* full: drop */
    mouseQ[tail % mouseQSize].x = r.x;
    mouseQ[tail % mouseQSize].y = r.y;
    mouseQ[tail % mouseQSize].buttons = r.buttons;
    mouseTail = tail + 1;
}

static void mouseInit()
{
    mousePresent = false;
    if (ARMDOS_IVT[0x33] == 0)
        return;
    struct armregs r = int33(0x0000);
    if ((r.r0 & 0xFFFF) != 0xFFFF)
        return;
    mousePresent = true;
    mouseShown = false;
    mouseHead = mouseTail = 0;
    int33(0x0007, 0, 0, (scrCols - 1) * 8);          /* ranges */
    int33(0x0008, 0, 0, (scrRows - 1) * 8);
    r = int33(0x0003);
    lastMouseX = (r.r2 & 0xFFFF) >> 3;
    lastMouseY = (r.r3 & 0xFFFF) >> 3;
    lastButtons = r.r1 & 3;
    /* moves, left/right press/release */
    int33(0x000C, 0, 0x1F, (unsigned) &armdos_mouse_event);
}

static void mouseDone()
{
    if (!mousePresent)
        return;
    int33(0x000C, 0, 0, 0);
    if (mouseShown)
        int33(0x0002);
    mouseShown = false;
    int33(0x0000);          /* reset: cursor hidden, handler removed */
}

/* ---------------------------------------------------------------- Ctrl-C */

static void int23Handler(struct armregs *f)
{
    ARMREGS_CLEAR_CARRY(f);             /* continue: Ctrl-C is just a key here */
}

static void int24Handler(struct armregs *f)
{
    armdosCritError = f->r5 & 0xFF;     /* DI low byte = error code */
    f->r0 = (f->r0 & ~0xFFu) | 3;       /* AL = 3: fail the call */
}

/* ----------------------------------------------------------------- screen */

static void readVideoInfo()
{
    scrCols = bdaW(0x4A);
    scrRows = bdaB(0x84) + 1;
    if (scrCols < 40 || scrCols > 132) scrCols = 80;
    if (scrRows < 25 || scrRows > 60) scrRows = 25;
    int charHeight = bdaB(0x85);
    if (charHeight < 8 || charHeight > 16) charHeight = 16;
    cursorLinesTop = charHeight - 2;
    cursorLinesBottom = charHeight - 1;
}

static void allocShadow()
{
    free(shadow);
    shadow = (uint16_t *) malloc(scrCols * scrRows * 2);
    volatile uint16_t *v = ARMDOS_TEXT_VRAM;
    for (int i = 0; i < scrCols * scrRows; ++i)
        shadow[i] = v[i];
    for (int y = 0; y < 64; ++y)
        dirtyLo[y] = 0x7FFF, dirtyHi[y] = 0;
    anyDirty = false;
}

static void markDirty(int y, int x0, int x1)
{
    if (x0 < dirtyLo[y]) dirtyLo[y] = x0;
    if (x1 > dirtyHi[y]) dirtyHi[y] = x1;
    anyDirty = true;
}

static void updateCaret()
{
    int size = caretSize;
    if (caretX < 0 || caretY < 0 || caretX >= scrCols || caretY >= scrRows)
        size = 0;
    if (size != hwCaretSize)
    {
        hwCaretSize = size;
        if (size <= 0)
            int10(0x0100, 0, 0x2000);
        else if (size >= 100)
            int10(0x0100, 0, cursorLinesBottom);              /* block */
        else if (size > 50)
            int10(0x0100, 0, ((cursorLinesBottom / 2) << 8) | cursorLinesBottom);
        else
            int10(0x0100, 0, (cursorLinesTop << 8) | cursorLinesBottom);
    }
    if (size > 0 && (caretX != hwCaretX || caretY != hwCaretY))
    {
        hwCaretX = caretX; hwCaretY = caretY;
        int10(0x0200, 0, 0, (caretY << 8) | caretX);
    }
}

void THardwareInfo::flushScreen() noexcept
{
    if (anyDirty && shadow)
    {
        bool hide = mousePresent && mouseShown;
        if (hide)
            int33(0x0002);
        volatile uint16_t *v = ARMDOS_TEXT_VRAM;
        for (int y = 0; y < scrRows; ++y)
            if (dirtyLo[y] < dirtyHi[y])
            {
                int base = y * scrCols;
                for (int x = dirtyLo[y]; x < dirtyHi[y]; ++x)
                    v[base + x] = shadow[base + x];
                dirtyLo[y] = 0x7FFF;
                dirtyHi[y] = 0;
            }
        anyDirty = false;
        if (hide)
            int33(0x0001);
    }
    updateCaret();
}

static inline uint8_t cellChar(const TScreenCell &c)
{
    TStringView t = c.character.getText();
    uint8_t ch = t.size() ? (uint8_t) t[0] : ' ';
    return ch ? ch : ' ';
}

static inline uint8_t cellAttr(const TScreenCell &c)
{
    TColorAttr a = c.attribute;
    TColor fg = a.getForeground(), bg = a.getBackground();
    uint8_t f = fg.isBIOS() ? (uint8_t) fg.asBIOS() : 0x07;
    uint8_t b = bg.isBIOS() ? (uint8_t) bg.asBIOS() : 0x00;
    if (a.getStyle() & slReverse)
    {
        uint8_t t = f; f = b; b = t;
    }
    return (uint8_t) (f | (b << 4));
}

void THardwareInfo::screenWrite( ushort x, ushort y, TScreenCell *buf, DWORD len ) noexcept
{
    if (!shadow || y >= scrRows || x >= scrCols)
        return;
    if (x + len > (DWORD) scrCols)
        len = scrCols - x;
    uint16_t *s = &shadow[y * scrCols + x];
    int lo = -1, hi = -1;
    for (DWORD i = 0; i < len; ++i)
    {
        uint16_t w = (uint16_t) (cellChar(buf[i]) | (cellAttr(buf[i]) << 8));
        if (s[i] != w)
        {
            s[i] = w;
            if (lo < 0) lo = i;
            hi = i + 1;
        }
    }
    if (lo >= 0)
        markDirty(y, x + lo, x + hi);
}

TScreenCell *THardwareInfo::allocateScreenBuffer() noexcept
{
    readVideoInfo();
    allocShadow();
    size_t n = (size_t) scrCols * scrRows;
    TScreenCell *b = (TScreenCell *) malloc(n * sizeof(TScreenCell));
    if (b)
        memset((void *) b, 0, n * sizeof(TScreenCell));
    return b;
}

void THardwareInfo::freeScreenBuffer( TScreenCell *buffer ) noexcept
{
    free(buffer);
}

void THardwareInfo::clearScreen( ushort, ushort ) noexcept
{
    if (!shadow)
        return;
    for (int i = 0; i < scrCols * scrRows; ++i)
        shadow[i] = 0x0720;
    for (int y = 0; y < scrRows; ++y)
        markDirty(y, 0, scrCols);
    flushScreen();
}

ushort THardwareInfo::getScreenRows() noexcept { return scrRows; }
ushort THardwareInfo::getScreenCols() noexcept { return scrCols; }

ushort THardwareInfo::getScreenMode() noexcept
{
    ushort mode = bdaB(0x49);
    if (mode == 7)
        return TDisplay::smMono;
    mode = TDisplay::smCO80;
    if (scrRows > 25)
        mode |= TDisplay::smFont8x8;
    return mode;
}

void THardwareInfo::setScreenMode( ushort mode ) noexcept
{
    armdosSetLines((mode & TDisplay::smFont8x8) ? 50 : 25);
    /* TScreen keeps its buffer on a mode change (a console window has a
     * fixed size): here the screen changed size, so give it a new one. */
    free(TScreen::screenBuffer);
    TScreen::screenBuffer = allocateScreenBuffer();
    hwCaretSize = -1; hwCaretX = hwCaretY = -1;
    if (mousePresent)
    {
        int33(0x0007, 0, 0, (scrCols - 1) * 8);
        int33(0x0008, 0, 0, (scrRows - 1) * 8);
    }
}

void THardwareInfo::setCaretSize( ushort size ) noexcept { caretSize = size; }
void THardwareInfo::setCaretPosition( ushort x, ushort y ) noexcept { caretX = x; caretY = y; }
ushort THardwareInfo::getCaretSize() noexcept
{
    return caretSize < 1 ? 1 : caretSize > 100 ? 100 : caretSize;
}
BOOL THardwareInfo::isCaretVisible() noexcept { return caretSize > 0; }

/* ------------------------------------------------------------ console up */

THardwareInfo::THardwareInfo() noexcept
{
}

THardwareInfo::~THardwareInfo()
{
}

void THardwareInfo::setUpConsole() noexcept
{
    if (consoleUp)
        return;
    consoleUp = true;
    readVideoInfo();
    /* save what DOS showed, to put it back at exit */
    savedCols = scrCols; savedRows = scrRows;
    free(savedScreen);
    savedScreen = (uint16_t *) malloc(scrCols * scrRows * 2);
    if (savedScreen)
    {
        volatile uint16_t *v = ARMDOS_TEXT_VRAM;
        for (int i = 0; i < scrCols * scrRows; ++i)
            savedScreen[i] = v[i];
    }
    savedCurPos = bdaW(0x50);
    savedCurShape = bdaW(0x60);
    hwCaretSize = -1; hwCaretX = hwCaretY = -1;

    oldInt23 = armdos_getvect(0x23);
    oldInt24 = armdos_getvect(0x24);
    armdos_setvect(0x23, int23Handler);
    armdos_setvect(0x24, int24Handler);
    mouseInit();
}

void THardwareInfo::restoreConsole() noexcept
{
    if (!consoleUp)
        return;
    consoleUp = false;
    mouseDone();
    if (savedRows != scrRows)
    {
        armdosSetLines(savedRows);
        readVideoInfo();
    }
    if (savedScreen && savedCols == scrCols && savedRows == scrRows)
    {
        volatile uint16_t *v = ARMDOS_TEXT_VRAM;
        for (int i = 0; i < scrCols * scrRows; ++i)
            v[i] = savedScreen[i];
    }
    int10(0x0100, 0, savedCurShape);
    int10(0x0200, 0, 0, savedCurPos);
    armdos_setvect(0x23, oldInt23);
    armdos_setvect(0x24, oldInt24);
}

/* ----------------------------------------------------------------- mouse */

DWORD THardwareInfo::getButtonCount() noexcept { return mousePresent ? 2 : 0; }

void THardwareInfo::cursorOn() noexcept
{
    if (mousePresent && !mouseShown)
    {
        flushScreen();
        int33(0x0001);
        mouseShown = true;
    }
}

void THardwareInfo::cursorOff() noexcept
{
    if (mousePresent && mouseShown)
    {
        int33(0x0002);
        mouseShown = false;
    }
}

static ushort shiftState()
{
    struct armregs r = {};
    r.r0 = 0x1200;
    _armdos_int16(&r);
    unsigned s = r.r0 & 0xFFFF;
    ushort k = 0;
    if (s & 0x0003) k |= kbShift;
    if (s & 0x0100) k |= kbLeftCtrl;
    if (s & 0x0400) k |= kbRightCtrl;
    if ((s & 0x0004) && !(s & 0x0500)) k |= kbLeftCtrl;
    if (s & 0x0200) k |= kbLeftAlt;
    if (s & 0x0800) k |= kbRightAlt;
    if ((s & 0x0008) && !(s & 0x0A00)) k |= kbLeftAlt;
    if (s & 0x0010) k |= kbScrollState;
    if (s & 0x0020) k |= kbNumState;
    if (s & 0x0040) k |= kbCapsState;
    return k;
}

BOOL THardwareInfo::getMouseEvent( MouseEventType& event ) noexcept
{
    if (!mousePresent || mouseHead == mouseTail)
        return False;
    altAlone = false;
    armdos_disable();
    unsigned h = mouseHead;
    MouseRec r;
    r.x = mouseQ[h % mouseQSize].x;
    r.y = mouseQ[h % mouseQSize].y;
    r.buttons = mouseQ[h % mouseQSize].buttons;
    mouseHead = h + 1;
    armdos_enable();
    lastMouseX = r.x < scrCols ? r.x : scrCols - 1;
    lastMouseY = r.y < scrRows ? r.y : scrRows - 1;
    lastButtons = r.buttons;
    event.where.x = lastMouseX;
    event.where.y = lastMouseY;
    event.buttons = lastButtons;
    event.eventFlags = 0;
    event.wheel = 0;
    event.controlKeyState = shiftState();
    return True;
}

/* -------------------------------------------------------------- keyboard */

/* Alt pressed and released on its own activates EDIT's menu bar: the
 * shift flags in the BIOS data area are watched; if no key reached the BIOS
 * buffer while Alt was down, a kbAltAlone (0xFE00) key event is made. */
static void pollAlt()
{
    bool a = (bdaB(0x17) & 0x08) != 0;
    if (a && !altDown)
    {
        altDown = true;
        altAlone = true;
    }
    else if (!a && altDown)
    {
        altDown = false;
        if (altAlone && bdaW(0x1A) == bdaW(0x1C))
            altPending = true;
        altAlone = false;
    }
}

static bool keyAvailable()
{
    struct armregs r = {};
    r.r0 = 0x1100;
    _armdos_int16(&r);
    return !(r.cpsr & ARM_CPSR_Z);
}

BOOL THardwareInfo::getKeyEvent( TEvent& event ) noexcept
{
    pollAlt();
    if (altPending)
    {
        altPending = false;
        event.what = evKeyDown;
        event.keyDown.keyCode = 0xFE00;         /* kbAltAlone (edit.h) */
        event.keyDown.controlKeyState = 0;
        event.keyDown.textLength = 0;
        return True;
    }
    if (!keyAvailable())
        return False;
    altAlone = false;
    struct armregs r = {};
    r.r0 = 0x1000;
    _armdos_int16(&r);
    ushort code = (ushort) (r.r0 & 0xFFFF);
    uchar ch = code & 0xFF, scan = code >> 8;
    ushort state = shiftState();
    if (ch == 0xE0 && scan != 0)
    {
        /* grey keys of the enhanced keyboard */
        state |= kbEnhanced;
        ch = 0;
        code = scan << 8;
        if (scan == 0xE0) code = 0x1C0D;            /* keypad Enter  */
    }
    else if (scan == 0xE0)
    {
        state |= kbEnhanced;                        /* keypad / or Enter */
        code = (ch == 0x2F) ? 0x352F : (ch == 0x0D) ? 0x1C0D : (ch == 0x0A) ? 0x1C0A : code;
    }
    /* Borland's codes for the editing keys with Shift / Ctrl (the BIOS
     * reports Shift+Del as Del with Shift down) */
    if ((code & 0xFF) == 0)
    {
        uchar sc = code >> 8;
        if (sc == 0x53 && (state & kbShift)) code = kbShiftDel;
        else if (sc == 0x52 && (state & kbShift)) code = kbShiftIns;
        else if (sc == 0x92) code = kbCtrlIns;
        else if (sc == 0x93) code = kbCtrlDel;
    }
    /* modern Turbo Vision's Ctrl+letter codes carry no scan code */
    if ((state & kbCtrlShift) && (code & 0xFF) >= 1 && (code & 0xFF) <= 0x1A)
    {
        uchar sc = code >> 8;
        if (sc != 0x1C && sc != 0x0E && sc != 0x0F && sc != 0x01)
            code &= 0xFF;
    }
    event.what = evKeyDown;
    event.keyDown.keyCode = code;
    event.keyDown.controlKeyState = state;
    ch = event.keyDown.charScan.charCode;
    if (ch >= ' ' && ch != 0x7F)
    {
        event.keyDown.text[0] = (char) ch;
        event.keyDown.textLength = 1;
    }
    else
        event.keyDown.textLength = 0;
    if (event.keyDown.keyCode == kbIns)
        insertState = !insertState;
    if (insertState)
        event.keyDown.controlKeyState |= kbInsState;
    return True;
}

void THardwareInfo::waitForEvents( int timeoutMs ) noexcept
{
    flushScreen();
    uint32_t start = ARMDOS_BIOS_TICKS;
    uint32_t ticks = timeoutMs < 0 ? 0xFFFFFFFFu : (uint32_t) (timeoutMs + 54) / 55;
    while (true)
    {
        pollAlt();
        if (mouseHead != mouseTail || altPending || keyAvailable())
            return;
        if (ARMDOS_BIOS_TICKS - start >= ticks)
            return;
        armdos_halt();      /* until the next interrupt: key, mouse or tick */
    }
}

void THardwareInfo::interruptEventWait() noexcept
{
}

uint32_t THardwareInfo::getTickCount() noexcept
{
    return ARMDOS_BIOS_TICKS;
}

uint64_t THardwareInfo::getTickCountMs() noexcept
{
    return (uint64_t) ARMDOS_BIOS_TICKS * 55;
}

BOOL THardwareInfo::setClipboardText( TStringView ) noexcept
{
    return False;       /* TV keeps its own clipboard (TEditor::clipboard) */
}

BOOL THardwareInfo::requestClipboardText( void (&)(TStringView) ) noexcept
{
    return False;
}

/* ----------------------------------------------------------- 25/50 lines */

void armdosSetLines(int lines)
{
    /* AX=1112h: 8x8 font -> 50 rows; AX=1114h: 8x16 font -> 25 rows */
    int10(lines > 25 ? 0x1112 : 0x1114, 0);
}
