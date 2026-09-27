' ==========================================================================
'  BANANAS.BAS - a banana-throwing duel between two chimps on the rooftops
'                of a city at dusk.
'
'  Copyright (c) 2026 Europa Micro Systems.  ARM QuickBASIC sample program.
'
'  How to play
'    Two chimps stand on the roofs of a random skyline.  Taking turns, each
'    player types an ANGLE (0-180 degrees, 0 = straight at the other chimp,
'    90 = straight up) and a SPEED (1-200), pressing Enter after each.
'    The banana flies on a ballistic arc, pulled down by gravity and pushed
'    sideways by the wind (the arrow in the middle of the top bar).
'    A banana that hits a building blasts a hole in it; one that hits a chimp
'    scores a point for the thrower (or for the other player, if a chimp is
'    careless enough to hit itself).  The first player to reach the target
'    score wins.
'
'  Keys
'    0-9, Backspace, Enter ... type the angle and the speed
'    Esc ..................... quit the game
'
'  Runs in SCREEN 13 (320 x 200, 256 colours).
' ==========================================================================

DECLARE SUB SetPalette ()
DECLARE SUB MakeCity ()
DECLARE SUB DrawChimp (cx!, cy!, pose!)
DECLARE SUB EraseChimp (cx!, cy!)
DECLARE SUB StatusBar ()
DECLARE SUB Crater (ex!, ey!, r!)
DECLARE SUB Blast (ex!, ey!, r!)
DECLARE SUB BigText (t$, tx!, ty!, sc!, c1!, c2!)
DECLARE SUB Snooze (secs!)
DECLARE SUB Message (m$, mc!)
DECLARE SUB Dance (who!)
DECLARE FUNCTION GetNum! (gr!, gc!, gp$, gmax!)
DECLARE FUNCTION Throw! (who!, ang!, spd!)

' --- colour numbers (see SetPalette) -------------------------------------
CONST SKY = 200          ' 200..224: sky bands, 8 pixels each
CONST STAR = 241
CONST MOON = 243
CONST FUR = 120
CONST FACE = 121
CONST DARK = 122
CONST BANANA = 123
CONST WLIT = 110
CONST WDIM = 111
CONST WOFF = 112
CONST FIRE1 = 245
CONST FIRE2 = 246
CONST FIRE3 = 247   ' >= SKY: count as "air"
CONST TOPBAR = 24        ' rows 0..23 are the status bar

DIM bx(1 TO 16), bw(1 TO 16), bt(1 TO 16)     ' buildings: left, width, top
DIM chx(1 TO 2), chy(1 TO 2), score(1 TO 2), nm$(1 TO 2)
DIM spx(1 TO 40), spy(1 TO 40), spk(1 TO 40)  ' explosion sparks
DIM box(0 TO 3, 1 TO 4), boy(0 TO 3, 1 TO 4)   ' banana frames

' banana frames: a centre pixel plus four more making a crescent,
' turning a quarter of a circle from one frame to the next
FOR i = 0 TO 3: FOR j = 1 TO 4: READ box(i, j), boy(i, j): NEXT: NEXT
DATA -2,-1, -1,0, 1,0, 2,-1
DATA 1,-2, 0,-1, 0,1, 1,2
DATA 2,1, 1,0, -1,0, -2,1
DATA -1,2, 0,1, 0,-1, -1,-2
' building colours (red, green, blue 0..63), read by SetPalette
DATA 12,10,20, 16,11,24, 9,14,20, 20,11,17, 13,13,15, 18,15,22

RANDOMIZE TIMER
SCREEN 13
SetPalette

' ------------------------------------------------------------------ title
Title:
CLS
FOR i = 0 TO 24: LINE (0, i * 8)-(319, i * 8 + 7), SKY + i, BF: NEXT
FOR i = 1 TO 60: PSET (RND * 319, RND * 90), STAR + INT(RND * 2): NEXT
BigText "BANANAS", 48, 14, 4, 14, 6
COLOR 15: LOCATE 7, 10: PRINT " a rooftop chimp duel "
' a little street scene
LINE (20, 150)-(80, 199), 101, BF
LINE (240, 140)-(300, 199), 103, BF
FOR yy = 156 TO 192 STEP 9: FOR xx = 26 TO 72 STEP 8
  LINE (xx, yy)-(xx + 3, yy + 4), WLIT, BF: NEXT: NEXT
