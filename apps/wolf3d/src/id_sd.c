//
//      ID Engine
//      ID_SD.c - Sound Manager for Wolfenstein 3D
//      v1.2
//      By Jason Blochowiak
//
//      ARM-DOS version (2026): back on the real hardware, the way the 1992
//      WOLF3D.EXE did it (ID_SD.C + ID_SD_A.ASM of id's source release),
//      replacing Wolf4SDL's SDL_mixer / emulated-OPL layer:
//
//        * INT 08h is hooked and PIT channel 0 reprogrammed: 140 Hz (the
//          "slow" service: PC speaker / AdLib sound effects at 140 Hz), or
//          700 Hz while AdLib music plays (the "fast" service: one IMF
//          register/delay step per 700 Hz tick, effects every 5th tick).
//          The BIOS tick is chained whenever 65536 PIT counts have passed,
//          so the DOS clock keeps time; otherwise the handler sends EOI.
//        * PC speaker effects (AUDIOT's PC sounds): one byte per 1/140 s,
//          0 = silence, else PIT channel 2 divisor = byte * 60; speaker and
//          gate through port 0x61.
//        * AdLib: OPL2 register writes through ports 0x388/0x389 with the
//          original's status-read delays; detected with the classic timer
//          test. If the machine has no OPL the detection fails and the game
//          offers PC speaker sound only - as on a 1992 PC without a card.
//
//        * Sound Blaster: the digitized sounds of VSWAP (guards' shouts, doors,
//          guns) through the DSP + 8237 DMA + IRQ via the SDK's sb.h, one at a
//          time, stereo position through the SB Pro mixer - see below.
//

#include "wl_def.h"
#include <armdos.h>
#include <sb.h>

#define outb(p, v) armdos_outb((p), (uint8_t)(v))
#define inb(p)     armdos_inb(p)

#define PIT_CLOCK  1193182u

globalsoundpos channelSoundPos[MIX_CHANNELS];

//      Global variables
        boolean         AdLibPresent,
                        SoundBlasterPresent,SBProPresent,
                        SoundPositioned;
        byte            SoundMode;
        byte            MusicMode;
        byte            DigiMode;
static  byte          **SoundTable;
        int             DigiMap[LASTSOUND];
        int             DigiChannel[STARTMUSIC - STARTDIGISOUNDS];

//      Internal variables
static  boolean                 SD_Started;
static  boolean                 nextsoundpos;
static  volatile int            SoundNumber;
static  int                     DigiNumber;
static  volatile word           SoundPriority;
static  word                    DigiPriority;
static  int                     LeftPosition;
static  int                     RightPosition;

        word                    NumDigi;
        digiinfo                *DigiList;
static  volatile boolean        DigiPlaying;

//      Timer variables (shared with the INT 08h handler)
static  armdos_vect_t           t0OldService;
static  boolean                 t0Installed;
static  volatile unsigned       TimerRate;          // interrupts per second
static  volatile unsigned       TimerDivisor;       // PIT counts per interrupt
static  unsigned                TimerCount;         // for chaining the BIOS
static  volatile uint32_t       PitCountLo, PitCountHi;  // PIT counts since start
static  unsigned                count_fx;

//      PC Sound variables
static  volatile byte           pcLastSample;
static  byte * volatile         pcSound;
static  volatile longword       pcLengthLeft;

//      AdLib variables
static  byte * volatile         alSound;
static  volatile byte           alBlock;
static  volatile longword       alLengthLeft;
static  volatile longword       alTimeCount;
static  Instrument              alZeroInst;

//      Sequencer variables
static  volatile boolean        sqActive;
static  word                   *sqHack;
static  word * volatile         sqHackPtr;
static  volatile int            sqHackLen;
static  int                     sqHackSeqLen;
static  volatile longword       sqHackTime;

static void SDL_SetTimerSpeed(void);
static void SDL_DigitizedDone(void);

// ------------------------------------------------------------------ AdLib ---

static inline byte readstat(void)
{
    return inb(0x388);
}

