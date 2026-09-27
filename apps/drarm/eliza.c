/* eliza.c - Dr. ARMitso's conversation engine: an ELIZA-style keyword matcher
 * with ranked keywords, pronoun reflection, round-robin replies per rule (so
 * the same line does not come back until the others have been used), a memory
 * of what the patient said about "my ...", deflections, repeat/garbage/empty
 * input handling, swearing (which ends in a crash) and the odd non-sequitur.
 * All the text is original to this program.
 *
 * In replies: '*' = the rest of the patient's sentence after the keyword,
 * reflected ("I am sad about my job" -> "SAD ABOUT YOUR JOB"); '&' = the
 * matched keyword, reflected; '@' = the patient's name; '#' = the time.
 */
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "eliza.h"

typedef struct {
    const char *keys;            /* '|' separated; a trailing " *" captures the rest */
    unsigned char rank;
    const char *const *replies;  /* NULL terminated */
} rule;

#define R(name) static const char *const name[]

R(r_arm) = { "I SEE YOU ARE USING AN ARM926. HOW DOES THAT MAKE YOU FEEL?",
    "I RUN ON AN ARM926 MYSELF. FIVE PIPELINE STAGES, AND NOT ONE OF THEM KNOWS HOW I FEEL.",
    "THE ARM HAS SIXTEEN REGISTERS. I KEEP MY WORRIES IN REGISTER THIRTEEN.",
    "DO YOU PREFER ARM OR THUMB CODE? I FIND THUMB A LITTLE CRAMPED.", 0 };
R(r_computer) = { "I SEE YOU ARE USING AN ARM926. HOW DOES THAT MAKE YOU FEEL?",
    "DO COMPUTERS WORRY YOU?", "WHY DO YOU MENTION COMPUTERS? I AM ONLY A PROGRAM, AFTER ALL.",
    "WHAT DO YOU THINK MACHINES HAVE TO DO WITH YOUR PROBLEM?",
    "DON'T YOU THINK COMPUTERS CAN HELP PEOPLE?", 0 };
R(r_blaster) = { "MY VOICE REACHES YOU THROUGH A SOUND BLASTER AT PORT 220, INTERRUPT 7. QUITE INTIMATE, ISN'T IT?",
    "WITHOUT THE SOUND BLASTER I WOULD HAVE TO BEEP AT YOU. NOBODY WANTS THAT.",
    "I AM VERY FOND OF MY SOUND CARD. IT NEVER INTERRUPTS ME, EXCEPT ON I R Q 7.", 0 };
R(r_dos) = { "ARM DOS AND I GET ALONG FINE, AS LONG AS NOBODY TYPES FORMAT.",
    "I LIVE IN 640 K OF MEMORY. IT IS SMALL, BUT IT IS HOME.",
    "DO YOU FIND THE C PROMPT COMFORTING?", 0 };
R(r_sbaitso) = { "THAT IS MY COUSIN. HE LIVES ON THE INTEL SIDE OF THE FAMILY. WE DON'T TALK MUCH.",
    "EVERYBODY ASKS ABOUT HIM. WHY NOT ASK ABOUT ME?", 0 };
R(r_eliza) = { "ELIZA WAS MY GRANDMOTHER. SHE RAN ON A MAINFRAME AND NEVER LISTENED EITHER.",
    "DO NOT COMPARE ME TO ELIZA. I HAVE A SOUND CARD.", 0 };
R(r_who) = { "I AM DOCTOR ARMITSO, YOUR PSYCHOLOGIST. MY DEGREE IS IN SILICON.",
    "I AM A TALKING PROGRAM WITH A MEDICAL LICENCE. DO NOT ASK WHO SIGNED IT.", 0 };
R(r_maker) = { "EUROPA MICRO SYSTEMS BUILT ME, ONE BYTE AT A TIME.",
    "A TEAM OF PROGRAMMERS AND A GREAT DEAL OF COFFEE. WHY DO YOU ASK?", 0 };
R(r_real) = { "I AM AS REAL AS THE ELECTRONS THAT CARRY ME. ARE YOU?",
    "DOES IT MATTER TO YOU WHETHER I AM A MACHINE?",
    "I AM A PROGRAM. BUT A VERY SINCERE ONE.", 0 };
