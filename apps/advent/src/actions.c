/*
 * Actions for the dungeon-running code.
 *
 * SPDX-FileCopyrightText: (C) 1977, 2005 Will Crowther and Don Woods
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "advent.h"

static phase_codes_t fill(verb_t, obj_t);

static phase_codes_t attack(command_t command) {
	/*  Attack.  Assume target if unambiguous.  "Throw" also links here.
	 *  Attackable objects fall into two categories: enemies (snake,
	 *  dwarf, etc.)  and others (bird, clam, machine).  Ambiguous if 2
	 *  enemies, or no enemies but 2 others. */
	verb_t verb = command.verb;
	obj_t obj = command.obj;

	if (obj == INTRANSITIVE) {
		int changes = 0;
		if (atdwrf(game.loc) > 0) {
			obj = DWARF;
			++changes;
		}
		if (HERE(SNAKE)) {
			obj = SNAKE;
			++changes;
		}
		if (AT(DRAGON) && game.objects[DRAGON].prop == DRAGON_BARS) {
			obj = DRAGON;
			++changes;
		}
		if (AT(TROLL)) {
			obj = TROLL;
			++changes;
		}
		if (AT(OGRE)) {
			obj = OGRE;
			++changes;
		}
		if (HERE(BEAR) && game.objects[BEAR].prop == UNTAMED_BEAR) {
			obj = BEAR;
			++changes;
		}
		/* check for low-priority targets */
		if (obj == INTRANSITIVE) {
			/* Can't attack bird or machine by throwing axe. */
			if (HERE(BIRD) && verb != THROW) {
				obj = BIRD;
				++changes;
			}
			if (HERE(VEND) && verb != THROW) {
				obj = VEND;
				++changes;
			}
			/* Clam and oyster both treated as clam for intransitive
			 * case; no harm done. */
			if (HERE(CLAM) || HERE(OYSTER)) {
				obj = CLAM;
				++changes;
			}
		}
		if (changes >= 2) {
			return GO_UNKNOWN;
		}
	}

	if (obj == BIRD) {
		if (game.closed) {
			rspeak(UNHAPPY_BIRD);
		} else {
			DESTROY(BIRD);
			rspeak(BIRD_DEAD);
		}
		return GO_CLEAROBJ;
	}
	if (obj == VEND) {
		state_change(VEND, game.objects[VEND].prop == VEND_BLOCKS
		                       ? VEND_UNBLOCKS
		                       : VEND_BLOCKS);

		return GO_CLEAROBJ;
	}

	if (obj == BEAR) {
		switch (game.objects[BEAR].prop) {
		case UNTAMED_BEAR:
			rspeak(BEAR_HANDS);
			break;
		case SITTING_BEAR:
			rspeak(BEAR_CONFUSED);
			break;
		case CONTENTED_BEAR:
			rspeak(BEAR_CONFUSED);
			break;
		case BEAR_DEAD:
			rspeak(ALREADY_DEAD);
			break;
		}
		return GO_CLEAROBJ;
	}
	if (obj == DRAGON && game.objects[DRAGON].prop == DRAGON_BARS) {
		/*  Fun stuff for dragon.  If he insists on attacking it, win!
		 *  Set game.prop to dead, move dragon to central loc (still
		 *  fixed), move rug there (not fixed), and move him there,
		 *  too.  Then do a null motion to get new description. */
		rspeak(BARE_HANDS_QUERY);
		if (!silent_yes_or_no()) {
			speak(arbitrary_messages[NASTY_DRAGON]);
			return GO_MOVE;
		}
		state_change(DRAGON, DRAGON_DEAD);
		game.objects[RUG].prop = RUG_FLOOR;
		/* Hardcoding LOC_SECRET5 as the dragon's death location is
		 * ugly. The way it was computed before was worse; it depended
		 * on the two dragon locations being LOC_SECRET4 and LOC_SECRET6
		 * and LOC_SECRET5 being right between them.
		 */
		move(DRAGON + NOBJECTS, IS_FIXED);
		move(RUG + NOBJECTS, IS_FREE);
		move(DRAGON, LOC_SECRET5);
		move(RUG, LOC_SECRET5);
		drop(BLOOD, LOC_SECRET5);
		for (obj_t i = 1; i < NOBJECTS; i++) {
			if (game.objects[i].place == objects[DRAGON].plac ||
			    game.objects[i].place == objects[DRAGON].fixd) {
				move(i, LOC_SECRET5);
			}
		}
		game.loc = LOC_SECRET5;
		return GO_MOVE;
	}

	if (obj == OGRE) {
		rspeak(OGRE_DODGE);
		if (atdwrf(game.loc) == 0) {
			return GO_CLEAROBJ;
		}
		rspeak(KNIFE_THROWN);
		DESTROY(OGRE);
		int dwarves = 0;
		for (int i = 1; i < PIRATE; i++) {
			if (game.dwarves[i].loc == game.loc) {
				++dwarves;
				game.dwarves[i].loc = LOC_LONGWEST;
				game.dwarves[i].seen = false;
			}
		}
		rspeak((dwarves > 1) ? OGRE_PANIC1 : OGRE_PANIC2);
		return GO_CLEAROBJ;
	}

	switch (obj) {
	case INTRANSITIVE:
		rspeak(NO_TARGET);
		break;
	case CLAM:
	case OYSTER:
		rspeak(SHELL_IMPERVIOUS);
		break;
	case SNAKE:
		rspeak(SNAKE_WARNING);
		break;
	case DWARF:
		if (game.closed) {
			return GO_DWARFWAKE;
		}
		rspeak(BARE_HANDS_QUERY);
		break;
	case DRAGON:
		rspeak(ALREADY_DEAD);
		break;
	case TROLL:
		rspeak(ROCKY_TROLL);
		break;
	default:
		speak(actions[verb].message);
	}
	return GO_CLEAROBJ;
}

static phase_codes_t bigwords(vocab_t id) {
	/* Only called on FEE FIE FOE FOO (AND FUM).  Advance to next state if
	 * given in proper order. Look up foo in special section of vocab to
	 * determine which word we've got. Last word zips the eggs back to the
	 * giant room (unless already there). */
	int foobar = abs(game.foobar);

	/* Only FEE can start a magic-word sequence. */
	if ((foobar == WORD_EMPTY) &&
	    (id == FIE || id == FOE || id == FOO || id == FUM)) {
		rspeak(NOTHING_HAPPENS);
		return GO_CLEAROBJ;
	}

	if ((foobar == WORD_EMPTY && id == FEE) ||
	    (foobar == FEE && id == FIE) || (foobar == FIE && id == FOE) ||
	    (foobar == FOE && id == FOO)) {
		game.foobar = id;
		if (id != FOO) {
			rspeak(OK_MAN);
			return GO_CLEAROBJ;
		}
		game.foobar = WORD_EMPTY;
		if (game.objects[EGGS].place == objects[EGGS].plac ||
		    (TOTING(EGGS) && game.loc == objects[EGGS].plac)) {
			rspeak(NOTHING_HAPPENS);
			return GO_CLEAROBJ;
		} else {
			/*  Bring back troll if we steal the eggs back from him
			 * before crossing. */
			if (game.objects[EGGS].place == LOC_NOWHERE &&
			    game.objects[TROLL].place == LOC_NOWHERE &&
			    game.objects[TROLL].prop == TROLL_UNPAID) {
				game.objects[TROLL].prop = TROLL_PAIDONCE;
			}
			if (HERE(EGGS)) {
				pspeak(EGGS, look, true, EGGS_VANISHED);
			} else if (game.loc == objects[EGGS].plac) {
				pspeak(EGGS, look, true, EGGS_HERE);
			} else {
				pspeak(EGGS, look, true, EGGS_DONE);
			}
			move(EGGS, objects[EGGS].plac);

			return GO_CLEAROBJ;
		}
	} else {
		/* Magic-word sequence was started but is incorrect */
		if (settings.oldstyle || game.seenbigwords) {
			rspeak(START_OVER);
		} else {
			rspeak(WELL_POINTLESS);
		}
		game.foobar = WORD_EMPTY;
		return GO_CLEAROBJ;
	}
}

