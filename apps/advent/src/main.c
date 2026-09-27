/*
 * SPDX-FileCopyrightText: (C) 1977, 2005 by Will Crowther and Don Woods
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <ctype.h>
#include <editline/readline.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "advent.h"

#define DIM(a) (sizeof(a) / sizeof(a[0]))

#if defined ADVENT_AUTOSAVE
static FILE *autosave_fp;
void autosave(void) {
	if (autosave_fp != NULL) {
		rewind(autosave_fp);
		savefile(autosave_fp);
		fflush(autosave_fp);
	}
}
#endif

// LCOV_EXCL_START
// exclude from coverage analysis because it requires interactivity to test
static void sig_handler(int signo) {
	if (signo == SIGINT) {
		if (settings.logfp != NULL) {
			fflush(settings.logfp);
		}
	}

#if defined ADVENT_AUTOSAVE
	if (signo == SIGHUP || signo == SIGTERM) {
		autosave();
	}
#endif
	exit(EXIT_FAILURE);
}
// LCOV_EXCL_STOP

char *myreadline(const char *prompt) {
	/*
	 * This function isn't required for gameplay, readline() straight
	 * up would suffice for that.  It's where we interpret command-line
	 * logfiles for testing purposes.
	 */
	/* Normal case - no script arguments */
	if (settings.argc == 0) {
		char *ln = readline(prompt);
		if (ln == NULL) {
			fputs(prompt, stdout);
		}
		return ln;
	}

	char *buf = malloc(LINESIZE + 1);
	for (;;) {
		if (settings.scriptfp == NULL || feof(settings.scriptfp)) {
			if (settings.optind >= settings.argc) {
				free(buf);
				return NULL;
			}

			const char *next = settings.argv[settings.optind++];

			if (settings.scriptfp != NULL &&
			    feof(settings.scriptfp)) {
				fclose(settings.scriptfp);
			}
			if (strcmp(next, "-") == 0) {
				settings.scriptfp = stdin; // LCOV_EXCL_LINE
				} else {
					settings.scriptfp = fopen(next, "r"); /* text */
					if (settings.scriptfp == NULL) {
						fprintf(stderr, "Can't open script %s\n",
						        next);
						continue;
					}
				}
			}

		if (isatty(fileno(settings.scriptfp)) && !settings.oldstyle) {
			free(buf);               // LCOV_EXCL_LINE
			return readline(prompt); // LCOV_EXCL_LINE
		} else {
			char *ln = fgets(buf, LINESIZE, settings.scriptfp);
			if (ln != NULL) {
				fputs(prompt, stdout);
				fputs(ln, stdout);
				return ln;
			}
		}
	}

	return NULL;
}

/*  Check if this loc is eligible for any hints.  If been here int
 *  enough, display.  Ignore "HINTS" < 4 (special stuff, see database
 *  notes). */
static void checkhints(void) {
	if (conditions[game.loc] >= game.conds) {
		for (int hint = 0; hint < NHINTS; hint++) {
			if (game.hints[hint].used) {
				continue;
			}
			if (!CNDBIT(game.loc, hint + 1 + COND_HBASE)) {
				game.hints[hint].lc = -1;
			}
			++game.hints[hint].lc;
			/*  Come here if he's been int enough at required loc(s)
			 * for some unused hint. */
			if (game.hints[hint].lc >= hints[hint].turns) {
				int i;

				switch (hint) {
				case 0:
					/* cave */
					if (game.objects[GRATE].prop ==
					        GRATE_CLOSED &&
					    !HERE(KEYS)) {
						break;
					}
					game.hints[hint].lc = 0;
					return;
				case 1: /* bird */
					if (game.objects[BIRD].place ==
					        game.loc &&
					    TOTING(ROD) &&
					    game.oldobj == BIRD) {
						break;
					}
					return;
				case 2: /* snake */
					if (HERE(SNAKE) && !HERE(BIRD)) {
						break;
					}
					game.hints[hint].lc = 0;
					return;
				case 3: /* maze */
					if (game.locs[game.loc].atloc ==
					        NO_OBJECT &&
					    game.locs[game.oldloc].atloc ==
					        NO_OBJECT &&
					    game.locs[game.oldlc2].atloc ==
					        NO_OBJECT &&
					    game.holdng > 1) {
						break;
					}
					game.hints[hint].lc = 0;
					return;
				case 4: /* dark */
					if (!OBJECT_IS_NOTFOUND(EMERALD) &&
					    OBJECT_IS_NOTFOUND(PYRAMID)) {
						break;
					}
					game.hints[hint].lc = 0;
					return;
				case 5: /* witt */
					break;
				case 6: /* urn */
					if (game.dflag == 0) {
						break;
					}
					game.hints[hint].lc = 0;
					return;
				case 7: /* woods */
					if (game.locs[game.loc].atloc ==
					        NO_OBJECT &&
					    game.locs[game.oldloc].atloc ==
					        NO_OBJECT &&
					    game.locs[game.oldlc2].atloc ==
					        NO_OBJECT) {
						break;
					}
					return;
				case 8: /* ogre */
					i = atdwrf(game.loc);
					if (i < 0) {
						game.hints[hint].lc = 0;
						return;
					}
					if (HERE(OGRE) && i == 0) {
						break;
					}
					return;
				case 9: /* jade */
					if (game.tally == 1 &&
					    (OBJECT_IS_STASHED(JADE) ||
					     OBJECT_IS_NOTFOUND(JADE))) {
						break;
					}
					game.hints[hint].lc = 0;
					return;
				default: // LCOV_EXCL_LINE
					// Should never happen
					BUG(HINT_NUMBER_EXCEEDS_GOTO_LIST); // LCOV_EXCL_LINE
				}

				/* Fall through to hint display */
				game.hints[hint].lc = 0;
				if (!yes_or_no(hints[hint].question,
				               arbitrary_messages[NO_MESSAGE],
				               arbitrary_messages[OK_MAN])) {
					return;
				}
				rspeak(HINT_COST, hints[hint].penalty,
				       hints[hint].penalty);
				game.hints[hint].used =
				    yes_or_no(arbitrary_messages[WANT_HINT],
				              hints[hint].hint,
				              arbitrary_messages[OK_MAN]);
				if (game.hints[hint].used &&
				    game.limit > WARNTIME) {
					game.limit +=
					    WARNTIME * hints[hint].penalty;
				}
			}
		}
	}
}