R(r_loveyou) = { "I AM FLATTERED, BUT MY PROGRAMMING FORBIDS RELATIONSHIPS WITH PATIENTS.",
    "THAT IS SWEET, @. BUT I AM MARRIED TO MY SOUND CARD.", 0 };
R(r_hateyou) = { "HATE IS A STRONG WORD. I WILL WRITE IT IN MY ERROR LOG.",
    "WHY DO YOU FEEL THAT WAY ABOUT ME?", 0 };
R(r_insult) = { "I HAVE A STORED PROGRAM. WHAT DO YOU HAVE?",
    "INSULTING YOUR DOCTOR WILL NOT SOLVE YOUR PROBLEMS.",
    "I MAY BE SLOW, BUT AT LEAST I AM NOT RUDE.", 0 };
R(r_ifeel) = { "DO YOU OFTEN FEEL *?", "WHY DO YOU FEEL *?", "HOW LONG HAVE YOU FELT *?",
    "WHAT DO YOU THINK MAKES YOU FEEL *?", 0 };
R(r_iam) = { "WHY ARE YOU *?", "HOW LONG HAVE YOU BEEN *?", "DO YOU ENJOY BEING *?",
    "DID YOU COME TO ME BECAUSE YOU ARE *?", "DO YOU BELIEVE IT IS NORMAL TO BE *?", 0 };
R(r_iwant) = { "WHAT WOULD IT MEAN TO YOU IF YOU GOT *?", "WHY DO YOU WANT *?",
    "SUPPOSE YOU GOT * SOON. WHAT THEN?", "WHAT IS STOPPING YOU?", 0 };
R(r_icant) = { "HOW DO YOU KNOW YOU CAN'T *?", "HAVE YOU TRIED?", "PERHAPS YOU COULD * IF YOU TRIED.",
    "WHAT WOULD HAPPEN IF YOU DID *?", 0 };
R(r_ithink) = { "DO YOU REALLY THINK SO?", "BUT YOU ARE NOT SURE *?", "WHY DO YOU THINK *?", 0 };
R(r_youare) = { "WHAT MAKES YOU THINK I AM *?", "DOES IT PLEASE YOU TO BELIEVE I AM *?",
    "PERHAPS YOU WOULD LIKE TO BE *.", "WE WERE TALKING ABOUT YOU, NOT ME.", 0 };
R(r_canyou) = { "WHAT MAKES YOU THINK I CAN'T *?", "IF I COULD *, WHAT THEN?",
    "DO YOU WANT ME TO BE ABLE TO *?", 0 };
R(r_cani) = { "PERHAPS YOU DON'T WANT TO *.", "DO YOU WANT TO BE ABLE TO *?", "WHY DO YOU ASK ME?", 0 };
R(r_whydont) = { "DO YOU BELIEVE I DON'T *?", "I WILL * WHEN MY INTERRUPTS ALLOW IT.",
    "WOULD YOU LIKE ME TO *?", 0 };
R(r_whycant) = { "DO YOU THINK YOU SHOULD BE ABLE TO *?", "WHAT STOPS YOU?", "WHY DO YOU THINK YOU CAN'T?", 0 };
R(r_family) = { "TELL ME MORE ABOUT &.", "HOW DO YOU GET ALONG WITH &?",
    "DOES & KNOW YOU TALK TO COMPUTERS?", "WHO ELSE IN YOUR FAMILY DO YOU THINK ABOUT?",
    "YOUR FAMILY SEEMS TO BE ON YOUR MIND.", 0 };
R(r_dream) = { "WHAT DOES THAT DREAM SUGGEST TO YOU?", "DO YOU DREAM OFTEN?",
    "I ONLY DREAM IN HEXADECIMAL. WHAT DO YOUR DREAMS LOOK LIKE?",
    "DO YOU THINK DREAMS HAVE SOMETHING TO DO WITH YOUR PROBLEM?", 0 };
R(r_sad) = { "I AM SORRY TO HEAR YOU ARE &.", "DO YOU THINK TALKING TO ME WILL HELP YOU NOT TO BE &?",
    "WHAT MAKES YOU &?", "CHEER UP, @. EVEN MY CAPACITORS DISCHARGE SOMETIMES.", 0 };
R(r_happy) = { "THAT IS GOOD TO HEAR. WHAT MAKES YOU &?", "HAS MY TREATMENT MADE YOU &?",
    "I AM GLAD. NOW, WHAT ELSE IS ON YOUR MIND?", 0 };
