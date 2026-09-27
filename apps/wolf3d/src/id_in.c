//
//	ID Engine
//	ID_IN.c - Input Manager
//	v1.0d1
//	By Jason Blochowiak
//
//	ARM-DOS version (2026): the keyboard is read the way the original
//	WOLF3D.EXE read it (ID_IN.C, INL_KeyService): an INT 09h handler takes
//	the set-1 scan code from port 0x60, keeps Keyboard[] and LastScan up to
//	date, translates it to ASCII (for typing savegame names), and sends the
//	EOI; the BIOS never sees the keys while the game runs. The mouse is the
//	INT 33h driver's (if one is loaded). The joystick is the game port's
//	(ARMDOS joystick, below: the original's code).
//
//	Wolf4SDL's API is kept (IN_ProcessEvents & co.): as the interrupt does
//	the work, "processing events" is a no-op and waiting for one is WFI.
//

#include "wl_def.h"
#include <armdos.h>

/*
=============================================================================

					GLOBAL VARIABLES

=============================================================================
*/

//
// configuration variables
//
boolean MousePresent;
boolean forcegrabmouse;

// 	Global variables
volatile bool   Keyboard[sc_Last];
char            textinput[TEXTINPUTSIZE];
volatile boolean Paused;
volatile ScanCode LastScan;

int JoyNumButtons;

bool GrabInput = false;

/*
=============================================================================

					LOCAL VARIABLES

=============================================================================
*/

static	boolean		IN_Started;
static	boolean		CapsLock;
static	armdos_vect_t	OldKeyVect;

static	byte    DirTable[] =        // Quick lookup for total direction
{
    dir_NorthWest,	dir_North,	dir_NorthEast,
    dir_West,		dir_None,	dir_East,
    dir_SouthWest,	dir_South,	dir_SouthEast
};

static	const byte	ASCIINames[] =		// Unshifted ASCII for scan codes
{
//	 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
	0  ,27 ,'1','2','3','4','5','6','7','8','9','0','-','=',8  ,9  ,	// 0
	'q','w','e','r','t','y','u','i','o','p','[',']',13 ,0  ,'a','s',	// 1
	'd','f','g','h','j','k','l',';',39 ,'`',0  ,92 ,'z','x','c','v',	// 2
	'b','n','m',',','.','/',0  ,'*',0  ,' ',0  ,0  ,0  ,0  ,0  ,0  ,	// 3
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,'7','8','9','-','4','5','6','+','1',	// 4
	'2','3','0',127,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 5
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 6
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0		// 7
},
				ShiftNames[] =		// Shifted ASCII for scan codes
{
//	 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
	0  ,27 ,'!','@','#','$','%','^','&','*','(',')','_','+',8  ,9  ,	// 0
	'Q','W','E','R','T','Y','U','I','O','P','{','}',13 ,0  ,'A','S',	// 1
	'D','F','G','H','J','K','L',':',34 ,'~',0  ,'|','Z','X','C','V',	// 2
	'B','N','M','<','>','?',0  ,'*',0  ,' ',0  ,0  ,0  ,0  ,0  ,0  ,	// 3
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,'7','8','9','-','4','5','6','+','1',	// 4
	'2','3','0',127,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 5
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 6
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0   	// 7
};

///////////////////////////////////////////////////////////////////////////
//
//	INL_KeyService() - Handles a keyboard interrupt (key up/down)
//
///////////////////////////////////////////////////////////////////////////
static void INL_KeyService(struct armregs *f)
{
	static boolean special;
	static int pauseskip;
	byte k, c;

	(void)f;
	k = armdos_inb(0x60);	// Get the scan code

	if (pauseskip)
	{
		// the rest of the Pause sequence (E1 1D 45 E1 9D C5)
		pauseskip--;
	}
	else if (k == 0xe0)		// Special key prefix
		special = true;
	else if (k == 0xe1)	// Handle Pause key
	{
		Paused = true;
		pauseskip = 5;
	}
	else if (k == 0xfa || k == 0xfe || k == 0x00 || k == 0xff)
		special = false;	// controller replies / overrun
	else
	{
		if (special && (k & 0x7f) == 0x2a)
			;				// fake shifts around the grey keys
		else if (k & 0x80)	// Break code
		{
			k &= 0x7f;
			Keyboard[k] = false;
		}
		else			// Make code
		{
			LastScan = k;
			Keyboard[k] = true;

			if (special)
				c = (k == 0x1c) ? 13 : (k == 0x35) ? '/' : 0;
			else
			{
				if (k == sc_CapsLock)
					CapsLock ^= true;

				if (Keyboard[sc_LShift] || Keyboard[sc_RShift])	// If shifted
				{
					c = ShiftNames[k];
					if ((c >= 'A') && (c <= 'Z') && CapsLock)
						c += 'a' - 'A';
				}
				else
				{
					c = ASCIINames[k];
					if ((c >= 'a') && (c <= 'z') && CapsLock)
						c -= 'a' - 'A';
				}
			}
			// Wolf4SDL reads typed text from textinput[] (SDL_TEXTINPUT)
			if (c >= ' ' && c < 127)
			{
				size_t n = strlen(textinput);
				if (n < sizeof(textinput) - 1)
				{
					textinput[n] = c;
					textinput[n + 1] = 0;
				}
			}
		}

		special = false;
	}

	armdos_outb(0x20,0x20);
}