void alOut(byte n, byte b)
{
    int i;
    uint32_t cpsr;

    // pushf / cli
    __asm__ volatile ("mrs %0, cpsr\n\torr r12, %0, #0x80\n\tmsr cpsr_c, r12"
                      : "=r"(cpsr) : : "r12", "memory");
    outb(0x388, n);
    for (i = 0; i < 6; i++)             // 3.3 us after the address
        (void)inb(0x388);
    outb(0x389, b);
    // popf
    __asm__ volatile ("msr cpsr_c, %0" : : "r"(cpsr) : "memory");
    for (i = 0; i < 35; i++)            // 23 us after the data
        (void)inb(0x388);
}

static boolean SDL_DetectAdLib(void)
{
    byte status1, status2;
    int  i;

    alOut(4, 0x60);     // Reset T1 & T2
    alOut(4, 0x80);     // Reset IRQ
    status1 = readstat();
    alOut(2, 0xff);     // Set timer 1
    alOut(4, 0x21);     // Start timer 1
    // wait > 80 us (the original: 100 reads of port 0x388)
    for (i = 0; i < 100; i++)
        (void)inb(0x388);
    {
        // ... and, as the emulated ISA bus may be faster than 1 us per
        // read, up to ~1 ms of PIT time on top of that
        uint32_t t0 = SDL_GetTicks();
        while (((readstat() & 0xe0) != 0xc0) && SDL_GetTicks() - t0 < 2)
            ;
    }
    status2 = readstat();
    alOut(4, 0x60);
    alOut(4, 0x80);

    if (((status1 & 0xe0) == 0x00) && ((status2 & 0xe0) == 0xc0))
    {
        for (i = 1; i <= 0xf5; i++)     // Zero all the registers
            alOut(i, 0);

        alOut(1, 0x20);     // Set WSE=1
        alOut(8, 0);        // Set CSM=0 & SEL=0

        return true;
    }
    return false;
}

// --------------------------------------------------- the timer interrupt ---

static void SDL_SoundFinished(void)
{
    SoundNumber = 0;
    SoundPriority = 0;
}

// DOFX of ID_SD_A.ASM: the next byte of the PC speaker and AdLib effects.
static void SDL_DoFX(void)
{
    byte *p = pcSound;

    if (p)
    {
        byte s = *p;

        pcSound = p + 1;
        if (s != pcLastSample)
        {
            pcLastSample = s;
            if (s)
            {
                unsigned t = s * 60;            // pcSoundLookup[s]
                outb(pcTAccess, 0xb6);          // channel 2 (speaker) timer
                outb(pcTimer, t & 0xff);
                outb(pcTimer, t >> 8);
                outb(pcSpeaker, inb(pcSpeaker) | 3);        // speaker & gate on
            }
            else
                outb(pcSpeaker, inb(pcSpeaker) & 0xfc);     // off
        }
        if (--pcLengthLeft == 0)
        {
            pcSound = 0;
            SoundNumber = 0;
            SoundPriority = 0;
            outb(pcSpeaker, inb(pcSpeaker) & 0xfd);         // speaker off
        }
    }

    p = alSound;
    if (p)
    {
        byte s = *p;

        if (s)
        {
            alOut(alFreqL, s);
            alOut(alFreqH, alBlock);
        }
        else
            alOut(alFreqH, 0);
        alSound = p + 1;
        if (--alLengthLeft == 0)
        {
            alSound = 0;
            SoundNumber = 0;
            SoundPriority = 0;
            alOut(alFreqH, 0);
        }
    }
}

// The IMF sequencer: register/value pairs with delays in 1/700 s units.
static void SDL_Sequence(void)
{
    if (sqHackLen)
    {
        word *ptr = sqHackPtr;

        while (sqHackTime <= alTimeCount)
        {
            word regval = ptr[0];
            sqHackTime = alTimeCount + ptr[1];
            alOut(regval & 0xff, regval >> 8);
            ptr += 2;
            sqHackLen -= 4;
            if (sqHackLen <= 0)
            {
                sqHackLen = 0;
                break;
            }
        }
        sqHackPtr = ptr;
    }
    alTimeCount++;
    if (!sqHackLen)
    {
        sqHackPtr = sqHack;
        sqHackLen = sqHackSeqLen;
        alTimeCount = 0;
        sqHackTime = 0;
    }
}

