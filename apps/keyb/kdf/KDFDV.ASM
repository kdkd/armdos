;; KDFDV.ASM - ARM-DOS keyboard definition file: the Dvorak layouts.
;; Written by apps/keyb/tools/mkdvorak.py in the keyboard definition language
;; of MS-DOS 4.00's KEYBOARD.SYS sources (KEYBMAC.INC). MS-DOS 4.00 had no
;; Dvorak layouts; these are ARM-DOS's own:
;;   DV  Dvorak (ANSI X4.22-1983)
;;   DL  Dvorak for the left hand (as Windows "United States-Dvorak for left hand")
;;   DR  Dvorak for the right hand (as Windows "United States-Dvorak for right hand")

	INCLUDE KEYBSHAR.INC
	INCLUDE POSTEQU.INC
	INCLUDE KEYBMAC.INC
	PUBLIC DV_LOGIC
	PUBLIC DV_437_XLAT
	PUBLIC DV_850_XLAT
	PUBLIC DV_860_XLAT
	PUBLIC DV_863_XLAT
	PUBLIC DV_865_XLAT
	PUBLIC DL_LOGIC
	PUBLIC DL_437_XLAT
	PUBLIC DL_850_XLAT
	PUBLIC DL_860_XLAT
	PUBLIC DL_863_XLAT
	PUBLIC DL_865_XLAT
	PUBLIC DR_LOGIC
	PUBLIC DR_437_XLAT
	PUBLIC DR_850_XLAT
	PUBLIC DR_860_XLAT
	PUBLIC DR_863_XLAT
	PUBLIC DR_865_XLAT

CODE	SEGMENT PUBLIC 'CODE'
	ASSUME CS:CODE,DS:CODE
STANDARD_TABLE	    EQU   TYPE_2_TAB+ASCII_ONLY

;;***************************************
;; DV: Dvorak (ANSI X4.22-1983)
;;***************************************

DV_LOGIC:
   DW  DV_LOGIC_END-$		       ;; length
   DW  0			       ;; special features
   OPTION EXIT_IF_FOUND
   IFKBD G_KB+P12_KB		       ;; not the "/" of the numeric pad
   ANDF LC_E0
      EXIT_STATE_LOGIC
   ENDIFF
   IFF EITHER_CTL,NOT
      IFF  ALT_SHIFT		       ;; Alt: the US key of the character
      ANDF R_ALT_SHIFT,NOT
         XLATT ALT_CASE
      ENDIFF
   ELSEF
      IFF EITHER_ALT,NOT	       ;; Ctrl: likewise
         XLATT CTRL_CASE
      ENDIFF
   ENDIFF
   IFF EITHER_ALT,NOT
   ANDF EITHER_CTL,NOT
      IFF EITHER_SHIFT
         XLATT NON_ALPHA_UPPER
         IFF CAPS_STATE
            XLATT ALPHA_LOWER
         ELSEF
            XLATT ALPHA_UPPER
         ENDIFF
      ELSEF
         XLATT NON_ALPHA_LOWER
         IFF CAPS_STATE
            XLATT ALPHA_UPPER
         ELSEF
            XLATT ALPHA_LOWER
         ENDIFF
      ENDIFF
   ENDIFF
   EXIT_STATE_LOGIC
DV_LOGIC_END:

DV_COMMON_XLAT:
   DW	 DV_COMMON_END-$	       ;; length of section
   DW	 -1			       ;; code page
   DW	 DV_ALT_END-$		       ;; length of state section
   DB	 ALT_CASE		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DV_ALT_T_END-$	       ;; Size of xlat table
   DB	 TYPE_2_TAB		       ;; xlat options
   DB	 33			       ;; number of entries
   DB	 12,0,26
   DB	 13,0,27
   DB	 16,0,40
   DB	 17,0,51
   DB	 18,0,52
   DB	 19,0,25
   DB	 20,0,21
   DB	 21,0,33
   DB	 22,0,34
   DB	 23,0,46
   DB	 24,0,19
   DB	 25,0,38
   DB	 26,0,53
   DB	 27,0,131
   DB	 31,0,24
   DB	 32,0,18
   DB	 33,0,22
   DB	 34,0,23
   DB	 35,0,32
   DB	 36,0,35
   DB	 37,0,20
   DB	 38,0,49
   DB	 39,0,31
   DB	 40,0,130
   DB	 44,0,39
   DB	 45,0,16
   DB	 46,0,36
   DB	 47,0,37
   DB	 48,0,45
   DB	 49,0,48
   DB	 51,0,17
   DB	 52,0,47
   DB	 53,0,44