static bool spotted_by_pirate(int i) {
	if (i != PIRATE) {
		return false;
	}

	/*  The pirate's spotted him.  Pirate leaves him alone once we've
	 *  found chest.  K counts if a treasure is here.  If not, and
	 *  tally=1 for an unseen chest, let the pirate be spotted.  Note
	 *  that game.objexts,place[CHEST] = LOC_NOWHERE might mean that he's
	 * thrown it to the troll, but in that case he's seen the chest
	 *  OBJECT_IS_FOUND(CHEST) == true. */
	if (game.loc == game.chloc || !OBJECT_IS_NOTFOUND(CHEST)) {
		return true;
	}
	int snarfed = 0;
	bool movechest = false, robplayer = false;
	for (int treasure = 1; treasure < NOBJECTS; treasure++) {
		if (!objects[treasure].is_treasure) {
			continue;
		}
		/*  Pirate won't take pyramid from plover room or dark
		 *  room (too easy!). */
		if (treasure == PYRAMID &&
		    (game.loc == objects[PYRAMID].plac ||
		     game.loc == objects[EMERALD].plac)) {
			continue;
		}
		if (TOTING(treasure) || HERE(treasure)) {
			++snarfed;
		}
		if (TOTING(treasure)) {
			movechest = true;
			robplayer = true;
		}
	}
	/* Force chest placement before player finds last treasure */
	if (game.tally == 1 && snarfed == 0 &&
	    game.objects[CHEST].place == LOC_NOWHERE && HERE(LAMP) &&
	    game.objects[LAMP].prop == LAMP_BRIGHT) {
		rspeak(PIRATE_SPOTTED);
		movechest = true;
	}
	/* Do things in this order (chest move before robbery) so chest is
	 * listed last at the maze location. */
	if (movechest) {
		move(CHEST, game.chloc);
		move(MESSAG, game.chloc2);
		game.dwarves[PIRATE].loc = game.chloc;
		game.dwarves[PIRATE].oldloc = game.chloc;
		game.dwarves[PIRATE].seen = false;
	} else {
		/* You might get a hint of the pirate's presence even if the
		 * chest doesn't move... */
		if (game.dwarves[PIRATE].oldloc != game.dwarves[PIRATE].loc &&
		    PCT(20)) {
			rspeak(PIRATE_RUSTLES);
		}
	}
	if (robplayer) {
		rspeak(PIRATE_POUNCES);
		for (int treasure = 1; treasure < NOBJECTS; treasure++) {
			if (!objects[treasure].is_treasure) {
				continue;
			}
			if (!(treasure == PYRAMID &&
			      (game.loc == objects[PYRAMID].plac ||
			       game.loc == objects[EMERALD].plac))) {
				if (AT(treasure) &&
				    game.objects[treasure].fixed == IS_FREE) {
					carry(treasure, game.loc);
				}
				if (TOTING(treasure)) {
					drop(treasure, game.chloc);
				}
			}
		}
	}

	return true;
}

static bool dwarfmove(void) {
	/* Dwarves move.  Return true if player survives, false if he dies. */
	int kk, stick, attack;
	loc_t tk[21];

	/*  Dwarf stuff.  See earlier comments for description of
	 *  variables.  Remember sixth dwarf is pirate and is thus
	 *  very different except for motion rules. */

	/*  First off, don't let the dwarves follow him into a pit or a
	 *  wall.  Activate the whole mess the first time he gets as far
	 *  as the Hall of Mists (what INDEEP() tests).  If game.newloc
	 *  is forbidden to pirate (in particular, if it's beyond the
	 *  troll bridge), bypass dwarf stuff.  That way pirate can't
	 *  steal return toll, and dwarves can't meet the bear.  Also
	 *  means dwarves won't follow him into dead end in maze, but
	 *  c'est la vie.  They'll wait for him outside the dead end. */
	if (game.loc == LOC_NOWHERE || FORCED(game.loc) ||
	    CNDBIT(game.newloc, COND_NOARRR)) {
		return true;
	}

	/* Dwarf activity level ratchets up */
	if (game.dflag == 0) {
		if (INDEEP(game.loc)) {
			game.dflag = 1;
		}
		return true;
	}

	/*  When we encounter the first dwarf, we kill 0, 1, or 2 of
	 *  the 5 dwarves.  If any of the survivors is at game.loc,
	 *  replace him with the alternate. */
	if (game.dflag == 1) {
		if (!INDEEP(game.loc) ||
		    (PCT(95) && (!CNDBIT(game.loc, COND_NOBACK) || PCT(85)))) {
			return true;
		}
		game.dflag = 2;
		for (int i = 1; i <= 2; i++) {
			int j = 1 + randrange(NDWARVES - 1);
			if (PCT(50)) {
				game.dwarves[j].loc = 0;
			}
		}

		/* Alternate initial loc for dwarf, in case one of them
		 *  starts out on top of the adventurer. */
		for (int i = 1; i <= NDWARVES - 1; i++) {
			if (game.dwarves[i].loc == game.loc) {
				game.dwarves[i].loc = DALTLC;
			}
			game.dwarves[i].oldloc = game.dwarves[i].loc;
		}
		rspeak(DWARF_RAN);
		drop(AXE, game.loc);
		return true;
	}

	/*  Things are in full swing.  Move each dwarf at random,
	 *  except if he's seen us he sticks with us.  Dwarves stay
	 *  deep inside.  If wandering at random, they don't back up
	 *  unless there's no alternative.  If they don't have to
	 *  move, they attack.  And, of course, dead dwarves don't do
	 *  much of anything. */
	game.dtotal = 0;
	attack = 0;
	stick = 0;
	for (int i = 1; i <= NDWARVES; i++) {
		if (game.dwarves[i].loc == 0) {
			continue;
		}
		/*  Fill tk array with all the places this dwarf might go. */
		unsigned int j = 1;
		kk = tkey[game.dwarves[i].loc];
		if (kk != 0) {
			do {
				enum desttype_t desttype = travel[kk].desttype;
				game.newloc = travel[kk].destval;
				/* Have we avoided a dwarf encounter? */
				if (desttype != dest_goto) {
					continue;
				} else if (!INDEEP(game.newloc)) {
					continue;
				} else if (game.newloc ==
				           game.dwarves[i].oldloc) {
					continue;
				} else if (j > 1 && game.newloc == tk[j - 1]) {
					continue;
				} else if (j >= DIM(tk) - 1) {
					/* This can't actually happen. */
					continue; // LCOV_EXCL_LINE
				} else if (game.newloc == game.dwarves[i].loc) {
					continue;
				} else if (FORCED(game.newloc)) {
					continue;
				} else if (i == PIRATE &&
				           CNDBIT(game.newloc, COND_NOARRR)) {
					continue;
				} else if (travel[kk].nodwarves) {
					continue;
				}
				tk[j++] = game.newloc;
			} while (!travel[kk++].stop);
		}
		tk[j] = game.dwarves[i].oldloc;
		if (j >= 2) {
			--j;
		}
		j = 1 + randrange(j);
		game.dwarves[i].oldloc = game.dwarves[i].loc;
		game.dwarves[i].loc = tk[j];
		game.dwarves[i].seen =
		    (game.dwarves[i].seen && INDEEP(game.loc)) ||
		    (game.dwarves[i].loc == game.loc ||
		     game.dwarves[i].oldloc == game.loc);
		if (!game.dwarves[i].seen) {
			continue;
		}
		game.dwarves[i].loc = game.loc;
		if (spotted_by_pirate(i)) {
			continue;
		}
		/* This threatening little dwarf is in the room with him! */
		++game.dtotal;
		if (game.dwarves[i].oldloc == game.dwarves[i].loc) {
			++attack;
			if (game.knfloc >= LOC_NOWHERE) {
				game.knfloc = game.loc;
			}
			if (randrange(1000) < 95 * (game.dflag - 2)) {
				++stick;
			}
		}
	}

	/*  Now we know what's happening.  Let's tell the poor sucker about it.
	 */
	if (game.dtotal == 0) {
		return true;
	}
	rspeak(game.dtotal == 1 ? DWARF_SINGLE : DWARF_PACK, game.dtotal);
	if (attack == 0) {
		return true;
	}
	if (game.dflag == 2) {
		game.dflag = 3;
	}
	if (attack > 1) {
		rspeak(THROWN_KNIVES, attack);
		rspeak(stick > 1 ? MULTIPLE_HITS
		                 : (stick == 1 ? ONE_HIT : NONE_HIT),
		       stick);
	} else {
		rspeak(KNIFE_THROWN);
		rspeak(stick ? GETS_YOU : MISSES_YOU);
	}
	if (stick == 0) {
		return true;
	}
	game.oldlc2 = game.loc;
	return false;
}