FOR yy = 146 TO 192 STEP 9: FOR xx = 246 TO 292 STEP 8
  LINE (xx, yy)-(xx + 3, yy + 4), WDIM, BF: NEXT: NEXT
DrawChimp 50, 150, 2
DrawChimp 270, 140, 3
LINE (72, 88)-(248, 136), 0, BF
LINE (72, 88)-(248, 136), 14, B
COLOR 7
LOCATE 13, 11: LINE INPUT "Player 1 [Coco]: "; nm$(1)
LOCATE 14, 11: LINE INPUT "Player 2 [Momo]: "; nm$(2)
LOCATE 15, 11: LINE INPUT "Points to win [3]: "; a$
target = VAL("0" + a$): IF target < 1 THEN target = 3
LOCATE 16, 11: LINE INPUT "Gravity [9.8]: "; a$
grav = VAL("0" + a$): IF grav <= 0 THEN grav = 9.8
FOR i = 1 TO 2
  nm$(i) = UCASE$(LEFT$(LTRIM$(RTRIM$(nm$(i))), 10))
NEXT
IF nm$(1) = "" THEN nm$(1) = "COCO"
IF nm$(2) = "" THEN nm$(2) = "MOMO"
score(1) = 0: score(2) = 0
turn = 1: gaveup = 0

' ------------------------------------------------------------------ rounds
DO
  MakeCity
  wind = INT(RND * 17) - 8
  IF RND < .2 THEN wind = wind + INT(RND * 9) - 4
  StatusBar
  DrawChimp chx(1), chy(1), 0
  DrawChimp chx(2), chy(2), 0
  who = 0
  DO
    ' ask the player whose turn it is
    IF turn = 1 THEN col = 1 ELSE col = 28
    ang = GetNum(2, col, "Angle:", 180)
    IF gaveup THEN EXIT DO
    spd = GetNum(3, col, "Speed:", 200)
    IF gaveup THEN EXIT DO
    thrower = turn
    who = Throw(turn, ang, spd)
    StatusBar
    turn = 3 - turn
  LOOP UNTIL who > 0
  IF gaveup THEN EXIT DO
  ' a chimp was hit: the other one scores
  winner = 3 - who
  score(winner) = score(winner) + 1
  StatusBar
  IF who = thrower THEN
    Message "OOPS! " + nm$(who) + " HIT ITSELF", 12
  ELSE
    Message nm$(winner) + " SCORES!", 14
  END IF
  Dance winner
  Snooze 1
LOOP UNTIL score(winner) >= target

' ------------------------------------------------------------------ the end
LINE (40, 60)-(280, 140), 0, BF
LINE (40, 60)-(280, 140), 14, B
LINE (42, 62)-(278, 138), 6, B
COLOR 14
IF gaveup THEN
  LOCATE 10, 11: PRINT "Game abandoned."
ELSE
  t$ = nm$(winner) + " WINS!"
  LOCATE 10, 21 - LEN(t$) \ 2: PRINT t$
END IF
COLOR 15
t$ = nm$(1) + " " + LTRIM$(STR$(score(1))) + " - " + LTRIM$(STR$(score(2))) + " " + nm$(2)
LOCATE 12, 21 - LEN(t$) \ 2: PRINT t$
COLOR 7: LOCATE 15, 12: PRINT "Play again? (Y/N)"
PLAY "T180 L8 O3 C E G > C4"
DO
  a$ = UCASE$(INKEY$)
LOOP UNTIL a$ = "Y" OR a$ = "N" OR a$ = CHR$(27)
IF a$ = "Y" THEN GOTO Title
SCREEN 0
COLOR 7, 0
CLS
PRINT "Thanks for playing BANANAS."
END

