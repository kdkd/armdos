// ID_VL.C

#include "wl_def.h"
#include <armdos.h>

// Uncomment the following line, if you get destination out of bounds
// assertion errors and want to ignore them during debugging
//#define IGNORE_BAD_DEST

#ifdef IGNORE_BAD_DEST
#undef assert
#define assert(x) if(!(x)) return
#define assert_ret(x) if(!(x)) return 0
#else
#define assert_ret(x) assert(x)
#endif

boolean  fullscreen = true;
#ifdef ARMDOS
// VGA mode 13h: 320x200, 256 colours, linear frame buffer at 0xA0000.
// (WOLF3D.EXE used the planar "mode X" variant of the same 320x200 mode for
// page flipping; here the game draws into screenBuffer and each finished
// frame is copied to the frame buffer, which looks the same.)
int16_t  screenWidth = 320;
int16_t  screenHeight = 200;
int      screenBits = 8;
#elif defined(_arch_dreamcast)
int16_t  screenWidth = 320;
int16_t  screenHeight = 200;
int      screenBits = 8;
#else
int16_t  screenWidth = 640;
int16_t  screenHeight = 400;
int      screenBits = -1;      // use "best" color depth according to libSDL
#endif

SDL_Surface *screen = NULL;
unsigned screenPitch;

SDL_Surface *screenBuffer = NULL;
unsigned bufferPitch;

int      scaleFactor;

boolean	 screenfaded;
unsigned bordercolor;

uint32_t *ylookup;

SDL_Color palette1[256], palette2[256];
SDL_Color curpal[256];


#define CASSERT(x) extern int ASSERT_COMPILE[((x) != 0) * 2 - 1];
#define RGB(r, g, b) {(r)*255/63, (g)*255/63, (b)*255/63, SDL_ALPHA_OPAQUE}

SDL_Color gamepal[]={
#ifdef SPEAR
    #include "sodpal.inc"
#else
    #include "wolfpal.inc"
#endif
};

CASSERT(lengthof(gamepal) == 256)

static boolean vgamode;

static void VL_BiosSetMode(unsigned mode)
{
    struct armregs r;

    memset(&r, 0, sizeof(r));
    r.r0 = mode & 0xff;             // INT 10h AH=00h set video mode
    _armdos_int10(&r);
}

//===========================================================================

/*
=======================
=
= VL_WaitVBL
=
= Waits for the start of the next vertical retrace (port 3DAh bit 3), the
= given number of times - without spinning: the CPU sleeps in WFI between
= polls and the game's timer interrupt (140 or 700 Hz) wakes it. As the 1.4 ms
= retrace can fall between two polls, a frame's worth of time (1/70 s)
= passing counts as a retrace too. Long waits sleep until the last frame.
= (Before the timer is installed there is nothing to wake us: then it polls,
= as WOLF3D.EXE always did.)
=
=======================
*/

void VL_WaitVBL (int vbls)
{
    Uint32 start;
    int    inretrace, now;

    if (vbls <= 0)
        return;
    if (vbls > 1)
        SDL_Delay((vbls - 1) * 1000 / 70);

    if (!SD_TimerRunning())
    {
        while (armdos_inb(0x3da) & 8)       // leave a retrace in progress
            ;
        while (!(armdos_inb(0x3da) & 8))    // wait for the next one
            ;
        return;
    }

    start = SDL_GetTicks();
    inretrace = armdos_inb(0x3da) & 8;
    for (;;)
    {
        armdos_halt();                      // until the next interrupt
        now = armdos_inb(0x3da) & 8;
        if (now && !inretrace)
            return;                         // a new retrace has begun
        inretrace = now;
        if (SDL_GetTicks() - start >= 14)   // a whole frame has gone by
            return;
    }
}

/*
=======================
=
= VL_Shutdown
=
=======================
*/

void VL_Shutdown (void)
{
    SDL_FreeSurface (screenBuffer);
    SDL_FreeSurface (screen);

    free (ylookup);
    free (pixelangle);
    free (wallheight);
    screenBuffer = NULL;
    screen = NULL;
    ylookup = NULL;
    pixelangle = NULL;
    wallheight = NULL;

    VL_SetTextMode ();
}

/*
=======================
=
= VL_SetTextMode
=
=======================
*/

void VL_SetTextMode (void)
{
    if (vgamode)
    {
        VL_BiosSetMode (0x03);
        vgamode = false;
    }
}

