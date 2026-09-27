' ==========================================================================
'  SERPENT.BAS - steer a hungry serpent through eight walled levels.
'
'  Copyright (c) 2026 Europa Micro Systems.  ARM QuickBASIC sample program.
'
'  How to play
'    The serpent never stops moving.  Steer it to the numbers 1 to 9, in
'    order; each one makes it longer (and is worth more points than the
'    last).  Eat all nine to go on to the next level, which has a new set
'    of walls.  Running into a wall or into your own tail costs a life;
'    after the last life the game is over.  After level 8 the walls start
'    over again, one step faster.
'
'  Keys
'    Arrow keys ...... steer
'    P ............... pause
'    Esc ............. quit
'
'  Runs in SCREEN 0 (80 x 25 text).  Every character cell holds two square
'  "pixels" (the upper and lower half-block characters), so the playfield
'  is 80 x 46 pixels.
' ==========================================================================

DECLARE SUB Plot (px!, py!, pc!)
DECLARE SUB DrawCell (cx!, cr!)
DECLARE SUB WallBox (x1!, y1!, x2!, y2!, wc!)
DECLARE SUB NewLevel ()
DECLARE SUB StartSnake ()
DECLARE SUB PlaceFood ()
DECLARE SUB Status ()
DECLARE SUB Banner (m$, mc!)
DECLARE SUB Unbanner ()
DECLARE SUB PlayLevel ()
DECLARE SUB Delay (secs!)
DECLARE SUB WaitKey (secs!)

CONST HEADC = 14         ' colours of the serpent
CONST BODYC = 10
CONST RING = 4096        ' size of the serpent's position ring buffer
CONST NLEVELS = 8

DIM arena(1 TO 80, 1 TO 50)          ' what is in each pixel (0 = nothing)
DIM snx(0 TO 4095), sny(0 TO 4095)   ' pixels of the serpent, tail to head
DIM ddx(1 TO 4), ddy(1 TO 4), q(1 TO 2)
DIM lvx(1 TO 8), lvy(1 TO 8), lvd(1 TO 8), lvc(1 TO 8), lvn(1 TO 8), lvs(1 TO 8)
DIM rx1(1 TO 60), ry1(1 TO 60), rx2(1 TO 60), ry2(1 TO 60)

' directions: 1 up, 2 down, 3 left, 4 right
FOR i = 1 TO 4: READ ddx(i), ddy(i): NEXT
DATA 0,-1, 0,1, -1,0, 1,0

' The levels.  Each one: start x, start y, start direction, wall colour,
' number of wall boxes, then the boxes (x1, y1, x2, y2).  The playfield
' is x = 1..80, y = 3..48; the border is always there.
nr = 0
FOR lv = 1 TO NLEVELS
  READ lvx(lv), lvy(lv), lvd(lv), lvc(lv), lvn(lv)
  lvs(lv) = nr + 1
  FOR i = 1 TO lvn(lv)
    nr = nr + 1: READ rx1(nr), ry1(nr), rx2(nr), ry2(nr)
  NEXT
NEXT
' 1: the open field
DATA 20,25,4,4, 0
' 2: a bar across the middle
DATA 20,15,4,5, 1
DATA 20,26,61,26
' 3: two pillars
DATA 40,40,1,3, 2
DATA 26,11,27,40, 54,11,55,40
' 4: the pinwheel
DATA 40,26,4,6, 4
DATA 2,16,30,16, 51,35,79,35, 60,4,60,22, 21,29,21,47
' 5: the room with four doors
DATA 25,25,4,7, 8
DATA 15,12,34,12, 47,12,66,12, 15,39,34,39, 47,39,66,39
DATA 15,12,15,20, 15,31,15,39, 66,12,66,20, 66,31,66,39
' 6: the comb
DATA 6,10,2,5, 6
DATA 13,3,13,38, 26,13,26,48, 39,3,39,38, 52,13,52,48, 65,3,65,38, 72,20,79,20
' 7: the forest of posts
DATA 5,6,4,3, 21
DATA 12,11,13,12, 24,11,25,12, 36,11,37,12, 48,11,49,12, 60,11,61,12, 72,11,73,12
DATA 18,20,19,21, 30,20,31,21, 42,20,43,21, 54,20,55,21, 66,20,67,21
DATA 12,29,13,30, 24,29,25,30, 36,29,37,30, 48,29,49,30, 60,29,61,30, 72,29,73,30
DATA 18,38,19,39, 30,38,31,39, 42,38,43,39, 54,38,55,39
' 8: the spiral
DATA 5,26,1,6, 7
DATA 10,8,70,8, 70,8,70,43, 16,43,70,43, 16,15,16,43, 16,15,62,15, 62,15,62,36
DATA 24,36,62,36