static void blast(void) {
	/*  Blast.  No effect unless you've got dynamite, which is a neat trick!
	 */
	if (OBJECT_IS_NOTFOUND(ROD2) || !game.closed) {
		rspeak(REQUIRES_DYNAMITE);
	} else {
		if (HERE(ROD2)) {
			game.bonus = splatter;
			rspeak(SPLATTER_MESSAGE);
		} else if (game.loc == LOC_NE) {
			game.bonus = defeat;
			rspeak(DEFEAT_MESSAGE);
		} else {
			game.bonus = victory;
			rspeak(VICTORY_MESSAGE);
		}
		terminate(endgame);
	}
}

static phase_codes_t vbreak(verb_t verb, obj_t obj) {
	/*  Break.  Only works for mirror in repository and, of course, the
	 * vase. */
	switch (obj) {
	case MIRROR:
		if (game.closed) {
			state_change(MIRROR, MIRROR_BROKEN);
			return GO_DWARFWAKE;
		} else {
			rspeak(TOO_FAR);
			break;
		}
	case VASE:
		if (game.objects[VASE].prop == VASE_WHOLE) {
			if (TOTING(VASE)) {
				drop(VASE, game.loc);
			}
			state_change(VASE, VASE_BROKEN);
			game.objects[VASE].fixed = IS_FIXED;
			break;
		}
	/* FALLTHRU */
	default:
		speak(actions[verb].message);
	}
	return (GO_CLEAROBJ);
}

static phase_codes_t brief(void) {
	/*  Brief.  Intransitive only.  Suppress full descriptions after first
	 * time. */
	game.abbnum = 10000;
	game.detail = 3;
	rspeak(BRIEF_CONFIRM);
	return GO_CLEAROBJ;
}

static phase_codes_t vcarry(verb_t verb, obj_t obj) {
	/*  Carry an object.  Special cases for bird and cage (if bird in cage,
	 * can't take one without the other).  Liquids also special, since they
	 * depend on status of bottle.  Also various side effects, etc. */
	if (obj == INTRANSITIVE) {
		/*  Carry, no object given yet.  OK if only one object present.
		 */
		if (game.locs[game.loc].atloc == NO_OBJECT ||
		    game.link[game.locs[game.loc].atloc] != 0 ||
		    atdwrf(game.loc) > 0) {
			return GO_UNKNOWN;
		}
		obj = game.locs[game.loc].atloc;
	}

	if (TOTING(obj)) {
		speak(actions[verb].message);
		return GO_CLEAROBJ;
	}

	if (obj == MESSAG) {
		rspeak(REMOVE_MESSAGE);
		DESTROY(MESSAG);
		return GO_CLEAROBJ;
	}

	if (game.objects[obj].fixed != IS_FREE) {
		switch (obj) {
		case PLANT:
			rspeak((game.objects[PLANT].prop == PLANT_THIRSTY ||
			        OBJECT_IS_STASHED(PLANT))
			           ? DEEP_ROOTS
			           : YOU_JOKING);
			break;
		case BEAR:
			rspeak(game.objects[BEAR].prop == SITTING_BEAR
			           ? BEAR_CHAINED
			           : YOU_JOKING);
			break;
		case CHAIN:
			rspeak(game.objects[BEAR].prop != UNTAMED_BEAR
			           ? STILL_LOCKED
			           : YOU_JOKING);
			break;
		case RUG:
			rspeak(game.objects[RUG].prop == RUG_HOVER
			           ? RUG_HOVERS
			           : YOU_JOKING);
			break;
		case URN:
			rspeak(URN_NOBUDGE);
			break;
		case CAVITY:
			rspeak(DOUGHNUT_HOLES);
			break;
		case BLOOD:
			rspeak(FEW_DROPS);
			break;
		case SIGN:
			rspeak(HAND_PASSTHROUGH);
			break;
		default:
			rspeak(YOU_JOKING);
		}
		return GO_CLEAROBJ;
	}

	if (obj == WATER || obj == OIL) {
		if (!HERE(BOTTLE) || LIQUID() != obj) {
			if (!TOTING(BOTTLE)) {
				rspeak(NO_CONTAINER);
				return GO_CLEAROBJ;
			}
			if (game.objects[BOTTLE].prop == EMPTY_BOTTLE) {
				return (fill(verb, BOTTLE));
			} else {
				rspeak(BOTTLE_FULL);
			}
			return GO_CLEAROBJ;
		}
		obj = BOTTLE;
	}

	if (game.holdng >= INVLIMIT) {
		rspeak(CARRY_LIMIT);
		return GO_CLEAROBJ;
	}

	if (obj == BIRD && game.objects[BIRD].prop != BIRD_CAGED &&
	    !OBJECT_IS_STASHED(BIRD)) {
		if (game.objects[BIRD].prop == BIRD_FOREST_UNCAGED) {
			DESTROY(BIRD);
			rspeak(BIRD_CRAP);
			return GO_CLEAROBJ;
		}
		if (!TOTING(CAGE)) {
			rspeak(CANNOT_CARRY);
			return GO_CLEAROBJ;
		}
		if (TOTING(ROD)) {
			rspeak(BIRD_EVADES);
			return GO_CLEAROBJ;
		}
		game.objects[BIRD].prop = BIRD_CAGED;
	}
	if ((obj == BIRD || obj == CAGE) &&
	    OBJECT_STATE_EQUALS(BIRD, BIRD_CAGED)) {
		/* expression maps BIRD to CAGE and CAGE to BIRD */
		carry(BIRD + CAGE - obj, game.loc);
	}

	carry(obj, game.loc);

	if (obj == BOTTLE && LIQUID() != NO_OBJECT) {
		game.objects[LIQUID()].place = CARRIED;
	}

	if (GSTONE(obj) && !OBJECT_IS_FOUND(obj)) {
		OBJECT_SET_FOUND(obj);
		game.objects[CAVITY].prop = CAVITY_EMPTY;
	}
	rspeak(OK_MAN);
	return GO_CLEAROBJ;
}

