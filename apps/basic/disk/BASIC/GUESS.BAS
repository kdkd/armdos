10 REM GUESS.BAS - guess the number
20 RANDOMIZE TIMER
30 CLS
40 PRINT "I'm thinking of a number between 1 and 100."
50 N = INT(RND * 100) + 1: T = 0
60 INPUT "Your guess"; G
70 T = T + 1
80 IF G < N THEN PRINT "Too low!": GOTO 60
90 IF G > N THEN PRINT "Too high!": GOTO 60
100 PRINT "Right! You got it in"; T; "guesses."
110 IF T <= 7 THEN PLAY "T180 L16 O4 CEG>C8" ELSE BEEP
120 INPUT "Play again (Y/N)"; A$
130 IF A$ = "Y" OR A$ = "y" THEN 30
140 PRINT "Thanks for playing."
