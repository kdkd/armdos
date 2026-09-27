/*
 * Dungeon types and macros.
 *
 * SPDX-FileCopyrightText: (C) 1977, 2005 by Will Crowther and Don Woods
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "dungeon.h"

/* LCG PRNG parameters tested against
 * Knuth vol. 2. by the original authors */
#define LCG_A 1093L
#define LCG_C 221587L
#define LCG_M 1048576L

#define LINESIZE 1024
#define TOKLEN 5          // # outputting characters in a token */
#define PIRATE NDWARVES
#define DALTLC LOC_NUGGET // alternate dwarf location
#define INVLIMIT 7        // inventory limit (# of objects)
#define INTRANSITIVE -1   // illegal object number
#define GAMELIMIT 330     // base limit of turns
#define NOVICELIMIT 1000  // limit of turns for novice
#define WARNTIME 30       // late game starts at game.limit-this
#define FLASHTIME 50      // turns from first warning till blinding flash
#define PANICTIME 15      // time left after closing
#define BATTERYLIFE 2500  // turn limit increment from batteries
#define WORD_NOT_FOUND                                                         \
	-1 // "Word not found" flag value for the vocab hash functions.
#define WORD_EMPTY 0     // "Word empty" flag value for the vocab hash functions
#define PIT_KILL_PROB 35 // Percentage probability of dying from fall in pit.
#define CARRIED -1       // Player is toting it
#define READ_MODE "rb"   // b is not needed for POSIX but harmless
#define WRITE_MODE "wb"  // b is not needed for POSIX but harmless

/* Special object-state values - integers > 0 are object-specific */
#define STATE_NOTFOUND -1 // 'Not found" state of treasures
#define STATE_FOUND 0     // After discovered, before messed with
#define STATE_IN_CAVITY 1 // State value common to all gemstones

/* Special fixed object-state values - integers > 0 are location */
#define IS_FIXED -1
#define IS_FREE 0

/* (ESR) It is fitting that translation of the original ADVENT should
 * have left us a maze of twisty little conditionals that resists all
 * understanding.  Setting and use of what is now the per-object state
 * member (which used to be an array of its own) is our mystery. This
 * state tangles together information about whether the object is a
 * treasure, whether the player has seen it yet, and its activation
 * state.
 *
 * Things we think we know:
 *
 * STATE_NOTFOUND is only set on treasures. Non-treasures start the
 * game in STATE_FOUND.
 *
 * PROP_STASHIFY is supposed to map a state property value to a
 * negative range, where the object cannot be picked up but the value
 * can be recovered later.  Various objects get this property when
 * the cave starts to close. Only seems to be significant for the bird
 * and readable objects, notably the clam/oyster - but the code around
 * those tests is difficult to read.
 *
 * All tests of the prop member are done with either these macros or ==.
 */
#define OBJECT_IS_NOTFOUND(obj) (game.objects[obj].prop == STATE_NOTFOUND)
#define OBJECT_IS_FOUND(obj) (game.objects[obj].prop == STATE_FOUND)
#define OBJECT_SET_FOUND(obj) (game.objects[obj].prop = STATE_FOUND)
#define OBJECT_SET_NOT_FOUND(obj) (game.objects[obj].prop = STATE_NOTFOUND)
#define OBJECT_IS_NOTFOUND2(g, o) (g.objects[o].prop == STATE_NOTFOUND)
#define PROP_IS_INVALID(val) (val < -MAX_STATE - 1 || val > MAX_STATE)
#define PROP_STASHIFY(n) (-1 - (n))
#define OBJECT_STASHIFY(obj, pval) game.objects[obj].prop = PROP_STASHIFY(pval)
#define OBJECT_IS_STASHED(obj) (game.objects[obj].prop < STATE_NOTFOUND)
#define OBJECT_STATE_EQUALS(obj, pval)                                         \
	((game.objects[obj].prop == pval) ||                                   \
	 (game.objects[obj].prop == PROP_STASHIFY(pval)))

