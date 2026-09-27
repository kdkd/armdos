10 REM SINE.BAS - a sine wave plotter (text mode)
20 CLS: KEY OFF
30 PRINT TAB(25); "SIN(X) FROM 0 TO 4*PI"
40 PRINT TAB(40); "|"
50 FOR X = 0 TO 12.6 STEP .35
60   Y = SIN(X)
70   C = INT(40 + 30 * Y)
80   IF C < 40 THEN PRINT TAB(C); "*"; TAB(40); "|" ELSE IF C > 40 THEN PRINT TAB(40); "|"; TAB(C); "*" ELSE PRINT TAB(40); "*"
90 NEXT X
100 PRINT: PRINT "Now in colour, with graphics (SCREEN 13) - press a key"
110 A$ = INKEY$: IF A$ = "" THEN 110
120 SCREEN 13
130 LINE (0, 100)-(319, 100), 8: LINE (0, 0)-(0, 199), 8
140 FOR X = 0 TO 319
150   PSET (X, 100 - 80 * SIN(X / 25)), 14
160   PSET (X, 100 - 80 * COS(X / 25)), 12
170   PSET (X, 100 - 40 * SIN(X / 12.5)), 10
180 NEXT X
190 LOCATE 24, 1: PRINT "SIN  COS  SIN(2X)/2 - press a key";
200 A$ = INKEY$: IF A$ = "" THEN 200
210 SCREEN 0: PRINT "Bye."