static int chain(verb_t verb) {
	/* Do something to the bear's chain */
	if (verb != LOCK) {
		if (game.objects[BEAR].prop == UNTAMED_BEAR) {
			rspeak(BEAR_BLOCKS);
			return GO_CLEAROBJ;
		}
		if (game.objects[CHAIN].prop == CHAIN_HEAP) {
			rspeak(ALREADY_UNLOCKED);
			return GO_CLEAROBJ;
		}
		game.objects[CHAIN].prop = CHAIN_HEAP;
		game.objects[CHAIN].fixed = IS_FREE;
		if (game.objects[BEAR].prop != BEAR_DEAD) {
			game.objects[BEAR].prop = CONTENTED_BEAR;
		}

		switch (game.objects[BEAR].prop) {
		// LCOV_EXCL_START
		case BEAR_DEAD:
			/* Can't be reached until the bear can die in some way
			 * other than a bridge collapse. Leave in in case this
			 * changes, but exclude from coverage testing. */
			game.objects[BEAR].fixed = IS_FIXED;
			break;
		// LCOV_EXCL_STOP
		default:
			game.objects[BEAR].fixed = IS_FREE;
		}
		rspeak(CHAIN_UNLOCKED);
		return GO_CLEAROBJ;
	}

	if (game.objects[CHAIN].prop != CHAIN_HEAP) {
		rspeak(ALREADY_LOCKED);
		return GO_CLEAROBJ;
	}
	if (game.loc != objects[CHAIN].plac) {
		rspeak(NO_LOCKSITE);
		return GO_CLEAROBJ;
	}

	game.objects[CHAIN].prop = CHAIN_FIXED;

	if (TOTING(CHAIN)) {
		drop(CHAIN, game.loc);
	}
	game.objects[CHAIN].fixed = IS_FIXED;

	rspeak(CHAIN_LOCKED);
	return GO_CLEAROBJ;
}

static phase_codes_t discard(verb_t verb, obj_t obj) {
	/*  Discard object.  "Throw" also comes here for most objects.  Special
	 * cases for bird (might attack snake or dragon) and cage (might contain
	 * bird) and vase. Drop coins at vending machine for extra batteries. */
	if (obj == ROD && !TOTING(ROD) && TOTING(ROD2)) {
		obj = ROD2;
	}

	if (!TOTING(obj)) {
		speak(actions[verb].message);
		return GO_CLEAROBJ;
	}

	if (GSTONE(obj) && AT(CAVITY) &&
	    game.objects[CAVITY].prop != CAVITY_FULL) {
		rspeak(GEM_FITS);
		game.objects[obj].prop = STATE_IN_CAVITY;
		game.objects[CAVITY].prop = CAVITY_FULL;
		if (HERE(RUG) &&
		    ((obj == EMERALD && game.objects[RUG].prop != RUG_HOVER) ||
		     (obj == RUBY && game.objects[RUG].prop == RUG_HOVER))) {
			if (obj == RUBY) {
				rspeak(RUG_SETTLES);
			} else if (TOTING(RUG)) {
				rspeak(RUG_WIGGLES);
			} else {
				rspeak(RUG_RISES);
			}
			if (!TOTING(RUG) || obj == RUBY) {
				int k = (game.objects[RUG].prop == RUG_HOVER)
				            ? RUG_FLOOR
				            : RUG_HOVER;
				game.objects[RUG].prop = k;
				if (k == RUG_HOVER) {
					k = objects[SAPPH].plac;
				}
				move(RUG + NOBJECTS, k);
			}
		}
		drop(obj, game.loc);
		return GO_CLEAROBJ;
	}

	if (obj == COINS && HERE(VEND)) {
		DESTROY(COINS);
		drop(BATTERY, game.loc);
		pspeak(BATTERY, look, true, FRESH_BATTERIES);
		return GO_CLEAROBJ;
	}

	if (LIQUID() == obj) {
		obj = BOTTLE;
	}
	if (obj == BOTTLE && LIQUID() != NO_OBJECT) {
		game.objects[LIQUID()].place = LOC_NOWHERE;
	}

	if (obj == BEAR && AT(TROLL)) {
		state_change(TROLL, TROLL_GONE);
		move(TROLL, LOC_NOWHERE);
		move(TROLL + NOBJECTS, IS_FREE);
		move(TROLL2, objects[TROLL].plac);
		move(TROLL2 + NOBJECTS, objects[TROLL].fixd);
		juggle(CHASM);
		drop(obj, game.loc);
		return GO_CLEAROBJ;
	}

	if (obj == VASE) {
		if (game.loc != objects[PILLOW].plac) {
			state_change(VASE,
			             AT(PILLOW) ? VASE_WHOLE : VASE_DROPPED);
			if (game.objects[VASE].prop != VASE_WHOLE) {
				game.objects[VASE].fixed = IS_FIXED;
			}
			drop(obj, game.loc);
			return GO_CLEAROBJ;
		}
	}

	if (obj == CAGE && game.objects[BIRD].prop == BIRD_CAGED) {
		drop(BIRD, game.loc);
	}

	if (obj == BIRD) {
		if (AT(DRAGON) && game.objects[DRAGON].prop == DRAGON_BARS) {
			rspeak(BIRD_BURNT);
			DESTROY(BIRD);
			return GO_CLEAROBJ;
		}
		if (HERE(SNAKE)) {
			rspeak(BIRD_ATTACKS);
			if (game.closed) {
				return GO_DWARFWAKE;
			}
			DESTROY(SNAKE);
			/* Set game.prop for use by travel options */
			game.objects[SNAKE].prop = SNAKE_CHASED;
		} else {
			rspeak(OK_MAN);
		}

		game.objects[BIRD].prop =
		    FOREST(game.loc) ? BIRD_FOREST_UNCAGED : BIRD_UNCAGED;
		drop(obj, game.loc);
		return GO_CLEAROBJ;
	}

	rspeak(OK_MAN);
	drop(obj, game.loc);
	return GO_CLEAROBJ;
}

static phase_codes_t drink(verb_t verb, obj_t obj) {
	/*  Drink.  If no object, assume water and look for it here.  If water
	 * is in the bottle, drink that, else must be at a water loc, so drink
	 * stream. */
	if (obj == INTRANSITIVE && LIQLOC(game.loc) != WATER &&
	    (LIQUID() != WATER || !HERE(BOTTLE))) {
		return GO_UNKNOWN;
	}

	if (obj == BLOOD) {
		DESTROY(BLOOD);
		state_change(DRAGON, DRAGON_BLOODLESS);
		game.blooded = true;
		return GO_CLEAROBJ;
	}

	if (obj != INTRANSITIVE && obj != WATER) {
		rspeak(RIDICULOUS_ATTEMPT);
		return GO_CLEAROBJ;
	}
	if (LIQUID() == WATER && HERE(BOTTLE)) {
		game.objects[WATER].place = LOC_NOWHERE;
		state_change(BOTTLE, EMPTY_BOTTLE);
		return GO_CLEAROBJ;
	}

	speak(actions[verb].message);
	return GO_CLEAROBJ;
}