#define PROMPT "> "

/*
 * DESTROY(N)     = Get rid of an item by putting it in LOC_NOWHERE
 * MOD(N,M)       = Arithmetic modulus
 * TOTING(OBJ)    = true if the OBJ is being carried
 * AT(OBJ)        = true if on either side of two-placed object
 * HERE(OBJ)      = true if the OBJ is at "LOC" (or is being carried)
 * CNDBIT(L,N)    = true if COND(L) has bit n set (bit 0 is units bit)
 * LIQUID()       = object number of liquid in bottle
 * LIQLOC(LOC)    = object number of liquid (if any) at LOC
 * FORCED(LOC)    = true if LOC moves without asking for input (COND=2)
 * IS_DARK_HERE() = true if location "LOC" is dark
 * PCT(N)         = true N% of the time (N integer from 0 to 100)
 * GSTONE(OBJ)    = true if OBJ is a gemstone
 * FOREST(LOC)    = true if LOC is part of the forest
 * OUTSIDE(LOC)   = true if location not in the cave
 * INSIDE(LOC)    = true if location is in the cave or the building at the
 *                  beginning of the game
 * INDEEP(LOC)    = true if location is in the Hall of Mists or deeper
 * BUG(X)         = report bug and exit
 */
#define DESTROY(N) move(N, LOC_NOWHERE)
#define MOD(N, M) ((N) % (M))
#define TOTING(OBJ) (game.objects[OBJ].place == CARRIED)
#define AT(OBJ)                                                                \
	(game.objects[OBJ].place == game.loc ||                                \
	 game.objects[OBJ].fixed == game.loc)
#define HERE(OBJ) (AT(OBJ) || TOTING(OBJ))
#define CNDBIT(L, N) (tstbit(conditions[L], N))
#define LIQUID()                                                               \
	(game.objects[BOTTLE].prop == WATER_BOTTLE ? WATER                     \
	 : game.objects[BOTTLE].prop == OIL_BOTTLE ? OIL                       \
	                                           : NO_OBJECT)
#define LIQLOC(LOC)                                                            \
	(CNDBIT((LOC), COND_FLUID) ? CNDBIT((LOC), COND_OILY) ? OIL : WATER    \
	                           : NO_OBJECT)
#define FORCED(LOC) CNDBIT(LOC, COND_FORCED)
#define IS_DARK_HERE()                                                         \
	(!CNDBIT(game.loc, COND_LIT) &&                                        \
	 (game.objects[LAMP].prop == LAMP_DARK || !HERE(LAMP)))
#define PCT(N) (randrange(100) < (N))
#define GSTONE(OBJ)                                                            \
	((OBJ) == EMERALD || (OBJ) == RUBY || (OBJ) == AMBER || (OBJ) == SAPPH)
#define FOREST(LOC) CNDBIT(LOC, COND_FOREST)
#define OUTSIDE(LOC) (CNDBIT(LOC, COND_ABOVE) || FOREST(LOC))
#define INSIDE(LOC) (!OUTSIDE(LOC) || LOC == LOC_BUILDING)
#define INDEEP(LOC) CNDBIT((LOC), COND_DEEP)
#define BUG(x) bug(x, #x)

enum bugtype {
	SPECIAL_TRAVEL_500_GT_L_GT_300_EXCEEDS_GOTO_LIST,
	VOCABULARY_TYPE_N_OVER_1000_NOT_BETWEEN_0_AND_3,
	INTRANSITIVE_ACTION_VERB_EXCEEDS_GOTO_LIST,
	TRANSITIVE_ACTION_VERB_EXCEEDS_GOTO_LIST,
	CONDITIONAL_TRAVEL_ENTRY_WITH_NO_ALTERATION,
	LOCATION_HAS_NO_TRAVEL_ENTRIES,
	HINT_NUMBER_EXCEEDS_GOTO_LIST,
	SPEECHPART_NOT_TRANSITIVE_OR_INTRANSITIVE_OR_UNKNOWN,
	ACTION_RETURNED_PHASE_CODE_BEYOND_END_OF_SWITCH,
};

