/* ttstext.c - text normalisation for DRARM's speech engine: numbers, ordinals,
 * money, abbreviations, symbols and acronyms become plain words; punctuation
 * becomes phrase marks. Each token remembers where it came from in the source
 * text (so the caller can show words as they are spoken). */
#include <string.h>
#include <stdlib.h>
#include "tts.h"
#include "ttsint.h"

static int toupper_(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

int tts_spell_mode;

static tts_token *tk;
static int ntk, maxtk;

static void add(int kind, const char *text, int len, int src)
{
    if (ntk >= maxtk) return;
    if (len > (int)sizeof tk[0].text - 1) len = sizeof tk[0].text - 1;
    tk[ntk].kind = (unsigned char)kind;
    memcpy(tk[ntk].text, text, len); tk[ntk].text[len] = 0;
    tk[ntk].src = (short)src;
    tk[ntk].nodict = 0;
    ntk++;
}
static void addw(const char *w, int src) { add(TK_WORD, w, (int)strlen(w), src); }

/* letter names in phoneme notation */
static const char *const letter_ph[26] = {
    "EY1", "bIY1", "sIY1", "dIY1", "IY1", "EH1f", "jIY1", "EY1CH", "AY1", "jEY1", "kEY1",
    "EH1l", "EH1m", "EH1n", "OW1", "pIY1", "kyUW1", "AA1r", "EH1s", "tIY1", "yUW1", "vIY1",
    "dAH1bAXlyUW", "EH1ks", "wAY1", "zIY1"
};

static void add_letter(int c, int src)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= 'A' && c <= 'Z') add(TK_PHON, letter_ph[c - 'A'], (int)strlen(letter_ph[c - 'A']), src);
    else if (c >= '0' && c <= '9') {
        static const char *const dg[] = { "ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE" };
        addw(dg[c - '0'], src);
    }
}

static const char *const ones[] = { "", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN",
    "EIGHT", "NINE", "TEN", "ELEVEN", "TWELVE", "THIRTEEN", "FOURTEEN", "FIFTEEN", "SIXTEEN",
    "SEVENTEEN", "EIGHTEEN", "NINETEEN" };
static const char *const tens[] = { "", "", "TWENTY", "THIRTY", "FORTY", "FIFTY", "SIXTY",
    "SEVENTY", "EIGHTY", "NINETY" };
static const char *const ord_ones[] = { "", "FIRST", "SECOND", "THIRD", "FOURTH", "FIFTH",
    "SIXTH", "SEVENTH", "EIGHTH", "NINTH", "TENTH", "ELEVENTH", "TWELFTH", "THIRTEENTH",
    "FOURTEENTH", "FIFTEENTH", "SIXTEENTH", "SEVENTEENTH", "EIGHTEENTH", "NINETEENTH" };
static const char *const ord_tens[] = { "", "", "TWENTIETH", "THIRTIETH", "FORTIETH",
    "FIFTIETH", "SIXTIETH", "SEVENTIETH", "EIGHTIETH", "NINETIETH" };

/* words for n (0 < n < 1000); ordinal: the last word becomes an ordinal */
static void say_hundreds(unsigned n, int src, int ordinal)
{
    if (n >= 100) {
        addw(ones[n / 100], src);
        addw(ordinal && n % 100 == 0 ? "HUNDREDTH" : "HUNDRED", src);
        n %= 100;
    }
    if (n >= 20) {
        if (n % 10) { addw(tens[n / 10], src); addw(ordinal ? ord_ones[n % 10] : ones[n % 10], src); }
        else addw(ordinal ? ord_tens[n / 10] : tens[n / 10], src);
    } else if (n) addw(ordinal ? ord_ones[n] : ones[n], src);
}

static void say_number(unsigned long n, int src, int ordinal)
{
    static const char *const big[] = { "BILLION", "MILLION", "THOUSAND" };
    static const unsigned long div[] = { 1000000000ul, 1000000ul, 1000ul };
    int i;
    if (n == 0) { addw(ordinal ? "ZEROTH" : "ZERO", src); return; }
    for (i = 0; i < 3; i++)
        if (n >= div[i]) {
            say_hundreds((unsigned)(n / div[i]), src, 0);
            n %= div[i];
            addw(ordinal && !n ? (i == 2 ? "THOUSANDTH" : i == 1 ? "MILLIONTH" : "BILLIONTH") : big[i], src);
        }
    if (n) say_hundreds((unsigned)n, src, ordinal);
}

/* a year-like 4-digit number: 1100-1999 and 2010-2099 read in pairs */
static void say_year(unsigned n, int src)
{
    unsigned hi = n / 100, lo = n % 100;
    say_hundreds(hi, src, 0);
    if (lo == 0) addw("HUNDRED", src);
    else if (lo < 10) { addw("OH", src); say_hundreds(lo, src, 0); }
    else say_hundreds(lo, src, 0);
}