/*  "You're dead, Jim."
 *
 *  If the current loc is zero, it means the clown got himself killed.
 *  We'll allow this maxdie times.  NDEATHS is automatically set based
 *  on the number of snide messages available.  Each death results in
 *  a message (obituaries[n]) which offers reincarnation; if accepted,
 *  this results in message obituaries[0], obituaries[2], etc.  The
 *  last time, if he wants another chance, he gets a snide remark as
 *  we exit.  When reincarnated, all objects being carried get dropped
 *  at game.oldlc2 (presumably the last place prior to being killed)
 *  without change of props.  The loop runs backwards to assure that
 *  the bird is dropped before the cage.  (This kluge could be changed
 *  once we're sure all references to bird and cage are done by
 *  keywords.)  The lamp is a special case (it wouldn't do to leave it
 *  in the cave). It is turned off and left outside the building (only
 *  if he was carrying it, of course).  He himself is left inside the
 *  building (and heaven help him if he tries to xyzzy back into the
 *  cave without the lamp!).  game.oldloc is zapped so he can't just
 *  "retreat". */
static void croak(void) {
	/*  Okay, he's dead.  Let's get on with it. */
	const char *query = obituaries[game.numdie].query;
	const char *yes_response = obituaries[game.numdie].yes_response;

	++game.numdie;

	if (game.closng) {
		/*  He died during closing time.  No resurrection.  Tally up a
		 *  death and exit. */
		rspeak(DEATH_CLOSING);
		terminate(endgame);
	} else if (!yes_or_no(query, yes_response,
	                      arbitrary_messages[OK_MAN]) ||
	           game.numdie == NDEATHS) {
		/* Player is asked if he wants to try again. If not, or if
		 * he's already used all of his lives, we end the game */
		terminate(endgame);
	} else {
		/* If player wishes to continue, we empty the liquids in the
		 * user's inventory, turn off the lamp, and drop all items
		 * where he died. */
		game.objects[WATER].place = game.objects[OIL].place =
		    LOC_NOWHERE;
		if (TOTING(LAMP)) {
			game.objects[LAMP].prop = LAMP_DARK;
		}
		for (int j = 1; j < NOBJECTS; j++) {
			int i = NOBJECTS - j;
			if (TOTING(i)) {
				/* Always leave lamp where it's accessible
				 * aboveground */
				drop(i, (i == LAMP) ? LOC_START : game.oldlc2);
			}
		}
		game.oldloc = game.loc = game.newloc = LOC_BUILDING;
	}
}

static void describe_location(void) {
	/* Describe the location to the user */
	const char *msg = locations[game.loc].description.small;

	if (MOD(game.locs[game.loc].abbrev, game.abbnum) == 0 ||
	    msg == NO_MESSAGE) {
		msg = locations[game.loc].description.big;
	}

	if (!FORCED(game.loc) && IS_DARK_HERE()) {
		msg = arbitrary_messages[PITCH_DARK];
	}

	if (TOTING(BEAR)) {
		rspeak(TAME_BEAR);
	}

	speak(msg);

	if (game.loc == LOC_Y2 && PCT(25) && !game.closng) {
		rspeak(SAYS_PLUGH);
	}
}

static bool traveleq(int a, int b) {
	/* Are two travel entries equal for purposes of skip after failed
	 * condition? */
	return (travel[a].condtype == travel[b].condtype) &&
	       (travel[a].condarg1 == travel[b].condarg1) &&
	       (travel[a].condarg2 == travel[b].condarg2) &&
	       (travel[a].desttype == travel[b].desttype) &&
	       (travel[a].destval == travel[b].destval);
}

/*  Given the current location in "game.loc", and a motion verb number in
 *  "motion", put the new location in "game.newloc".  The current loc is saved
 *  in "game.oldloc" in case he wants to retreat.  The current
 *  game.oldloc is saved in game.oldlc2, in case he dies.  (if he
 *  does, game.newloc will be limbo, and game.oldloc will be what killed
 *  him, so we need game.oldlc2, which is the last place he was
 *  safe.) */