/*
=======================
=
= VL_SetVGAPlaneMode
=
=======================
*/

void VL_SetVGAPlaneMode (void)
{
    int i;

    VL_BiosSetMode (0x13);
    vgamode = true;

    screen = ARMDOS_WrapSurface((void *)0xA0000, 320, 200, 320);
    screenBuffer = SDL_CreateRGBSurface(0, screenWidth, screenHeight, 8, 0, 0, 0, 0);
    if(!screen || !screenBuffer)
    {
        VL_SetTextMode ();
        printf("Unable to create screen buffer surface\n");
        exit(1);
    }

    VL_SetPalette (gamepal, false);

    screenPitch = screen->pitch;
    bufferPitch = screenBuffer->pitch;

    scaleFactor = 1;

    ylookup = SafeMalloc(screenHeight * sizeof(*ylookup));
    pixelangle = SafeMalloc(screenWidth * sizeof(*pixelangle));
    wallheight = SafeMalloc(screenWidth * sizeof(*wallheight));

    for (i = 0; i < screenHeight; i++)
        ylookup[i] = i * bufferPitch;
}

/*
=============================================================================

						PALETTE OPS

		To avoid snow, do a WaitVBL BEFORE calling these

=============================================================================
*/

/*
=================
=
= VL_ConvertPalette
=
=================
*/

void VL_ConvertPalette(byte *srcpal, SDL_Color *destpal, int numColors)
{
    int i;

    for(i=0; i<numColors; i++)
    {
        destpal[i].r = *srcpal++ * 255 / 63;
        destpal[i].g = *srcpal++ * 255 / 63;
        destpal[i].b = *srcpal++ * 255 / 63;
        destpal[i].a = SDL_ALPHA_OPAQUE;
    }
}

/*
=================
=
= VL_FillPalette
=
=================
*/

void VL_FillPalette (int red, int green, int blue)
{
    int i;
    SDL_Color pal[256];

    for(i=0; i<256; i++)
    {
        pal[i].r = red;
        pal[i].g = green;
        pal[i].b = blue;
        pal[i].a = SDL_ALPHA_OPAQUE;
    }

    VL_SetPalette(pal, true);
}

//===========================================================================

/*
=================
=
= VL_GetColor
=
=================
*/

void VL_GetColor	(int color, int *red, int *green, int *blue)
{
    SDL_Color *col = &curpal[color];
    *red = col->r;
    *green = col->g;
    *blue = col->b;
}

//===========================================================================

/*
=================
=
= VL_SetPalette
=
=================
*/

void VL_SetPalette (SDL_Color *palette, bool forceupdate)
{
    int i;

    (void)forceupdate;
    memcpy(curpal, palette, sizeof(SDL_Color) * 256);

    // the VGA DAC: index to 3C8h, then 6-bit red, green, blue to 3C9h
    armdos_outb(0x3c8, 0);
    for (i = 0; i < 256; i++)
    {
        armdos_outb(0x3c9, (palette[i].r * 63 + 127) / 255);
        armdos_outb(0x3c9, (palette[i].g * 63 + 127) / 255);
        armdos_outb(0x3c9, (palette[i].b * 63 + 127) / 255);
    }
}


//===========================================================================

/*
=================
=
= VL_GetPalette
=
=================
*/

void VL_GetPalette (SDL_Color *palette)
{
    memcpy(palette, curpal, sizeof(SDL_Color) * 256);
}


//===========================================================================

/*
=================
=
= VL_FadeOut
=
= Fades the current palette to the given color in the given number of steps
=
=================
*/

void VL_FadeOut (int start, int end, int red, int green, int blue, int steps)
{
	int		    i,j,orig,delta;
	SDL_Color   *origptr, *newptr;

    red = red * 255 / 63;
    green = green * 255 / 63;
    blue = blue * 255 / 63;

	VL_WaitVBL(1);
	VL_GetPalette(palette1);
	memcpy(palette2, palette1, sizeof(SDL_Color) * 256);

//
// fade through intermediate frames
//
	for (i=0;i<steps;i++)
	{
		origptr = &palette1[start];
		newptr = &palette2[start];
		for (j=start;j<=end;j++)
		{
			orig = origptr->r;
			delta = red-orig;
			newptr->r = orig + delta * i / steps;
			orig = origptr->g;
			delta = green-orig;
			newptr->g = orig + delta * i / steps;
			orig = origptr->b;
			delta = blue-orig;
			newptr->b = orig + delta * i / steps;
			newptr->a = SDL_ALPHA_OPAQUE;
			origptr++;
			newptr++;
		}

        VL_WaitVBL(1);
		VL_SetPalette (palette2, true);
	}

//
// final color
//
	VL_FillPalette (red,green,blue);

	screenfaded = true;
}