enum speaktype { touch, look, hear, study, change };

enum termination { endgame, quitgame, scoregame };

enum speechpart { unknown, intransitive, transitive };

typedef enum { NO_WORD_TYPE, MOTION, OBJECT, ACTION, NUMERIC } word_type_t;

typedef enum scorebonus { none, splatter, defeat, victory } score_t;

/* Phase codes for action returns.
 * These were at one time FORTRAN line numbers.
 */
typedef enum {
	GO_TERMINATE,
	GO_MOVE,
	GO_TOP,
	GO_CLEAROBJ,
	GO_CHECKHINT,
	GO_WORD2,
	GO_UNKNOWN,
	GO_DWARFWAKE,
} phase_codes_t;

/* Use fixed-lwength types to make the save format moore portable */
typedef int32_t vocab_t;  // index into a vocabulary array */
typedef int32_t verb_t;   // index into an actions array */
typedef int32_t obj_t;    // index into the object array */
typedef int32_t loc_t;    // index into the locations array */
typedef int32_t turn_t;   // turn counter or threshold */
typedef int32_t bool32_t; // turn counter or threshold */

struct game_t {
	int32_t lcg_x;
	int32_t abbnum;   // How often to print int descriptions
	score_t bonus;    // What kind of finishing bonus we are getting
	loc_t chloc;      // pirate chest location
	loc_t chloc2;     // pirate chest alternate location
	turn_t clock1;    // # turns from finding last treasure to close
	turn_t clock2;    // # turns from warning till blinding flash
	bool32_t clshnt;  // has player read the clue in the endgame?
	bool32_t closed;  // whether we're all the way closed
	bool32_t closng;  // whether it's closing time yet
	bool32_t lmwarn;  // has player been warned about lamp going dim?
	bool32_t novice;  // asked for instructions at start-up?
	bool32_t panic;   // has player found out he's trapped?
	bool32_t wzdark;  // whether the loc he's leaving was dark
	bool32_t blooded; // has player drunk of dragon's blood?
	int32_t conds;    // min value for cond[loc] if loc has any hints
	int32_t detail;   // level of detail in descriptions

	/*  dflag controls the level of activation of dwarves:
	 *	0	No dwarf stuff yet (wait until reaches Hall Of Mists)
	 *	1	Reached Hall Of Mists, but hasn't met first dwarf
	 *	2	Met 1t dwarf, others start moving, no knives thrown yet
	 *      3	A knife has been thrown (first set always misses) 3+
	 * Dwarves are mad (increases their accuracy) */
	int32_t dflag;

	int32_t dkill;  // dwarves killed
	int32_t dtotal; // total dwarves (including pirate) in loc
	int32_t foobar; // progress in saying "FEE FIE FOE FOO".
	int32_t holdng; // number of objects being carried
	int32_t igo;    // # uses of "go" instead of a direction
	int32_t iwest;  // # times he's said "west" instead of "w"
	loc_t knfloc;   // knife location; LOC_NOWERE if none, -1 after caveat
	turn_t limit;   // lifetime of lamp
	loc_t loc;      // where player is now
	loc_t newloc;   // where player is going
	turn_t numdie;  // number of times killed so far
	loc_t oldloc;   // where player was
	loc_t oldlc2;   // where player was two moves ago
	obj_t oldobj;   // last object player handled
	int32_t saved;  // point penalty for saves
	int32_t tally;  // count of treasures gained
	int32_t thresh; // current threshold for endgame scoring tier
	bool32_t seenbigwords; // have we red the graffiti in the Giant's Room?
	turn_t trnluz;         // # points lost so far due to turns used
	turn_t turns;          // counts commands given (ignores yes/no)
	char zzword[TOKLEN + 1]; // randomly generated magic word from bird
	struct {
		int32_t abbrev; // has location been seen?
		int32_t atloc;  // head of object linked list per location
	} locs[NLOCATIONS];
	struct {
		int32_t seen; // true if dwarf has seen him
		loc_t loc;    // location of dwarves, initially hard-wired in
		loc_t oldloc; // prior loc of each dwarf, initially garbage
	} dwarves[NDWARVES + 1];
	struct {
		loc_t fixed;  // fixed location of object (if not IS_FREE)
		int32_t prop; // object state
		loc_t place;  // location of object
	} objects[NOBJECTS];
	struct {
		bool32_t used; // hints[i].used = true iff hint i has been used.
		int32_t lc;    // hints[i].lc = show int at LOC with cond bit i
	} hints[NHINTS];
	obj_t link[NOBJECTS * 2]; // object-list links
};