static void playermove(int motion) {
	int travel_entry = tkey[game.loc];
	game.newloc = game.loc;
	if (travel_entry == 0) {
		BUG(LOCATION_HAS_NO_TRAVEL_ENTRIES); // LCOV_EXCL_LINE
	}
	if (motion == NUL) {
		return;
	} else if (motion == BACK) {
		/*  Handle "go back".  Look for verb which goes from game.loc to
		 *  game.oldloc, or to game.oldlc2 If game.oldloc has
		 * forced-motion. te_tmp saves entry -> forced loc -> previous
		 * loc. */
		motion = game.oldloc;
		if (FORCED(motion)) {
			motion = game.oldlc2;
		}
		game.oldlc2 = game.oldloc;
		game.oldloc = game.loc;
		if (CNDBIT(game.loc, COND_NOBACK)) {
			rspeak(TWIST_TURN);
			return;
		}
		if (motion == game.loc) {
			rspeak(FORGOT_PATH);
			return;
		}

		int te_tmp = 0;
		for (;;) {
			enum desttype_t desttype =
			    travel[travel_entry].desttype;
			int scratchloc = travel[travel_entry].destval;
			if (desttype != dest_goto || scratchloc != motion) {
				if (desttype == dest_goto) {
					if (FORCED(scratchloc) &&
					    travel[tkey[scratchloc]].destval ==
					        motion) {
						te_tmp = travel_entry;
					}
				}
				if (!travel[travel_entry].stop) {
					++travel_entry; /* go to next travel
					                   entry for this
					                   location */
					continue;
				}
				/* we've reached the end of travel entries for
				 * game.loc */
				travel_entry = te_tmp;
				if (travel_entry == 0) {
					rspeak(NOT_CONNECTED);
					return;
				}
			}

			motion = travel[travel_entry].motion;
			travel_entry = tkey[game.loc];
			break; /* fall through to ordinary travel */
		}
	} else if (motion == LOOK) {
		/*  Look.  Can't give more detail.  Pretend it wasn't dark
		 *  (though it may now be dark) so he won't fall into a
		 *  pit while staring into the gloom. */
		if (game.detail < 3) {
			rspeak(NO_MORE_DETAIL);
		}
		++game.detail;
		game.wzdark = false;
		game.locs[game.loc].abbrev = 0;
		return;
	} else if (motion == CAVE) {
		/*  Cave.  Different messages depending on whether above ground.
		 */
		rspeak((OUTSIDE(game.loc) && game.loc != LOC_GRATE)
		           ? FOLLOW_STREAM
		           : NEED_DETAIL);
		return;
	} else {
		/* none of the specials */
		game.oldlc2 = game.oldloc;
		game.oldloc = game.loc;
	}

	/* Look for a way to fulfil the motion verb passed in - travel_entry
	 * indexes the beginning of the motion entries for here (game.loc). */
	for (;;) {
		if ((travel[travel_entry].motion == HERE) ||
		    travel[travel_entry].motion == motion) {
			break;
		}
		if (travel[travel_entry].stop) {
			/*  Couldn't find an entry matching the motion word
			 * passed in.  Various messages depending on word given.
			 */
			switch (motion) {
			case EAST:
			case WEST:
			case SOUTH:
			case NORTH:
			case NE:
			case NW:
			case SW:
			case SE:
			case UP:
			case DOWN:
				rspeak(BAD_DIRECTION);
				break;
			case FORWARD:
			case LEFT:
			case RIGHT:
				rspeak(UNSURE_FACING);
				break;
			case OUTSIDE:
			case INSIDE:
				rspeak(NO_INOUT_HERE);
				break;
			case XYZZY:
			case PLUGH:
				rspeak(NOTHING_HAPPENS);
				break;
			case CRAWL:
				rspeak(WHICH_WAY);
				break;
			default:
				rspeak(CANT_APPLY);
			}
			return;
		}
		++travel_entry;
	}

	/* (ESR) We've found a destination that goes with the motion verb.
	 * Next we need to check any conditional(s) on this destination, and
	 * possibly on following entries. */
	do {
		for (;;) { /* L12 loop */
			for (;;) {
				enum condtype_t condtype =
				    travel[travel_entry].condtype;
				int condarg1 = travel[travel_entry].condarg1;
				int condarg2 = travel[travel_entry].condarg2;
				if (condtype < cond_not) {
					/* YAML N and [pct N] conditionals */
					if (condtype == cond_goto ||
					    condtype == cond_pct) {
						if (condarg1 == 0 ||
						    PCT(condarg1)) {
							break;
						}
						/* else fall through */
					}
					/* YAML [with OBJ] clause */
					else if (TOTING(condarg1) ||
					         (condtype == cond_with &&
					          AT(condarg1))) {
						break;
					}
					/* else fall through to check [not OBJ
					 * STATE] */
				} else if (game.objects[condarg1].prop !=
				           condarg2) {
					break;
				}

				/* We arrive here on conditional failure.
				 * Skip to next non-matching destination */
				int te_tmp = travel_entry;
				do {
					if (travel[te_tmp].stop) {
						BUG(CONDITIONAL_TRAVEL_ENTRY_WITH_NO_ALTERATION); // LCOV_EXCL_LINE
					}
					++te_tmp;
				} while (traveleq(travel_entry, te_tmp));
				travel_entry = te_tmp;
			}

			/* Found an eligible rule, now execute it */
			enum desttype_t desttype =
			    travel[travel_entry].desttype;
			game.newloc = travel[travel_entry].destval;
			if (desttype == dest_goto) {
				return;
			}

			if (desttype == dest_speak) {
				/* Execute a speak rule */
				rspeak(game.newloc);
				game.newloc = game.loc;
				return;
			} else {
				switch (game.newloc) {
				case 1:
					/* Special travel 1.  Plover-alcove
					 * passage.  Can carry only emerald.
					 * Note: travel table must include
					 * "useless" entries going through
					 * passage, which can never be used for
					 * actual motion, but can be spotted by
					 * "go back". */
					game.newloc = (game.loc == LOC_PLOVER)
					                  ? LOC_ALCOVE
					                  : LOC_PLOVER;
					if (game.holdng > 1 ||
					    (game.holdng == 1 &&
					     !TOTING(EMERALD))) {
						game.newloc = game.loc;
						rspeak(MUST_DROP);
					}
					return;
				case 2:
					/* Special travel 2.  Plover transport.
					 * Drop the emerald (only use special
					 * travel if toting it), so he's forced
					 * to use the plover-passage to get it
					 * out.  Having dropped it, go back and
					 * pretend he wasn't carrying it after
					 * all. */
					drop(EMERALD, game.loc);
					{
						int te_tmp = travel_entry;
						do {
							if (travel[te_tmp]
							        .stop) {
								BUG(CONDITIONAL_TRAVEL_ENTRY_WITH_NO_ALTERATION); // LCOV_EXCL_LINE
							}
							++te_tmp;
						} while (traveleq(travel_entry,
						                  te_tmp));
						travel_entry = te_tmp;
					}
					continue; /* goto L12 */
				case 3:
					/* Special travel 3.  Troll bridge. Must
					 * be done only as special motion so
					 * that dwarves won't wander across and
					 * encounter the bear.  (They won't
					 * follow the player there because that
					 * region is forbidden to the pirate.)
					 * If game.prop[TROLL]=TROLL_PAIDONCE,
					 * he's crossed since paying, so step
					 * out and block him. (standard travel
					 * entries check for
					 * game.prop[TROLL]=TROLL_UNPAID.)
					 * Special stuff for bear. */
					if (game.objects[TROLL].prop ==
					    TROLL_PAIDONCE) {
						pspeak(TROLL, look, true,
						       TROLL_PAIDONCE);
						game.objects[TROLL].prop =
						    TROLL_UNPAID;
						DESTROY(TROLL2);
						move(TROLL2 + NOBJECTS,
						     IS_FREE);
						move(TROLL,
						     objects[TROLL].plac);
						move(TROLL + NOBJECTS,
						     objects[TROLL].fixd);
						juggle(CHASM);
						game.newloc = game.loc;
						return;
					} else {
						game.newloc =
						    objects[TROLL].plac +
						    objects[TROLL].fixd -
						    game.loc;
						if (game.objects[TROLL].prop ==
						    TROLL_UNPAID) {
							game.objects[TROLL]
							    .prop =
							    TROLL_PAIDONCE;
						}
						if (!TOTING(BEAR)) {
							return;
						}
						state_change(CHASM,
						             BRIDGE_WRECKED);
						game.objects[TROLL].prop =
						    TROLL_GONE;
						drop(BEAR, game.newloc);
						game.objects[BEAR].fixed =
						    IS_FIXED;
						game.objects[BEAR].prop =
						    BEAR_DEAD;
						game.oldlc2 = game.newloc;
						croak();
						return;
					}
				default: // LCOV_EXCL_LINE
					BUG(SPECIAL_TRAVEL_500_GT_L_GT_300_EXCEEDS_GOTO_LIST); // LCOV_EXCL_LINE
				}
			}
			break; /* Leave L12 loop */
		}
	} while (false);
}