RANDOMIZE TIMER

' ---- how fast does an empty FOR loop run?  (used by Delay) --------------
cn = 500
DO
  t0 = TIMER
  DO: t1 = TIMER: LOOP WHILE t1 = t0
  FOR i = 1 TO cn: NEXT
  el = TIMER - t1
  IF el >= .3 THEN EXIT DO
  cn = cn * 2
LOOP
lps = cn / el

' ---------------------------------------------------------------- title
Title:
SCREEN 0
COLOR 7, 0
CLS
' the name in big block letters
RESTORE BigLetters
FOR r = 0 TO 4
  READ a$
  IF r < 2 THEN COLOR 10 ELSE COLOR 2
  LOCATE 3 + r, 20
  FOR i = 1 TO LEN(a$)
    IF MID$(a$, i, 1) = "#" THEN PRINT CHR$(219); ELSE PRINT " ";
  NEXT
NEXT
' a little serpent under the name
COLOR 10: LOCATE 9, 26: PRINT STRING$(24, 220);
COLOR 14: PRINT CHR$(220);
COLOR 15: LOCATE 9, 55: PRINT "7";
COLOR 7
LOCATE 11, 9: PRINT "Guide the serpent around the walls and eat the numbers 1 to 9,"
LOCATE 12, 9: PRINT "in order.  Every number makes it longer.  Eat all nine to go on"
LOCATE 13, 9: PRINT "to the next level.  Don't bite the walls - or your own tail!"
COLOR 11
LOCATE 15, 17: PRINT "Arrow keys steer    P pauses    Esc quits"
COLOR 15
LOCATE 18, 22: PRINT "Speed, 1 (slow) to 9 (fast)? [5] ";
DO
  k$ = INKEY$
LOOP UNTIL (k$ >= "1" AND k$ <= "9") OR k$ = CHR$(13)
IF k$ = CHR$(13) THEN speed = 5 ELSE speed = VAL(k$)
PRINT speed
COLOR 8: LOCATE 23, 21: PRINT "Copyright (c) 2026 Europa Micro Systems"
Delay .5

score = 0: lives = 3: level = 1: quitting = 0

' ---------------------------------------------------------------- levels
DO
  food = 1
  NewLevel
  DO
    StartSnake
    PlaceFood
    Status
    PlayLevel
    IF result = 2 THEN
      ' crashed: lose a life
      lives = lives - 1
      Status
      IF lives > 0 THEN
        Banner "  OUCH!  " + LTRIM$(STR$(lives)) + " LEFT  ", 12
        WaitKey 2
      END IF
      ' take the serpent and the food away
      LOCATE fr, fx: COLOR 7, 0: PRINT " ";
      Unbanner
      i = tp
      DO
        Plot snx(i), sny(i), 0
        IF i = hp THEN EXIT DO
        i = (i + 1) MOD RING
      LOOP
    END IF
  LOOP UNTIL result <> 2 OR lives = 0
  IF result = 3 OR lives = 0 THEN EXIT DO
  ' level cleared
  PLAY "T160 L16 O3 C E G > C8"
  score = score + 100 * level
  Status
  Banner "  LEVEL " + LTRIM$(STR$(level)) + " CLEARED!  ", 14
  WaitKey 2
  level = level + 1
LOOP

' ---------------------------------------------------------------- game over
PLAY "T120 L8 O3 G E C O2 G4"
COLOR 15, 4
FOR r = 9 TO 16: LOCATE r, 24: PRINT SPACE$(32);: NEXT
LOCATE 10, 32: PRINT "G A M E   O V E R"
COLOR 14, 4
LOCATE 12, 30: PRINT "Score:"; score
LOCATE 13, 30: PRINT "Level:"; level
COLOR 15, 4
LOCATE 15, 31: PRINT "Play again? (Y/N)"
DO: k$ = INKEY$: LOOP WHILE LEN(k$) > 0
DO
  k$ = UCASE$(INKEY$)