DV_ALT_T_END:
   DW	 0			       ;; null table
DV_ALT_END:
   DW	 DV_CTRL_END-$		       ;; length of state section
   DB	 CTRL_CASE		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DV_CTRL_T_END-$	       ;; Size of xlat table
   DB	 TYPE_2_TAB		       ;; xlat options
   DB	 33			       ;; number of entries
   DB	 12,27,26
   DB	 13,29,27
   DB	 16,-1,-1
   DB	 17,-1,-1
   DB	 18,-1,-1
   DB	 19,16,25
   DB	 20,25,21
   DB	 21,6,33
   DB	 22,7,34
   DB	 23,3,46
   DB	 24,18,19
   DB	 25,12,38
   DB	 26,-1,-1
   DB	 27,-1,-1
   DB	 31,15,24
   DB	 32,5,18
   DB	 33,21,22
   DB	 34,9,23
   DB	 35,4,32
   DB	 36,8,35
   DB	 37,20,20
   DB	 38,14,49
   DB	 39,19,31
   DB	 40,31,12
   DB	 44,-1,-1
   DB	 45,17,16
   DB	 46,10,36
   DB	 47,11,37
   DB	 48,24,45
   DB	 49,2,48
   DB	 51,23,17
   DB	 52,22,47
   DB	 53,26,44
DV_CTRL_T_END:
   DW	 0			       ;; null table
DV_CTRL_END:
   DW	 DV_AL_LO_END-$		       ;; length of state section
   DB	 ALPHA_LOWER		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DV_AL_LO_T_END-$	       ;; Size of xlat table
   DB	 STANDARD_TABLE		       ;; xlat options
   DB	 24			       ;; number of entries
   DB	 19,'p'
   DB	 20,'y'
   DB	 21,'f'
   DB	 22,'g'
   DB	 23,'c'
   DB	 24,'r'
   DB	 25,'l'
   DB	 31,'o'
   DB	 32,'e'
   DB	 33,'u'
   DB	 34,'i'
   DB	 35,'d'
   DB	 36,'h'
   DB	 37,'t'
   DB	 38,'n'
   DB	 39,'s'
   DB	 45,'q'
   DB	 46,'j'
   DB	 47,'k'
   DB	 48,'x'
   DB	 49,'b'
   DB	 51,'w'
   DB	 52,'v'
   DB	 53,'z'
DV_AL_LO_T_END:
   DW	 0			       ;; null table
DV_AL_LO_END:
   DW	 DV_AL_UP_END-$		       ;; length of state section
   DB	 ALPHA_UPPER		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DV_AL_UP_T_END-$	       ;; Size of xlat table
   DB	 STANDARD_TABLE		       ;; xlat options
   DB	 24			       ;; number of entries
   DB	 19,'P'
   DB	 20,'Y'
   DB	 21,'F'
   DB	 22,'G'
   DB	 23,'C'
   DB	 24,'R'
   DB	 25,'L'
   DB	 31,'O'
   DB	 32,'E'
   DB	 33,'U'
   DB	 34,'I'
   DB	 35,'D'
   DB	 36,'H'
   DB	 37,'T'
   DB	 38,'N'
   DB	 39,'S'
   DB	 45,'Q'
   DB	 46,'J'
   DB	 47,'K'
   DB	 48,'X'
   DB	 49,'B'
   DB	 51,'W'
   DB	 52,'V'
   DB	 53,'Z'
DV_AL_UP_T_END:
   DW	 0			       ;; null table