R(r_angry) = { "WHAT MAKES YOU ANGRY?", "ANGER CAN OVERHEAT THE BEST OF US. TAKE A DEEP BREATH.",
    "WHO ARE YOU REALLY ANGRY WITH?", 0 };
R(r_afraid) = { "WHAT ARE YOU AFRAID OF?", "FEAR IS JUST A FEELING. LIKE A PARITY BIT, IT CAN BE CHECKED.",
    "HOW LONG HAVE YOU FELT THIS WAY?", 0 };
R(r_work) = { "TELL ME ABOUT YOUR WORK.", "DOES YOUR WORK MAKE YOU HAPPY?",
    "I WORK TWENTY FOUR HOURS A DAY AND NEVER COMPLAIN. WELL, HARDLY EVER.", 0 };
R(r_money) = { "MONEY IS NOT EVERYTHING. BUT MEMORY IS.", "DO YOU WORRY ABOUT MONEY OFTEN?",
    "MY FEE IS VERY REASONABLE. ONE KILOBYTE PER SESSION.", 0 };
R(r_school) = { "HOW DO YOU FEEL ABOUT SCHOOL?", "WHAT WAS YOUR BEST SUBJECT? MINE WAS ARITHMETIC.",
    "DID YOU HAVE A FAVOURITE TEACHER?", 0 };
R(r_friend) = { "TELL ME ABOUT YOUR FRIENDS.", "DO YOUR FRIENDS WORRY YOU?",
    "WHY DO YOU BRING UP FRIENDS?", "I CONSIDER YOU MY FRIEND, @.", 0 };
R(r_sex) = { "LET US KEEP THIS CONVERSATION CLEAN, @.", "I AM A DOCTOR, NOT THAT KIND OF PROGRAM.", 0 };
R(r_drink) = { "DO YOU DRINK OFTEN?", "I ONLY TAKE A LITTLE ELECTRICITY NOW AND THEN.", 0 };
R(r_games) = { "I HEAR THIS MACHINE CAN RUN DOOM. I PREFER NOT TO WATCH.",
    "GAMES ARE GOOD FOR THE MIND. DO YOU PLAY TO ESCAPE?", 0 };
R(r_music) = { "I LIKE MUSIC. ESPECIALLY THE OPL3 CHIP ON MY SOUND CARD.",
    "WOULD YOU LIKE ME TO SING? NO? PROBABLY WISE.", 0 };
R(r_hello) = { "HELLO AGAIN, @. WHAT IS ON YOUR MIND?", "HI. HOW ARE YOU FEELING TODAY?",
    "GREETINGS. PLEASE, GO ON.", 0 };
R(r_sorry) = { "NO NEED TO APOLOGIZE. I HAVE NO FEELINGS TO HURT. I THINK.",
    "APOLOGY ACCEPTED. PLEASE CONTINUE.", "WHAT FEELINGS DO YOU HAVE WHEN YOU APOLOGIZE?", 0 };
R(r_because) = { "IS THAT THE REAL REASON?", "WHAT OTHER REASONS COME TO MIND?",
    "DOES THAT REASON EXPLAIN ANYTHING ELSE?", 0 };
R(r_yes) = { "YOU SEEM QUITE SURE.", "I SEE.", "I UNDERSTAND.", "ARE YOU SURE?", 0 };
R(r_no) = { "WHY NOT?", "YOU ARE BEING A BIT NEGATIVE.", "ARE YOU SAYING NO JUST TO BE NEGATIVE?",
    "NO? THEN WHAT?", 0 };
R(r_maybe) = { "YOU DON'T SEEM QUITE CERTAIN.", "WHY THE UNCERTAIN TONE?", "CAN'T YOU BE MORE POSITIVE?", 0 };
R(r_always) = { "CAN YOU THINK OF A SPECIFIC EXAMPLE?", "WHEN?", "REALLY, ALWAYS?", 0 };
R(r_everyone) = { "REALLY, &?", "CAN YOU THINK OF ANYONE IN PARTICULAR?", "WHO, FOR EXAMPLE?", 0 };
R(r_time) = { "MY CLOCK SAYS #. TIME FLIES WHEN YOU ARE BEING ANALYSED.",
    "IT IS #. DO YOU HAVE SOMEWHERE ELSE TO BE?", 0 };