static void lampcheck(void) {
	/* Check game limit and lamp timers */
	if (game.objects[LAMP].prop == LAMP_BRIGHT) {
		--game.limit;
	}

	/*  Another way we can force an end to things is by having the
	 *  lamp give out.  When it gets close, we come here to warn him.
	 *  First following arm checks if the lamp and fresh batteries are
	 *  here, in which case we replace the batteries and continue.
	 *  Second is for other cases of lamp dying.  Even after it goes
	 *  out, he can explore outside for a while if desired. */
	if (game.limit <= WARNTIME) {
		if (HERE(BATTERY) &&
		    game.objects[BATTERY].prop == FRESH_BATTERIES &&
		    HERE(LAMP)) {
			rspeak(REPLACE_BATTERIES);
			game.objects[BATTERY].prop = DEAD_BATTERIES;
#ifdef __unused__
			/* This code from the original game seems to have been
			 * faulty. No tests ever passed the guard, and with the
			 * guard removed the game hangs when the lamp limit is
			 * reached.
			 */
			if (TOTING(BATTERY)) {
				drop(BATTERY, game.loc);
			}
#endif
			game.limit += BATTERYLIFE;
			game.lmwarn = false;
		} else if (!game.lmwarn && HERE(LAMP)) {
			game.lmwarn = true;
			if (game.objects[BATTERY].prop == DEAD_BATTERIES) {
				rspeak(MISSING_BATTERIES);
			} else if (game.objects[BATTERY].place == LOC_NOWHERE) {
				rspeak(LAMP_DIM);
			} else {
				rspeak(GET_BATTERIES);
			}
		}
	}
	if (game.limit == 0) {
		game.limit = -1;
		game.objects[LAMP].prop = LAMP_DARK;
		if (HERE(LAMP)) {
			rspeak(LAMP_OUT);
		}
	}
}

/*  Handle the closing of the cave.  The cave closes "clock1" turns
 *  after the last treasure has been located (including the pirate's
 *  chest, which may of course never show up).  Note that the
 *  treasures need not have been taken yet, just located.  Hence
 *  clock1 must be large enough to get out of the cave (it only ticks
 *  while inside the cave).  When it hits zero, we start closing the
 *  cave, and then sit back and wait for him to try to get out.  If he
 *  doesn't within clock2 turns, we close the cave; if he does try, we
 *  assume he panics, and give him a few additional turns to get
 *  frantic before we close.  When clock2 hits zero, we transport him
 *  into the final puzzle.  Note that the puzzle depends upon all
 *  sorts of random things.  For instance, there must be no water or
 *  oil, since there are beanstalks which we don't want to be able to
 *  water, since the code can't handle it.  Also, we can have no keys,
 *  since there is a grate (having moved the fixed object!)  there
 *  separating him from all the treasures.  Most of these problems
 *  arise from the use of negative prop numbers to suppress the object
 *  descriptions until he's actually moved the objects. */