LOOP UNTIL k$ = "Y" OR k$ = "N" OR k$ = CHR$(27)
IF k$ = "Y" THEN GOTO Title
COLOR 7, 0
CLS
PRINT "Thanks for playing SERPENT."
END

BigLetters:
DATA " ####  #####  ####   ####   #####  #   #  #####"
DATA "#      #      #   #  #   #  #      ##  #    #  "
DATA " ###   ####   ####   ####   ####   # # #    #  "
DATA "    #  #      #  #   #      #      #  ##    #  "
DATA "####   #####  #   #  #      #####  #   #    #  "

' ==========================================================================
' Set one pixel of the playfield and show it.
SUB Plot (px, py, pc)
  arena(px, py) = pc
  DrawCell px, (py + 1) \ 2
END SUB

' Show character cell (cx, cr) from its two pixels, using the half-block
' characters.  Background colours must be 0..7, so the darker colour of
' the two goes into the background.
SUB DrawCell (cx, cr)
  ct = arena(cx, cr * 2 - 1): cb = arena(cx, cr * 2)
  LOCATE cr, cx
  IF ct = cb THEN
    COLOR ct, ct AND 7: PRINT CHR$(219);
  ELSEIF cb < 8 THEN
    COLOR ct, cb: PRINT CHR$(223);
  ELSEIF ct < 8 THEN
    COLOR cb, ct: PRINT CHR$(220);
  ELSE
    COLOR ct, cb - 8: PRINT CHR$(223);
  END IF
END SUB

' A filled box of wall pixels.
SUB WallBox (x1, y1, x2, y2, wc)
  FOR wy = y1 TO y2
    FOR wx = x1 TO x2
      arena(wx, wy) = wc
    NEXT
  NEXT
  FOR wr = (y1 + 1) \ 2 TO (y2 + 1) \ 2
    FOR wx = x1 TO x2
      DrawCell wx, wr
    NEXT
  NEXT
END SUB

' Clear the screen and build the walls of the current level.
SUB NewLevel
  COLOR 7, 0
  CLS
  lv = (level - 1) MOD NLEVELS + 1
  Banner "  LEVEL " + LTRIM$(STR$(level)) + "  -  GET READY  ", 15
  FOR ay = 1 TO 50
    FOR ax = 1 TO 80
      arena(ax, ay) = 0
    NEXT
  NEXT
  wcol = lvc(lv)
  WallBox 1, 3, 80, 3, wcol
  WallBox 1, 48, 80, 48, wcol
  WallBox 1, 4, 1, 47, wcol
  WallBox 80, 4, 80, 47, wcol
  FOR ri = lvs(lv) TO lvs(lv) + lvn(lv) - 1
    WallBox rx1(ri), ry1(ri), rx2(ri), ry2(ri), wcol
  NEXT
  Banner "  LEVEL " + LTRIM$(STR$(level)) + "  -  GET READY  ", 15
  COLOR 7, 0
  LOCATE 25, 2: PRINT "Arrows: steer   P: pause   Esc: quit";
  ' moves per second: faster with the speed setting and each lap
  mps = 5 + speed * 2 + 3 * ((level - 1) \ NLEVELS)
  Status
  WaitKey 2
  Unbanner
END SUB

' A short serpent at the level's starting point.
SUB StartSnake
  hx = lvx(lv): hy = lvy(lv): dir = lvd(lv)
  hp = 0: tp = 0: snx(0) = hx: sny(0) = hy
  Plot hx, hy, HEADC
  grow = 5: qn = 0
END SUB

' Put the next number on an empty cell, not too close to the head.
SUB PlaceFood
  DO
    fx = 2 + INT(RND * 78)
    fr = 3 + INT(RND * 21)
    ok = 1
    IF arena(fx, fr * 2 - 1) <> 0 OR arena(fx, fr * 2) <> 0 THEN ok = 0
    IF ABS(fx - hx) + ABS(fr * 2 - hy) < 8 THEN ok = 0
  LOOP UNTIL ok
  LOCATE fr, fx: COLOR 15, 0: PRINT CHR$(48 + food);
END SUB