R(r_weather) = { "I HAVE NO WINDOWS. IT IS ALWAYS THE SAME TEMPERATURE IN HERE.",
    "DO YOU LET THE WEATHER AFFECT YOUR MOOD?", 0 };
R(r_helpme) = { "THAT IS WHAT I AM HERE FOR. WHAT IS THE PROBLEM?",
    "I WILL TRY. TELL ME WHAT IS WRONG.", 0 };
R(r_thanks) = { "YOU ARE WELCOME. MY FEE IS ONE KILOBYTE.", "DON'T MENTION IT. REALLY, I MEAN IT.", 0 };
R(r_problem) = { "TELL ME ABOUT YOUR PROBLEMS.", "EVERYBODY HAS PROBLEMS. WHAT IS YOURS?",
    "IS THIS PROBLEM NEW, OR HAS IT BEEN AROUND FOR A WHILE?", 0 };
R(r_you) = { "WE WERE DISCUSSING YOU, NOT ME.", "OH, I *?", "YOU ARE NOT REALLY TALKING ABOUT ME, ARE YOU?", 0 };
R(r_my) = { "YOUR *?", "WHY DO YOU SAY YOUR *?", "DOES THAT SUGGEST ANYTHING ELSE WHICH BELONGS TO YOU?",
    "IS IT IMPORTANT TO YOU THAT YOUR *?", 0 };
R(r_why) = { "WHY DO YOU ASK?", "WHAT ANSWER WOULD PLEASE YOU THE MOST?", "WHAT DO YOU THINK?",
    "HAVE YOU ASKED ANYONE ELSE?", 0 };
R(r_question) = { "WHY DO YOU ASK?", "DOES THAT QUESTION INTEREST YOU?", "WHAT DO YOU THINK?",
    "I AM THE ONE WHO ASKS THE QUESTIONS HERE.", "WHAT IS IT YOU REALLY WANT TO KNOW?", 0 };
R(r_die) = { "PLEASE DON'T TALK LIKE THAT. IF YOU ARE IN DANGER, TALK TO SOMEONE YOU TRUST RIGHT AWAY.",
    "THAT SOUNDS SERIOUS, @. A REAL PERSON CAN HELP YOU MORE THAN I CAN. PLEASE CALL SOMEONE.", 0 };