/*
=================
=
= VL_FadeIn
=
=================
*/

void VL_FadeIn (int start, int end, SDL_Color *palette, int steps)
{
	int i,j,delta;

	VL_WaitVBL(1);
	VL_GetPalette(palette1);
	memcpy(palette2, palette1, sizeof(SDL_Color) * 256);

//
// fade through intermediate frames
//
	for (i=0;i<steps;i++)
	{
		for (j=start;j<=end;j++)
		{
			delta = palette[j].r-palette1[j].r;
			palette2[j].r = palette1[j].r + delta * i / steps;
			delta = palette[j].g-palette1[j].g;
			palette2[j].g = palette1[j].g + delta * i / steps;
			delta = palette[j].b-palette1[j].b;
			palette2[j].b = palette1[j].b + delta * i / steps;
			palette2[j].a = SDL_ALPHA_OPAQUE;
		}

        VL_WaitVBL(1);
		VL_SetPalette(palette2, true);
	}

//
// final color
//
	VL_SetPalette (palette, true);
	screenfaded = false;
}

/*
=============================================================================

							PIXEL OPS

=============================================================================
*/

byte *VL_LockSurface(SDL_Surface *surface)
{
    if(SDL_MUSTLOCK(surface))
    {
        if(SDL_LockSurface(surface) < 0)
            return NULL;
    }
    return (byte *) surface->pixels;
}

void VL_UnlockSurface(SDL_Surface *surface)
{
    if(SDL_MUSTLOCK(surface))
    {
        SDL_UnlockSurface(surface);
    }
}

/*
=================
=
= VL_Plot
=
=================
*/

void VL_Plot (int x, int y, int color)
{
    byte *dest;

    assert(x >= 0 && x < screenWidth
            && y >= 0 && y < screenHeight
            && "VL_Plot: Pixel out of bounds!");

    dest = VL_LockSurface(screenBuffer);
    if(dest == NULL) return;

    dest[ylookup[y] + x] = color;

    VL_UnlockSurface(screenBuffer);
}

/*
=================
=
= VL_GetPixel
=
=================
*/

byte VL_GetPixel (int x, int y)
{
    byte col;

    assert_ret(x >= 0 && x < screenWidth
            && y >= 0 && y < screenHeight
            && "VL_GetPixel: Pixel out of bounds!");

    if (!VL_LockSurface(screenBuffer))
        return 0;

    col = ((byte *) screenBuffer->pixels)[ylookup[y] + x];

    VL_UnlockSurface(screenBuffer);

    return col;
}


/*
=================
=
= VL_Hlin
=
=================
*/

void VL_Hlin (int x, int y, int width, int color)
{
    byte *dest;

    assert(x >= 0 && x + width <= screenWidth
            && y >= 0 && y < screenHeight
            && "VL_Hlin: Destination rectangle out of bounds!");

    dest = VL_LockSurface(screenBuffer);
    if(dest == NULL) return;

    memset(dest + ylookup[y] + x, color, width);

    VL_UnlockSurface(screenBuffer);
}


/*
=================
=
= VL_Vlin
=
=================
*/

void VL_Vlin (int x, int y, int height, int color)
{
	byte *dest;

	assert(x >= 0 && x < screenWidth
			&& y >= 0 && y + height <= screenHeight
			&& "VL_Vlin: Destination rectangle out of bounds!");

	dest = VL_LockSurface(screenBuffer);
	if(dest == NULL) return;

	dest += ylookup[y] + x;

	while (height--)
	{
		*dest = color;
		dest += bufferPitch;
	}

	VL_UnlockSurface(screenBuffer);
}


/*
=================
=
= VL_Bar
=
=================
*/

void VL_Bar (int x, int y, int width, int height, int color)
{
    VL_BarScaledCoord(scaleFactor*x, scaleFactor*y,scaleFactor*width, scaleFactor*height, color);
}