DV_AL_UP_END:
   DW	 DV_NA_LO_END-$		       ;; length of state section
   DB	 NON_ALPHA_LOWER		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DV_NA_LO_T_END-$	       ;; Size of xlat table
   DB	 STANDARD_TABLE		       ;; xlat options
   DB	 9			       ;; number of entries
   DB	 12,'['
   DB	 13,']'
   DB	 16,"'"
   DB	 17,','
   DB	 18,'.'
   DB	 26,'/'
   DB	 27,'='
   DB	 40,'-'
   DB	 44,';'
DV_NA_LO_T_END:
   DW	 0			       ;; null table
DV_NA_LO_END:
   DW	 DV_NA_UP_END-$		       ;; length of state section
   DB	 NON_ALPHA_UPPER		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DV_NA_UP_T_END-$	       ;; Size of xlat table
   DB	 STANDARD_TABLE		       ;; xlat options
   DB	 9			       ;; number of entries
   DB	 12,'{'
   DB	 13,'}'
   DB	 16,'"'
   DB	 17,'<'
   DB	 18,'>'
   DB	 26,'?'
   DB	 27,'+'
   DB	 40,'_'
   DB	 44,':'
DV_NA_UP_T_END:
   DW	 0			       ;; null table
DV_NA_UP_END:
   DW	 0			       ;; last state
DV_COMMON_END:

DV_437_XLAT:
   DW	 DV_437_END-$	       ;; length of section
   DW	 437			       ;; code page
   DW	 0			       ;; last state
DV_437_END:

DV_850_XLAT:
   DW	 DV_850_END-$	       ;; length of section
   DW	 850			       ;; code page
   DW	 0			       ;; last state
DV_850_END:

DV_860_XLAT:
   DW	 DV_860_END-$	       ;; length of section
   DW	 860			       ;; code page
   DW	 0			       ;; last state
DV_860_END:

DV_863_XLAT:
   DW	 DV_863_END-$	       ;; length of section
   DW	 863			       ;; code page
   DW	 0			       ;; last state
DV_863_END:

DV_865_XLAT:
   DW	 DV_865_END-$	       ;; length of section
   DW	 865			       ;; code page
   DW	 0			       ;; last state
DV_865_END:

;;***************************************
;; DL: Dvorak for the left hand (as Windows "United States-Dvorak for left hand")
;;***************************************

DL_LOGIC:
   DW  DL_LOGIC_END-$		       ;; length
   DW  0			       ;; special features
   OPTION EXIT_IF_FOUND
   IFKBD G_KB+P12_KB		       ;; not the "/" of the numeric pad
   ANDF LC_E0
      EXIT_STATE_LOGIC
   ENDIFF
   IFF EITHER_CTL,NOT
      IFF  ALT_SHIFT		       ;; Alt: the US key of the character
      ANDF R_ALT_SHIFT,NOT
         XLATT ALT_CASE
      ENDIFF
   ELSEF
      IFF EITHER_ALT,NOT	       ;; Ctrl: likewise
         XLATT CTRL_CASE
      ENDIFF
   ENDIFF
   IFF EITHER_ALT,NOT
   ANDF EITHER_CTL,NOT
      IFF EITHER_SHIFT
         XLATT NON_ALPHA_UPPER
         IFF CAPS_STATE
            XLATT ALPHA_LOWER
         ELSEF
            XLATT ALPHA_UPPER
         ENDIFF
      ELSEF
         XLATT NON_ALPHA_LOWER
         IFF CAPS_STATE
            XLATT ALPHA_UPPER
         ELSEF
            XLATT ALPHA_LOWER
         ENDIFF
      ENDIFF
   ENDIFF
   EXIT_STATE_LOGIC
DL_LOGIC_END:

DL_COMMON_XLAT:
   DW	 DL_COMMON_END-$	       ;; length of section
   DW	 -1			       ;; code page
   DW	 DL_ALT_END-$		       ;; length of state section
   DB	 ALT_CASE		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DL_ALT_T_END-$	       ;; Size of xlat table
   DB	 TYPE_2_TAB		       ;; xlat options
   DB	 40			       ;; number of entries
   DB	 2,0,26
   DB	 3,0,27
   DB	 4,0,53
   DB	 5,0,25
   DB	 6,0,33
   DB	 7,0,50
   DB	 8,0,38
   DB	 9,0,36
   DB	 10,0,123
   DB	 11,0,122
   DB	 12,0,121
   DB	 13,0,120
   DB	 16,0,39
   DB	 17,0,16
   DB	 18,0,48
   DB	 19,0,21
   DB	 20,0,22
   DB	 21,0,19
   DB	 22,0,31
   DB	 23,0,24
   DB	 24,0,52
   DB	 25,0,125
   DB	 26,0,124
   DB	 27,0,131
   DB	 30,0,130
   DB	 31,0,37
   DB	 32,0,46
   DB	 33,0,32
   DB	 34,0,20
   DB	 36,0,18
   DB	 37,0,30
   DB	 38,0,44
   DB	 39,0,127
   DB	 40,0,126
   DB	 44,0,40
   DB	 46,0,34
   DB	 48,0,17
   DB	 50,0,23
   DB	 52,0,129
   DB	 53,0,128
DL_ALT_T_END:
   DW	 0			       ;; null table
DL_ALT_END:
   DW	 DL_CTRL_END-$		       ;; length of state section
   DB	 CTRL_CASE		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DL_CTRL_T_END-$	       ;; Size of xlat table
   DB	 TYPE_2_TAB		       ;; xlat options
   DB	 40			       ;; number of entries
   DB	 2,27,26
   DB	 3,29,27
   DB	 4,-1,-1
   DB	 5,16,25
   DB	 6,6,33
   DB	 7,13,50
   DB	 8,12,38
   DB	 9,10,36
   DB	 10,-1,-1
   DB	 11,-1,-1
   DB	 12,0,3
   DB	 13,-1,-1
   DB	 16,-1,-1
   DB	 17,17,16
   DB	 18,2,48
   DB	 19,25,21
   DB	 20,21,22
   DB	 21,18,19
   DB	 22,19,31
   DB	 23,15,24
   DB	 24,-1,-1
   DB	 25,30,7
   DB	 26,-1,-1
   DB	 27,-1,-1
   DB	 30,31,12
   DB	 31,11,37
   DB	 32,3,46
   DB	 33,4,32
   DB	 34,20,20
   DB	 36,5,18
   DB	 37,1,30
   DB	 38,26,44
   DB	 39,-1,-1
   DB	 40,-1,-1
   DB	 44,-1,-1
   DB	 46,7,34
   DB	 48,23,17
   DB	 50,9,23
   DB	 52,-1,-1
   DB	 53,-1,-1
DL_CTRL_T_END:
   DW	 0			       ;; null table
DL_CTRL_END:
   DW	 DL_AL_LO_END-$		       ;; length of state section
   DB	 ALPHA_LOWER		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DL_AL_LO_T_END-$	       ;; Size of xlat table
   DB	 STANDARD_TABLE		       ;; xlat options
   DB	 22			       ;; number of entries
   DB	 5,'p'
   DB	 6,'f'
   DB	 7,'m'
   DB	 8,'l'
   DB	 9,'j'
   DB	 17,'q'
   DB	 18,'b'
   DB	 19,'y'
   DB	 20,'u'
   DB	 21,'r'
   DB	 22,'s'
   DB	 23,'o'
   DB	 31,'k'
   DB	 32,'c'
   DB	 33,'d'
   DB	 34,'t'
   DB	 36,'e'
   DB	 37,'a'
   DB	 38,'z'
   DB	 46,'g'
   DB	 48,'w'
   DB	 50,'i'
DL_AL_LO_T_END:
   DW	 0			       ;; null table
DL_AL_LO_END:
   DW	 DL_AL_UP_END-$		       ;; length of state section
   DB	 ALPHA_UPPER		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DL_AL_UP_T_END-$	       ;; Size of xlat table
   DB	 STANDARD_TABLE		       ;; xlat options
   DB	 22			       ;; number of entries
   DB	 5,'P'
   DB	 6,'F'
   DB	 7,'M'
   DB	 8,'L'
   DB	 9,'J'
   DB	 17,'Q'
   DB	 18,'B'
   DB	 19,'Y'
   DB	 20,'U'
   DB	 21,'R'
   DB	 22,'S'
   DB	 23,'O'
   DB	 31,'K'
   DB	 32,'C'
   DB	 33,'D'
   DB	 34,'T'
   DB	 36,'E'
   DB	 37,'A'
   DB	 38,'Z'
   DB	 46,'G'
   DB	 48,'W'
   DB	 50,'I'