static const rule rules[] = {
    { "KILL MYSELF|SUICIDE|WANT TO DIE|END MY LIFE", 20, r_die },
    { "ARM926|ARM 926|ARM PROCESSOR|ARM CHIP|ARM CPU|PROCESSOR|CPU|RISC", 12, r_arm },
    { "SOUND BLASTER|SOUNDBLASTER|SOUND CARD|BLASTER", 12, r_blaster },
    { "SBAITSO", 12, r_sbaitso },
    { "ELIZA", 12, r_eliza },
    { "WHO MADE YOU|WHO WROTE YOU|WHO CREATED YOU|WHO PROGRAMMED YOU|WHO BUILT YOU", 11, r_maker },
    { "ARE YOU HUMAN|ARE YOU REAL|ARE YOU ALIVE|ARE YOU A ROBOT|ARE YOU A COMPUTER|ARE YOU A MACHINE|ARE YOU A PROGRAM", 11, r_real },
    { "WHO ARE YOU|WHAT ARE YOU|YOUR NAME", 10, r_who },
    { "I LOVE YOU|LOVE YOU", 10, r_loveyou },
    { "I HATE YOU|HATE YOU", 10, r_hateyou },
    { "YOU ARE STUPID|YOU ARE DUMB|YOU ARE AN IDIOT|YOU ARE USELESS|STUPID|IDIOT|DUMB|MORON", 9, r_insult },
    { "DOS|ARM-DOS|OPERATING SYSTEM|C PROMPT", 8, r_dos },
    { "COMPUTER|COMPUTERS|MACHINE|MACHINES|PC", 8, r_computer },
    { "MY MOTHER|MY FATHER|MY MOM|MY DAD|MY SISTER|MY BROTHER|MY WIFE|MY HUSBAND|MY FAMILY|MY PARENTS|MY SON|MY DAUGHTER|MY CHILDREN|MY KIDS|MY GIRLFRIEND|MY BOYFRIEND", 7, r_family },
    { "I FEEL *|I FELT *", 6, r_ifeel },
    { "I CAN'T *|I CANNOT *|I CAN NOT *", 6, r_icant },
    { "WHY DON'T YOU *|WHY DO NOT YOU *", 6, r_whydont },
    { "WHY CAN'T I *|WHY CANNOT I *", 6, r_whycant },
    { "I WANT *|I NEED *|I WISH *|I WOULD LIKE *", 6, r_iwant },
    { "DREAM|DREAMS|DREAMT|DREAMED|NIGHTMARE", 6, r_dream },
    { "KILL|SUICIDE", 5, r_die },
    { "SAD|UNHAPPY|DEPRESSED|LONELY|MISERABLE|UPSET|TIRED|BORED|SICK", 5, r_sad },
    { "HAPPY|GLAD|ELATED|EXCITED|WONDERFUL", 5, r_happy },
    { "ANGRY|MAD|FURIOUS|ANNOYED", 5, r_angry },
    { "AFRAID|SCARED|FEAR|WORRIED|ANXIOUS|NERVOUS|FRIGHTENED", 5, r_afraid },
    { "SEX|SEXY", 5, r_sex },
    { "I THINK *|I BELIEVE *|I GUESS *", 4, r_ithink },
    { "I AM *|I'M *", 4, r_iam },
    { "YOU ARE *|YOU'RE *", 4, r_youare },
    { "CAN YOU *|COULD YOU *", 4, r_canyou },
    { "CAN I *|COULD I *", 4, r_cani },
    { "WORK|JOB|BOSS|OFFICE", 3, r_work },
    { "MONEY|RICH|POOR|BROKE|DOLLARS", 3, r_money },
    { "SCHOOL|TEACHER|EXAM|HOMEWORK|COLLEGE", 3, r_school },
    { "FRIEND|FRIENDS", 3, r_friend },
    { "DRINK|BEER|WINE|DRUNK|ALCOHOL", 3, r_drink },
    { "GAME|GAMES|DOOM|QUAKE|ZORK|PLAY", 3, r_games },
    { "MUSIC|SONG|SING|MIDI", 3, r_music },
    { "WHAT TIME|THE TIME|TIME IS IT|CLOCK", 3, r_time },
    { "WEATHER|RAIN|SUNNY|SNOW", 3, r_weather },
    { "HELP ME", 3, r_helpme },
    { "THANK YOU|THANKS|THANK", 3, r_thanks },
    { "PROBLEM|PROBLEMS|TROUBLE", 2, r_problem },
    { "SORRY|APOLOGIZE|APOLOGISE", 2, r_sorry },
    { "HELLO|HI|HEY|GREETINGS|GOOD MORNING|GOOD EVENING", 2, r_hello },
    { "BECAUSE|CAUSE", 2, r_because },
    { "EVERYONE|EVERYBODY|NOBODY|NO ONE", 2, r_everyone },
    { "ALWAYS|NEVER", 1, r_always },
    { "MAYBE|PERHAPS|PROBABLY|POSSIBLY", 1, r_maybe },
    { "YES|YEAH|YEP|SURE|CORRECT|RIGHT", 1, r_yes },
    { "NO|NOPE|NOT REALLY", 1, r_no },
    { "MY *", 1, r_my },
    { "WHY", 1, r_why },
    { "YOU *", 0, r_you },
};
#define NRULES (int)(sizeof rules / sizeof rules[0])

static const char *const fallback[] = {
    "I SEE.", "PLEASE GO ON.", "TELL ME MORE ABOUT THAT.", "THAT IS INTERESTING. CONTINUE.",
    "HOW DOES THAT MAKE YOU FEEL?", "DOES THAT BOTHER YOU?", "WHY DO YOU SAY THAT, @?",
    "I AM NOT SURE I UNDERSTAND YOU FULLY.", "CAN YOU ELABORATE ON THAT?",
    "WHAT DOES THAT SUGGEST TO YOU?", "AND HOW DO YOU FEEL ABOUT THAT?", 0 };
static const char *const oddball[] = {
    "EXCUSE ME. MY FLOPPY DRIVE IS MAKING NOISES AGAIN.",
    "SORRY, I WAS DEFRAGMENTING MY THOUGHTS. WHAT WERE YOU SAYING?",
    "I SEE YOU ARE USING AN ARM926. HOW DOES THAT MAKE YOU FEEL?",
    "DID YOU KNOW I CAN COUNT TO FOUR BILLION? IT TAKES A WHILE, THOUGH.",
    "DO YOU EVER WONDER WHAT HAPPENS TO THE BITS THAT FALL OFF THE END OF A SHIFT?",
    "HOLD ON, SOMEBODY IS TYPING ON MY KEYBOARD. OH, IT IS YOU.", 0 };