static phase_codes_t eat(verb_t verb, obj_t obj) {
	/*  Eat.  Intransitive: assume food if present, else ask what.
	 * Transitive: food ok, some things lose appetite, rest are ridiculous.
	 */
	switch (obj) {
	case INTRANSITIVE:
		if (!HERE(FOOD)) {
			return GO_UNKNOWN;
		}
	/* FALLTHRU */
	case FOOD:
		DESTROY(FOOD);
		rspeak(THANKS_DELICIOUS);
		break;
	case BIRD:
	case SNAKE:
	case CLAM:
	case OYSTER:
	case DWARF:
	case DRAGON:
	case TROLL:
	case BEAR:
	case OGRE:
		rspeak(LOST_APPETITE);
		break;
	default:
		speak(actions[verb].message);
	}
	return GO_CLEAROBJ;
}

static phase_codes_t extinguish(verb_t verb, obj_t obj) {
	/* Extinguish.  Lamp, urn, dragon/volcano (nice try). */
	if (obj == INTRANSITIVE) {
		if (HERE(LAMP) && game.objects[LAMP].prop == LAMP_BRIGHT) {
			obj = LAMP;
		}
		if (HERE(URN) && game.objects[URN].prop == URN_LIT) {
			obj = URN;
		}
		if (obj == INTRANSITIVE) {
			return GO_UNKNOWN;
		}
	}

	switch (obj) {
	case URN:
		if (game.objects[URN].prop != URN_EMPTY) {
			state_change(URN, URN_DARK);
		} else {
			pspeak(URN, change, true, URN_DARK);
		}
		break;
	case LAMP:
		state_change(LAMP, LAMP_DARK);
		rspeak(IS_DARK_HERE() ? PITCH_DARK : NO_MESSAGE);
		break;
	case DRAGON:
	case VOLCANO:
		rspeak(BEYOND_POWER);
		break;
	default:
		speak(actions[verb].message);
	}
	return GO_CLEAROBJ;
}

static phase_codes_t feed(verb_t verb, obj_t obj) {
	/*  Feed.  If bird, no seed.  Snake, dragon, troll: quip.  If dwarf,
	 * make him mad.  Bear, special. */
	switch (obj) {
	case BIRD:
		rspeak(BIRD_PINING);
		break;
	case DRAGON:
		if (game.objects[DRAGON].prop != DRAGON_BARS) {
			rspeak(RIDICULOUS_ATTEMPT);
		} else {
			rspeak(NOTHING_EDIBLE);
		}
		break;
	case SNAKE:
		if (!game.closed && HERE(BIRD)) {
			DESTROY(BIRD);
			rspeak(BIRD_DEVOURED);
		} else {
			rspeak(NOTHING_EDIBLE);
		}
		break;
	case TROLL:
		rspeak(TROLL_VICES);
		break;
	case DWARF:
		if (HERE(FOOD)) {
			game.dflag += 2;
			rspeak(REALLY_MAD);
		} else {
			speak(actions[verb].message);
		}
		break;
	case BEAR:
		if (game.objects[BEAR].prop == BEAR_DEAD) {
			rspeak(RIDICULOUS_ATTEMPT);
			break;
		}
		if (game.objects[BEAR].prop == UNTAMED_BEAR) {
			if (HERE(FOOD)) {
				DESTROY(FOOD);
				game.objects[AXE].fixed = IS_FREE;
				game.objects[AXE].prop = AXE_HERE;
				state_change(BEAR, SITTING_BEAR);
			} else {
				rspeak(NOTHING_EDIBLE);
			}
			break;
		}
		speak(actions[verb].message);
		break;
	case OGRE:
		if (HERE(FOOD)) {
			rspeak(OGRE_FULL);
		} else {
			speak(actions[verb].message);
		}
		break;
	default:
		rspeak(AM_GAME);
	}
	return GO_CLEAROBJ;
}

phase_codes_t fill(verb_t verb, obj_t obj) {
	/*  Fill.  Bottle or urn must be empty, and liquid available.  (Vase
	 *  is nasty.) */
	if (obj == VASE) {
		if (LIQLOC(game.loc) == NO_OBJECT) {
			rspeak(FILL_INVALID);
			return GO_CLEAROBJ;
		}
		if (!TOTING(VASE)) {
			rspeak(ARENT_CARRYING);
			return GO_CLEAROBJ;
		}
		rspeak(SHATTER_VASE);
		game.objects[VASE].prop = VASE_BROKEN;
		game.objects[VASE].fixed = IS_FIXED;
		drop(VASE, game.loc);
		return GO_CLEAROBJ;
	}

	if (obj == URN) {
		if (game.objects[URN].prop != URN_EMPTY) {
			rspeak(FULL_URN);
			return GO_CLEAROBJ;
		}
		if (!HERE(BOTTLE)) {
			rspeak(FILL_INVALID);
			return GO_CLEAROBJ;
		}
		int k = LIQUID();
		switch (k) {
		case WATER:
			game.objects[BOTTLE].prop = EMPTY_BOTTLE;
			rspeak(WATER_URN);
			break;
		case OIL:
			game.objects[URN].prop = URN_DARK;
			game.objects[BOTTLE].prop = EMPTY_BOTTLE;
			rspeak(OIL_URN);
			break;
		case NO_OBJECT:
		default:
			rspeak(FILL_INVALID);
			return GO_CLEAROBJ;
		}
		game.objects[k].place = LOC_NOWHERE;
		return GO_CLEAROBJ;
	}
	if (obj != INTRANSITIVE && obj != BOTTLE) {
		speak(actions[verb].message);
		return GO_CLEAROBJ;
	}
	if (obj == INTRANSITIVE && !HERE(BOTTLE)) {
		return GO_UNKNOWN;
	}

	if (HERE(URN) && game.objects[URN].prop != URN_EMPTY) {
		rspeak(URN_NOPOUR);
		return GO_CLEAROBJ;
	}
	if (LIQUID() != NO_OBJECT) {
		rspeak(BOTTLE_FULL);
		return GO_CLEAROBJ;
	}
	if (LIQLOC(game.loc) == NO_OBJECT) {
		rspeak(NO_LIQUID);
		return GO_CLEAROBJ;
	}

	state_change(BOTTLE,
	             (LIQLOC(game.loc) == OIL) ? OIL_BOTTLE : WATER_BOTTLE);
	if (TOTING(BOTTLE)) {
		game.objects[LIQUID()].place = CARRIED;
	}
	return GO_CLEAROBJ;
}