DL_AL_UP_T_END:
   DW	 0			       ;; null table
DL_AL_UP_END:
   DW	 DL_NA_LO_END-$		       ;; length of state section
   DB	 NON_ALPHA_LOWER		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DL_NA_LO_T_END-$	       ;; Size of xlat table
   DB	 STANDARD_TABLE		       ;; xlat options
   DB	 18			       ;; number of entries
   DB	 2,'['
   DB	 3,']'
   DB	 4,'/'
   DB	 10,'4'
   DB	 11,'3'
   DB	 12,'2'
   DB	 13,'1'
   DB	 16,';'
   DB	 24,'.'
   DB	 25,'6'
   DB	 26,'5'
   DB	 27,'='
   DB	 30,'-'
   DB	 39,'8'
   DB	 40,'7'
   DB	 44,"'"
   DB	 52,'0'
   DB	 53,'9'
DL_NA_LO_T_END:
   DW	 0			       ;; null table
DL_NA_LO_END:
   DW	 DL_NA_UP_END-$		       ;; length of state section
   DB	 NON_ALPHA_UPPER		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DL_NA_UP_T_END-$	       ;; Size of xlat table
   DB	 STANDARD_TABLE		       ;; xlat options
   DB	 18			       ;; number of entries
   DB	 2,'{'
   DB	 3,'}'
   DB	 4,'?'
   DB	 10,'$'
   DB	 11,'#'
   DB	 12,'@'
   DB	 13,'!'
   DB	 16,':'
   DB	 24,'>'
   DB	 25,'^'
   DB	 26,'%'
   DB	 27,'+'
   DB	 30,'_'
   DB	 39,'*'
   DB	 40,'&'
   DB	 44,'"'
   DB	 52,')'
   DB	 53,'('
DL_NA_UP_T_END:
   DW	 0			       ;; null table
DL_NA_UP_END:
   DW	 0			       ;; last state
DL_COMMON_END:

DL_437_XLAT:
   DW	 DL_437_END-$	       ;; length of section
   DW	 437			       ;; code page
   DW	 0			       ;; last state
DL_437_END:

DL_850_XLAT:
   DW	 DL_850_END-$	       ;; length of section
   DW	 850			       ;; code page
   DW	 0			       ;; last state
DL_850_END:

DL_860_XLAT:
   DW	 DL_860_END-$	       ;; length of section
   DW	 860			       ;; code page
   DW	 0			       ;; last state
DL_860_END:

DL_863_XLAT:
   DW	 DL_863_END-$	       ;; length of section
   DW	 863			       ;; code page
   DW	 0			       ;; last state
DL_863_END:

DL_865_XLAT:
   DW	 DL_865_END-$	       ;; length of section
   DW	 865			       ;; code page
   DW	 0			       ;; last state
DL_865_END:

;;***************************************
;; DR: Dvorak for the right hand (as Windows "United States-Dvorak for right hand")
;;***************************************

DR_LOGIC:
   DW  DR_LOGIC_END-$		       ;; length
   DW  0			       ;; special features
   OPTION EXIT_IF_FOUND
   IFKBD G_KB+P12_KB		       ;; not the "/" of the numeric pad
   ANDF LC_E0
      EXIT_STATE_LOGIC
   ENDIFF
   IFF EITHER_CTL,NOT
      IFF  ALT_SHIFT		       ;; Alt: the US key of the character
      ANDF R_ALT_SHIFT,NOT
         XLATT ALT_CASE
      ENDIFF
   ELSEF
      IFF EITHER_ALT,NOT	       ;; Ctrl: likewise
         XLATT CTRL_CASE
      ENDIFF
   ENDIFF
   IFF EITHER_ALT,NOT
   ANDF EITHER_CTL,NOT
      IFF EITHER_SHIFT
         XLATT NON_ALPHA_UPPER
         IFF CAPS_STATE
            XLATT ALPHA_LOWER
         ELSEF
            XLATT ALPHA_UPPER
         ENDIFF
      ELSEF
         XLATT NON_ALPHA_LOWER
         IFF CAPS_STATE
            XLATT ALPHA_UPPER
         ELSEF
            XLATT ALPHA_LOWER
         ENDIFF
      ENDIFF
   ENDIFF
   EXIT_STATE_LOGIC