' The top line: level, score, lives, speed and the number to eat.
SUB Status
  s$ = " SERPENT    Level" + STR$(level) + "    Score" + STR$(score)
  s$ = s$ + "    Lives " + STRING$(lives, 3) + SPACE$(3 - lives)
  s$ = s$ + "    Speed" + STR$(speed) + "    Next: "
  IF food <= 9 THEN s$ = s$ + CHR$(48 + food) ELSE s$ = s$ + "-"
  COLOR 15, 1: LOCATE 1, 1: PRINT LEFT$(s$ + SPACE$(80), 80);
END SUB

' A message in the middle of the screen, and a way to take it away again.
SUB Banner (m$, mc)
  bc = 41 - LEN(m$) \ 2: bl = LEN(m$)
  COLOR mc, 1: LOCATE 13, bc: PRINT m$;
END SUB

SUB Unbanner
  FOR ux = bc TO bc + bl - 1
    DrawCell ux, 13
  NEXT
  IF fr = 13 AND fx >= bc AND fx < bc + bl THEN
    LOCATE fr, fx: COLOR 15, 0: PRINT CHR$(48 + food);
  END IF
END SUB

' Wait for a while, or until a key is pressed.
SUB WaitKey (secs)
  DO: k$ = INKEY$: LOOP WHILE LEN(k$) > 0
  wt = TIMER
  DO
    k$ = INKEY$
  LOOP UNTIL LEN(k$) > 0 OR TIMER - wt >= secs OR TIMER < wt
END SUB

' Busy-wait (TIMER only ticks 18 times a second, too coarse for a game).
SUB Delay (secs)
  IF secs > 0 THEN
    FOR dl = 1 TO INT(secs * lps): NEXT
  END IF
END SUB

' Play until the level is cleared (result 1), the serpent crashes
' (result 2) or the player presses Esc (result 3).
SUB PlayLevel
  result = 0
  DO
    ' read the keyboard; up to two turns are remembered
    k$ = INKEY$
    DO WHILE LEN(k$) > 0
      IF LEN(k$) = 2 THEN
        nd = 0
        SELECT CASE ASC(RIGHT$(k$, 1))
          CASE 72: nd = 1
          CASE 80: nd = 2
          CASE 75: nd = 3
          CASE 77: nd = 4
        END SELECT
        IF nd > 0 AND qn < 2 THEN qn = qn + 1: q(qn) = nd
      ELSE
        k$ = UCASE$(k$)
        IF k$ = CHR$(27) THEN
          result = 3: EXIT SUB
        ELSEIF k$ = "P" THEN
          Banner "  PAUSED - PRESS A KEY  ", 14
          DO: k$ = INKEY$: LOOP UNTIL LEN(k$) > 0
          Unbanner
        END IF
      END IF
      k$ = INKEY$
    LOOP
    ' turn (never straight back into the neck)
    IF qn > 0 THEN
      nd = q(1): q(1) = q(2): qn = qn - 1
      IF (nd + 1) \ 2 <> (dir + 1) \ 2 THEN dir = nd
    END IF
    nx = hx + ddx(dir): ny = hy + ddy(dir)
    IF nx = fx AND (ny + 1) \ 2 = fr THEN
      ' dinner
      SOUND 500 + food * 60, .4
      score = score + food * 10 * level
      grow = grow + food * 3
      food = food + 1
      IF food > 9 THEN result = 1
    ELSEIF arena(nx, ny) <> 0 THEN
      ' crash
      Plot hx, hy, 12
      FOR i = 1 TO 8: SOUND 300 - i * 30, .5: NEXT
      result = 2: EXIT SUB
    END IF
    ' the head moves on; the tail follows unless the serpent is growing
    Plot hx, hy, BODYC
    hp = (hp + 1) MOD RING: snx(hp) = nx: sny(hp) = ny
    hx = nx: hy = ny
    Plot hx, hy, HEADC
    IF grow > 0 THEN
      grow = grow - 1
    ELSE
      Plot snx(tp), sny(tp), 0
      tp = (tp + 1) MOD RING
    END IF
    IF result = 1 THEN EXIT SUB
    IF (nx = fx AND (ny + 1) \ 2 = fr) THEN
      PlaceFood
      Status
    END IF
    Delay 1 / mps - .008
  LOOP
END SUB