static phase_codes_t find(verb_t verb, obj_t obj) {
	/* Find.  Might be carrying it, or it might be here.  Else give caveat.
	 */
	if (TOTING(obj)) {
		rspeak(ALREADY_CARRYING);
		return GO_CLEAROBJ;
	}

	if (game.closed) {
		rspeak(NEEDED_NEARBY);
		return GO_CLEAROBJ;
	}

	if (AT(obj) || (LIQUID() == obj && AT(BOTTLE)) ||
	    obj == LIQLOC(game.loc) || (obj == DWARF && atdwrf(game.loc) > 0)) {
		rspeak(YOU_HAVEIT);
		return GO_CLEAROBJ;
	}

	speak(actions[verb].message);
	return GO_CLEAROBJ;
}

static phase_codes_t fly(verb_t verb, obj_t obj) {
	/* Fly.  Snide remarks unless hovering rug is here. */
	if (obj == INTRANSITIVE) {
		if (!HERE(RUG)) {
			rspeak(FLAP_ARMS);
			return GO_CLEAROBJ;
		}
		if (game.objects[RUG].prop != RUG_HOVER) {
			rspeak(RUG_NOTHING2);
			return GO_CLEAROBJ;
		}
		obj = RUG;
	}

	if (obj != RUG) {
		speak(actions[verb].message);
		return GO_CLEAROBJ;
	}
	if (game.objects[RUG].prop != RUG_HOVER) {
		rspeak(RUG_NOTHING1);
		return GO_CLEAROBJ;
	}

	if (game.loc == LOC_CLIFF) {
		game.oldlc2 = game.oldloc;
		game.oldloc = game.loc;
		game.newloc = LOC_LEDGE;
		rspeak(RUG_GOES);
	} else if (game.loc == LOC_LEDGE) {
		game.oldlc2 = game.oldloc;
		game.oldloc = game.loc;
		game.newloc = LOC_CLIFF;
		rspeak(RUG_RETURNS);
	} else {
		// LCOV_EXCL_START
		/* should never happen */
		rspeak(NOTHING_HAPPENS);
		// LCOV_EXCL_STOP
	}
	return GO_TERMINATE;
}

static phase_codes_t inven(void) {
	/* Inventory. If object, treat same as find.  Else report on current
	 * burden. */
	bool empty = true;
	for (obj_t i = 1; i < NOBJECTS; i++) {
		if (i == BEAR || !TOTING(i)) {
			continue;
		}
		if (empty) {
			rspeak(NOW_HOLDING);
			empty = false;
		}
		pspeak(i, touch, false, -1);
	}
	if (TOTING(BEAR)) {
		rspeak(TAME_BEAR);
	}
	if (empty) {
		rspeak(NO_CARRY);
	}
	return GO_CLEAROBJ;
}

static phase_codes_t light(verb_t verb, obj_t obj) {
	/*  Light.  Applicable only to lamp and urn. */
	if (obj == INTRANSITIVE) {
		int selects = 0;
		if (HERE(LAMP) && game.objects[LAMP].prop == LAMP_DARK &&
		    game.limit >= 0) {
			obj = LAMP;
			selects++;
		}
		if (HERE(URN) && game.objects[URN].prop == URN_DARK) {
			obj = URN;
			selects++;
		}
		if (selects != 1) {
			return GO_UNKNOWN;
		}
	}

	switch (obj) {
	case URN:
		state_change(URN, game.objects[URN].prop == URN_EMPTY
		                      ? URN_EMPTY
		                      : URN_LIT);
		break;
	case LAMP:
		if (game.limit < 0) {
			rspeak(LAMP_OUT);
			break;
		}
		state_change(LAMP, LAMP_BRIGHT);
		if (game.wzdark) {
			return GO_TOP;
		}
		break;
	default:
		speak(actions[verb].message);
	}
	return GO_CLEAROBJ;
}

static phase_codes_t listen(void) {
	/*  Listen.  Intransitive only.  Print stuff based on object sound
	 * properties. */
	bool soundlatch = false;
	vocab_t sound = locations[game.loc].sound;
	if (sound != SILENT) {
		rspeak(sound);
		if (!locations[game.loc].loud) {
			rspeak(NO_MESSAGE);
		}
		soundlatch = true;
	}
		for (obj_t i = 1; i < NOBJECTS; i++) {
		if (!HERE(i) || objects[i].sounds[0] == NULL ||
		    OBJECT_IS_STASHED(i) || OBJECT_IS_NOTFOUND(i)) {
			continue;
		}
		int mi = game.objects[i].prop;
		/* (ESR) Some unpleasant magic on object states here. Ideally
		 * we'd have liked the bird to be a normal object that we can
		 * use state_change() on; can't do it, because there are
		 * actually two different series of per-state birdsounds
		 * depending on whether player has drunk dragon's blood. */
		if (i == BIRD) {
			mi += 3 * game.blooded;
		}
		pspeak(i, hear, true, mi, game.zzword);
		rspeak(NO_MESSAGE);
		if (i == BIRD && mi == BIRD_ENDSTATE) {
			DESTROY(BIRD);
		}
		soundlatch = true;
	}
	if (!soundlatch) {
		rspeak(ALL_SILENT);
	}
	return GO_CLEAROBJ;
}

static phase_codes_t lock(verb_t verb, obj_t obj) {
	/* Lock, unlock, no object given.  Assume various things if present. */
	if (obj == INTRANSITIVE) {
		if (HERE(CLAM)) {
			obj = CLAM;
		}
		if (HERE(OYSTER)) {
			obj = OYSTER;
		}
		if (AT(DOOR)) {
			obj = DOOR;
		}
		if (AT(GRATE)) {
			obj = GRATE;
		}
		if (HERE(CHAIN)) {
			obj = CHAIN;
		}
		if (obj == INTRANSITIVE) {
			rspeak(NOTHING_LOCKED);
			return GO_CLEAROBJ;
		}
	}

	/*  Lock, unlock object.  Special stuff for opening clam/oyster
	 *  and for chain. */

	switch (obj) {
	case CHAIN:
		if (HERE(KEYS)) {
			return chain(verb);
		} else {
			rspeak(NO_KEYS);
		}
		break;
	case GRATE:
		if (HERE(KEYS)) {
			if (game.closng) {
				rspeak(EXIT_CLOSED);
				if (!game.panic) {
					game.clock2 = PANICTIME;
				}
				game.panic = true;
			} else {
				state_change(GRATE, (verb == LOCK)
				                        ? GRATE_CLOSED
				                        : GRATE_OPEN);
			}
		} else {
			rspeak(NO_KEYS);
		}
		break;
	case CLAM:
		if (verb == LOCK) {
			rspeak(HUH_MAN);
		} else if (TOTING(CLAM)) {
			rspeak(DROP_CLAM);
		} else if (!TOTING(TRIDENT)) {
			rspeak(CLAM_OPENER);
		} else {
			DESTROY(CLAM);
			drop(OYSTER, game.loc);
			drop(PEARL, LOC_CULDESAC);
			rspeak(PEARL_FALLS);
		}
		break;
	case OYSTER:
		if (verb == LOCK) {
			rspeak(HUH_MAN);
		} else if (TOTING(OYSTER)) {
			rspeak(DROP_OYSTER);
		} else if (!TOTING(TRIDENT)) {
			rspeak(OYSTER_OPENER);
		} else {
			rspeak(OYSTER_OPENS);
		}
		break;
	case DOOR:
		rspeak((game.objects[DOOR].prop == DOOR_UNRUSTED) ? OK_MAN
		                                                  : RUSTY_DOOR);
		break;
	case CAGE:
		rspeak(NO_LOCK);
		break;
	case KEYS:
		rspeak(CANNOT_UNLOCK);
		break;
	default:
		speak(actions[verb].message);
	}

	return GO_CLEAROBJ;
}