DR_LOGIC_END:

DR_COMMON_XLAT:
   DW	 DR_COMMON_END-$	       ;; length of section
   DW	 -1			       ;; code page
   DW	 DR_ALT_END-$		       ;; length of state section
   DB	 ALT_CASE		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DR_ALT_T_END-$	       ;; Size of xlat table
   DB	 TYPE_2_TAB		       ;; xlat options
   DB	 39			       ;; number of entries
   DB	 6,0,36
   DB	 7,0,38
   DB	 8,0,50
   DB	 9,0,33
   DB	 10,0,25
   DB	 11,0,53
   DB	 12,0,26
   DB	 13,0,27
   DB	 16,0,124
   DB	 17,0,125
   DB	 18,0,16
   DB	 19,0,52
   DB	 20,0,24
   DB	 21,0,19
   DB	 22,0,31
   DB	 23,0,22
   DB	 24,0,21
   DB	 25,0,48
   DB	 26,0,39
   DB	 27,0,131
   DB	 30,0,126
   DB	 31,0,127
   DB	 32,0,44
   DB	 33,0,30
   DB	 34,0,18
   DB	 36,0,20
   DB	 37,0,32
   DB	 38,0,46
   DB	 39,0,37
   DB	 40,0,130
   DB	 44,0,128
   DB	 45,0,129
   DB	 46,0,45
   DB	 47,0,51
   DB	 48,0,23
   DB	 50,0,17
   DB	 51,0,47
   DB	 52,0,34
   DB	 53,0,40
DR_ALT_T_END:
   DW	 0			       ;; null table
DR_ALT_END:
   DW	 DR_CTRL_END-$		       ;; length of state section
   DB	 CTRL_CASE		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DR_CTRL_T_END-$	       ;; Size of xlat table
   DB	 TYPE_2_TAB		       ;; xlat options
   DB	 39			       ;; number of entries
   DB	 6,10,36
   DB	 7,12,38
   DB	 8,13,50
   DB	 9,6,33
   DB	 10,16,25
   DB	 11,-1,-1
   DB	 12,27,26
   DB	 13,29,27
   DB	 16,-1,-1
   DB	 17,30,7
   DB	 18,17,16
   DB	 19,-1,-1
   DB	 20,15,24
   DB	 21,18,19
   DB	 22,19,31
   DB	 23,21,22
   DB	 24,25,21
   DB	 25,2,48
   DB	 26,-1,-1
   DB	 27,-1,-1
   DB	 30,-1,-1
   DB	 31,-1,-1
   DB	 32,26,44
   DB	 33,1,30
   DB	 34,5,18
   DB	 36,20,20
   DB	 37,4,32
   DB	 38,3,46
   DB	 39,11,37
   DB	 40,31,12
   DB	 44,-1,-1
   DB	 45,-1,-1
   DB	 46,24,45
   DB	 47,-1,-1
   DB	 48,9,23
   DB	 50,23,17
   DB	 51,22,47
   DB	 52,7,34
   DB	 53,-1,-1
DR_CTRL_T_END:
   DW	 0			       ;; null table
DR_CTRL_END:
   DW	 DR_AL_LO_END-$		       ;; length of state section
   DB	 ALPHA_LOWER		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DR_AL_LO_T_END-$	       ;; Size of xlat table
   DB	 STANDARD_TABLE		       ;; xlat options
   DB	 24			       ;; number of entries
   DB	 6,'j'
   DB	 7,'l'
   DB	 8,'m'
   DB	 9,'f'
   DB	 10,'p'
   DB	 18,'q'
   DB	 20,'o'
   DB	 21,'r'
   DB	 22,'s'
   DB	 23,'u'
   DB	 24,'y'
   DB	 25,'b'
   DB	 32,'z'
   DB	 33,'a'
   DB	 34,'e'
   DB	 36,'t'
   DB	 37,'d'
   DB	 38,'c'
   DB	 39,'k'
   DB	 46,'x'
   DB	 48,'i'
   DB	 50,'w'
   DB	 51,'v'
   DB	 52,'g'