static void SDL_t0Service(struct armregs *f)
{
    uint32_t lo;

    // the PIT counts that have elapsed (SDL_GetTicks)
    lo = PitCountLo + TimerDivisor;
    if (lo < PitCountLo)
        PitCountHi++;
    PitCountLo = lo;

    if (TimerRate == TickBase * 10)
    {
        // SDL_t0FastAsmService: 700 Hz
        if (++count_fx >= 5)
        {
            count_fx = 0;
            SDL_DoFX();
        }
        if (sqActive)
            SDL_Sequence();
    }
    else
    {
        // SDL_t0SlowAsmService: 140 Hz
        SDL_DoFX();
    }

    if (DigiPlaying && !sb_busy())
        SDL_DigitizedDone();

    // COMMONEND: chain the BIOS every 65536 PIT counts, else acknowledge
    TimerCount += TimerDivisor;
    if (TimerCount >= 0x10000)
    {
        TimerCount -= 0x10000;
        if (t0OldService)
        {
            t0OldService(f);
            return;
        }
    }
    outb(0x20, 0x20);
}

static void SDL_SetTimer0(unsigned speed)
{
    armdos_disable();
    outb(0x43, 0x36);                   // Change timer 0
    outb(0x40, speed & 0xff);
    outb(0x40, (speed >> 8) & 0xff);
    TimerDivisor = speed ? speed : 0x10000;
    armdos_enable();
}

static void SDL_SetIntsPerSec(unsigned ints)
{
    TimerRate = ints;
    SDL_SetTimer0((PIT_CLOCK + ints / 2) / ints);
}

static void SDL_SetTimerSpeed(void)
{
    unsigned rate;

    if (MusicMode == smm_AdLib)
        rate = TickBase * 10;           // 700 Hz
    else
        rate = TickBase * 2;            // 140 Hz

    if (rate != TimerRate)
        SDL_SetIntsPerSec(rate);
}

// VL_WaitVBL sleeps in WFI only when the timer interrupt is there to wake it
int SD_TimerRunning(void)
{
    return t0Installed;
}

Uint32 SDL_GetTicks(void)
{
    uint32_t lo, hi;
    uint64_t counts;

    if (!t0Installed)
    {
        // before SD_Startup: the BIOS tick (54.9 ms)
        uint32_t t = *(volatile uint32_t *)0x46c;
        return (Uint32)(((uint64_t)t * 65536u * 1000u) / PIT_CLOCK);
    }
    do
    {
        hi = PitCountHi;
        lo = PitCountLo;
    } while (hi != PitCountHi);
    counts = ((uint64_t)hi << 32) | lo;
    return (Uint32)((counts * 1000u) / PIT_CLOCK);
}

void SDL_Delay(Uint32 ms)
{
    Uint32 start = SDL_GetTicks();

    // Sleep in WFI until the time has passed: the next interrupt (at most
    // 1/140 s away) wakes us up.
    while (SDL_GetTicks() - start < ms)
    {
        if (t0Installed)
            armdos_halt();
    }
}

void Delay (int32_t wolfticks)
{
    if (wolfticks > 0)
        SDL_Delay ((wolfticks * 100) / 7);
}

// ------------------------------------------------------------ PC speaker ---

static void SDL_PCPlaySound(PCSound *sound)
{
    armdos_disable();
    pcLastSample = (byte)-1;
    pcLengthLeft = sound->common.length;
    pcSound = sound->data;
    armdos_enable();
}

static void SDL_PCStopSound(void)
{
    armdos_disable();
    pcSound = 0;
    outb(pcSpeaker, inb(pcSpeaker) & 0xfd);     // ~2
    armdos_enable();
}

static void SDL_ShutPC(void)
{
    armdos_disable();
    pcSound = 0;
    outb(pcSpeaker, inb(pcSpeaker) & 0xfc);     // ~3
    armdos_enable();
}

// ------------------------------------------------------------- digitized ---
//
// Sound Blaster digitized sounds (the VSWAP sound pages, 8-bit unsigned at
// 7042 Hz: WOLF3D.EXE's time constant 256 - 1000000/7000), one at a time -
// a new one cuts the old one off, as on the original's single DMA channel.
// The card is driven through the SDK's sb.h (DSP, 8237 DMA, IRQ); the stereo
// position goes to the SB Pro mixer's voice volume register like
// SDL_PositionSBP did.
//