' ==========================================================================
' Big text: print the string in the 8x8 font in the corner, then copy it
' pixel by pixel as sc x sc blocks with a drop shadow (colour c2).
SUB BigText (t$, tx, ty, sc, c1, c2)
  COLOR 15: LOCATE 25, 1: PRINT t$;
  FOR gy = 0 TO 7
    FOR gx = 0 TO LEN(t$) * 8 - 1
      IF POINT(gx, 192 + gy) = 15 THEN
        LINE (tx + gx * sc + 2, ty + gy * sc + 2)-(tx + gx * sc + sc + 1, ty + gy * sc + sc + 1), c2, BF
        LINE (tx + gx * sc, ty + gy * sc)-(tx + gx * sc + sc - 1, ty + gy * sc + sc - 1), c1, BF
      END IF
    NEXT
  NEXT
  LINE (0, 192)-(319, 199), SKY + 24, BF
END SUB

' A random skyline, the stars, the moon, and the two chimps' positions.
SUB MakeCity
  CLS
  FOR i = 0 TO 24: LINE (0, i * 8)-(319, i * 8 + 7), SKY + i, BF: NEXT
  FOR i = 1 TO 70: PSET (RND * 319, TOPBAR + RND * 80), STAR + INT(RND * 2): NEXT
  ' crescent moon: a full disc, then a bite of sky taken out of it
  mx = 40 + RND * 240: my = 42 + RND * 14
  CIRCLE (mx, my), 9, MOON: PAINT (mx, my), MOON, MOON
  FOR dy = -9 TO 9
    hw = SQR(81 - dy * dy)
    LINE (mx + 4 - hw, my - 3 + dy)-(mx + 4 + hw, my - 3 + dy), SKY + (my - 3 + dy) \ 8
  NEXT
  ' buildings, left to right
  nb = 0: x = 0
  DO WHILE x < 320
    nb = nb + 1
    w = 26 + INT(RND * 22)
    IF 320 - (x + w) < 22 THEN w = 320 - x
    h = 36 + INT(RND * 95)
    bx(nb) = x: bw(nb) = w: bt(nb) = 200 - h
    c = 100 + INT(RND * 6)
    LINE (x, 200 - h)-(x + w - 2, 199), c, BF
    ' roof trim, sometimes an antenna
    LINE (x, 200 - h)-(x + w - 2, 200 - h), c + 30
    IF RND < .3 THEN
      ax = x + 4 + INT(RND * (w - 10))
      LINE (ax, 200 - h - 8)-(ax, 200 - h - 1), c + 30
      PSET (ax, 200 - h - 9), 12
    END IF
    ' windows
    FOR wy = 200 - h + 5 TO 193 STEP 8
      FOR wx = x + 4 TO x + w - 7 STEP 7
        r = RND
        IF r < .45 THEN
          wc = WLIT
        ELSEIF r < .6 THEN
          wc = WDIM
        ELSE
          wc = WOFF
        END IF
        LINE (wx, wy)-(wx + 3, wy + 4), wc, BF
      NEXT
    NEXT
    x = x + w
  LOOP
  ' chimps stand in the middle of the 2nd and the 2nd-last building
  chx(1) = bx(2) + bw(2) \ 2: chy(1) = bt(2)
  chx(2) = bx(nb - 1) + bw(nb - 1) \ 2: chy(2) = bt(nb - 1)
END SUB

' A chimp standing with its feet on row cy.  pose: 0 arms down,
' 1 left arm up, 2 right arm up, 3 both arms up.
SUB DrawChimp (cx, cy, pose)
  ' legs and feet
  LINE (cx - 4, cy - 6)-(cx - 2, cy - 1), FUR, BF
  LINE (cx + 2, cy - 6)-(cx + 4, cy - 1), FUR, BF
  LINE (cx - 5, cy - 1)-(cx - 2, cy - 1), DARK
  LINE (cx + 2, cy - 1)-(cx + 5, cy - 1), DARK
  ' body and belly
  CIRCLE (cx, cy - 10), 5, FUR, , , 1.3
  PAINT (cx, cy - 10), FUR, FUR
  CIRCLE (cx, cy - 9), 2, FACE, , , 1.5
  PAINT (cx, cy - 9), FACE, FACE
  ' arms
  IF pose = 1 OR pose = 3 THEN
    LINE (cx - 4, cy - 13)-(cx - 8, cy - 21), FUR
    LINE (cx - 5, cy - 13)-(cx - 9, cy - 21), FUR
    PSET (cx - 9, cy - 22), FACE
  ELSE
    LINE (cx - 4, cy - 13)-(cx - 7, cy - 5), FUR
    LINE (cx - 5, cy - 13)-(cx - 8, cy - 5), FUR
    PSET (cx - 8, cy - 4), FACE
  END IF
  IF pose = 2 OR pose = 3 THEN
    LINE (cx + 4, cy - 13)-(cx + 8, cy - 21), FUR
    LINE (cx + 5, cy - 13)-(cx + 9, cy - 21), FUR
    PSET (cx + 9, cy - 22), FACE
  ELSE
    LINE (cx + 4, cy - 13)-(cx + 7, cy - 5), FUR
    LINE (cx + 5, cy - 13)-(cx + 8, cy - 5), FUR
    PSET (cx + 8, cy - 4), FACE
  END IF
  ' head, ears, muzzle, eyes, mouth
  CIRCLE (cx - 4, cy - 19), 1, FACE
  CIRCLE (cx + 4, cy - 19), 1, FACE
  CIRCLE (cx, cy - 19), 3, FUR
  PAINT (cx, cy - 19), FUR, FUR
  LINE (cx - 2, cy - 18)-(cx + 2, cy - 16), FACE, BF
  PSET (cx - 1, cy - 20), DARK: PSET (cx + 1, cy - 20), DARK
  PSET (cx - 1, cy - 17), DARK: PSET (cx + 1, cy - 17), DARK