void VL_BarScaledCoord (int scx, int scy, int scwidth, int scheight, int color)
{
	byte *dest;

	assert(scx >= 0 && scx + scwidth <= screenWidth
			&& scy >= 0 && scy + scheight <= screenHeight
			&& "VL_BarScaledCoord: Destination rectangle out of bounds!");

	dest = VL_LockSurface(screenBuffer);
	if(dest == NULL) return;

	dest += ylookup[scy] + scx;

	while (scheight--)
	{
		memset(dest, color, scwidth);
		dest += bufferPitch;
	}
	VL_UnlockSurface(screenBuffer);
}

/*
============================================================================

							MEMORY OPS

============================================================================
*/


/*
===================
=
= VL_DePlaneVGA
=
= Unweave a VGA graphic to simplify drawing
=
===================
*/

void VL_DePlaneVGA (byte *source, int width, int height)
{
    int  x,y,plane;
    word size,pwidth;
    byte *temp,*dest,*srcline;

    size = width * height;

    if (width & 3)
        Quit ("DePlaneVGA: width not divisible by 4!");

    temp = SafeMalloc(size);

//
// munge pic into the temp buffer
//
    srcline = source;
    pwidth = width >> 2;

    for (plane = 0; plane < 4; plane++)
    {
        dest = temp;

        for (y = 0; y < height; y++)
        {
            for (x = 0; x < pwidth; x++)
                *(dest + (x << 2) + plane) = *srcline++;

            dest += width;
        }
    }

//
// copy the temp buffer back into the original source
//
    memcpy (source,temp,size);

    free (temp);
}


/*
=================
=
= VL_MemToScreenScaledCoord
=
= Draws a block of data to the screen with scaling according to scaleFactor.
=
=================
*/

void VL_MemToScreen (byte *source, int width, int height, int x, int y)
{
    VL_MemToScreenScaledCoord(source, width, height, scaleFactor*x, scaleFactor*y);
}

void VL_MemToScreenScaledCoord (byte *source, int width, int height, int destx, int desty)
{
    byte *dest;
    int i, j, sci, scj;
    int m, n;

    assert(destx >= 0 && destx + width * scaleFactor <= screenWidth
            && desty >= 0 && desty + height * scaleFactor <= screenHeight
            && "VL_MemToScreenScaledCoord: Destination rectangle out of bounds!");

    dest = VL_LockSurface(screenBuffer);
    if(dest == NULL) return;

    for(j = 0, scj = 0; j < height; j++, scj += scaleFactor)
    {
        for(i = 0, sci = 0; i < width; i++, sci += scaleFactor)
        {
            byte col = source[(j * width) + i];
            for(m = 0; m < scaleFactor; m++)
            {
                for(n = 0; n < scaleFactor; n++)
                {
                    dest[ylookup[scj + m + desty] + sci + n + destx] = col;
                }
            }
        }
    }
    VL_UnlockSurface(screenBuffer);
}

/*
=================
=
= VL_MemToScreenScaledCoord
=
= Draws a part of a block of data to the screen.
= The block has the size origwidth*origheight.
= The part at (srcx, srcy) has the size width*height
= and will be painted to (destx, desty) with scaling according to scaleFactor.
=
=================
*/

void VL_MemToScreenScaledCoord2 (byte *source, int origwidth, int srcx, int srcy,
                                int destx, int desty, int width, int height)
{
    byte *dest;
    int i, j, sci, scj;
    int m, n;

    assert(destx >= 0 && destx + width * scaleFactor <= screenWidth
            && desty >= 0 && desty + height * scaleFactor <= screenHeight
            && "VL_MemToScreenScaledCoord: Destination rectangle out of bounds!");

    dest = VL_LockSurface(screenBuffer);
    if(dest == NULL) return;

    for(j = 0, scj = 0; j < height; j++, scj += scaleFactor)
    {
        for(i = 0, sci = 0; i < width; i++, sci += scaleFactor)
        {
            byte col = source[((j + srcy) * origwidth) + (i + srcx)];

            for(m = 0; m < scaleFactor; m++)
            {
                for(n = 0; n < scaleFactor; n++)
                {
                    dest[ylookup[scj + m + desty] + sci + n + destx] = col;
                }
            }
        }
    }
    VL_UnlockSurface(screenBuffer);
}

//==========================================================================

/*
=================
=
= VL_ScreenToScreen
=
=================
*/

void VL_ScreenToScreen (SDL_Surface *source, SDL_Surface *dest)
{
    SDL_BlitSurface(source, NULL, dest, NULL);
}