///////////////////////////////////////////////////////////////////////////
//
//	Mouse (INT 33h)
//
///////////////////////////////////////////////////////////////////////////
static int INL_MouseInt(int ax, int *bx, int *cx, int *dx)
{
    struct armregs r;

    memset(&r, 0, sizeof(r));
    r.r0 = ax;
    _armdos_int33(&r);
    if (bx) *bx = (int16_t)r.r1;
    if (cx) *cx = (int16_t)r.r2;
    if (dx) *dx = (int16_t)r.r3;
    return (int)(r.r0 & 0xffff);
}

static boolean INL_StartMouse(void)
{
    // no driver: the vector is empty (or AX comes back 0)
    if (armdos_getvect(0x33) == NULL)
        return false;
    return INL_MouseInt(0, NULL, NULL, NULL) == 0xffff;
}

Uint32 SDL_GetMouseState(int *x, int *y)
{
    int bx = 0, cx = 0, dx = 0;

    if (MousePresent)
        INL_MouseInt(3, &bx, &cx, &dx);
    if (x) *x = cx;
    if (y) *y = dx;
    // INT 33h: bit 0 left, bit 1 right, bit 2 middle -> SDL order
    return (bx & 1) | ((bx & 4) ? SDL_BUTTON(SDL_BUTTON_MIDDLE) : 0)
                    | ((bx & 2) ? SDL_BUTTON(SDL_BUTTON_RIGHT) : 0);
}

Uint32 SDL_GetRelativeMouseState(int *x, int *y)
{
    int cx = 0, dx = 0;

    if (MousePresent)
        INL_MouseInt(0x0b, NULL, &cx, &dx);   // motion counters (mickeys)
    if (x) *x = cx;
    if (y) *y = dx;
    return SDL_GetMouseState(NULL, NULL);
}

static int INL_GetMouseButtons (void)
{
    int buttons = SDL_GetMouseState(NULL,NULL);
    int middlePressed = buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE);
    int rightPressed = buttons & SDL_BUTTON(SDL_BUTTON_RIGHT);

    buttons &= ~(SDL_BUTTON(SDL_BUTTON_MIDDLE) | SDL_BUTTON(SDL_BUTTON_RIGHT));

    if (middlePressed)
        buttons |= 1 << 2;
    if (rightPressed)
        buttons |= 1 << 1;

    return buttons;
}

///////////////////////////////////////////////////////////////////////////
//
//	ARMDOS joystick: the original's game port code (ID_IN.C v1.0d1), on the
//	ARM-PC's game adapter at 201h. IN_GetJoyAbs counts polls of 201h with
//	interrupts off until the axis one-shots drop, as the original's asm did
//	(each IN takes an ISA cycle, so the counts are the ones a PC gave);
//	INL_StartJoy auto-configures a stick found centred at startup, and the
//	control panel's "Joystick Enabled" runs CalibrateJoystick (wl_menu.c).
//
///////////////////////////////////////////////////////////////////////////
#define	JoyScaleMax		32768
#define	JoyScaleShift	8
#define	MaxJoyValue		5000

typedef	struct
{
	word	joyMinX,joyMinY,
			threshMinX,threshMinY,
			threshMaxX,threshMaxY,
			joyMaxX,joyMaxY,
			joyMultXL,joyMultYL,
			joyMultXH,joyMultYH;
} JoystickDef;

static	JoystickDef	JoyDefs[MaxJoys];