#define ORIGSAMPLERATE 7042

static byte sbpOldFMMix, sbpOldVOCMix;

static byte sbMixer(byte reg, int val)
{
    byte old;

    outb(sb_info.port + 4, reg);
    old = inb(sb_info.port + 5);
    if (val >= 0)
        outb(sb_info.port + 5, val);
    return old;
}

// SDL_StartSB: the SB Pro mixer check of WOLF3D.EXE, which also boosts the
// FM output to match the digitized sound (and puts it back on exit).
static void SDL_StartSB(void)
{
    sb_speaker(1);                      // DSP speaker on (D1h)

    SBProPresent = false;
    sbpOldFMMix = sbMixer(0x26, 0xbb);  // FM volume
    if (sbMixer(0x26, -1) == 0xbb)
    {
        sbMixer(0x26, 0xff);
        if (sbMixer(0x26, -1) == 0xff)
        {
            SBProPresent = true;
            sbpOldVOCMix = sbMixer(0x04, -1);   // voice volume
            sbMixer(0x0e, 0);           // SB Pro stereo DAC off
        }
    }
}

static void SDL_ShutSB(void)
{
    sb_stop();
    if (SBProPresent)
    {
        sbMixer(0x26, sbpOldFMMix);
        sbMixer(0x04, sbpOldVOCMix);
    }
}

static void SDL_PositionSBP(int leftpos, int rightpos)
{
    byte v;

    if (!SBProPresent)
        return;
    leftpos = 15 - leftpos;
    rightpos = 15 - rightpos;
    v = ((leftpos & 0x0f) << 4) | (rightpos & 0x0f);
    armdos_disable();
    outb(sb_info.port + 4, 0x04);       // mixer: voice volume
    outb(sb_info.port + 5, v);
    armdos_enable();
}

void SD_StopDigitized(void)
{
    DigiPlaying = false;
    DigiNumber = 0;
    DigiPriority = 0;
    SoundPositioned = false;
    if ((DigiMode == sds_PC) && (SoundMode == sdm_PC))
        SDL_SoundFinished();

    if (DigiMode == sds_SoundBlaster)
        sb_stop();
    channelSoundPos[0].valid = 0;
}

// called from the timer interrupt: the sound has played out
static void SDL_DigitizedDone(void)
{
    DigiPlaying = false;
    DigiNumber = 0;
    DigiPriority = 0;
    SoundPositioned = false;
    channelSoundPos[0].valid = 0;
}

int SD_GetChannelForDigi(int which)
{
    (void)which;
    return 0;                           // one voice
}

void SD_SetPosition(int channel, int leftpos, int rightpos)
{
    (void)channel;
    if((leftpos < 0) || (leftpos > 15) || (rightpos < 0) || (rightpos > 15)
            || ((leftpos == 15) && (rightpos == 15)))
        Quit("SD_SetPosition: Illegal position");

    if (DigiMode == sds_SoundBlaster)
        SDL_PositionSBP(leftpos, rightpos);
}

void SD_PrepareSound(int which)
{
    (void)which;                        // played straight from the page file
}

int SD_PlayDigitized(word which,int leftpos,int rightpos)
{
    byte *samples;

    if (!DigiMode)
        return 0;

    if (which >= NumDigi)
        Quit("SD_PlayDigitized: bad sound number %i", which);

    samples = PM_GetSoundPage(DigiList[which].startpage);
    if (samples + DigiList[which].length > PM_GetPageEnd())
        Quit("SD_PlayDigitized(%i): Sound reaches out of page file!\n", which);

    SD_SetPosition(0, leftpos, rightpos);
    DigiPlaying = false;
    if (sb_play_pcm(samples, DigiList[which].length, ORIGSAMPLERATE, SB_8BIT))
        DigiPlaying = true;

    return 0;                           // channel 0
}

void SD_SetDigiDevice(byte mode)
{
    if (mode == DigiMode)
        return;

    SD_StopDigitized();

    if (mode == sds_SoundBlaster && !SoundBlasterPresent)
        return;
    DigiMode = mode;
}