END SUB

' Paint the sky back over a chimp (its box is always above its roof).
SUB EraseChimp (cx, cy)
  FOR ey = cy - 24 TO cy - 1
    LINE (cx - 10, ey)-(cx + 10, ey), SKY + ey \ 8
  NEXT
END SUB

' The black bar at the top: names, scores, the wind arrow.
SUB StatusBar
  LINE (0, 0)-(319, TOPBAR - 1), 0, BF
  LINE (0, TOPBAR - 1)-(319, TOPBAR - 1), 8
  COLOR 14: LOCATE 1, 1: PRINT nm$(1); score(1)
  sb$ = LTRIM$(STR$(score(2))) + " " + nm$(2)
  LOCATE 1, 41 - LEN(sb$): PRINT sb$;
  COLOR 7: LOCATE 1, 15: PRINT "first to"; target; " ";
  COLOR 11: LOCATE 2, 19: PRINT "WIND";
  IF wind = 0 THEN
    LOCATE 3, 19: PRINT "calm";
  ELSE
    ex = 160 + wind * 5
    LINE (160, 20)-(ex, 20), 11
    LINE (160, 19)-(ex, 19), 11
    LINE (ex, 19)-(ex - SGN(wind) * 4, 16), 11
    LINE (ex, 20)-(ex - SGN(wind) * 4, 23), 11
  END IF
END SUB

' Type a number at (gr, gc): digits, Backspace, Enter; Esc sets gaveup (quit).
FUNCTION GetNum (gr, gc, gp$, gmax)
  gs$ = ""
  COLOR 15: LOCATE gr, gc: PRINT gp$; " _   ";
  DO
    k$ = INKEY$
    IF LEN(k$) = 1 THEN
      IF k$ >= "0" AND k$ <= "9" AND LEN(gs$) < 3 THEN
        gs$ = gs$ + k$
      ELSEIF k$ = CHR$(8) AND LEN(gs$) > 0 THEN
        gs$ = LEFT$(gs$, LEN(gs$) - 1)
      ELSEIF k$ = CHR$(13) AND LEN(gs$) > 0 THEN
        EXIT DO
      ELSEIF k$ = CHR$(27) THEN
        gaveup = 1: EXIT DO
      END IF
      LOCATE gr, gc + LEN(gp$) + 1: PRINT gs$; "_   ";
    END IF
  LOOP
  gv = VAL("0" + gs$): IF gv > gmax THEN gv = gmax
  LOCATE gr, gc + LEN(gp$) + 1: PRINT LTRIM$(STR$(gv)); "    ";
  GetNum = gv
END FUNCTION