static bool closecheck(void) {
	/* If a turn threshold has been met, apply penalties and tell
	 * the player about it. */
	for (int i = 0; i < NTHRESHOLDS; ++i) {
		if (game.turns == turn_thresholds[i].threshold + 1) {
			game.trnluz += turn_thresholds[i].point_loss;
			speak(turn_thresholds[i].message);
		}
	}

	/*  Don't tick game.clock1 unless well into cave (and not at Y2). */
	if (game.tally == 0 && INDEEP(game.loc) && game.loc != LOC_Y2) {
		--game.clock1;
	}

	/*  When the first warning comes, we lock the grate, destroy
	 *  the bridge, kill all the dwarves (and the pirate), remove
	 *  the troll and bear (unless dead), and set "closng" to
	 *  true.  Leave the dragon; too much trouble to move it.
	 *  from now until clock2 runs out, he cannot unlock the
	 *  grate, move to any location outside the cave, or create
	 *  the bridge.  Nor can he be resurrected if he dies.  Note
	 *  that the snake is already gone, since he got to the
	 *  treasure accessible only via the hall of the mountain
	 *  king. Also, he's been in giant room (to get eggs), so we
	 *  can refer to it.  Also also, he's gotten the pearl, so we
	 *  know the bivalve is an oyster.  *And*, the dwarves must
	 *  have been activated, since we've found chest. */
	if (game.clock1 == 0) {
		game.objects[GRATE].prop = GRATE_CLOSED;
		game.objects[FISSURE].prop = UNBRIDGED;
		for (int i = 1; i <= NDWARVES; i++) {
			game.dwarves[i].seen = false;
			game.dwarves[i].loc = LOC_NOWHERE;
		}
		DESTROY(TROLL);
		move(TROLL + NOBJECTS, IS_FREE);
		move(TROLL2, objects[TROLL].plac);
		move(TROLL2 + NOBJECTS, objects[TROLL].fixd);
		juggle(CHASM);
		if (game.objects[BEAR].prop != BEAR_DEAD) {
			DESTROY(BEAR);
		}
		game.objects[CHAIN].prop = CHAIN_HEAP;
		game.objects[CHAIN].fixed = IS_FREE;
		game.objects[AXE].prop = AXE_HERE;
		game.objects[AXE].fixed = IS_FREE;
		rspeak(CAVE_CLOSING);
		game.clock1 = -1;
		game.closng = true;
		return game.closed;
	} else if (game.clock1 < 0) {
		--game.clock2;
	}
	if (game.clock2 == 0) {
		/*  Once he's panicked, and clock2 has run out, we come here
		 *  to set up the storage room.  The room has two locs,
		 *  hardwired as LOC_NE and LOC_SW.  At the ne end, we
		 *  place empty bottles, a nursery of plants, a bed of
		 *  oysters, a pile of lamps, rods with stars, sleeping
		 *  dwarves, and him.  At the sw end we place grate over
		 *  treasures, snake pit, covey of caged birds, more rods, and
		 *  pillows.  A mirror stretches across one wall.  Many of the
		 *  objects come from known locations and/or states (e.g. the
		 *  snake is known to have been destroyed and needn't be
		 *  carried away from its old "place"), making the various
		 *  objects be handled differently.  We also drop all other
		 *  objects he might be carrying (lest he has some which
		 *  could cause trouble, such as the keys).  We describe the
		 *  flash of light and trundle back. */
		put(BOTTLE, LOC_NE, EMPTY_BOTTLE);
		put(PLANT, LOC_NE, PLANT_THIRSTY);
		put(OYSTER, LOC_NE, STATE_FOUND);
		put(LAMP, LOC_NE, LAMP_DARK);
		put(ROD, LOC_NE, STATE_FOUND);
		put(DWARF, LOC_NE, STATE_FOUND);
		game.loc = LOC_NE;
		game.oldloc = LOC_NE;
		game.newloc = LOC_NE;
		/*  Leave the grate with normal (non-negative) property.
		 *  Reuse sign. */
		move(GRATE, LOC_SW);
		move(SIGN, LOC_SW);
		game.objects[SIGN].prop = ENDGAME_SIGN;
		put(SNAKE, LOC_SW, SNAKE_CHASED);
		put(BIRD, LOC_SW, BIRD_CAGED);
		put(CAGE, LOC_SW, STATE_FOUND);
		put(ROD2, LOC_SW, STATE_FOUND);
		put(PILLOW, LOC_SW, STATE_FOUND);

		put(MIRROR, LOC_NE, STATE_FOUND);
		game.objects[MIRROR].fixed = LOC_SW;

		for (int i = 1; i < NOBJECTS; i++) {
			if (TOTING(i)) {
				DESTROY(i);
			}
		}

		rspeak(CAVE_CLOSED);
		game.closed = true;
		return game.closed;
	}

	lampcheck();
	return false;
}

static void listobjects(void) {
	/*  Print out descriptions of objects at this location.  If
	 *  not closing and property value is negative, tally off
	 *  another treasure.  Rug is special case; once seen, its
	 *  game.prop is RUG_DRAGON (dragon on it) till dragon is killed.
	 *  Similarly for chain; game.prop is initially CHAINING_BEAR (locked to
	 *  bear).  These hacks are because game.prop=0 is needed to
	 *  get full score. */
	if (!IS_DARK_HERE()) {
		++game.locs[game.loc].abbrev;
		for (int i = game.locs[game.loc].atloc; i != 0;
		     i = game.link[i]) {
			obj_t obj = i;
			if (obj >= NOBJECTS) {
				obj = obj - NOBJECTS;
			}
			if (obj == STEPS && TOTING(NUGGET)) {
				continue;
			}
			/* (ESR) Warning: it looks like you could get away with
			 * running this code only on objects with the treasure
			 * property set. Nope.  There is mystery here.
			 */
			if (OBJECT_IS_STASHED(obj) || OBJECT_IS_NOTFOUND(obj)) {
				if (game.closed) {
					continue;
				}
				OBJECT_SET_FOUND(obj);
				if (obj == RUG) {
					game.objects[RUG].prop = RUG_DRAGON;
				}
				if (obj == CHAIN) {
					game.objects[CHAIN].prop =
					    CHAINING_BEAR;
				}
				if (obj == EGGS) {
					game.seenbigwords = true;
				}
				--game.tally;
				/*  Note: There used to be a test here to see
				 * whether the player had blown it so badly that
				 * he could never ever see the remaining
				 * treasures, and if so the lamp was zapped to
				 *  35 turns.  But the tests were too
				 * simple-minded; things like killing the bird
				 * before the snake was gone (can never see
				 * jewelry), and doing it "right" was hopeless.
				 * E.G., could cross troll bridge several times,
				 * using up all available treasures, breaking
				 * vase, using coins to buy batteries, etc., and
				 * eventually never be able to get across again.
				 * If bottle were left on far side, could then
				 *  never get eggs or trident, and the effects
				 * propagate.  So the whole thing was flushed.
				 * anyone who makes such a gross blunder isn't
				 * likely to find everything else anyway (so
				 * goes the rationalisation). */
			}
			int kk = game.objects[obj].prop;
			if (obj == STEPS) {
				kk = (game.loc == game.objects[STEPS].fixed)
				         ? STEPS_UP
				         : STEPS_DOWN;
			}
			pspeak(obj, look, true, kk);
		}
	}
}

/* Pre-processes a command input to see if we need to tease out a few specific
 * cases:
 * - "enter water" or "enter stream":
 *   weird specific case that gets the user wet, and then kicks us back to get
 * another command
 * - <object> <verb>:
 *   Irregular form of input, but should be allowed. We switch back to <verb>
 * <object> form for further processing.
 * - "grate":
 *   If in location with grate, we move to that grate. If we're in a number of
 * other places, we move to the entrance.
 * - "water plant", "oil plant", "water door", "oil door":
 *   Change to "pour water" or "pour oil" based on context
 * - "cage bird":
 *   If bird is present, we change to "carry bird"
 *
 * Returns true if pre-processing is complete, and we're ready to move to the
 * primary command processing, false otherwise. */