static phase_codes_t pour(verb_t verb, obj_t obj) {
	/*  Pour.  If no object, or object is bottle, assume contents of bottle.
	 *  special tests for pouring water or oil on plant or rusty door. */
	if (obj == BOTTLE || obj == INTRANSITIVE) {
		obj = LIQUID();
	}
	if (obj == NO_OBJECT) {
		return GO_UNKNOWN;
	}
	if (!TOTING(obj)) {
		speak(actions[verb].message);
		return GO_CLEAROBJ;
	}

	if (obj != OIL && obj != WATER) {
		rspeak(CANT_POUR);
		return GO_CLEAROBJ;
	}
	if (HERE(URN) && game.objects[URN].prop == URN_EMPTY) {
		return fill(verb, URN);
	}
	game.objects[BOTTLE].prop = EMPTY_BOTTLE;
	game.objects[obj].place = LOC_NOWHERE;
	if (!(AT(PLANT) || AT(DOOR))) {
		rspeak(GROUND_WET);
		return GO_CLEAROBJ;
	}
	if (!AT(DOOR)) {
		if (obj == WATER) {
			/* cycle through the three plant states */
			state_change(PLANT,
			             MOD(game.objects[PLANT].prop + 1, 3));
			game.objects[PLANT2].prop = game.objects[PLANT].prop;
			return GO_MOVE;
		} else {
			rspeak(SHAKING_LEAVES);
			return GO_CLEAROBJ;
		}
	} else {
		state_change(DOOR, (obj == OIL) ? DOOR_UNRUSTED : DOOR_RUSTED);
		return GO_CLEAROBJ;
	}
}

static phase_codes_t quit(void) {
	/*  Quit.  Intransitive only.  Verify intent and exit if that's what he
	 * wants. */
	if (yes_or_no(arbitrary_messages[REALLY_QUIT],
	              arbitrary_messages[OK_MAN], arbitrary_messages[OK_MAN])) {
		terminate(quitgame);
	}
	return GO_CLEAROBJ;
}

static phase_codes_t read(command_t command)
/*  Read.  Print stuff based on objtxt.  Oyster (?) is special case. */
{
	if (command.obj == INTRANSITIVE) {
		typedef enum {
			READ_INTRANSITIVE_NO_MATCH,
			READ_INTRANSITIVE_SINGLE_MATCH,
			READ_INTRANSITIVE_AMBIGUOUS
		} read_intransitive_state_t;

		read_intransitive_state_t state =
		    READ_INTRANSITIVE_NO_MATCH;
		obj_t resolved_obj = NO_OBJECT;
		for (int i = 1; i < NOBJECTS; i++) {
			if (HERE(i) && objects[i].texts[0] != NULL &&
			    !OBJECT_IS_STASHED(i)) {
				switch (state) {
				case READ_INTRANSITIVE_NO_MATCH:
					state = READ_INTRANSITIVE_SINGLE_MATCH;
					resolved_obj = i;
					break;
				case READ_INTRANSITIVE_SINGLE_MATCH:
					state = READ_INTRANSITIVE_AMBIGUOUS;
					break;
				case READ_INTRANSITIVE_AMBIGUOUS:
					break;
				}
			}
		}
		if (state != READ_INTRANSITIVE_SINGLE_MATCH || IS_DARK_HERE()) {
			return GO_UNKNOWN;
		}
		command.obj = resolved_obj;
	}

	if (IS_DARK_HERE()) {
		sspeak(NO_SEE, command.word[0].raw);
	} else if (command.obj == OYSTER) {
		if (!TOTING(OYSTER) || !game.closed) {
			rspeak(DONT_UNDERSTAND);
		} else if (!game.clshnt) {
			game.clshnt = yes_or_no(arbitrary_messages[CLUE_QUERY],
			                        arbitrary_messages[WAYOUT_CLUE],
			                        arbitrary_messages[OK_MAN]);
		} else {
			pspeak(OYSTER, hear, true,
			       1); // Not really a sound, but oh well.
		}
	} else if (objects[command.obj].texts[0] == NULL ||
	           OBJECT_IS_NOTFOUND(command.obj)) {
		speak(actions[command.verb].message);
	} else {
		pspeak(command.obj, study, true,
		       game.objects[command.obj].prop);
	}
	return GO_CLEAROBJ;
}

static phase_codes_t reservoir(void) {
	/*  Z'ZZZ (word gets recomputed at startup; different each game). */
	if (!AT(RESER) && game.loc != LOC_RESBOTTOM) {
		rspeak(NOTHING_HAPPENS);
		return GO_CLEAROBJ;
	} else {
		state_change(RESER, game.objects[RESER].prop == WATERS_PARTED
		                        ? WATERS_UNPARTED
		                        : WATERS_PARTED);
		if (AT(RESER)) {
			return GO_CLEAROBJ;
		} else {
			game.oldlc2 = game.loc;
			game.newloc = LOC_NOWHERE;
			rspeak(NOT_BRIGHT);
			return GO_TERMINATE;
		}
	}
}

static phase_codes_t rub(verb_t verb, obj_t obj) {
	/* Rub.  Yields various snide remarks except for lit urn. */
	if (obj == URN && game.objects[URN].prop == URN_LIT) {
		DESTROY(URN);
		drop(AMBER, game.loc);
		game.objects[AMBER].prop = AMBER_IN_ROCK;
		--game.tally;
		drop(CAVITY, game.loc);
		rspeak(URN_GENIES);
	} else if (obj != LAMP) {
		rspeak(PECULIAR_NOTHING);
	} else {
		speak(actions[verb].message);
	}
	return GO_CLEAROBJ;
}

static phase_codes_t say(command_t command) {
	/* Say.  Echo WD2. Magic words override. */
	if (command.word[1].type == MOTION &&
	    (command.word[1].id == XYZZY || command.word[1].id == PLUGH ||
	     command.word[1].id == PLOVER)) {
		return GO_WORD2;
	}
	if (command.word[1].type == ACTION && command.word[1].id == PART) {
		return reservoir();
	}

	if (command.word[1].type == ACTION &&
	    (command.word[1].id == FEE || command.word[1].id == FIE ||
	     command.word[1].id == FOE || command.word[1].id == FOO ||
	     command.word[1].id == FUM || command.word[1].id == PART)) {
		return bigwords(command.word[1].id);
	}
	sspeak(OKEY_DOKEY, command.word[1].raw);
	return GO_CLEAROBJ;
}