static inline uint32_t armdos_irq_save(void)	// pushf / cli
{
	uint32_t o, t;
	__asm__ volatile("mrs %0, cpsr\n\torr %1, %0, #0x80\n\tmsr cpsr_c, %1" : "=r"(o), "=r"(t) :: "memory");
	return o;
}
static inline void armdos_irq_restore(uint32_t o)	// popf
{
	__asm__ volatile("msr cpsr_c, %0" :: "r"(o) : "memory");
}
boolean	JoysPresent[MaxJoys];
int		joystickport;			// ARMDOS joystick: 0 = port 1, 1 = port 2 (CONFIG.WL1)

//
//	IN_GetJoyAbs() - Reads the absolute position of the specified joystick
//
void IN_GetJoyAbs(word joy,word *xp,word *yp)
{
	byte	xb,yb,xs,ys,al;
	word	x,y,bp;
	volatile byte *port = (volatile byte *)(0x10000000 + 0x201);

	xs = joy? 2 : 0;		// Find shift value for x axis
	xb = 1 << xs;			// Use shift value to get x bit mask
	ys = joy? 3 : 1;		// Do the same for y axis
	yb = 1 << ys;

	uint32_t ps = armdos_irq_save();	// cli: make sure an interrupt doesn't screw the timings
	al = *port;
	*port = al;				// Clear the resistors
	x = y = 0;
	bp = MaxJoyValue;
	for (;;)
	{
		al = *port;			// Get bits indicating whether all are finished
		if (--bp == 0)		// We have a silly value - abort
			break;
		x += al & xb;		// Possibly increment count register
		y += al & yb;
		if (!(al & (xb | yb)))	// If both bits were 0, drop out
			break;
	}
	armdos_irq_restore(ps);

	*xp = x >> xs;
	*yp = y >> ys;
}

//
//	INL_GetJoyDelta() - Returns the relative movement of the specified
//		joystick (from +/-127)
//
static void INL_GetJoyDelta(word joy,int *dx,int *dy)
{
	word		x,y;
	JoystickDef	*def;

	IN_GetJoyAbs(joy,&x,&y);
	def = JoyDefs + joy;

	if (x < def->threshMinX)
	{
		if (x < def->joyMinX)
			x = def->joyMinX;

		x = -(x - def->threshMinX);
		x *= def->joyMultXL;
		x >>= JoyScaleShift;
		*dx = (x > 127)? -127 : -x;
	}
	else if (x > def->threshMaxX)
	{
		if (x > def->joyMaxX)
			x = def->joyMaxX;

		x = x - def->threshMaxX;
		x *= def->joyMultXH;
		x >>= JoyScaleShift;
		*dx = (x > 127)? 127 : x;
	}
	else
		*dx = 0;

	if (y < def->threshMinY)
	{
		if (y < def->joyMinY)
			y = def->joyMinY;

		y = -(y - def->threshMinY);
		y *= def->joyMultYL;
		y >>= JoyScaleShift;
		*dy = (y > 127)? -127 : -y;
	}
	else if (y > def->threshMaxY)
	{
		if (y > def->joyMaxY)
			y = def->joyMaxY;

		y = y - def->threshMaxY;
		y *= def->joyMultYH;
		y >>= JoyScaleShift;
		*dy = (y > 127)? 127 : y;
	}
	else
		*dy = 0;
}

//
//	INL_GetJoyButtons() - Returns the button status of the specified joystick
//
static word INL_GetJoyButtons(word joy)
{
	word	result;

	result = armdos_inb(0x201);	// Get all the joystick buttons
	result >>= joy? 6 : 4;	// Shift into bits 0-1
	result &= 3;				// Mask off the useless bits
	result ^= 3;
	return(result);
}

//
//	IN_GetJoyButtonsDB() - Returns the de-bounced button status of the
//		specified joystick
//
word IN_GetJoyButtonsDB(word joy)
{
	word	result1,result2;

	do
	{
		result1 = INL_GetJoyButtons(joy);
		SDL_Delay(1);
		result2 = INL_GetJoyButtons(joy);
	} while (result1 != result2);
	return(result1);
}

//
//	INL_SetJoyScale() - Sets up scaling values for the specified joystick
//
static void INL_SetJoyScale(word joy)
{
	JoystickDef	*def;

	def = &JoyDefs[joy];
	def->joyMultXL = JoyScaleMax / (def->threshMinX - def->joyMinX);
	def->joyMultXH = JoyScaleMax / (def->joyMaxX - def->threshMaxX);
	def->joyMultYL = JoyScaleMax / (def->threshMinY - def->joyMinY);
	def->joyMultYH = JoyScaleMax / (def->joyMaxY - def->threshMaxY);
}

