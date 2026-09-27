/*
 * messages.c - every text COMMAND prints, numbered as in MS-DOS 4.0's
 * CMD/COMMAND/COMMAND.SKL with the texts of MESSAGES/USA-MS.MSG (sections
 * COMMAND, COMMON, EXTEND, PARSE).
 *
 * Branding (ARM-DOS policy): 464 and 1040 say ARM-DOS instead of MS-DOS.
 */
#include "cmd.h"

#define CRLF "\r\n"

struct msgent { uint16_t n; const char *t; };

static const struct msgent msgs[] = {
    /* class A - resident */
    { 201, "A" }, { 202, "R" }, { 203, "I" }, { 204, "F" }, { 205, "Y" }, { 206, "N" },
    { 210, "Abort" },
    { 211, ", Retry" },
    { 212, ", Ignore" },
    { 213, ", Fail" },
    { 214, "?" },
    { 215, "reading" },
    { 216, "writing" },
    { 217, " %1 drive %2" CRLF },
    { 218, " %1 device %2" CRLF },
    { 219, "Please insert volume %1 serial %2-%3" CRLF },
    { 220, "File allocation table bad, drive %1" CRLF },
    { 221, "Invalid COMMAND.COM" CRLF },
    { 222, "Insert disk with %1 in drive %2" CRLF },
    { 223, "Press any key to continue . . ." CRLF },
    { 224, CRLF "Terminate batch job (Y/N)?" },
    { 225, "Cannot execute %1" CRLF },
    { 226, "Error in EXE file" CRLF },
    { 227, "Program too big to fit in memory" CRLF },
    { 228, CRLF "No free file handles" },
    { 229, "Bad Command or file name" CRLF },
    { 230, "Access denied " },
    { 231, CRLF "Memory allocation error" },
    { 232, CRLF "Cannot load COMMAND, system halted" CRLF },
    { 233, CRLF "Cannot start COMMAND, exiting" CRLF },
    { 234, CRLF "Top level process aborted, cannot continue" CRLF },
    { 235, CRLF },
    /* class B - initialisation */
    { 461, "Incorrect DOS version" CRLF },
    { 463, "Out of environment space" CRLF },
    { 464, CRLF CRLF "ARM-DOS(R) Version 4.00" CRLF
           "             (C)Copyright Europa Micro Systems 1988" CRLF },
    { 465, "Specified COMMAND search directory bad" CRLF },
    { 466, "Specified COMMAND search directory bad access denied" CRLF },
    /* class F - transient */
    { 1002, "Duplicate file name or file not found" CRLF },
    { 1003, "Invalid path or file name" CRLF },
    { 1004, "Insufficient disk space" CRLF },
    { 1007, "Out of environment space" CRLF },
    { 1008, "File creation error" CRLF },
    { 1009, "Batch file missing" CRLF },
    { 1010, CRLF "Insert disk with batch file" CRLF },
    { 1011, "Bad command or file name" CRLF },
    { 1014, "Access denied " },
    { 1015, "File cannot be copied onto itself" CRLF },
    { 1016, "Content of destination lost before copy" CRLF },
    { 1017, "Invalid filename or file not found" CRLF },
    { 1018, "%1 File(s) copied" CRLF },
    { 1019, "%1 File(s) " },
    { 1020, "%1 bytes free" CRLF },
    { 1021, "Invalid drive specification" CRLF },
    { 1022, "Code page %1 not prepared for system" CRLF },
    { 1023, "Code page %1 not prepared for all devices" CRLF },
    { 1024, "Active code page: %1" CRLF },
    { 1025, "NLSFUNC not installed" CRLF },
    { 1026, "Invalid code page" CRLF },
    { 1027, "Current drive is no longer valid" },
    { 1028, "Press any key to continue . . ." CRLF },
    { 1029, "Label not found" CRLF },
    { 1030, "Syntax error" CRLF },
    { 1031, "Invalid date" CRLF },
    { 1032, "Current date is %1 %2" CRLF },
    { 1033, "SunMonTueWedThuFriSat" },
    { 1034, "Enter new date (%1): " },
    { 1035, "Invalid time" CRLF },
    { 1036, "Current time is %1" CRLF },
    { 1037, "Enter new time: " },
    { 1038, ",    Delete (Y/N)?" },
    { 1039, "All files in directory will be deleted!" CRLF "Are you sure (Y/N)?" },
    { 1040, "ARM-DOS Version %1.%2" },
    { 1041, "Volume in drive %1 has no label" CRLF },
    { 1042, "Volume in drive %1 is %2" CRLF },
    { 1043, "Volume Serial Number is %1-%2" CRLF },
    { 1044, "Invalid directory" CRLF },
    { 1045, "Unable to create directory" CRLF },
    { 1046, "Invalid path, not directory," CRLF "or directory not empty" CRLF },
    { 1047, "Must specify ON or OFF" CRLF },
    { 1048, "Directory of  %1" CRLF },
    { 1049, "No Path" CRLF },
    { 1050, "Invalid drive in search path" CRLF },
    { 1051, "Invalid device" CRLF },
    { 1052, "FOR cannot be nested" CRLF },
    { 1053, "Intermediate file error during pipe" CRLF },
    { 1054, "Cannot do binary reads from a device" CRLF },
    { 1055, "BREAK is %1" CRLF },
    { 1056, "VERIFY is %1" CRLF },
    { 1057, "ECHO is %1" CRLF },
    { 1059, "off" },
    { 1060, "on" },
    { 1061, "Error writing to device" CRLF },
    { 1062, "Invalid path" CRLF },
    { 1063, "%1" }, { 1064, "%1" }, { 1065, "%1" }, { 1066, "%1" },
    { 1067, "\t" },
    { 1068, " <DIR>    " },
    { 1069, "\b \b" },
    { 1070, CRLF },
    { 1071, "%1" },
    { 1072, "mm-dd-yy" },
    { 1073, "dd-mm-yy" },
    { 1074, "yy-mm-dd" },
    { 1075, "%1 %2" },
    { 1076, "%1" },
    { 1077, " %1  %2" },
    { 1078, "Directory already exists" CRLF },
    /* ARM-DOS additions: VER's copyright lines */
    { 1901, "Copyright (C) 2026 Kevin Day, with lots of help from Europa." CRLF },
    { 1902, "See documentation for full copyright notices and acknowledgements." CRLF },
    /* ARM-DOS additions: the hidden commands (see eggs.c; spoilers in README.md) */
    { 1910, "Microsoft Diagnostics? On an ARM926? Type ARMINFO - it knows this" CRLF
            "machine better than anyone in Redmond ever will." CRLF },
    { 1911, "Windows? This machine has GEM. Type GEM - it was here first, and it" CRLF
            "doesn't need a 386." CRLF },
    { 1912, "DELTREE arrives with DOS 6.0, in 1993. Patience." CRLF },
    { 1913, "640K ought to be enough for anybody. You have 15 MB of extended" CRLF
            "memory anyway. Relax." CRLF },
    { 1914, "No Intel inside. This is an ARM926EJ-S." CRLF
            "(For the other kind of PC, look for the ELBOW box.)" CRLF },
    { 1915, "Nothing happens." CRLF },
    { 1916, "A hollow voice says \"Plugh\"." CRLF },
    { 1917, "Degreelessness mode on. (It doesn't do anything here, but it feels good.)" CRLF },
    { 1918, "Very happy ammo added. Now go and find a keyboard with DOOM on it." CRLF },
    { 1919, "I'm sorry, Dave. I'm afraid I can't do that." CRLF },
    { 1920, "ELIZA has retired. Her colleague will see you now." CRLF },
    { 1921, "42. Now, what was the question?" CRLF },
    { 1922, "It's 1988. Nobody has heard of sudo. Besides, this is DOS: you are" CRLF
            "already root." CRLF },
    { 1923, "This is not Unix. Did you mean DIR? Running DIR." CRLF },
    /* UNAME (eggs.c) */
    { 1930, "uname: invalid option -- '%1'" CRLF },
    { 1931, "uname: unrecognized option '%1'" CRLF },
    { 1932, "uname: extra operand '%1'" CRLF },
    { 1933, "Try 'uname --help' for more information." CRLF },
    { 1934, "Usage: uname [OPTION]..." CRLF
            "Print certain system information.  With no OPTION, same as -s." CRLF
            CRLF
            "  -a, --all                print all information, in the following order:" CRLF
            "  -s, --kernel-name        print the kernel name" CRLF
            "  -n, --nodename           print the network node hostname" CRLF
            "  -r, --kernel-release     print the kernel release" CRLF
            "  -v, --kernel-version     print the kernel version" CRLF
            "  -m, --machine            print the machine hardware name" CRLF
            "  -p, --processor          print the processor type" CRLF
            "  -i, --hardware-platform  print the hardware platform" CRLF
            "  -o, --operating-system   print the operating system" CRLF
            "      --help     display this help and exit" CRLF
            "      --version  output version information and exit" CRLF
            CRLF
            "(This is still not Unix.)" CRLF },
    { 1935, "uname (ARM-DOS) 4.00" CRLF
            "Copyright (C) 1988 Europa Micro Systems. Not a trace of Unix inside." CRLF },
    { 0, 0 }
};