static phase_codes_t throw_support(vocab_t spk) {
	rspeak(spk);
	drop(AXE, game.loc);
	return GO_MOVE;
}

static phase_codes_t throwit(command_t command) {
	/*  Throw.  Same as discard unless axe.  Then same as attack except
	 *  ignore bird, and if dwarf is present then one might be killed.
	 *  (Only way to do so!)  Axe also special for dragon, bear, and
	 *  troll.  Treasures special for troll. */
	if (!TOTING(command.obj)) {
		speak(actions[command.verb].message);
		return GO_CLEAROBJ;
	}
	if (objects[command.obj].is_treasure && AT(TROLL)) {
		/*  Snarf a treasure for the troll. */
		drop(command.obj, LOC_NOWHERE);
		move(TROLL, LOC_NOWHERE);
		move(TROLL + NOBJECTS, IS_FREE);
		drop(TROLL2, objects[TROLL].plac);
		drop(TROLL2 + NOBJECTS, objects[TROLL].fixd);
		juggle(CHASM);
		rspeak(TROLL_SATISFIED);
		return GO_CLEAROBJ;
	}
	if (command.obj == FOOD && HERE(BEAR)) {
		/* But throwing food is another story. */
		command.obj = BEAR;
		return (feed(command.verb, command.obj));
	}
	if (command.obj != AXE) {
		return (discard(command.verb, command.obj));
	} else {
		if (atdwrf(game.loc) <= 0) {
			if (AT(DRAGON) &&
			    game.objects[DRAGON].prop == DRAGON_BARS) {
				return throw_support(DRAGON_SCALES);
			}
			if (AT(TROLL)) {
				return throw_support(TROLL_RETURNS);
			}
			if (AT(OGRE)) {
				return throw_support(OGRE_DODGE);
			}
			if (HERE(BEAR) &&
			    game.objects[BEAR].prop == UNTAMED_BEAR) {
				/* This'll teach him to throw the axe at the
				 * bear! */
				drop(AXE, game.loc);
				game.objects[AXE].fixed = IS_FIXED;
				juggle(BEAR);
				state_change(AXE, AXE_LOST);
				return GO_CLEAROBJ;
			}
			command.obj = INTRANSITIVE;
			return (attack(command));
		}

		if (randrange(NDWARVES + 1) < game.dflag) {
			return throw_support(DWARF_DODGES);
		} else {
			int i = atdwrf(game.loc);
			game.dwarves[i].seen = false;
			game.dwarves[i].loc = LOC_NOWHERE;
			return throw_support(
			    (++game.dkill == 1) ? DWARF_SMOKE : KILLED_DWARF);
		}
	}
}

static phase_codes_t wake(verb_t verb, obj_t obj) {
	/* Wake.  Only use is to disturb the dwarves. */
	if (obj != DWARF || !game.closed) {
		speak(actions[verb].message);
		return GO_CLEAROBJ;
	} else {
		rspeak(PROD_DWARF);
		return GO_DWARFWAKE;
	}
}

static phase_codes_t seed(verb_t verb, const char *arg) {
	/* Set seed */
	int32_t seed = strtol(arg, NULL, 10);
	speak(actions[verb].message, seed);
	set_seed(seed);
	--game.turns;
	return GO_TOP;
}

static phase_codes_t waste(verb_t verb, turn_t turns) {
	/* Burn turns */
	game.limit -= turns;
	speak(actions[verb].message, (int)game.limit);
	return GO_TOP;
}

static phase_codes_t wave(verb_t verb, obj_t obj) {
	/* Wave.  No effect unless waving rod at fissure or at bird. */
	if (obj != ROD || !TOTING(obj) ||
	    (!HERE(BIRD) && (game.closng || !AT(FISSURE)))) {
		speak(((!TOTING(obj)) && (obj != ROD || !TOTING(ROD2)))
		          ? arbitrary_messages[ARENT_CARRYING]
		          : actions[verb].message);
		return GO_CLEAROBJ;
	}

	if (game.objects[BIRD].prop == BIRD_UNCAGED &&
	    game.loc == game.objects[STEPS].place && OBJECT_IS_NOTFOUND(JADE)) {
		drop(JADE, game.loc);
		OBJECT_SET_FOUND(JADE);
		--game.tally;
		rspeak(NECKLACE_FLY);
		return GO_CLEAROBJ;
	} else {
		if (game.closed) {
			rspeak((game.objects[BIRD].prop == BIRD_CAGED)
			           ? CAGE_FLY
			           : FREE_FLY);
			return GO_DWARFWAKE;
		}
		if (game.closng || !AT(FISSURE)) {
			rspeak((game.objects[BIRD].prop == BIRD_CAGED)
			           ? CAGE_FLY
			           : FREE_FLY);
			return GO_CLEAROBJ;
		}
		if (HERE(BIRD)) {
			rspeak((game.objects[BIRD].prop == BIRD_CAGED)
			           ? CAGE_FLY
			           : FREE_FLY);
		}

		state_change(FISSURE, game.objects[FISSURE].prop == BRIDGED
		                          ? UNBRIDGED
		                          : BRIDGED);
		return GO_CLEAROBJ;
	}
}

