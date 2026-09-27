# MORE.COM — the MS-DOS 4.00 MORE filter for ARM-DOS

    MORE < FILE          TYPE FILE | MORE          DIR | MORE          MORE FILE

A C port of `CMD/MORE/MORE.ASM` from Microsoft's MS-DOS 4.0 source (MIT
licence), checked against the real MORE.COM in DOSBox-X. 2 KB. Goes to
`C:\DOS\MORE.COM`.

* Duplicates standard input to a new handle and re-opens standard input from
  standard error, so the key press comes from the console even in a pipe.
* Writes CR LF first, then every character with INT 21h AH=02h (so output
  redirection and ^C work) while tracking the cursor: CR, LF, BS, TAB (to
  the next multiple of 8), BEL (no column), wrap at the screen width from
  INT 10h AH=0Fh. The screen height is 25, or what ANSI.SYS reports through
  IOCTL 440Ch/037Fh (screen-size aware as 4.0 is).
* On the last row: `-- More --` on standard error, a key without echo (INT
  21h AX=0C08h; an extended key's second byte is swallowed), then CR LF CR
  LF, and the next 24 lines. ^Z or the end of input ends it; there is no Q.
  Ctrl-C at the prompt ends MORE (`^C`, from DOS).
* Switches are ignored, as in 4.0 (`MORE /X` = `MORE`). A file name is not (see below).

Tests: `make more-test` (apps/more/tests/run.mjs: the paging screens, the
prompt with stdout redirected, and the output bytes, against the real MORE).

Deviations: `MORE FILE` pages the file. The 4.00 MORE ignored its command line
(it sat reading the keyboard, which looks like a hang); later versions of MORE
took a file name, and that is the form people remember, so ARM-DOS accepts it.
The first argument not starting with `/` is the file; a missing one gives
`File not found - FILE`. Everything else is as in 4.00.