//
//	IN_SetupJoy() - Sets up thresholding values and calls INL_SetJoyScale()
//		to set up scaling values
//
void IN_SetupJoy(word joy,word minx,word maxx,word miny,word maxy)
{
	word		d,r;
	JoystickDef	*def;

	def = &JoyDefs[joy];

	def->joyMinX = minx;
	def->joyMaxX = maxx;
	r = maxx - minx;
	d = r / 3;
	def->threshMinX = ((r / 2) - d) + minx;
	def->threshMaxX = ((r / 2) + d) + minx;

	def->joyMinY = miny;
	def->joyMaxY = maxy;
	r = maxy - miny;
	d = r / 3;
	def->threshMinY = ((r / 2) - d) + miny;
	def->threshMaxY = ((r / 2) + d) + miny;

	INL_SetJoyScale(joy);
}

//
//	INL_StartJoy() - Detects & auto-configures the specified joystick
//					The auto-config assumes the joystick is centered
//
static boolean INL_StartJoy(word joy)
{
	word		x,y;

	IN_GetJoyAbs(joy,&x,&y);

	if
	(
		((x == 0) || (x > MaxJoyValue - 10))
	||	((y == 0) || (y > MaxJoyValue - 10))
	)
		return(false);
	else
	{
		IN_SetupJoy(joy,0,x * 2,0,y * 2);
		return(true);
	}
}

void IN_GetJoyDelta (int *dx, int *dy)
{
    INL_GetJoyDelta (joystickport, dx, dy);
}

void IN_GetJoyFineDelta (int *dx, int *dy)
{
    INL_GetJoyDelta (joystickport, dx, dy);
}

//
//	IN_JoyButtons - all four buttons, 1 = pressed (a Gravis-style pad has
//	its buttons 3 and 4 on the second stick's lines)
//
int IN_JoyButtons (void)
{
	unsigned joybits;

	joybits = armdos_inb(0x201);	// Get all the joystick buttons
	joybits >>= 4;				// only the high bits are useful
	joybits ^= 15;				// return with 1=pressed

	return joybits;
}

boolean IN_JoyPresent (void)
{
    return JoysPresent[joystickport];
}

void IN_CenterMouse (void)
{
    // the motion counters are relative; just clear them
    if (MousePresent)
        SDL_GetRelativeMouseState(NULL, NULL);
}

/*
=============================================================================

                          INPUT PROCESSING

=============================================================================
*/

void IN_ProcessEvents (void)
{
    // INT 09h does the work
}