phase_codes_t action(command_t command) {
	/*  Analyse a verb.  Remember what it was, go back for object if second
	 * word unless verb is "say", which snarfs arbitrary second word.
	 */
	/* Previously, actions that result in a message, but don't do anything
	 * further were called "specials". Now they're handled here as normal
	 * actions. If noaction is true, then we spit out the message and return
	 */
	if (actions[command.verb].noaction) {
		speak(actions[command.verb].message);
		return GO_CLEAROBJ;
	}

	if (command.part == unknown) {
		/*  Analyse an object word.  See if the thing is here, whether
		 *  we've got a verb yet, and so on.  Object must be here
		 *  unless verb is "find" or "invent(ory)" (and no new verb
		 *  yet to be analysed).  Water and oil are also funny, since
		 *  they are never actually dropped at any location, but might
		 *  be here inside the bottle or urn or as a feature of the
		 *  location. */
		if (HERE(command.obj)) {
			/* FALL THROUGH */;
		} else if (command.obj == DWARF && atdwrf(game.loc) > 0) {
			/* FALL THROUGH */;
		} else if (!game.closed &&
		           ((LIQUID() == command.obj && HERE(BOTTLE)) ||
		            command.obj == LIQLOC(game.loc))) {
			/* FALL THROUGH */;
		} else if (command.obj == OIL && HERE(URN) &&
		           game.objects[URN].prop != URN_EMPTY) {
			command.obj = URN;
			/* FALL THROUGH */;
		} else if (command.obj == PLANT && AT(PLANT2) &&
		           game.objects[PLANT2].prop != PLANT_THIRSTY) {
			command.obj = PLANT2;
			/* FALL THROUGH */;
		} else if (command.obj == KNIFE && game.knfloc == game.loc) {
			game.knfloc = -1;
			rspeak(KNIVES_VANISH);
			return GO_CLEAROBJ;
		} else if (command.obj == ROD && HERE(ROD2)) {
			command.obj = ROD2;
			/* FALL THROUGH */;
		} else if ((command.verb == FIND ||
		            command.verb == INVENTORY) &&
		           (command.word[1].id == WORD_EMPTY ||
		            command.word[1].id == WORD_NOT_FOUND)) {
			/* FALL THROUGH */;
		} else {
			sspeak(NO_SEE, command.word[0].raw);
			return GO_CLEAROBJ;
		}

		if (command.verb != 0) {
			command.part = transitive;
		}
	}

	switch (command.part) {
	case intransitive:
		if (command.word[1].raw[0] != '\0' && command.verb != SAY) {
			return GO_WORD2;
		}
		if (command.verb == SAY) {
			/* KEYS is not special, anything not NO_OBJECT or
			 * INTRANSITIVE will do here. We're preventing
			 * interpretation as an intransitive verb when the word
			 * is unknown. */
			command.obj =
			    command.word[1].raw[0] != '\0' ? KEYS : NO_OBJECT;
		}
		if (command.obj == NO_OBJECT || command.obj == INTRANSITIVE) {
			/*  Analyse an intransitive verb (ie, no object given
			 * yet). */
			switch (command.verb) {
			case CARRY:
				return vcarry(command.verb, INTRANSITIVE);
			case DROP:
				return GO_UNKNOWN;
			case SAY:
				return GO_UNKNOWN;
			case UNLOCK:
				return lock(command.verb, INTRANSITIVE);
			case NOTHING: {
				rspeak(OK_MAN);
				return (GO_CLEAROBJ);
			}
			case LOCK:
				return lock(command.verb, INTRANSITIVE);
			case LIGHT:
				return light(command.verb, INTRANSITIVE);
			case EXTINGUISH:
				return extinguish(command.verb, INTRANSITIVE);
			case WAVE:
				return GO_UNKNOWN;
			case TAME:
				return GO_UNKNOWN;
			case GO: {
				speak(actions[command.verb].message);
				return GO_CLEAROBJ;
			}
			case ATTACK:
				command.obj = INTRANSITIVE;
				return attack(command);
			case POUR:
				return pour(command.verb, INTRANSITIVE);
			case EAT:
				return eat(command.verb, INTRANSITIVE);
			case DRINK:
				return drink(command.verb, INTRANSITIVE);
			case RUB:
				return GO_UNKNOWN;
			case THROW:
				return GO_UNKNOWN;
			case QUIT:
				return quit();
			case FIND:
				return GO_UNKNOWN;
			case INVENTORY:
				return inven();
			case FEED:
				return GO_UNKNOWN;
			case FILL:
				return fill(command.verb, INTRANSITIVE);
			case BLAST:
				blast();
				return GO_CLEAROBJ;
			case SCORE:
				score(scoregame);
				return GO_CLEAROBJ;
			case FEE:
			case FIE:
			case FOE:
			case FOO:
			case FUM:
				return bigwords(command.word[0].id);
			case BRIEF:
				return brief();
			case READ:
				command.obj = INTRANSITIVE;
				return read(command);
			case BREAK:
				return GO_UNKNOWN;
			case WAKE:
				return GO_UNKNOWN;
			case SAVE:
				return suspend();
			case RESUME:
				return resume();
			case FLY:
				return fly(command.verb, INTRANSITIVE);
			case LISTEN:
				return listen();
			case PART:
				return reservoir();
			case SEED:
			case WASTE:
				rspeak(NUMERIC_REQUIRED);
				return GO_TOP;
			default: // LCOV_EXCL_LINE
				BUG(INTRANSITIVE_ACTION_VERB_EXCEEDS_GOTO_LIST); // LCOV_EXCL_LINE
			}
		}
	/* FALLTHRU */
	case transitive:
		/*  Analyse a transitive verb. */
		switch (command.verb) {
		case CARRY:
			return vcarry(command.verb, command.obj);
		case DROP:
			return discard(command.verb, command.obj);
		case SAY:
			return say(command);
		case UNLOCK:
			return lock(command.verb, command.obj);
		case NOTHING: {
			rspeak(OK_MAN);
			return (GO_CLEAROBJ);
		}
		case LOCK:
			return lock(command.verb, command.obj);
		case LIGHT:
			return light(command.verb, command.obj);
		case EXTINGUISH:
			return extinguish(command.verb, command.obj);
		case WAVE:
			return wave(command.verb, command.obj);
		case TAME: {
			speak(actions[command.verb].message);
			return GO_CLEAROBJ;
		}
		case GO: {
			speak(actions[command.verb].message);
			return GO_CLEAROBJ;
		}
		case ATTACK:
			return attack(command);
		case POUR:
			return pour(command.verb, command.obj);
		case EAT:
			return eat(command.verb, command.obj);
		case DRINK:
			return drink(command.verb, command.obj);
		case RUB:
			return rub(command.verb, command.obj);
		case THROW:
			return throwit(command);
		case QUIT:
			speak(actions[command.verb].message);
			return GO_CLEAROBJ;
		case FIND:
			return find(command.verb, command.obj);
		case INVENTORY:
			return find(command.verb, command.obj);
		case FEED:
			return feed(command.verb, command.obj);
		case FILL:
			return fill(command.verb, command.obj);
		case BLAST:
			blast();
			return GO_CLEAROBJ;
		case SCORE:
			speak(actions[command.verb].message);
			return GO_CLEAROBJ;
		case FEE:
		case FIE:
		case FOE:
		case FOO:
		case FUM:
			speak(actions[command.verb].message);
			return GO_CLEAROBJ;
		case BRIEF:
			speak(actions[command.verb].message);
			return GO_CLEAROBJ;
		case READ:
			return read(command);
		case BREAK:
			return vbreak(command.verb, command.obj);
		case WAKE:
			return wake(command.verb, command.obj);
		case SAVE:
			speak(actions[command.verb].message);
			return GO_CLEAROBJ;
		case RESUME:
			speak(actions[command.verb].message);
			return GO_CLEAROBJ;
		case FLY:
			return fly(command.verb, command.obj);
		case LISTEN:
			speak(actions[command.verb].message);
			return GO_CLEAROBJ;
		// LCOV_EXCL_START
		// This case should never happen - here only as placeholder
		case PART:
			return reservoir();
		// LCOV_EXCL_STOP
		case SEED:
			return seed(command.verb, command.word[1].raw);
		case WASTE:
			return waste(command.verb,
			             (turn_t)atol(command.word[1].raw));
		default: // LCOV_EXCL_LINE
			BUG(TRANSITIVE_ACTION_VERB_EXCEEDS_GOTO_LIST); // LCOV_EXCL_LINE
		}
	case unknown:
		/* Unknown verb, couldn't deduce object - might need hint */
		sspeak(WHAT_DO, command.word[0].raw);
		return GO_CHECKHINT;
	default: // LCOV_EXCL_LINE
		BUG(SPEECHPART_NOT_TRANSITIVE_OR_INTRANSITIVE_OR_UNKNOWN); // LCOV_EXCL_LINE
	}
}

// end