static void SDL_SetupDigi(void)
{
    // Correct padding enforced by PM_Startup()
    word *soundInfoPage = (word *) (void *) PM_GetPage(ChunksInFile-1);
    NumDigi = (word) PM_GetPageSize(ChunksInFile - 1) / 4;

    DigiList = SafeMalloc(NumDigi * sizeof(*DigiList));
    int i,page;
    for(i = 0; i < NumDigi; i++)
    {
        // Calculate the size of the digi from the sizes of the pages between
        // the start page and the start page of the next sound

        DigiList[i].startpage = soundInfoPage[i * 2];
        if((int) DigiList[i].startpage >= ChunksInFile - 1)
        {
            NumDigi = i;
            break;
        }

        int lastPage;
        if(i < NumDigi - 1)
        {
            lastPage = soundInfoPage[i * 2 + 2];
            if(lastPage == 0 || lastPage + PMSoundStart > ChunksInFile - 1) lastPage = ChunksInFile - 1;
            else lastPage += PMSoundStart;
        }
        else lastPage = ChunksInFile - 1;

        int size = 0;
        for(page = PMSoundStart + DigiList[i].startpage; page < lastPage; page++)
            size += PM_GetPageSize(page);

        // Don't include padding of sound info page, if padding was added
        if(lastPage == ChunksInFile - 1 && PMSoundInfoPagePadded) size--;

        // Patch lower 16-bit of size with size from sound info page.
        // The original VSWAP contains padding which is included in the page size,
        // but not included in the 16-bit size. So we use the more precise value.
        if((size & 0xffff0000) != 0 && (size & 0xffff) < soundInfoPage[i * 2 + 1])
            size -= 0x10000;
        size = (size & 0xffff0000) | soundInfoPage[i * 2 + 1];

        DigiList[i].length = size;
    }

    for(i = 0; i < LASTSOUND; i++)
        DigiMap[i] = -1;
    for(i = 0; i < STARTMUSIC - STARTDIGISOUNDS; i++)
        DigiChannel[i] = -1;
}

// ------------------------------------------------------------ AdLib FX ---

static void SDL_ALStopSound(void)
{
    alSound = 0;
    alOut(alFreqH + 0, 0);
}

static void SDL_AlSetFXInst(Instrument *inst)
{
    byte c,m;

    m = 0;      // modulator cell for channel 0
    c = 3;      // carrier cell for channel 0
    alOut(m + alChar,inst->mChar);
    alOut(m + alScale,inst->mScale);
    alOut(m + alAttack,inst->mAttack);
    alOut(m + alSus,inst->mSus);
    alOut(m + alWave,inst->mWave);
    alOut(c + alChar,inst->cChar);
    alOut(c + alScale,inst->cScale);
    alOut(c + alAttack,inst->cAttack);
    alOut(c + alSus,inst->cSus);
    alOut(c + alWave,inst->cWave);

    // Note: Switch commenting on these lines for old MUSE compatibility
//    alOutInIRQ(alFeedCon,inst->nConn);
    alOut(alFeedCon,0);
}

static void SDL_ALPlaySound(AdLibSound *sound)
{
    Instrument      *inst;
    byte            *data;

    armdos_disable();
    SDL_ALStopSound();

    alLengthLeft = sound->common.length;
    data = sound->data;
    alBlock = ((sound->block & 7) << 2) | 0x20;
    inst = &sound->inst;

    if (!(inst->mSus | inst->cSus))
    {
        armdos_enable();
        Quit("SDL_ALPlaySound() - Bad instrument");
    }

    SDL_AlSetFXInst(inst);
    alSound = (byte *)data;
    armdos_enable();
}

static void SDL_ShutAL(void)
{
    armdos_disable();
    alSound = 0;
    alOut(alEffects,0);
    alOut(alFreqH + 0,0);
    SDL_AlSetFXInst(&alZeroInst);
    armdos_enable();
}

static void SDL_StartAL(void)
{
    alOut(alEffects, 0);
    SDL_AlSetFXInst(&alZeroInst);
}

static void SDL_ShutDevice(void)
{
    switch (SoundMode)
    {
        case sdm_PC:
            SDL_ShutPC();
            break;
        case sdm_AdLib:
            SDL_ShutAL();
            break;
        default:
            break;
    }
    SoundMode = sdm_Off;
}