void IN_WaitAndProcessEvents (void)
{
    // sleep until the next interrupt (a key, or the timer)
    armdos_halt();
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_Startup() - Starts up the Input Mgr
//
///////////////////////////////////////////////////////////////////////////
void IN_Startup(void)
{
	if (IN_Started)
		return;

    IN_ClearKeysDown();
    IN_ClearTextInput();

    OldKeyVect = armdos_getvect(0x09);
    armdos_disable();
    armdos_setvect(0x09, INL_KeyService);
    armdos_enable();

    MousePresent = param_nomouse ? false : INL_StartMouse();
    GrabInput = MousePresent;

    // ARMDOS joystick: detect both sticks (NOJOYS on the command line skips it, as in the original)
    for (int i = 0; i < MaxJoys; i++)
        JoysPresent[i] = param_nojoys ? false : INL_StartJoy(i);
    if (!JoysPresent[0] && JoysPresent[1])
        joystickport = 1;
    JoyNumButtons = 4;

    IN_Started = true;
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_Shutdown() - Shuts down the Input Mgr
//
///////////////////////////////////////////////////////////////////////////
void IN_Shutdown(void)
{
	if (!IN_Started)
		return;

    armdos_disable();
    armdos_setvect(0x09, OldKeyVect);
    armdos_enable();
    // Clear ctrl/alt/shift flags: we ate the break codes
    ARMDOS_BDA[0x17] &= 0xf0;
    ARMDOS_BDA[0x18] &= 0xfa;

	IN_Started = false;
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_ClearKeysDown() - Clears the keyboard array
//
///////////////////////////////////////////////////////////////////////////
void IN_ClearKeysDown(void)
{
	LastScan = sc_None;

	memset ((void *)Keyboard,0,sizeof(Keyboard));
}


void IN_ClearTextInput (void)
{
    armdos_disable();
    memset (textinput,0,sizeof(textinput));
    armdos_enable();
}


///////////////////////////////////////////////////////////////////////////
//
//	IN_ReadControl() - Reads the device associated with the specified
//		player and fills in the control info struct
//
///////////////////////////////////////////////////////////////////////////
void IN_ReadControl (ControlInfo *info)
{
	word buttons;
	int  dx,dy;
	int  mx,my;

	dx = dy = 0;
	mx = my = 0;
	buttons = 0;

	IN_ProcessEvents();

    if (Keyboard[sc_Home])
    {
        mx = -1;
        my = -1;
    }
    else if (Keyboard[sc_PgUp])
    {
        mx = 1;
        my = -1;
    }
    else if (Keyboard[sc_End])
    {
        mx = -1;
        my = 1;
    }
    else if (Keyboard[sc_PgDn])
    {
        mx = 1;
        my = 1;
    }

    if (Keyboard[sc_UpArrow])
        my = -1;
    else if (Keyboard[sc_DownArrow])
        my = 1;

    if (Keyboard[sc_LeftArrow])
        mx = -1;
    else if (Keyboard[sc_RightArrow])
        mx = 1;

	dx = mx * 127;
	dy = my * 127;

	info->x = dx;
	info->xaxis = mx;
	info->y = dy;
	info->yaxis = my;
	info->button0 = (buttons & 1) != 0;
	info->button1 = (buttons & (1 << 1)) != 0;
	info->button2 = (buttons & (1 << 2)) != 0;
	info->button3 = (buttons & (1 << 3)) != 0;
	info->dir = DirTable[((my + 1) * 3) + (mx + 1)];
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_WaitForKey() - Waits for a scan code, then clears LastScan and
//		returns the scan code
//
///////////////////////////////////////////////////////////////////////////
ScanCode IN_WaitForKey (void)
{
	ScanCode result;

	for (result = LastScan; !result; result = LastScan)
		IN_WaitAndProcessEvents();

	LastScan = 0;

	return result;
}


///////////////////////////////////////////////////////////////////////////
//
//	IN_Ack() - waits for a button or key press.  If a button is down, upon
// calling, it must be released for it to be recognized
//
///////////////////////////////////////////////////////////////////////////

boolean	btnstate[NUMBUTTONS];

void IN_StartAck (void)
{
    int i;

    IN_ProcessEvents();
//
// get initial state of everything
//
	IN_ClearKeysDown();
	memset (btnstate,0,sizeof(btnstate));

	int buttons = IN_JoyButtons() << 4;

	if (MousePresent)
		buttons |= IN_MouseButtons();

	for (i = 0; i < NUMBUTTONS; i++, buttons >>= 1)
    {
		if (buttons & 1)
			btnstate[i] = true;
    }
}


boolean IN_CheckAck (void)
{
    int i;

    IN_ProcessEvents();
//
// see if something has been pressed
//
	if (LastScan)
		return true;

	int buttons = IN_JoyButtons() << 4;

	if (MousePresent)
		buttons |= IN_MouseButtons();

	for (i = 0; i < NUMBUTTONS; i++, buttons >>= 1)
	{
		if (buttons & 1)
		{
			if (!btnstate[i])
            {
                // Wait until button has been released
                do
                {
                    IN_WaitAndProcessEvents();

                    buttons = IN_JoyButtons() << 4;

                    if (MousePresent)
                        buttons |= IN_MouseButtons();

                } while (buttons & (1 << i));

				return true;
            }
		}
		else
			btnstate[i] = false;
	}

	return false;
}


void IN_Ack (void)
{
	IN_StartAck ();

    do
    {
        IN_WaitAndProcessEvents ();

    } while (!IN_CheckAck());
}


///////////////////////////////////////////////////////////////////////////
//
//	IN_UserInput() - Waits for the specified delay time (in ticks) or the
//		user pressing a key or a mouse button. If the clear flag is set, it
//		then either clears the key or waits for the user to let the mouse
//		button up.
//
///////////////////////////////////////////////////////////////////////////
boolean IN_UserInput (longword delay)
{
	longword	lasttime;

	lasttime = GetTimeCount();
	IN_StartAck ();

	do
	{
        IN_ProcessEvents();

		if (IN_CheckAck())
			return true;

        SDL_Delay(5);

	} while (GetTimeCount() - lasttime < delay);

	return false;
}

//===========================================================================

int IN_MouseButtons (void)
{
	if (MousePresent)
		return INL_GetMouseButtons();
	else
		return 0;
}

bool IN_IsInputGrabbed (void)
{
    return GrabInput;
}