/*
 * Game application settings - settings, but not state of the game, per se.
 * This data is not saved in a saved game.
 */
struct settings_t {
	FILE *logfp;
	bool oldstyle;
	bool prompt;
	char **argv;
	int argc;
	int optind;
	FILE *scriptfp;
	int debug;
};

typedef struct {
	char raw[LINESIZE];
	vocab_t id;
	word_type_t type;
} command_word_t;

typedef enum {
	EMPTY,
	RAW,
	TOKENIZED,
	GIVEN,
	PREPROCESSED,
	PROCESSING,
	EXECUTED
} command_state_t;

typedef struct {
	enum speechpart part;
	command_word_t word[2];
	verb_t verb;
	obj_t obj;
	command_state_t state;
} command_t;

/*
 * Bump on save format change.
 *
 * Note: Verify that the tests run clean before bumping this, then
 * rebuild the check files afterwards with "make -B buildchexks".
 * Otherwise you will get a spurious failure due to the old version
 * having been generated into a check file.
 */
#define SAVE_VERSION 32

/*
 * Goes at start of file so saves can be identified by file(1) and the like.
 */
#define ADVENT_MAGIC "open-adventure\n"

/*
 * If you change the first three members, the resume function may not properly
 * reject saves from older versions. Later members can change, but bump the
 * version when you do that.
 */
struct save_t {
	char magic[sizeof(ADVENT_MAGIC)];
	int32_t version;
	int32_t canary;
	struct game_t game;
};

extern struct game_t game;
extern struct save_t save;
extern struct settings_t settings;

extern char *myreadline(const char *);
extern bool get_command_input(command_t *);
extern void clear_command(command_t *);
extern void speak(const char *, ...);
extern void sspeak(int msg, ...);
extern void pspeak(vocab_t, enum speaktype, bool, int, ...);
extern void rspeak(vocab_t, ...);
extern void echo_input(FILE *, const char *, const char *);
extern bool silent_yes_or_no(void);
extern bool yes_or_no(const char *, const char *, const char *);
extern void juggle(obj_t);
extern void move(obj_t, loc_t);
extern void put(obj_t, loc_t, int);
extern void carry(obj_t, loc_t);
extern void drop(obj_t, loc_t);
extern int atdwrf(loc_t);
extern int setbit(int);
extern bool tstbit(int, int);
extern void set_seed(int32_t);
extern int32_t randrange(int32_t);
extern int score(enum termination);
extern void terminate(enum termination) __attribute__((noreturn));
extern int savefile(FILE *);
#if defined ADVENT_AUTOSAVE
extern void autosave(void);
#endif
extern int suspend(void);
extern int resume(void);
extern int restore(FILE *);
extern int initialise(void);
extern phase_codes_t action(command_t);
extern void state_change(obj_t, int);
extern bool is_valid(struct game_t);
extern void bug(enum bugtype, const char *) __attribute__((__noreturn__));

/* represent an empty command word */
static const command_word_t empty_command_word = {
    .raw = "",
    .id = WORD_EMPTY,
    .type = NO_WORD_TYPE,
};

/* end */
