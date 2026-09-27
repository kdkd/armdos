# NLSFUNC.EXE and COUNTRY.SYS - countries and code pages

    COUNTRY=049,,C:\DOS\COUNTRY.SYS        (CONFIG.SYS)
    NLSFUNC [[d:][path]COUNTRY.SYS]
    CHCP [nnn]                             (COMMAND.COM)

**COUNTRY.SYS** holds each country's date, time and currency formats, upper-case table and
collating sequence per code page. It is MS-DOS 4.00's own, assembled from its MIT-licensed
source (`country/MKCNTRY.ASM`, `MKCNTRY.INC`; Microsoft, MIT licence, `country/LICENSE`) by
`tools/mkcountry.mjs` - with `--ms` byte for byte MS-DOS 4.00's COUNTRY.SYS - plus one
ARM-DOS addition: **Brazil (055)**, code pages 850 and 437 (dd/mm/yyyy, `Cr$`, 24-hour time;
Portugal's and Latin America's tables), which DOS 4.00 did not have. The countries: 001 US,
002 Canada (French), 003 Latin America, 031 Netherlands, 032 Belgium, 033 France, 034 Spain,
039 Italy, 041 Switzerland, 044 United Kingdom, 045 Denmark, 046 Sweden, 047 Norway, 049
Germany, 055 Brazil, 061 International English, 351 Portugal, 358 Finland, 785 Arabic, 972
Israel, and the DBCS countries 081 082 086 088 (tables only).

**COUNTRY=** in CONFIG.SYS (the kernel, kernel/README.md, "National language support") reads it at boot: DATE,
TIME, DIR and COMMAND.COM follow the country (`Current date is Thu 01.01.2026`, `Current time
is 12.00.02,25` for 049); without a code page the country's first one is used (Switzerland
850, Canada 863). ARM-DOS keeps COUNTRY.SYS in C:\DOS: with no path, `\COUNTRY.SYS` and then
`\DOS\COUNTRY.SYS` of the boot drive are tried (DOS 4.00 tries the root only).

**NLSFUNC** is the TSR the kernel asks for COUNTRY.SYS after boot (INT 2Fh AH=14h, the
interface in kernel/README.md, "National language support"): CHCP's code page change, AH=38h/65h information about
another country or code page, AH=38h "set country". A re-creation of MS-DOS 4.00's NLSFUNC
(`CMD/NLSFUNC`, Microsoft, MIT licence; Portions (C) Microsoft Corp., MIT License):

* silent when it installs; `NLSFUNC already installed`; `File not found` for a COUNTRY.SYS
  given on its command line that is not there (all on standard error, as NLSFUNC 4.00);
* COUNTRY.SYS: the file given to NLSFUNC, else the kernel's (COUNTRY='s, default
  `\COUNTRY.SYS` on the current drive, and - ARM-DOS - `\DOS\COUNTRY.SYS` on the boot drive);
* for CHCP it loads the code page's tables and selects the code page on CON (DISPLAY.SYS,
  which tells KEYB), and changes nothing if that fails.

So, as in DOS 4.00: `CHCP` shows the global code page; `CHCP 850` needs NLSFUNC (`NLSFUNC not
installed`), a code page the country has (`Invalid code page`), and, with DISPLAY.SYS, a code
page prepared on CON (`Code page 850 not prepared for all devices`); without DISPLAY.SYS the
screen stays at 437 while DOS's tables switch.

Resident size: 3.1 KB (with its PSP).

## Tests

`make nlsfunc-test` (tests/run.mjs): COUNTRY.SYS = DOS 4.00's (when available); DATE, TIME, DIR
and the code page for 18 COUNTRY= lines against DOS 4.00 under DOSBox-X; the COUNTRY= error
messages; a CHCP session with DISPLAY.SYS, KEYB and NLSFUNC (19 commands, the messages on the
screen, the screen's and KEYB's code page following) against DOS 4.00; NLSFUNC without COUNTRY=
and with a missing file. The kernel side: kernel/tests scenarios `nls` and `nls-nlsfunc`.

## Deviations

* `\DOS\COUNTRY.SYS` as a second default (above).
* Brazil (055) is new.
* A failed `CHCP nnn` leaves the global code page as it was (DOS 4.00's NLSFUNC is not
  documented on this point).