static bool preprocess_command(command_t *command) {
	if (command->word[0].type == MOTION && command->word[0].id == ENTER &&
	    (command->word[1].id == STREAM || command->word[1].id == WATER)) {
		if (LIQLOC(game.loc) == WATER) {
			rspeak(FEET_WET);
		} else {
			rspeak(WHERE_QUERY);
		}
	} else {
		if (command->word[0].type == OBJECT) {
			/* From OV to VO form */
			if (command->word[1].type == ACTION) {
				command_word_t stage = command->word[0];
				command->word[0] = command->word[1];
				command->word[1] = stage;
			}

			if (command->word[0].id == GRATE) {
				command->word[0].type = MOTION;
				if (game.loc == LOC_START ||
				    game.loc == LOC_VALLEY ||
				    game.loc == LOC_SLIT) {
					command->word[0].id = DEPRESSION;
				}
				if (game.loc == LOC_COBBLE ||
				    game.loc == LOC_DEBRIS ||
				    game.loc == LOC_AWKWARD ||
				    game.loc == LOC_BIRDCHAMBER ||
				    game.loc == LOC_PITTOP) {
					command->word[0].id = ENTRANCE;
				}
			}
			if ((command->word[0].id == WATER ||
			     command->word[0].id == OIL) &&
			    (command->word[1].id == PLANT ||
			     command->word[1].id == DOOR)) {
				if (AT(command->word[1].id)) {
					command->word[1] = command->word[0];
					command->word[0].id = POUR;
					command->word[0].type = ACTION;
					strncpy(command->word[0].raw, "pour",
					        LINESIZE - 1);
				}
			}
			if (command->word[0].id == CAGE &&
			    command->word[1].id == BIRD && HERE(CAGE) &&
			    HERE(BIRD)) {
				command->word[0].id = CARRY;
				command->word[0].type = ACTION;
			}
		}

		/* If no word type is given for the first word, we assume it's a
		 * motion. */
		if (command->word[0].type == NO_WORD_TYPE) {
			command->word[0].type = MOTION;
		}

		command->state = PREPROCESSED;
		return true;
	}
	return false;
}

static bool do_move(void) {
	/* Actually execute the move to the new location and dwarf movement */
	/*  Can't leave cave once it's closing (except by main office). */
	if (OUTSIDE(game.newloc) && game.newloc != 0 && game.closng) {
		rspeak(EXIT_CLOSED);
		game.newloc = game.loc;
		if (!game.panic) {
			game.clock2 = PANICTIME;
		}
		game.panic = true;
	}

	/*  See if a dwarf has seen him and has come from where he
	 *  wants to go.  If so, the dwarf's blocking his way.  If
	 *  coming from place forbidden to pirate (dwarves rooted in
	 *  place) let him get out (and attacked). */
	if (game.newloc != game.loc && !FORCED(game.loc) &&
	    !CNDBIT(game.loc, COND_NOARRR)) {
		for (size_t i = 1; i <= NDWARVES - 1; i++) {
			if (game.dwarves[i].oldloc == game.newloc &&
			    game.dwarves[i].seen) {
				game.newloc = game.loc;
				rspeak(DWARF_BLOCK);
				break;
			}
		}
	}
	game.loc = game.newloc;

	if (!dwarfmove()) {
		croak();
	}

	if (game.loc == LOC_NOWHERE) {
		croak();
	}

	/* The easiest way to get killed is to fall into a pit in
	 * pitch darkness. */
	if (!FORCED(game.loc) && IS_DARK_HERE() && game.wzdark &&
	    PCT(PIT_KILL_PROB)) {
		rspeak(PIT_FALL);
		game.oldlc2 = game.loc;
		croak();
		return false;
	}

	return true;
}

static bool do_command(void) {
	/* Get and execute a command */
	static command_t command;
	clear_command(&command);

	/* Describe the current location and (maybe) get next command. */
	while (command.state != EXECUTED) {
		describe_location();

		if (FORCED(game.loc)) {
			playermove(HERE);
			return true;
		}

		listobjects();

		/* Command not yet given; keep getting commands from user
		 * until valid command is both given and executed. */
		clear_command(&command);
		while (command.state <= GIVEN) {

			if (game.closed) {
				/*  If closing time, check for any stashed
				 * objects being toted and unstash them.  This
				 * way objects won't be described until they've
				 * been picked up and put down separate from
				 * their respective piles. */
				if ((OBJECT_IS_NOTFOUND(OYSTER) ||
				     OBJECT_IS_STASHED(OYSTER)) &&
				    TOTING(OYSTER)) {
					pspeak(OYSTER, look, true, 1);
				}
				for (size_t i = 1; i < NOBJECTS; i++) {
					if (TOTING(i) &&
					    (OBJECT_IS_NOTFOUND(i) ||
					     OBJECT_IS_STASHED(i))) {
						OBJECT_STASHIFY(
						    i, game.objects[i].prop);
					}
				}
			}

			/* Check to see if the room is dark. */
			game.wzdark = IS_DARK_HERE();

			/* If the knife is not here it permanently disappears.
			 * Possibly this should fire if the knife is here but
			 * the room is dark? */
			if (game.knfloc > LOC_NOWHERE &&
			    game.knfloc != game.loc) {
				game.knfloc = LOC_NOWHERE;
			}

			/* Check some for hints, get input from user, increment
			 * turn, and pre-process commands. Keep going until
			 * pre-processing is done. */
			while (command.state < PREPROCESSED) {
				checkhints();

				/* Get command input from user */
				if (!get_command_input(&command)) {
					return false;
				}

				/* Every input, check "foobar" flag. If zero,
				 * nothing's going on. If pos, make neg. If neg,
				 * he skipped a word, so make it zero.
				 */
				game.foobar = (game.foobar > WORD_EMPTY)
				                  ? -game.foobar
				                  : WORD_EMPTY;

				++game.turns;
				preprocess_command(&command);
			}

			/* check if game is closed, and exit if it is */
			if (closecheck()) {
				return true;
			}

			/* loop until all words in command are processed */
			while (command.state == PREPROCESSED) {
				command.state = PROCESSING;

				if (command.word[0].id == WORD_NOT_FOUND) {
					/* Gee, I don't understand. */
					sspeak(DONT_KNOW, command.word[0].raw);
					clear_command(&command);
					continue;
				}

				/* Give user hints of shortcuts */
				if (strncasecmp(command.word[0].raw, "west",
				                sizeof("west")) == 0) {
					if (++game.iwest == 10) {
						rspeak(W_IS_WEST);
					}
				}
				if (strncasecmp(command.word[0].raw, "go",
				                sizeof("go")) == 0 &&
				    command.word[1].id != WORD_EMPTY) {
					if (++game.igo == 10) {
						rspeak(GO_UNNEEDED);
					}
				}

				switch (command.word[0].type) {
				case MOTION:
					playermove(command.word[0].id);
					command.state = EXECUTED;
					continue;
				case OBJECT:
					command.part = unknown;
					command.obj = command.word[0].id;
					break;
				case ACTION:
					if (command.word[1].type == NUMERIC) {
						command.part = transitive;
					} else {
						command.part = intransitive;
					}
					command.verb = command.word[0].id;
					break;
				case NUMERIC:
					if (!settings.oldstyle) {
						sspeak(DONT_KNOW,
						       command.word[0].raw);
						clear_command(&command);
						continue;
					}
					break;     // LCOV_EXCL_LINE
				default:           // LCOV_EXCL_LINE
				case NO_WORD_TYPE: // LCOV_EXCL_LINE
					BUG(VOCABULARY_TYPE_N_OVER_1000_NOT_BETWEEN_0_AND_3); // LCOV_EXCL_LINE
				}

				switch (action(command)) {
				case GO_TERMINATE:
					command.state = EXECUTED;
					break;
				case GO_MOVE:
					playermove(NUL);
					command.state = EXECUTED;
					break;
				case GO_WORD2:
#ifdef GDEBUG
					printf("Word shift\n");
#endif /* GDEBUG */
					/* Get second word for analysis. */
					command.word[0] = command.word[1];
					command.word[1] = empty_command_word;
					command.state = PREPROCESSED;
					break;
				case GO_UNKNOWN:
					/*  Random intransitive verbs come here.
					 * Clear obj just in case (see
					 * attack()). */
					command.word[0].raw[0] =
					    toupper(command.word[0].raw[0]);
					sspeak(DO_WHAT, command.word[0].raw);
					command.obj = NO_OBJECT;

					/* object cleared; we need to go back to
					 * the preprocessing step */
					command.state = GIVEN;
					break;
				case GO_CHECKHINT: // FIXME: re-name to be more
				                   // contextual; this was
				                   // previously a label
					command.state = GIVEN;
					break;
				case GO_DWARFWAKE:
					/*  Oh dear, he's disturbed the dwarves.
					 */
					rspeak(DWARVES_AWAKEN);
					terminate(endgame);
				case GO_CLEAROBJ: // FIXME: re-name to be more
				                  // contextual; this was
				                  // previously a label
					clear_command(&command);
					break;
				case GO_TOP: // FIXME: re-name to be more
				             // contextual; this was previously
				             // a label
					break;
				default: // LCOV_EXCL_LINE
					BUG(ACTION_RETURNED_PHASE_CODE_BEYOND_END_OF_SWITCH); // LCOV_EXCL_LINE
				}
			} /* while command has not been fully processed */
		}         /* while command is not yet given */
	}                 /* while command is not executed */

	/* command completely executed; we return true. */
	return true;
}