/* EXTEND section: extended error texts 1-90 (no CR LF; the retriever adds it) */
static const char *const extend[] = {
    0,
    "Invalid function", "File not found", "Path not found", "Too many open files",
    "Access denied ", "Invalid handle", "Memory control blocks destroyed",
    "Insufficient memory", "Invalid memory block address", "Invalid Environment",
    "Invalid format", "Invalid function parameter", "Invalid data", "",
    "Invalid drive specification", "Attempt to remove current directory",
    "Not same device", "No more files", "Write protect error", "Invalid unit",
    "Not ready", "Invalid device request", "Data error",
    "Invalid device request parameters", "Seek error", "Invalid media type",
    "Sector not found", "Printer out of paper error", "Write fault error",
    "Read fault error", "General failure", "Sharing violation", "Lock violation",
    "Invalid disk change", "FCB unavailable", "System resource exhausted",
    "Code page mismatch", "Out of input", "Insufficient disk space",
    "", "", "", "", "", "", "", "", "", "",                            /* 40-49 */
    "NET809: Network request not supported", "NET801: Remote computer not listening",
    "NET802: Duplicate name on network", "NET803: Network path not found",
    "NET804: Network busy", "NET805: Network device no longer exists",
    "NET806: NETBIOS command limit exceeded", "NET807: System error; NETBIOS error",
    "NET808: Incorrect response from network", "NET810: Unexpected network error",
    "NET811: Incompatible remote adapter", "NET812: Print queue full",
    "NET813: Not enough space for print file", "NET814: Print file was cancelled",
    "NET815: Network name was deleted", "Access denied",
    "NET817: Network device type incorrect", "NET818: Network name not found",
    "NET819: Network name limit exceeded", "NET820: NETBIOS session limit exceeded",
    "NET821: Sharing temporarily paused", "NET823: Network request not accepted",
    "NET822: Print or disk redirection is paused", "NET476: Netbeui not loaded",
    "NET477: Unexpected adapter close", "", "", "", "", "",          /* 75-79 */
    "File exists", "", "Cannot make directory entry", "Fail on INT 24",
    "Too many redirections", "Duplicate redirection", "Invalid password",
    "Invalid parameter", "Network data fault", "Function not supported by network",
    "Required system component not installed",
};

