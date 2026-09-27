# Pit Poker - PITPOKER.EXE (a door for The ARM Pit BBS)

Jacks-or-Better video poker on the full-pay 9/6 table, dealt by Parity, the
sysop's cat. Original program for ARM-DOS, built on the door library (`../lib`).

    PITPOKER <dir with DOOR.SYS or DORINFO1.DEF>   (the BBS starts it like this)
    PITPOKER /L                                   (play locally)

* ANSI table: the pay table (bet 1-5 columns, the current bet highlighted, the
  winning row lit after a hand), five cards drawn with CP437 boxes and suits
  (red hearts/diamonds, black clubs/spades), HELD markers updated in place.
  TTY callers get the same text without cursor positioning.
* Keys: Enter deals at the current bet, 1-5 changes the bet and deals; 1-5
  hold/unhold, Enter draws; S scores, Q cash out.
* Pays per credit: Jacks or Better 1, Two Pair 2, Three of a Kind 3, Straight 4,
  Flush 6, Full House 9, Four of a Kind 25, Straight Flush 50, Royal Flush 250
  (4,000 at five credits).
* **Bankroll**: every new day a player with less than 100 credits is topped up
  to 100; winnings above that carry over. Broke = the table is closed until
  tomorrow. The leaderboard ranks the biggest bankroll ever held.

## Files (in the door's directory, C:\BBS\DOORS\POKER on the BBS disk)

| file | what |
|---|---|
| `POKER.DAT` | players: `name|credits|day|peak|peakdate|besthand|besthanddate|hands|royals` (hand 0-9 = Nothing .. Royal Flush), seeded with the regulars |
| `SCORES.TXT` | title + top five by peak bankroll with best hand, rewritten when it changes |
| `NEWS.TXT` | royal flushes, straight flushes, fours of a kind, and callers who leave up 200+ credits with 300+ |

## Tests

`node apps/doors/tests/local.mjs poker`: four hands with holds and a bet change;
the test reads the cards off the screen, scores them itself and checks the
credits; POKER.DAT and SCORES.TXT; a restart keeps the bankroll.
Screenshots `build/term-test/door-poker-*.png`.