' Throw a banana.  Returns 0 for a miss or a building, else the chimp hit.
FUNCTION Throw (who, ang, spd)
  Throw = 0
  tx = chx(who): ty = chy(who)
  ' the arm on the throwing side goes up
  EraseChimp tx, ty
  IF who = 1 THEN
    CALL DrawChimp(tx, ty, 2)
  ELSE
    CALL DrawChimp(tx, ty, 1)
  END IF
  PLAY "T255 L64 O4 C E"
  arad = ang / 57.29578
  vx = COS(arad) * spd: vy = -SIN(arad) * spd
  IF who = 2 THEN vx = -vx
  x = tx: y = ty - 27
  wa = wind / 4
  drawn = 0: n = 0: hit = 0: f = 0
  DO
    ' move: never more than about 1.5 pixels per step
    dt = 1.5 / (ABS(vx) + ABS(vy) + 1)
    vx = vx + wa * dt: vy = vy + grav * dt
    x = x + vx * dt: y = y + vy * dt
    nx = INT(x): ny = INT(y): n = n + 1: nf = (n \ 5) MOD 4
    ' redraw only when the banana has moved to another pixel
    IF drawn = 0 OR nx <> px OR ny <> py OR nf <> f THEN
      IF drawn THEN
        ' put back what was under the banana
        PSET (px + box(f, 4), py + boy(f, 4)), s4
        PSET (px + box(f, 3), py + boy(f, 3)), s3
        PSET (px + box(f, 2), py + boy(f, 2)), s2
        PSET (px + box(f, 1), py + boy(f, 1)), s1
        PSET (px, py), s0
        drawn = 0
      END IF
      IF x < 0 OR x > 319 OR y > 199 THEN EXIT DO
      IF y >= 0 THEN
        px = nx: py = ny: f = nf
        s0 = POINT(px, py)
        IF py >= TOPBAR AND s0 < SKY THEN hit = 1: EXIT DO
        s1 = POINT(px + box(f, 1), py + boy(f, 1))
        s2 = POINT(px + box(f, 2), py + boy(f, 2))
        s3 = POINT(px + box(f, 3), py + boy(f, 3))
        s4 = POINT(px + box(f, 4), py + boy(f, 4))
        PSET (px, py), BANANA
        PSET (px + box(f, 1), py + boy(f, 1)), BANANA
        PSET (px + box(f, 2), py + boy(f, 2)), BANANA
        PSET (px + box(f, 3), py + boy(f, 3)), BANANA
        PSET (px + box(f, 4), py + boy(f, 4)), BANANA
        drawn = 1
      END IF
    END IF
    IF n = 60 THEN
      EraseChimp tx, ty
      DrawChimp tx, ty, 0
    END IF
  LOOP
  IF drawn THEN
    PSET (px + box(f, 4), py + boy(f, 4)), s4
    PSET (px + box(f, 3), py + boy(f, 3)), s3
    PSET (px + box(f, 2), py + boy(f, 2)), s2
    PSET (px + box(f, 1), py + boy(f, 1)), s1
    PSET (px, py), s0
  END IF
  IF n < 60 THEN
    EraseChimp tx, ty
    DrawChimp tx, ty, 0
  END IF
  IF hit = 0 THEN
    SOUND 120, .5
    EXIT FUNCTION
  END IF
  ' what did we hit?
  victim = 0
  FOR k = 1 TO 2
    IF ABS(px - chx(k)) <= 10 AND py >= chy(k) - 24 AND py < chy(k) THEN victim = k
  NEXT
  IF victim = 0 THEN
    Blast px, py, 8
    Crater px, py, 8
    ' the blast may have nicked a chimp: redraw both
    FOR k = 1 TO 2
      IF ABS(px - chx(k)) < 20 AND ABS(py - chy(k)) < 34 THEN
        DrawChimp chx(k), chy(k), 0
      END IF
    NEXT
  ELSE
    Blast chx(victim), chy(victim) - 10, 22
    Crater chx(victim), chy(victim) - 10, 22
    Throw = victim
  END IF
END FUNCTION