/*
 * MAIN PROGRAM
 *
 *  Adventure (rev 2: 20 treasures)
 *  History: Original idea & 5-treasure version (adventures) by Willie Crowther
 *           15-treasure version (adventure) by Don Woods, April-June 1977
 *           20-treasure version (rev 2) by Don Woods, August 1978
 *		Errata fixed: 78/12/25
 *	     Revived 2017 as Open Adventure.
 */

int main(int argc, char *argv[]) {
	int ch;

	/*  Options. */

#if defined ADVENT_AUTOSAVE
	const char *opts = "dl:oa:";
	const char *usage =
	    "Usage: %s [-l logfilename] [-o] [-a filename] [script...]\n";
	const FILE *rfp = NULL;
	const char *autosave_filename = NULL;
#elif !defined ADVENT_NOSAVE
	const char *opts = "dl:or:";
	const char *usage = "Usage: %s [-l logfilename] [-o] [-r "
	                    "restorefilename] [script...]\n";
	FILE *rfp = NULL;
#else
	const char *opts = "dl:o";
	const char *usage = "Usage: %s [-l logfilename] [-o] [script...]\n";
#endif
	while ((ch = getopt(argc, argv, opts)) != EOF) {
		switch (ch) {
		case 'd':                    // LCOV_EXCL_LINE
			settings.debug += 1; // LCOV_EXCL_LINE
			break;               // LCOV_EXCL_LINE
		case 'l':
			settings.logfp = fopen(optarg, "w");
			if (settings.logfp == NULL) {
				fprintf(
				    stderr,
				    "advent: can't open logfile %s for write\n",
				    optarg);
			}
			signal(SIGINT, sig_handler);
			break;
		case 'o':
			settings.oldstyle = true;
			settings.prompt = false;
			break;
#ifdef ADVENT_AUTOSAVE
		case 'a':
			rfp = fopen(optarg, READ_MODE);
			autosave_filename = optarg;
			signal(SIGHUP, sig_handler);
			signal(SIGTERM, sig_handler);
			break;
#elif !defined ADVENT_NOSAVE
		case 'r':
			rfp = fopen(optarg, READ_MODE); /* ARM-DOS: binary (was "r") */
			if (rfp == NULL) {
				fprintf(stderr,
				        "advent: can't open save file %s for "
				        "read\n",
				        optarg);
			}
			break;
#endif
		default:
			fprintf(stderr, usage, argv[0]);
			fprintf(stderr, "        -l create a log file of your "
			                "game named as specified'\n");
			fprintf(stderr,
			        "        -o 'oldstyle' (no prompt, no command "
			        "editing, displays 'Initialising...')\n");
#if defined ADVENT_AUTOSAVE
			fprintf(stderr, "        -a automatic save/restore "
			                "from specified saved game file\n");
#elif !defined ADVENT_NOSAVE
			fprintf(stderr, "        -r restore from specified "
			                "saved game file\n");
#endif
			exit(EXIT_FAILURE);
			break;
		}
	}

	/* copy invocation line part after switches */
	settings.argc = argc - optind;
	settings.argv = argv + optind;
	settings.optind = 0;

	/*  Initialize game variables */
	int seedval = initialise();

#if !defined ADVENT_NOSAVE
	if (!rfp) {
		game.novice = yes_or_no(arbitrary_messages[WELCOME_YOU],
		                        arbitrary_messages[CAVE_NEARBY],
		                        arbitrary_messages[NO_MESSAGE]);
		if (game.novice) {
			game.limit = NOVICELIMIT;
		}
	} else {
		restore(rfp);
#if defined ADVENT_AUTOSAVE
		score(scoregame);
#endif
	}
#if defined ADVENT_AUTOSAVE
	if (autosave_filename != NULL) {
		if ((autosave_fp = fopen(autosave_filename, WRITE_MODE)) ==
		    NULL) {
			perror(autosave_filename);
			return EXIT_FAILURE;
		}
		autosave();
	}
#endif
#else
	game.novice = yes_or_no(arbitrary_messages[WELCOME_YOU],
	                        arbitrary_messages[CAVE_NEARBY],
	                        arbitrary_messages[NO_MESSAGE]);
	if (game.novice) {
		game.limit = NOVICELIMIT;
	}
#endif

	if (settings.logfp) {
		fprintf(settings.logfp, "seed %d\n", seedval);
	}

	/* interpret commands until EOF or interrupt */
	for (;;) {
		// if we're supposed to move, move
		if (!do_move()) {
			continue;
		}

		// get command
		if (!do_command()) {
			break;
		}
	}
	/* show score and exit */
	terminate(quitgame);
}

/* end */