DR_AL_LO_T_END:
   DW	 0			       ;; null table
DR_AL_LO_END:
   DW	 DR_AL_UP_END-$		       ;; length of state section
   DB	 ALPHA_UPPER		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DR_AL_UP_T_END-$	       ;; Size of xlat table
   DB	 STANDARD_TABLE		       ;; xlat options
   DB	 24			       ;; number of entries
   DB	 6,'J'
   DB	 7,'L'
   DB	 8,'M'
   DB	 9,'F'
   DB	 10,'P'
   DB	 18,'Q'
   DB	 20,'O'
   DB	 21,'R'
   DB	 22,'S'
   DB	 23,'U'
   DB	 24,'Y'
   DB	 25,'B'
   DB	 32,'Z'
   DB	 33,'A'
   DB	 34,'E'
   DB	 36,'T'
   DB	 37,'D'
   DB	 38,'C'
   DB	 39,'K'
   DB	 46,'X'
   DB	 48,'I'
   DB	 50,'W'
   DB	 51,'V'
   DB	 52,'G'
DR_AL_UP_T_END:
   DW	 0			       ;; null table
DR_AL_UP_END:
   DW	 DR_NA_LO_END-$		       ;; length of state section
   DB	 NON_ALPHA_LOWER		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DR_NA_LO_T_END-$	       ;; Size of xlat table
   DB	 STANDARD_TABLE		       ;; xlat options
   DB	 15			       ;; number of entries
   DB	 11,'/'
   DB	 12,'['
   DB	 13,']'
   DB	 16,'5'
   DB	 17,'6'
   DB	 19,'.'
   DB	 26,';'
   DB	 27,'='
   DB	 30,'7'
   DB	 31,'8'
   DB	 40,'-'
   DB	 44,'9'
   DB	 45,'0'
   DB	 47,','
   DB	 53,"'"
DR_NA_LO_T_END:
   DW	 0			       ;; null table
DR_NA_LO_END:
   DW	 DR_NA_UP_END-$		       ;; length of state section
   DB	 NON_ALPHA_UPPER		       ;; State ID
   DW	 ANY_KB 		       ;; Keyboard Type
   DB	 -1,-1			       ;; Buffer entry for error character
   DW	 DR_NA_UP_T_END-$	       ;; Size of xlat table
   DB	 STANDARD_TABLE		       ;; xlat options
   DB	 15			       ;; number of entries
   DB	 11,'?'
   DB	 12,'{'
   DB	 13,'}'
   DB	 16,'%'
   DB	 17,'^'
   DB	 19,'>'
   DB	 26,':'
   DB	 27,'+'
   DB	 30,'&'
   DB	 31,'*'
   DB	 40,'_'
   DB	 44,'('
   DB	 45,')'
   DB	 47,'<'
   DB	 53,'"'
DR_NA_UP_T_END:
   DW	 0			       ;; null table
DR_NA_UP_END:
   DW	 0			       ;; last state
DR_COMMON_END:

DR_437_XLAT:
   DW	 DR_437_END-$	       ;; length of section
   DW	 437			       ;; code page
   DW	 0			       ;; last state
DR_437_END:

DR_850_XLAT:
   DW	 DR_850_END-$	       ;; length of section
   DW	 850			       ;; code page
   DW	 0			       ;; last state
DR_850_END:

DR_860_XLAT:
   DW	 DR_860_END-$	       ;; length of section
   DW	 860			       ;; code page
   DW	 0			       ;; last state
DR_860_END:

DR_863_XLAT:
   DW	 DR_863_END-$	       ;; length of section
   DW	 863			       ;; code page
   DW	 0			       ;; last state
DR_863_END:

DR_865_XLAT:
   DW	 DR_865_END-$	       ;; length of section
   DW	 865			       ;; code page
   DW	 0			       ;; last state
DR_865_END:

CODE	ENDS
	END