static const char *const parse[] = {
    0, "Too many parameters", "Required parameter missing", "Invalid switch",
    "Invalid keyword", "", "Parameter value not in allowed range",
    "Parameter value not allowed", "Parameter value not allowed",
    "Parameter format not correct", "Invalid parameter",
    "Invalid parameter combination",
};

const char *msg(int n)
{
    for (const struct msgent *m = msgs; m->t; m++)
        if (m->n == n) return m->t;
    return "";
}

const char *ext_msg(int err)
{
    static char buf[24];
    if (err > 0 && err < (int)(sizeof extend / sizeof extend[0]) && extend[err][0])
        return extend[err];
    /* the retriever's fallback for a number without text (EXTEND999) */
    char *p = buf;
    const char *t = "Extended Error ";
    while (*t) *p++ = *t++;
    fmt_uint(p, err, 0, ' ');
    return buf;
}

const char *parse_msg(int n)
{
    static char buf[24];
    if (n > 0 && n < (int)(sizeof parse / sizeof parse[0]) && parse[n][0])
        return parse[n];
    char *p = buf;
    const char *t = "Parse Error ";
    while (*t) *p++ = *t++;
    fmt_uint(p, n, 0, ' ');
    return buf;
}

/* print message n with %1..%3 replaced */
void msgout(int h, int n, const char *s1, const char *s2, const char *s3)
{
    const char *t = msg(n);
    const char *start = t;
    for (; *t; t++) {
        if (*t == '%' && t[1] >= '1' && t[1] <= '3') {
            if (t > start) outn(h, start, t - start);
            const char *s = t[1] == '1' ? s1 : t[1] == '2' ? s2 : s3;
            if (s) out(h, s);
            t++;
            start = t + 1;
        }
    }
    if (t > start) outn(h, start, t - start);
}

void std_printf(int n) { msgout(1, n, 0, 0, 0); }
void std_eprintf(int n) { msgout(2, n, 0, 0, 0); }

void printf_crlf(int h, const char *s)
{
    out(h, s);
    crlf(h);
}

/* an extended (class 1) message: text, optional " - sub", CR LF */
void ext_error_out(int err, const char *sub)
{
    out(2, ext_msg(err));
    if (sub) {
        out(2, " - ");
        out(2, sub);
    }
    crlf(2);
}

void parse_error_out(int n, const char *sub)
{
    out(2, parse_msg(n));
    if (sub) {
        out(2, " - ");
        out(2, sub);
    }
    crlf(2);
}