static const struct { const char *abbr; const char *words; } abbrevs[] = {
    { "DR", "DOCTOR" }, { "MR", "MISTER" }, { "MRS", "MISSUS" }, { "MS", "MIZ" },
    { "ST", "STREET" }, { "JR", "JUNIOR" }, { "SR", "SENIOR" }, { "VS", "VERSUS" },
    { "ETC", "ETCETERA" }, { "EG", "FOR EXAMPLE" }, { "IE", "THAT IS" }, { "NO", 0 },
    { "KB", "KILOBYTES" }, { "MB", "MEGABYTES" }, { "K", "K" }, { "MHZ", "MEGAHERTZ" },
    { "KHZ", "KILOHERTZ" }, { "HZ", "HERTZ" }, { "OS", "O S" }, { "AI", "A I" },
    { "TV", "T V" }, { "UK", "U K" }, { "USA", "U S A" }, { "ASAP", "A S A P" },
    { "LOL", "L O L" }, { "OK", "OKAY" }, { "RAM", "RAM" }, { "ROM", "ROM" },
    { 0, 0 }
};

static char rules_only[40];

void tts_rules_words(const char *w)
{
    int i;
    for (i = 0; w[i] && i < (int)sizeof rules_only - 2; i++) rules_only[i] = (char)((w[i] >= 'a' && w[i] <= 'z') ? w[i] - 32 : w[i]);
    rules_only[i] = 0;
}

static int in_rules_only(const char *w)
{
    const char *p = rules_only;
    size_t n = strlen(w);
    while (*p) {
        while (*p == ' ') p++;
        if (!strncmp(p, w, n) && (p[n] == ' ' || !p[n])) return 1;
        while (*p && *p != ' ') p++;
    }
    return 0;
}

static int has_vowel(const char *w)
{
    for (; *w; w++) if (strchr("AEIOUY", *w)) return 1;
    return 0;
}

/* a word token: abbreviations, vowel-less acronyms are spelled */
static void add_word(const char *w, int len, int src, int dotted)
{
    char buf[40];
    int i;
    if (len > 38) len = 38;
    for (i = 0; i < len; i++) buf[i] = (w[i] >= 'a' && w[i] <= 'z') ? w[i] - 32 : w[i];
    buf[len] = 0;
    if (tts_spell_mode) { for (i = 0; i < len; i++) if (buf[i] != '\'') add_letter(buf[i], src); return; }
    if (in_rules_only(buf)) { add(TK_WORD, buf, len, src); tk[ntk - 1].nodict = 1; return; }
    for (i = 0; abbrevs[i].abbr; i++)
        if (!strcmp(buf, abbrevs[i].abbr) && abbrevs[i].words && (dotted || strcmp(buf, "NO"))) {
            const char *p = abbrevs[i].words;
            if (!strcmp(buf, "NO") || (!dotted && (!strcmp(buf, "ST") || !strcmp(buf, "MS")))) break;
            while (*p) {
                const char *e = strchr(p, ' ');
                int l = e ? (int)(e - p) : (int)strlen(p);
                if (l == 1) add_letter(*p, src); else add(TK_WORD, p, l, src);
                p += l; while (*p == ' ') p++;
            }
            return;
        }
    if (len == 1 && buf[0] != 'A' && buf[0] != 'I') { add_letter(buf[0], src); return; }
    if (!tts_no_dict && !has_vowel(buf) && len <= 5) {
        for (i = 0; i < len; i++) if (buf[i] != '\'') add_letter(buf[i], src);
        return;
    }
    add(TK_WORD, buf, len, src);
}