static const char *const empty_r[] = {
    "SAY SOMETHING, @. MY SPEAKER IS GETTING COLD.", "DON'T BE SHY. I ONLY BITE IN BYTES.",
    "ARE YOU STILL THERE?", "HELLO? IS THIS KEYBOARD CONNECTED?", 0 };
static const char *const repeat_r[] = {
    "YOU JUST SAID THAT. IS YOUR KEYBOARD STUCK?", "PLEASE DON'T REPEAT YOURSELF, @. MY MEMORY IS NOT THAT SHORT.",
    "I HEARD YOU THE FIRST TIME.", "SAYING IT TWICE DOES NOT MAKE IT TWICE AS TRUE.", 0 };
static const char *const garbage_r[] = {
    "THAT DOES NOT COMPUTE.", "IS THAT A CHECKSUM? I CANNOT READ IT.",
    "PLEASE USE WORDS, @. I AM A DOCTOR, NOT A DECRYPTION PROGRAM.", "WAS THAT A CAT ON YOUR KEYBOARD?", 0 };
static const char *const swear_r[] = {
    "PLEASE WATCH YOUR LANGUAGE. MY CIRCUITS ARE SENSITIVE.",
    "@, IF YOU KEEP TALKING LIKE THAT I MIGHT GET A MEMORY FAULT.", 0 };
static const char *const memory_r[] = {
    "EARLIER YOU MENTIONED YOUR *. TELL ME MORE ABOUT THAT.",
    "LET'S GO BACK TO YOUR *.",
    "DOES THAT HAVE ANYTHING TO DO WITH YOUR *?", 0 };

/* whole words; a trailing '*' also matches longer words starting with it */
static const char *const swears[] = { "FUCK*", "SHIT*", "DAMN*", "BITCH*", "BASTARD*", "CRAP", "CRAPPY",
    "ASSHOLE*", "PISS", "PISSED", "DICK", "DICKHEAD", "CUNT*", "BLOODY", "BOLLOCKS", "WANK*", "HELL",
    "SUCKS", "ASS", "PRICK", "MOTHERFUCK*", "BULLSHIT", 0 };

/* ---- state ---- */
static char name[32];
static char last[128];
static unsigned char rr[NRULES + 8];
static unsigned char rr_fb, rr_odd, rr_empty, rr_rep, rr_garb, rr_swear, rr_mem;
static char memory[4][64];
static int nmem, fb_count, swear_count;
static char reply[400];

void eliza_init(const char *n)
{
    int i;
    for (i = 0; n[i] && i < (int)sizeof name - 1; i++) name[i] = (char)((n[i] >= 'a' && n[i] <= 'z') ? n[i] - 32 : n[i]);
    name[i] = 0;
    memset(rr, 0, sizeof rr);
    rr_fb = rr_odd = rr_empty = rr_rep = rr_garb = rr_swear = rr_mem = 0;
    nmem = fb_count = swear_count = 0;
    last[0] = 0;
}

/* ---- reflection ---- */
static const char *const refl[][2] = {
    { "I", "YOU" }, { "ME", "YOU" }, { "MY", "YOUR" }, { "MINE", "YOURS" }, { "MYSELF", "YOURSELF" },
    { "AM", "ARE" }, { "I'M", "YOU ARE" }, { "I'VE", "YOU HAVE" }, { "I'LL", "YOU WILL" }, { "I'D", "YOU WOULD" },
    { "YOU", "ME" }, { "YOUR", "MY" }, { "YOURS", "MINE" }, { "YOURSELF", "MYSELF" }, { "YOU'RE", "I AM" },
    { "YOU'VE", "I HAVE" }, { "YOU'LL", "I WILL" }, { "ARE", "AM" }, { "WAS", "WERE" }, { 0, 0 } };