static void SDL_StartDevice(void)
{
    switch (SoundMode)
    {
        case sdm_AdLib:
            SDL_StartAL();
            break;
        default:
            break;
    }
    SoundNumber = 0;
    SoundPriority = 0;
}

//      Public routines

boolean SD_SetSoundMode(byte mode)
{
    boolean result = false;
    word    tableoffset;

    SD_StopSound();

    if ((mode == sdm_AdLib) && !AdLibPresent)
        mode = sdm_PC;

    switch (mode)
    {
        case sdm_Off:
            tableoffset = STARTADLIBSOUNDS;
            result = true;
            break;
        case sdm_PC:
            tableoffset = STARTPCSOUNDS;
            result = true;
            break;
        case sdm_AdLib:
            tableoffset = STARTADLIBSOUNDS;
            if (AdLibPresent)
                result = true;
            break;
        default:
            Quit("SD_SetSoundMode: Invalid sound mode %i", mode);
            return false;
    }
    SoundTable = &audiosegs[tableoffset];

    if (result && (mode != SoundMode))
    {
        SDL_ShutDevice();
        SoundMode = mode;
        SDL_StartDevice();
    }

    SDL_SetTimerSpeed();

    return(result);
}

boolean SD_SetMusicMode(byte mode)
{
    boolean result = false;

    SD_FadeOutMusic();
    while (SD_MusicPlaying())
        SDL_Delay(5);

    switch (mode)
    {
        case smm_Off:
            result = true;
            break;
        case smm_AdLib:
            if (AdLibPresent)
                result = true;
            break;
    }

    if (result)
        MusicMode = mode;

    SDL_SetTimerSpeed();

    return(result);
}

void SD_Startup(void)
{
    if (SD_Started)
        return;

    // SDL_t0Service at 140 Hz until music needs 700 Hz
    t0OldService = armdos_getvect(0x08);
    TimerCount = 0;
    PitCountLo = PitCountHi = 0;
    TimerRate = 0;
    armdos_disable();
    armdos_setvect(0x08, SDL_t0Service);
    t0Installed = true;
    armdos_enable();
    alTimeCount = 0;

    SD_SetSoundMode(sdm_Off);
    SD_SetMusicMode(smm_Off);

    AdLibPresent = param_noal ? false : SDL_DetectAdLib();
    // the Sound Blaster: BLASTER variable (A220 I7 D1 H5 T6), DSP reset
    SoundBlasterPresent = param_nosb ? false : sb_detect(NULL);
    if (SoundBlasterPresent)
        SDL_StartSB();

    SDL_SetupDigi();

    SD_Started = true;
}

void SD_Shutdown(void)
{
    if (!SD_Started)
        return;

    SD_MusicOff();
    SD_StopSound();
    SDL_ShutDevice();
    if (SoundBlasterPresent)
        SDL_ShutSB();

    // restore the BIOS timer: divisor 0 (65536 = 18.2 Hz) and INT 08h
    armdos_disable();
    outb(0x43, 0x36);
    outb(0x40, 0);
    outb(0x40, 0);
    armdos_setvect(0x08, t0OldService);
    t0Installed = false;
    TimerRate = 0;
    outb(pcSpeaker, inb(pcSpeaker) & 0xfc);
    armdos_enable();

    free (DigiList);
    DigiList = NULL;

    SD_Started = false;
}

void SD_PositionSound(int leftvol,int rightvol)
{
    LeftPosition = leftvol;
    RightPosition = rightvol;
    nextsoundpos = true;
}

boolean SD_PlaySound(int sound)
{
    boolean         ispos;
    SoundCommon     *s;
    int             lp,rp;

    lp = LeftPosition;
    rp = RightPosition;
    LeftPosition = 0;
    RightPosition = 0;

    ispos = nextsoundpos;
    nextsoundpos = false;

    if (sound == -1 || (DigiMode == sds_Off && SoundMode == sdm_Off))
        return 0;

    s = (SoundCommon *) SoundTable[sound];

    if ((SoundMode != sdm_Off) && !s)
            Quit("SD_PlaySound() - Uncached sound");

    if ((DigiMode != sds_Off) && (DigiMap[sound] != -1))
    {
        int channel = SD_PlayDigitized(DigiMap[sound], lp, rp);
        SoundPositioned = ispos;
        DigiNumber = sound;
        DigiPriority = s->priority;
        return channel + 1;
    }

    if (SoundMode == sdm_Off)
        return 0;

    if (!s->length)
        Quit("SD_PlaySound() - Zero length sound");
    if (s->priority < SoundPriority)
        return 0;

    switch (SoundMode)
    {
        case sdm_PC:
            SDL_PCPlaySound((PCSound *)s);
            break;
        case sdm_AdLib:
            SDL_ALPlaySound((AdLibSound *)s);
            break;
        default:
            break;
    }

    SoundNumber = sound;
    SoundPriority = s->priority;

    return 0;
}