static int isal(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static int isdg(int c) { return c >= '0' && c <= '9'; }

int tts_tokenise(const char *s, tts_token *out, int max)
{
    int i = 0, n = (int)strlen(s);
    tk = out; ntk = 0; maxtk = max;
    while (i < n) {
        int c = (unsigned char)s[i];
        if (isal(c)) {
            int j = i;
            while (j < n && (isal(s[j]) || (s[j] == '\'' && isal(s[j + 1])))) j++;
            /* dotted abbreviation "E.G." "DR." */
            add_word(s + i, j - i, i, j < n && s[j] == '.' && j - i <= 3);
            if (j < n && s[j] == '.' && j - i <= 3 && j + 1 < n && s[j + 1] == ' ' && ntk && tk[ntk - 1].kind == TK_WORD) {
                /* "DR. SMITH": the dot of a known title is not a full stop */
                const char *t = tk[ntk - 1].text;
                if (!strcmp(t, "DOCTOR") || !strcmp(t, "MISTER") || !strcmp(t, "MISSUS") || !strcmp(t, "STREET")
                    || !strcmp(t, "JUNIOR") || !strcmp(t, "SENIOR") || !strcmp(t, "VERSUS") || !strcmp(t, "MIZ")) j++;
            }
            i = j;
        } else if (isdg(c) || ((c == '$' || c == '-') && isdg(s[i + 1]))) {
            int money = 0, neg = 0, j;
            unsigned long v = 0;
            int src = i;
            if (c == '$') { money = 1; i++; }
            else if (c == '-') { neg = 1; i++; }
            j = i;
            while (j < n && (isdg(s[j]) || (s[j] == ',' && isdg(s[j + 1]) && isdg(s[j + 2]) && isdg(s[j + 3])))) {
                if (isdg(s[j]) && v < 400000000ul) v = v * 10 + (s[j] - '0');
                j++;
            }
            if (neg) addw("MINUS", src);
            if (tts_spell_mode || (!money && !neg && ((src > 0 && isal(s[src - 1])) || (j < n && isal(s[j]) && !(j + 1 < n && isal(s[j + 1]) && !isal(s[j + 2])))))) { int k; for (k = i; k < j; k++) if (isdg(s[k])) add_letter(s[k], k); i = j; continue; }
            /* ordinal suffix */
            if (j + 1 < n && ((toupper_(s[j]) == 'S' && toupper_(s[j + 1]) == 'T') || (toupper_(s[j]) == 'N' && toupper_(s[j + 1]) == 'D')
                || (toupper_(s[j]) == 'R' && toupper_(s[j + 1]) == 'D') || (toupper_(s[j]) == 'T' && toupper_(s[j + 1]) == 'H'))
                && !isal(s[j + 2])) {
                say_number(v, src, 1); i = j + 2; continue;
            }
            if (!money && j - i == 4 && v >= 1100 && v <= 2099 && !(v >= 2000 && v < 2010) && s[i + 1] != ',') say_year((unsigned)v, src);
            else if (j - i > 1 && s[i] == '0' && !money) { int k; for (k = i; k < j; k++) add_letter(s[k], k); }
            else say_number(v, src, 0);
            i = j;
            if (i + 1 < n && s[i] == '.' && isdg(s[i + 1])) {
                int cents = 0, k = 0;
                i++;
                if (money) {
                    while (i < n && isdg(s[i])) { if (k < 2) cents = cents * 10 + (s[i] - '0'); k++; i++; }
                    if (k == 1) cents *= 10;
                    addw(v == 1 ? "DOLLAR" : "DOLLARS", src);
                    if (cents) { addw("AND", src); say_number(cents, src, 0); addw(cents == 1 ? "CENT" : "CENTS", src); }
                    money = 0;
                } else {
                    addw("POINT", src);
                    while (i < n && isdg(s[i])) { add_letter(s[i], i); i++; }
                }
            }
            if (money) addw(v == 1 ? "DOLLAR" : "DOLLARS", src);
            if (i < n && s[i] == '%') { addw("PERCENT", src); i++; }
        } else {
            const char *w = 0;
            switch (c) {
            case '.': case '!': case ';': case ':':
                add(TK_PUNCT, (c == '!' ? "." : c == ';' || c == ':' ? "," : "."), 1, i);
                while (i + 1 < n && (s[i + 1] == '.' || s[i + 1] == '!')) i++;
                break;
            case '?': add(TK_PUNCT, "?", 1, i); while (i + 1 < n && (s[i + 1] == '?' || s[i + 1] == '!')) i++; break;
            case ',': add(TK_PUNCT, ",", 1, i); break;
            case '(': case ')': case '"': case '-':
                if (c == '-' && i > 0 && s[i - 1] != ' ' && s[i + 1] != ' ') break;   /* hyphenated word */
                add(TK_PUNCT, ",", 1, i); break;
            case '&': w = "AND"; break;
            case '@': w = "AT"; break;
            case '+': w = "PLUS"; break;
            case '=': w = "EQUALS"; break;
            case '%': w = "PERCENT"; break;
            case '#': w = "NUMBER"; break;
            case '*': w = "STAR"; break;
            case '/': w = "SLASH"; break;
            case '\\': w = "BACKSLASH"; break;
            case '<': w = "LESS THAN"; break;
            case '>': w = "GREATER THAN"; break;
            case '$': w = "DOLLARS"; break;
            default: break;
            }
            if (w) {
                while (*w) {
                    const char *e = strchr(w, ' ');
                    int l = e ? (int)(e - w) : (int)strlen(w);
                    add(TK_WORD, w, l, i);
                    w += l; while (*w == ' ') w++;
                }
            }
            i++;
        }
    }
    return ntk;
}

int tts_normalise(const char *in, char *out, int outsz)
{
    static tts_token t[128];
    int n = tts_tokenise(in, t, 128), i, o = 0;
    out[0] = 0;
    for (i = 0; i < n; i++) {
        int l = (int)strlen(t[i].text);
        if (o + l + 3 >= outsz) break;
        if (o && t[i].kind != TK_PUNCT) out[o++] = ' ';
        if (t[i].kind == TK_PHON) out[o++] = '/';
        memcpy(out + o, t[i].text, l); o += l;
    }
    out[o] = 0;
    return o;
}