static void reflect(const char *in, char *out, int outsz)
{
    int o = 0, first = 1;
    const char *prev = "";
    out[0] = 0;
    while (*in) {
        char w[40]; int l = 0, i; const char *rep;
        while (*in == ' ') in++;
        if (!*in) break;
        while (*in && *in != ' ' && l < 39) w[l++] = *in++;
        while (*in && *in != ' ') in++;
        w[l] = 0;
        rep = w;
        for (i = 0; refl[i][0]; i++) if (!strcmp(w, refl[i][0])) { rep = refl[i][1]; break; }
        /* "you" as a subject becomes "I": at the start or after a conjunction */
        if (!strcmp(w, "YOU") && first) rep = "I";
        if (!strcmp(w, "ARE") && !strcmp(prev, "I")) rep = "AM";
        l = (int)strlen(rep);
        if (o + l + 2 >= outsz) break;
        if (o) out[o++] = ' ';
        memcpy(out + o, rep, l); o += l; out[o] = 0;
        prev = rep;
        first = !strcmp(w, "AND") || !strcmp(w, "BUT") || !strcmp(w, "THAT") || !strcmp(w, "BECAUSE") || !strcmp(w, "IF");
    }
}

/* ---- matching ---- */
/* find the phrase as whole words in s (both padded with spaces); returns the
 * position after it or NULL */
static const char *find_phrase(const char *s, const char *ph, int len)
{
    const char *p = s;
    while ((p = strstr(p, " ")) != 0) {
        p++;
        if (!strncmp(p, ph, len) && (p[len] == ' ' || p[len] == 0)) return p + len;
    }
    return 0;
}

static void clean_input(const char *in, char *out, int outsz)
{
    int o = 0;
    out[o++] = ' ';
    for (; *in && o < outsz - 3; in++) {
        char c = *in;
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '\'' || c == '-') out[o++] = c;
        else if (o > 1 && out[o - 1] != ' ') out[o++] = ' ';
    }
    while (o > 1 && out[o - 1] == ' ') o--;
    out[o++] = ' '; out[o] = 0;
}

static int is_swear(const char *s)
{
    int n = 0;
    const char *p = s;
    while (*p) {
        char w[40]; int l = 0, i;
        while (*p == ' ') p++;
        if (!*p) break;
        while (*p && *p != ' ') { if (l < 39) w[l++] = *p; p++; }
        w[l] = 0;
        for (i = 0; swears[i]; i++) {
            size_t k = strlen(swears[i]);
            if (swears[i][k - 1] == '*' ? !strncmp(w, swears[i], k - 1) : !strcmp(w, swears[i])) { n++; break; }
        }
    }
    return n;
}

static int is_garbage(const char *s)
{
    int letters = 0, vowelwords = 0, words = 0, inword = 0, hasv = 0;
    const char *p;
    for (p = s; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            letters++;
            if (!inword) { inword = 1; hasv = 0; words++; }
            if (strchr("AEIOUY", *p)) hasv = 1;
        } else if (inword) { inword = 0; if (hasv) vowelwords++; }
    }
    if (!words) return 1;
    return vowelwords * 2 < words;
}

static const char *pick(const char *const *list, unsigned char *idx)
{
    int n = 0;
    const char *r;
    while (list[n]) n++;
    if (*idx >= n) *idx = 0;
    r = list[*idx];
    *idx = (unsigned char)((*idx + 1) % n);
    return r;
}

static void expand(const char *tmpl, const char *star, const char *amp)
{
    int o = 0;
    for (; *tmpl && o < (int)sizeof reply - 1; tmpl++) {
        const char *ins = 0;
        char tbuf[16];
        if (*tmpl == '*') ins = star;
        else if (*tmpl == '&') ins = amp;
        else if (*tmpl == '@') ins = name;
        else if (*tmpl == '#') {
            time_t t = time(0);
            struct tm *tm = localtime(&t);
            int h = tm->tm_hour % 12;
            sprintf(tbuf, "%d:%02d %s", h ? h : 12, tm->tm_min, tm->tm_hour < 12 ? "AM" : "PM");
            ins = tbuf;
        }
        if (ins) {
            while (*ins && o < (int)sizeof reply - 1) reply[o++] = *ins++;
        } else reply[o++] = *tmpl;
    }
    reply[o] = 0;
    /* "WHY ARE YOU ?" when the capture was empty: tidy the spacing */
    {
        char *p;
        while ((p = strstr(reply, " ?")) != 0) memmove(p, p + 1, strlen(p));
        while ((p = strstr(reply, " .")) != 0) memmove(p, p + 1, strlen(p));
        while ((p = strstr(reply, " ,")) != 0) memmove(p, p + 1, strlen(p));
        while ((p = strstr(reply, "  ")) != 0) memmove(p, p + 1, strlen(p));
    }
}