word SD_SoundPlaying(void)
{
    boolean result = false;

    switch (SoundMode)
    {
        case sdm_PC:
            result = pcSound? true : false;
            break;
        case sdm_AdLib:
            result = alSound? true : false;
            break;
        default:
            break;
    }

    if (result)
        return(SoundNumber);
    else
        return(false);
}

void SD_StopSound(void)
{
    if (DigiPlaying)
        SD_StopDigitized();

    switch (SoundMode)
    {
        case sdm_PC:
            SDL_PCStopSound();
            break;
        case sdm_AdLib:
            armdos_disable();
            SDL_ALStopSound();
            armdos_enable();
            break;
        default:
            break;
    }

    SoundPositioned = false;

    SDL_SoundFinished();
}

void SD_WaitSoundDone(void)
{
    while (SD_SoundPlaying())
        SDL_Delay(5);
}

void SD_MusicOn(void)
{
    sqActive = true;
}

int SD_MusicOff(void)
{
    word    i;

    sqActive = false;
    switch (MusicMode)
    {
        case smm_AdLib:
            armdos_disable();
            alOut(alEffects, 0);
            for (i = 0;i < sqMaxTracks;i++)
                alOut(alFreqH + i + 1, 0);
            armdos_enable();
            break;
        default:
            break;
    }

    return (int) (sqHackPtr-sqHack);
}

void SD_StartMusic(int chunk)
{
    SD_MusicOff();

    if (MusicMode == smm_AdLib)
    {
        int32_t chunkLen = CA_CacheAudioChunk(chunk);
        sqHack = (word *)(void *) audiosegs[chunk];     // alignment is correct
        if(*sqHack == 0) sqHackLen = sqHackSeqLen = chunkLen;
        else sqHackLen = sqHackSeqLen = *sqHack++;
        sqHackPtr = sqHack;
        sqHackTime = 0;
        alTimeCount = 0;
        SD_MusicOn();
    }
}

void SD_ContinueMusic(int chunk, int startoffs)
{
    int i;

    SD_MusicOff();

    if (MusicMode == smm_AdLib)
    {
        int32_t chunkLen = CA_CacheAudioChunk(chunk);
        word *ptr;
        sqHack = (word *)(void *) audiosegs[chunk];     // alignment is correct
        if(*sqHack == 0) sqHackLen = sqHackSeqLen = chunkLen;
        else sqHackLen = sqHackSeqLen = *sqHack++;
        ptr = sqHack;

        if(startoffs >= sqHackLen)
            startoffs = 0;

        // fast forward to correct position
        // (needed to reconstruct the instruments)

        for(i = 0; i < startoffs; i += 2)
        {
            byte reg = *(byte *)ptr;
            byte val = *(((byte *)ptr) + 1);
            if(reg >= 0xb1 && reg <= 0xb8) val &= 0xdf;           // disable play note flag
            else if(reg == 0xbd) val &= 0xe0;                     // disable drum flags

            alOut(reg,val);
            ptr += 2;
            sqHackLen -= 4;
        }
        sqHackPtr = ptr;
        sqHackTime = 0;
        alTimeCount = 0;

        SD_MusicOn();
    }
}

void SD_FadeOutMusic(void)
{
    switch (MusicMode)
    {
        case smm_AdLib:
            // DEBUG - quick hack to turn the music off
            SD_MusicOff();
            break;
        default:
            break;
    }
}

boolean SD_MusicPlaying(void)
{
    boolean result;

    switch (MusicMode)
    {
        case smm_AdLib:
            result = sqActive;
            break;
        default:
            result = false;
            break;
    }

    return(result);
}