' An expanding fireball with a rumble, a spray of sparks and a flicker.
' The sparks remember what they covered so they can be cleaned up.
SUB Blast (ex, ey, r)
  FOR br = 1 TO r STEP 1 + r \ 10
    CIRCLE (ex, ey), br, FIRE1 + (br MOD 3)
    SOUND 400 - br * 12, .2
  NEXT
  CIRCLE (ex, ey), r, FIRE3
  PAINT (ex, ey), FIRE3, FIRE3
  CIRCLE (ex, ey), r * .6, FIRE2
  PAINT (ex, ey), FIRE2, FIRE2
  nsp = r * 2: IF nsp > 40 THEN nsp = 40
  FOR sp = 1 TO nsp
    sa = RND * 6.283: sd = r + 2 + RND * r * .7
    spx(sp) = INT(ex + COS(sa) * sd): spy(sp) = INT(ey + SIN(sa) * sd)
    spk(sp) = POINT(spx(sp), spy(sp))
    PSET (spx(sp), spy(sp)), FIRE1 + INT(RND * 3)
  NEXT
  FOR fl = 1 TO 6
    IF fl MOD 2 THEN
      PALETTE FIRE2, 63 + 256 * 63 + 65536 * 40
    ELSE
      PALETTE FIRE2, 63 + 256 * 42
    END IF
    SOUND 40 + RND * 60, .6
  NEXT
  FOR sp = nsp TO 1 STEP -1
    IF spk(sp) >= 0 THEN PSET (spx(sp), spy(sp)), spk(sp)
  NEXT
END SUB

' Replace a disc with sky (row by row, so the colour bands match).
SUB Crater (ex, ey, r)
  FOR dy = -r - 1 TO r + 1
    yy = ey + dy
    IF yy >= TOPBAR AND yy <= 199 THEN
      hw = SQR(ABS((r + .5) * (r + .5) - dy * dy)) + 1
      IF ABS(dy) > r THEN hw = r * .4
      LINE (ex - hw, yy)-(ex + hw, yy), SKY + yy \ 8
    END IF
  NEXT
END SUB

' The winner hops about with its arms in the air.
SUB Dance (who)
  FOR dn = 1 TO 4
    EraseChimp chx(who), chy(who)
    DrawChimp chx(who), chy(who), 3
    PLAY "T200 L16 O4 E G"
    EraseChimp chx(who), chy(who)
    DrawChimp chx(who), chy(who), 1 + (dn MOD 2)
    PLAY "T200 L16 O4 C D"
  NEXT
  EraseChimp chx(who), chy(who)
  DrawChimp chx(who), chy(who), 0
END SUB

' A line of text in the status bar.
SUB Message (m$, mc)
  LINE (0, 8)-(319, 22), 0, BF
  COLOR mc: LOCATE 3, 21 - LEN(m$) \ 2: PRINT m$;
END SUB

SUB Snooze (secs)
  pt = TIMER
  DO: LOOP UNTIL TIMER - pt >= secs OR TIMER < pt
END SUB

' Dusk colours: sky gradient, buildings, windows, chimps, fire.
SUB SetPalette
  FOR i = 0 TO 24
    IF i < 14 THEN
      t = i / 13: r = 3 + 27 * t: g = 3 + 5 * t: b = 14 + 14 * t
    ELSE
      t = (i - 14) / 10: r = 30 + 33 * t: g = 8 + 26 * t: b = 28 - 14 * t
    END IF
    PALETTE SKY + i, INT(r) + 256 * INT(g) + 65536 * INT(b)
  NEXT
  PALETTE STAR, 63 + 256 * 63 + 65536 * 63
  PALETTE STAR + 1, 40 + 256 * 40 + 65536 * 50
  PALETTE MOON, 63 + 256 * 60 + 65536 * 44
  ' building bodies 100..105 and their roof trim 108..113
  FOR i = 0 TO 5
    READ r, g, b
    PALETTE 100 + i, r + 256 * g + 65536 * b
    PALETTE 130 + i, r + 8 + 256 * (g + 8) + 65536 * (b + 8)
  NEXT
  PALETTE WLIT, 63 + 256 * 54 + 65536 * 24
  PALETTE WDIM, 44 + 256 * 32 + 65536 * 14
  PALETTE WOFF, 6 + 256 * 6 + 65536 * 12
  PALETTE FUR, 30 + 256 * 17 + 65536 * 8
  PALETTE FACE, 52 + 256 * 38 + 65536 * 26
  PALETTE DARK, 6 + 256 * 3 + 65536 * 2
  PALETTE BANANA, 63 + 256 * 60 + 65536 * 10
  PALETTE FIRE1, 63 + 256 * 20
  PALETTE FIRE2, 63 + 256 * 42
  PALETTE FIRE3, 63 + 256 * 60 + 65536 * 30
END SUB