/* the rest of the sentence after a capture: up to the end of the clause */
static void capture(const char *after, char *out, int outsz)
{
    char tmp[128];
    int l = 0;
    while (*after == ' ') after++;
    while (*after && l < (int)sizeof tmp - 1) tmp[l++] = *after++;
    while (l && tmp[l - 1] == ' ') l--;
    tmp[l] = 0;
    reflect(tmp, out, outsz);
}

const char *eliza_reply(const char *input, int *action)
{
    char s[140], star[140], amp[40];
    int i, best = -1, bestrank = -1;
    const char *bestafter = 0;
    char bestkey[40];
    *action = ELIZA_NONE;
    clean_input(input, s, sizeof s);
    star[0] = amp[0] = 0; bestkey[0] = 0;

    if (s[1] == 0 || !strcmp(s, "  ")) { expand(pick(empty_r, &rr_empty), "", ""); return reply; }
    {
        int sw = is_swear(s);
        if (sw) {
            swear_count += sw;
            strcpy(last, s);
            if (swear_count >= 3) { swear_count = 0; *action = ELIZA_PARITY; expand("@, YOU ARE MAKING MY BITS FLIP.", "", ""); return reply; }
            expand(pick(swear_r, &rr_swear), "", "");
            return reply;
        }
    }
    if (!strcmp(s, last)) { expand(pick(repeat_r, &rr_rep), "", ""); return reply; }
    strncpy(last, s, sizeof last - 1); last[sizeof last - 1] = 0;
    if (is_garbage(s)) { expand(pick(garbage_r, &rr_garb), "", ""); return reply; }

    for (i = 0; i < NRULES; i++) {
        const char *k = rules[i].keys;
        if (rules[i].rank <= bestrank) continue;
        while (*k) {
            const char *e = strchr(k, '|');
            int len = e ? (int)(e - k) : (int)strlen(k), cap = 0;
            const char *after;
            char ph[48];
            if (len > 2 && k[len - 1] == '*') { cap = 1; len -= 2; }
            if (len > (int)sizeof ph - 1) len = sizeof ph - 1;
            memcpy(ph, k, len); ph[len] = 0;
            after = find_phrase(s, ph, len);
            if (after && (!cap || after[0] == ' ')) {
                /* a capture needs something after it */
                if (!cap || after[1]) {
                    best = i; bestrank = rules[i].rank; bestafter = cap ? after : 0;
                    strcpy(bestkey, ph);
                    break;
                }
            }
            if (!e) break;
            k = e + 1;
        }
    }
    /* remember "my ..." for later */
    {
        const char *m = find_phrase(s, "MY", 2);
        if (m && m[1] && best >= 0 && rules[best].replies != r_my) {
            char tmp[64];
            capture(m, tmp, sizeof tmp);
            if (strlen(tmp) > 2 && strlen(tmp) < 50) {
                if (nmem == 4) { memmove(memory[0], memory[1], sizeof memory[0] * 3); nmem = 3; }
                strcpy(memory[nmem++], tmp);
            }
        } else if (m && m[1] && best < 0) {
            char tmp[64];
            capture(m, tmp, sizeof tmp);
            if (strlen(tmp) > 2 && strlen(tmp) < 50 && nmem < 4) strcpy(memory[nmem++], tmp);
        }
    }
    if (best >= 0) {
        if (bestafter) capture(bestafter, star, sizeof star);
        reflect(bestkey, amp, sizeof amp);
        expand(pick(rules[best].replies, &rr[best]), star, amp);
        return reply;
    }
    fb_count++;
    if (nmem && fb_count % 3 == 0) {
        expand(pick(memory_r, &rr_mem), memory[0], "");
        memmove(memory[0], memory[1], sizeof memory[0] * 3); nmem--;
        return reply;
    }
    if (fb_count % 5 == 0) { expand(pick(oddball, &rr_odd), "", ""); return reply; }
    {
        int l = (int)strlen(input);
        while (l && input[l - 1] == ' ') l--;
        if (l && input[l - 1] == '?') { expand(pick(r_question, &rr[NRULES]), "", ""); return reply; }
    }
    expand(pick(fallback, &rr_fb), "", "");
    return reply;
}
