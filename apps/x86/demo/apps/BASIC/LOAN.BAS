10 REM LOAN.BAS - a loan repayment table (GW-BASIC, built from Microsoft's
20 REM MIT-licensed source on the ARM PC).  Type RUN.
30 KEY OFF: CLS
40 PRINT "LOAN CALCULATOR": PRINT
50 INPUT "Amount borrowed ($)"; P
60 INPUT "Yearly interest rate (%)"; R
70 INPUT "Number of monthly payments"; N
80 I = R / 1200
90 IF I = 0 THEN M = P / N ELSE M = P * I / (1 - (1 + I) ^ -N)
100 PRINT: PRINT USING "Monthly payment: $$#####.##"; M
110 INPUT "Print the table on the printer (Y/N)"; A$
120 PR = (A$ = "Y" OR A$ = "y")
130 IF PR THEN LPRINT "LOAN REPAYMENT TABLE": LPRINT USING "$$######.## at ##.##% over ### months"; P; R; N: LPRINT
140 PRINT "Month   Payment  Interest   Balance"
150 IF PR THEN LPRINT "Month   Payment  Interest   Balance"
160 B = P
170 FOR K = 1 TO N
180 T = B * I: B = B - (M - T)
190 IF B < 0 THEN B = 0
200 F$ = " ###  ######.## ######.## #######.##"
210 IF K <= 12 OR K = N THEN PRINT USING F$; K; M; T; B
220 IF K = 13 AND N > 13 THEN PRINT "  ..."
230 IF PR THEN LPRINT USING F$; K; M; T; B
240 NEXT K
250 IF PR THEN LPRINT CHR$(12);
260 PRINT: PRINT "Done."
270 END
