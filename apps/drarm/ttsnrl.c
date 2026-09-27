/* ttsnrl.c - English letter-to-sound rules for DRARM's speech engine.
 *
 * The rules follow the method and rule set of the US Naval Research
 * Laboratory report NRL-7948, "Automatic Translation of English Text to
 * Phonetics by Means of Letter-to-Sound Rules" (H. S. Elovitz, R. Johnson,
 * A. McHugh, J. E. Shore, 1976) - a US Government work in the public domain.
 * Written out here from the report's description, with a few additions.
 *
 * A rule is  "left[match]right=phonemes": the letters `match` at the current
 * position, preceded by `left` and followed by `right`, give `phonemes`.
 * Rules are tried in order within each letter; the first that matches wins.
 * Context symbols:
 *    ' '  word boundary (any non-letter)
 *    #    one or more vowels            :  zero or more consonants
 *    ^    one consonant                 .  one voiced consonant (B D G J L M N R V W Z)
 *    +    E, I or Y (front vowel)       %  suffix: ER E ES ED ING ELY (right only)
 *    &    sibilant: S C G Z X J CH SH   @  T S R D L Z N J TH CH SH (long-u consonants)
 * Phoneme notation: vowels and two-letter consonants in capitals (IY IH EY EH AE
 * AA AO OW UH UW AH AX ER AY AW OY, TH DH SH ZH NG CH WH), single consonants in
 * lower case (p b t d k g f v s z h m n l r w y j).
 */
#include <string.h>
#include "tts.h"

static const char *const rules[] = {
    /* A */
    "[A] =AX", " [ARE] =AAr", " [AR]O=AXr", "[AR]#=EHr", " ^[AS]#=EYs", "[A]WA=AX",
    "[AW]=AO", " :[ANY]=EHnIY", "[A]^+#=EY", "#:[ALLY]=AXlIY", " [AL]#=AXl",
    "[AGAIN]=AXgEHn", "#:[AG]E=IHj", "[A]^+:#=AE", " :[A]^+ =EY", "[A]^%=EY",
    " [ARR]=AXr", "[ARR]=AEr", " :[AR] =AAr", "[AR] =ER", "[AR]=AAr", "[AIR]=EHr",
    "[AI]=EY", "[AY]=EY", "[AU]=AO", "#:[AL] =AXl", "#:[ALS] =AXlz", "[ALK]=AOk",
    "[AL]^=AOl", " :[ABLE]=EYbAXl", "[ABLE]=AXbAXl", "[ANG]+=EYnj", "[A]=AE",
    /* B */
    " [BE]^#=bIH", "[BEING]=bIYIHNG", " [BOTH] =bOWTH", " [BUS]#=bIHz", "[BUIL]=bIHl",
    "[B]=b",
    /* C */
    " [CH]^=k", "^E[CH]=k", "[CH]=CH", " S[CI]#=sAY", "[CI]A=SH", "[CI]O=SH",
    "[CI]EN=SH", "[C]+=s", "[CK]=k", "[COM]%=kAHm", "[C]=k",
    /* D */
    "#:[DED] =dIHd", ".E[D] =d", "#:^E[D] =t", " [DE]^#=dIH", " [DO] =dUW",
    " [DOES]=dAHz", " [DOING]=dUWIHNG", " [DOW]=dAW", "[DU]A=jUW", "[D]=d",
    /* E */
    "#:[E] =", " :^[E] =", " :[E] =IY", "#[ED] =d", "#:[E]D =", "[EV]ER=EHv",
    "[E]^%=IY", "[ERI]#=IYrIY", "[ERI]=EHrIH", "#:[ER]#=ER", "[ER]#=EHr", "[ER]=ER",
    " [EVEN]=IYvEHn", "#:[E]W=", "@[EW]=UW", "[EW]=yUW", "[E]O=IY", "#:&[ES] =IHz",
    "#:[E]S =", "#:[ELY] =lIY", "#:[EMENT]=mEHnt", "[EFUL]=fUHl", "[EE]=IY",
    "[EARN]=ERn", " [EAR]^=ER", "[EAD]=EHd", "#:[EA] =IYAX", "[EA]SU=EH", "[EA]=IY",
    "[EIGH]=EY", "[EI]=IY", " [EYE]=AY", "[EY]=IY", "[EU]=yUW", "[E]=EH",
    /* F */
    "[FUL]=fUHl", "[F]=f",
    /* G */
    "[GIV]=gIHv", " [G]I^=g", "[GE]T=gEH", "SU[GGES]=gjEHs", "[GG]=g", " B#[G]=g",
    "[G]+=j", "[GREAT]=grEYt", "#[GH]=", "[G]=g",
    /* H */
    " [HAV]=hAEv", " [HERE]=hIYr", " [HOUR]=AWER", "[HOW]=hAW", "[H]#=h", "[H]=",
    /* I */
    " [IN]=IHn", " [I] =AY", "[IN]D=AYn", "[IER]=IYER", "#:R[IED]=IYd", "[IED] =AYd",
    "[IEN]=IYEHn", "[IE]T=AYEH", " :[I]%=AY", "[I]%=IY", "[IE]=IY", "[I]^+:#=IH",
    "[IR]#=AYr", "[IZ]%=AYz", "[IS]%=AYz", "[I]D%=AY", "+^[I]^+=IH", "[I]T%=AY",
    "#:^[I]^+=IH", "[I]^+=AY", "[IR]=ER", "[IGH]=AY", "[ILD]=AYld", "[IGN] =AYn",
    "[IGN]^=AYn", "[IGN]%=AYn", "[IQUE]=IYk", "[I]=IH",
    /* J */
    "[J]=j",
    /* K */
    " [K]N=", "[K]=k",
    /* L */
    "[LO]C#=lOW", "L[L]=", "#:^[L]%=AXl", "[LEAD]=lIYd", "[L]=l",
    /* M */
    "[MOV]=mUWv", "[M]=m",
    /* N */
    "E[NG]+=nj", "[NG]R=NGg", "[NG]#=NGg", "[NGL]%=NGgAXl", "[NG]=NG", "[NK]=NGk",
    " [NOW] =nAW", "[N]=n",
    /* O */
    "[OF] =AXv", "[OROUGH]=EROW", "#:[OR] =ER", "#:[ORS] =ERz", "[OR]=AOr",
    " [ONE]=wAHn", "[OW]=OW", " [OVER]=OWvER", "[OV]=AHv", "[O]^%=OW", "[O]^EN=OW",
    "[O]^I#=OW", "[OL]D=OWl", "[OUGHT]=AOt", "[OUGH]=AHf", " [OU]=AW", "H[OU]S#=AW",
    "[OUS]=AXs", "[OUR]=AOr", "[OULD]=UHd", "^[OU]^L=AH", "[OUP]=UWp", "[OU]=AW",
    "[OY]=OY", "[OING]=OWIHNG", "[OI]=OY", "[OOR]=AOr", "[OOK]=UHk", "[OOD]=UHd",
    "[OO]=UW", "[O]E=OW", "[O] =OW", "[OA]=OW", " [ONLY]=OWnlIY", " [ONCE]=wAHns",
    "[ON'T]=OWnt", "C[O]N=AA", "[O]NG=AO", " :^[O]N=AH", "I[ON]=AXn", "#:[ON] =AXn",
    "#^[ON]=AXn", "[O]ST =OW", "[OF]^=AOf", "[OTHER]=AHDHER", "[OSS] =AOs",
    "#:^[OM]=AHm", "[O]=AA",
    /* P */
    "[PH]=f", "[PEOP]=pIYp", "[POW]=pAW", "[PUT] =pUHt", "[P]=p",
    /* Q */
    "[QUAR]=kwAOr", "[QU]=kw", "[Q]=k",
    /* R */
    " [RE]^#=rIY", "[R]=r",
    /* S */
    "[SH]=SH", "#[SION]=ZHAXn", "[SOME]=sAHm", "#[SUR]#=ZHER", "[SUR]#=SHER",
    "#[SU]#=ZHUW", "#[SSU]#=SHUW", "#[SED] =zd", "#[S]#=z", "[SAID]=sEHd",
    "^[SION]=SHAXn", "[S]S=", ".[S] =z", "#:.E[S] =z", "#:^##[S] =z", "#:^#[S] =s",
    "U[S] =s", " :#[S] =z", " [SCH]=sk", "[S]C+=", "#[SM]=zm", "#[SN]'=zAXn", "[S]=s",
    /* T */
    " [THE] =DHAX", "[TO] =tUW", "[THAT] =DHAEt", " [THIS] =DHIHs", " [THEY]=DHEY",
    " [THERE]=DHEHr", "[THER]=DHER", "[THEIR]=DHEHr", " [THAN] =DHAEn",
    " [THEM] =DHEHm", "[THESE] =DHIYz", " [THEN]=DHEHn", "[THROUGH]=THrUW",
    "[THOSE]=DHOWz", "[THOUGH] =DHOW", " [THUS]=DHAHs", "[TH]=TH", "#:[TED] =tIHd",
    "S[TI]#N=CH", "[TI]O=SH", "[TI]A=SH", "[TIEN]=SHAXn", "[TUR]#=CHER", "[TU]A=CHUW",
    " [TWO]=tUW", "[T]=t",
    /* U */
    " [UN]I=yUWn", " [UN]=AHn", " [UPON]=AXpAOn", "@[UR]#=UHr", "[UR]#=yUHr",
    "[UR]=ER", "[U]^ =AH", "[U]^^=AH", "[UY]=AY", " G[U]#=", "G[U]%=", "G[U]#=w",
    "#N[U]=yUW", "@[U]=UW", "[U]=yUW",
    /* V */
    "[VIEW]=vyUW", "[V]=v",
    /* W */
    " [WERE]=wER", "[WA]S=wAA", "[WA]T=wAA", "[WHERE]=WHEHr", "[WHAT]=WHAAt",
    "[WHOL]=hOWl", "[WHO]=hUW", "[WH]=WH", "[WAR]=wAOr", "[WOR]^=wER", "[WR]=r",
    "[W]=w",
    /* X */
    "[X]=ks",
    /* Y */
    "[YOUNG]=yAHNG", " [YOU]=yUW", " [YES]=yEHs", " [Y]=y", "#:^[Y] =IY",
    "#:^[Y]I=IY", " :[Y] =AY", " :[Y]#=AY", " :[Y]^+:#=IH", " :[Y]^#=AY", "[Y]=IH",
    /* Z */
    "[Z]=z",
    /* apostrophe and anything else: silent */
    "[']=",
    0
};

/* first rule index per letter A..Z, then the apostrophe */
static short first_rule[28];
static int rules_indexed;

static void index_rules(void)
{
    int i;
    for (i = 0; i < 28; i++) first_rule[i] = -1;
    for (i = 0; rules[i]; i++) {
        const char *b = strchr(rules[i], '[');
        int c = b[1] == '\'' ? 26 : b[1] - 'A';
        if (first_rule[c] < 0) first_rule[c] = (short)i;
    }
    rules_indexed = 1;
}

static int is_vowel(int c) { return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U'; }
static int is_alpha(int c) { return c >= 'A' && c <= 'Z'; }
static int is_cons(int c) { return is_alpha(c) && !is_vowel(c); }
static int is_voiced(int c) { return c && strchr("BDGJLMNRVWZ", c) != 0; }
static int is_front(int c) { return c == 'E' || c == 'I' || c == 'Y'; }

/* text is padded: text[0] == ' ', letters, ' ' terminator(s) */
static int left_match(const char *pat, int plen, const char *text, int pos)
{
    int p = plen - 1, t = pos - 1;
    for (; p >= 0; p--) {
        char c = pat[p];
        int ch = t >= 0 ? text[t] : ' ';
        if (c == ' ') { if (is_alpha(ch)) return 0; t--; continue; }
        if (is_alpha(c) || c == '\'') { if (ch != c) return 0; t--; continue; }
        switch (c) {
        case '#': if (!is_vowel(ch)) return 0; t--; while (t >= 0 && is_vowel(text[t])) t--; break;
        case ':': while (t >= 0 && is_cons(text[t])) t--; break;
        case '^': if (!is_cons(ch)) return 0; t--; break;
        case '.': if (!is_voiced(ch)) return 0; t--; break;
        case '+': if (!is_front(ch)) return 0; t--; break;
        case '&':
            if (ch == 'H' && t >= 1 && (text[t - 1] == 'C' || text[t - 1] == 'S')) { t -= 2; break; }
            if (ch && strchr("SCGZXJ", ch)) { t--; break; }
            return 0;
        case '@':
            if (ch == 'H' && t >= 1 && strchr("TCS", text[t - 1])) { t -= 2; break; }
            if (ch && strchr("TSRDLZNJ", ch)) { t--; break; }
            return 0;
        default: return 0;
        }
    }
    return 1;
}

static int right_match(const char *pat, int plen, const char *text, int pos)
{
    int p, t = pos;
    for (p = 0; p < plen; p++) {
        char c = pat[p];
        int ch = text[t];
        if (c == ' ') { if (is_alpha(ch)) return 0; if (ch) t++; continue; }
        if (is_alpha(c) || c == '\'') { if (ch != c) return 0; t++; continue; }
        switch (c) {
        case '#': if (!is_vowel(ch)) return 0; t++; while (is_vowel(text[t])) t++; break;
        case ':': while (is_cons(text[t])) t++; break;
        case '^': if (!is_cons(ch)) return 0; t++; break;
        case '.': if (!is_voiced(ch)) return 0; t++; break;
        case '+': if (!is_front(ch)) return 0; t++; break;
        case '&':
            if ((ch == 'C' || ch == 'S') && text[t + 1] == 'H') { t += 2; break; }
            if (ch && strchr("SCGZXJ", ch)) { t++; break; }
            return 0;
        case '@':
            if (strchr("TCS", ch) && ch && text[t + 1] == 'H') { t += 2; break; }
            if (ch && strchr("TSRDLZNJ", ch)) { t++; break; }
            return 0;
        case '%':
            if (ch == 'E') {
                if (text[t + 1] == 'L' && text[t + 2] == 'Y') { t += 3; break; }
                if (text[t + 1] == 'R' || text[t + 1] == 'S' || text[t + 1] == 'D') { t += 2; break; }
                t++; break;
            }
            if (ch == 'I' && text[t + 1] == 'N' && text[t + 2] == 'G') { t += 3; break; }
            return 0;
        default: return 0;
        }
    }
    return 1;
}

int tts_rules_word(const char *word, char *out, int outsz)
{
    char text[72];
    int n = 0, pos, o = 0;
    if (!rules_indexed) index_rules();
    text[n++] = ' ';
    while (*word && n < 68) {
        char c = *word++;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (is_alpha(c) || c == '\'') text[n++] = c;
    }
    text[n] = ' '; text[n + 1] = ' '; text[n + 2] = 0;
    out[0] = 0;
    for (pos = 1; pos < n;) {
        int c = text[pos], r, done = 0;
        int ci = c == '\'' ? 26 : c - 'A';
        for (r = first_rule[ci]; r >= 0 && rules[r]; r++) {
            const char *rule = rules[r];
            const char *lb = strchr(rule, '['), *rb = strchr(lb, ']'), *eq = strchr(rb, '=');
            int mlen = (int)(rb - lb - 1);
            if (lb[1] != c) break;                    /* past this letter's rules */
            if (strncmp(lb + 1, text + pos, mlen)) continue;
            if (!left_match(rule, (int)(lb - rule), text, pos)) continue;
            if (!right_match(rb + 1, (int)(eq - rb - 1), text, pos + mlen)) continue;
            {
                int l = (int)strlen(eq + 1);
                if (o + l < outsz) { memcpy(out + o, eq + 1, l); o += l; out[o] = 0; }
            }
            pos += mlen; done = 1;
            break;
        }
        if (!done) pos++;
    }
    return o;
}

/* ---- exception dictionary ------------------------------------------------
 * Words the rules get wrong, plus the doctor's own vocabulary. Same notation;
 * a digit after a vowel marks stress (1 primary, 2 secondary, 0 none). Words
 * marked "!" are unstressed function words. */
static const char *const dict[] = {
    "A!AX", "AN!AEn", "THE!DHAX", "OF!AXv", "TO!tUW", "AND!AEnd", "OR!AOr", "IN!IHn",
    "ON!AAn", "AT!AEt", "IS!IHz", "IT!IHt", "AM!AEm", "AS!AEz", "BE!bIY", "BY!bAY",
    "FOR!fAOr", "FROM!frAHm", "WITH!wIHDH", "ARE!AAr", "WAS!wAAz", "WERE!wER",
    "HAS!hAEz", "HAD!hAEd", "HAVE!hAEv", "DO!dUW", "DOES!dAHz", "YOU!yUW", "YOUR!yAOr",
    "I!AY", "ME!mIY", "MY!mAY", "WE!wIY", "HE!hIY", "SHE!SHIY", "THEY!DHEY", "IT'S!IHts",
    "THAT!DHAEt", "THIS!DHIHs", "BUT!bAHt", "SO!sOW", "IF!IHf", "NOT!nAAt", "CAN!kAEn",
    "WILL!wIHl", "WOULD!wUHd", "SHOULD!SHUHd", "COULD!kUHd", "HIS!hIHz", "HER!hER",
    "OUR!AWr", "THEIR!DHEHr", "THEM!DHEHm", "US!AHs", "HIM!hIHm", "THAN!DHAEn",
    "YOU'RE!yAOr", "I'M!AYm", "I'VE!AYv", "I'LL!AYl", "I'D!AYd", "WE'RE!wIYr",
    "DON'T=dOW1nt", "CAN'T=kAE1nt", "WON'T=wOW1nt", "ISN'T=IH1zAXnt", "DOESN'T=dAH1zAXnt",
    "DIDN'T=dIH1dAXnt", "WHAT=WHAH1t", "WHO=hUW1", "WHY=WHAY1", "WHEN=WHEH1n",
    "WHERE=WHEH1r", "HOW=hAW1", "ONE=wAH1n", "TWO=tUW1", "FOUR=fAO1r", "EIGHT=EY1t",
    "ZERO=zIH1rOW", "HELLO=hEHlOW1", "DOCTOR=dAA1ktER", "DR=dAA1ktER",
    "ARMITSO=AArmIY1tsOW", "SBAITSO=sbAY1tsOW", "COMPUTER=kAXmpyUW1tER",
    "SOUND=sAW1nd", "BLASTER=blAE1stER", "CREATIVE=krIYEY1tIHv", "PSYCHOLOGIST=sAYkAA1lAXjIHst",
    "PROBLEM=prAA1blAXm", "PROBLEMS=prAA1blAXmz", "PEOPLE=pIY1pAXl", "FRIEND=frEH1nd",
    "FRIENDS=frEH1ndz", "MOTHER=mAH1DHER", "FATHER=fAA1DHER", "BROTHER=brAH1DHER",
    "SISTER=sIH1stER", "FAMILY=fAE1mIHlIY", "WOMAN=wUH1mAXn", "WOMEN=wIH1mIHn",
    "ANY=EH1nIY", "MANY=mEH1nIY", "SAID=sEH1d", "SAYS=sEH1z", "AGAIN=AXgEH1n",
    "BEEN=bIH1n", "GIVE=gIH1v", "LIVE=lIH1v", "LOVE=lAH1v", "ABOVE=AXbAH1v",
    "COME=kAH1m", "SOME=sAH1m", "DONE=dAH1n", "NONE=nAH1n", "GONE=gAO1n", "WANT=wAA1nt",
    "EVERY=EH1vrIY", "EVERYTHING=EH1vrIYTHIHNG", "EVERYONE=EH1vrIYwAHn", "SOMETHING=sAH1mTHIHNG",
    "NOTHING=nAH1THIHNG", "ANYTHING=EH1nIYTHIHNG", "FEEL=fIY1l", "FEELING=fIY1lIHNG",
    "FEELINGS=fIY1lIHNGz", "THINK=THIH1NGk", "KNOW=nOW1", "BECAUSE=bIYkAO1z",
    "MAYBE=mEY1bIY", "PLEASE=plIY1z", "NAME=nEY1m", "HERE=hIY1r", "THERE=DHEH1r",
    "VERY=vEH1rIY", "SORRY=sAA1rIY", "WORRY=wER1IY", "TRY=trAY1", "ENTER=EH1ntER",
    "HELP=hEH1lp", "TALK=tAO1k", "TELL=tEH1l", "MACHINE=mAXSHIY1n", "SCREEN=skrIY1n",
    "KEYBOARD=kIY1bAOrd", "ABOUT=AXbAW1t", "BUSY=bIH1zIY", "BUSINESS=bIH1znAXs",
    "MONEY=mAH1nIY", "HONEY=hAH1nIY", "LIFE=lAY1f", "WIFE=wAY1f", "HUSBAND=hAH1zbAXnd",
    "HAPPY=hAE1pIY", "SAD=sAE1d", "ANGRY=AE1NGgrIY", "DREAM=drIY1m", "DREAMS=drIY1mz",
    "GOODBYE=gUHdbAY1", "BYE=bAY1", "YES=yEH1s", "NO=nOW1", "OK=OWkEY1", "OKAY=OWkEY1",
    "ARM=AA1rm", "DOS=dAA1s", "PC=pIYsIY1", "IBM=AYbIYEH1m", "CPU=sIYpIYyUW1",
    "PARITY=pAE1rIHtIY", "ERROR=EH1rER", "MEMORY=mEH1mERIY", "SYSTEM=sIH1stAXm",
    "PROGRAM=prOW1grAEm", "CONFIDENCE=kAA1nfIHdEHns", "STRICT=strIH1kt",
    "CONVERSATION=kAAnvERsEY1SHAXn", "WHATEVER=WHAHtEH1vER", "MIND=mAY1nd",
    "FREELY=frIY1lIY", "WIPED=wAY1pt", "LEAVE=lIY1v", "ASK=AE1sk", "BOTHER=bAA1DHER",
    "LABS=lAE1bz", "EUROPA=yUHrOW1pAX", "MICRO=mAY1krOW", "SYSTEMS=sIH1stAXmz",
    "ARTIFICIAL=AArtIHfIH1SHAXl", "INTELLIGENT=IHntEH1lIHjAXnt", "OPERATOR=AA1pERAEtER",
    "TEXT=tEH1kst", "SPEECH=spIY1CH", "PITCH=pIH1CH", "TONE=tOW1n", "VOLUME=vAA1lyUWm",
    "SPEED=spIY1d", "SETTINGS=sEH1tIHNGz", "QUIT=kwIH1t", "READ=rIY1d", "SAY=sEY1",
    "ELIZA=IHlAY1zAX", "SHOULDN'T=SHUH1dAXnt", "COULDN'T=kUH1dAXnt", "WOULDN'T=wUH1dAXnt",
    "AREN'T=AA1rAXnt", "WASN'T=wAA1zAXnt", "HAVEN'T=hAE1vAXnt", "LET'S=lEH1ts",
    "THAT'S=DHAE1ts", "WHAT'S=WHAH1ts", "THERE'S=DHEH1rz", "HE'S=hIY1z", "SHE'S=SHIY1z",
    "YOURSELF=yAOrsEH1lf", "MYSELF=mAYsEH1lf", "PERHAPS=pERhAE1ps", "REALLY=rIY1lIY",
    "WORK=wER1k", "WORLD=wER1ld", "WORD=wER1d", "WORDS=wER1dz", "FUTURE=fyUW1CHER",
    "CHILDREN=CHIH1ldrAXn", "CHILD=CHAY1ld", "SCHOOL=skUW1l", "ALWAYS=AO1lwEYz",
    "BELIEVE=bIHlIY1v", "OFTEN=AO1fAXn", "LISTEN=lIH1sAXn", "PRETTY=prIH1tIY",
    "EARTH=ER1TH", "HEART=hAA1rt", "MINUTE=mIH1nIHt", "ONCE=wAH1ns", "ONLY=OW1nlIY",
    "OTHER=AH1DHER", "OWN=OW1n", "TOGETHER=tUWgEH1DHER", "TODAY=tAXdEY1",
    "TOMORROW=tAXmAA1rOW", "YESTERDAY=yEH1stERdEY", "BEAUTIFUL=byUW1tIHfAXl",
    "HUNDRED=hAH1ndrAXd", "THOUSAND=THAW1zAXnd", "MILLION=mIH1lyAXn", "BILLION=bIH1lyAXn",
    "NINETY=nAY1ntIY", "NINETEEN=nAYntIY1n", "NINTH=nAY1nTH", "SEVEN=sEH1vAXn", "ELEVEN=IHlEH1vAXn",
    "TWELVE=twEH1lv", "EIGHTY=EY1tIY", "EIGHTEEN=EYtIY1n", "FORTY=fAO1rtIY", "DOLLAR=dAA1lER",
    "DOLLARS=dAA1lERz", "PERCENT=pERsEH1nt", "BROWN=brAW1n", "DOWN=dAW1n", "TOWN=tAW1n",
    "COW=kAW1", "ALLOW=AXlAW1", "POWER=pAW1ER", "FLOWER=flAW1ER", "CROWD=krAW1d",
    "INTERESTING=IH1ntrEHstIHNG", "INTERESTED=IH1ntrEHstIHd", "DEPRESSED=dIHprEH1st",
    "DEPRESSION=dIHprEH1SHAXn", "DIFFERENT=dIH1frAXnt", "REMEMBER=rIHmEH1mbER",
    "BEFORE=bIHfAO1r", "BEGIN=bIHgIH1n", "BECOME=bIHkAH1m", "DECIDE=dIHsAY1d",
    "ENOUGH=IHnAH1f", "THROUGH=THrUW1", "THOUGHT=THAO1t", "LAUGH=lAE1f", "MAYBE=mEY1bIY",
    "EXPLAIN=IHksplEY1n", "EXAMPLE=IHgzAE1mpAXl", "UNDERSTAND=AHndERstAE1nd",
    "IMPORTANT=IHmpAO1rtAXnt", "QUESTION=kwEH1sCHAXn", "QUESTIONS=kwEH1sCHAXnz",
    "ANSWER=AE1nsER", "SURE=SHUH1r", "SUGAR=SHUH1gER", "GUESS=gEH1s", "GUY=gAY1",
    "MISTER=mIH1stER", "MISSUS=mIH1sAXz", "MISS=mIH1s", "AGO=AXgOW1", "ETCETERA=EHtsEH1tERAX",
    0
};

static const char *dict_lookup(const char *word, int *func)
{
    int i;
    size_t n = strlen(word);
    for (i = 0; dict[i]; i++) {
        const char *d = dict[i];
        if (!strncmp(d, word, n) && (d[n] == '=' || d[n] == '!')) {
            *func = d[n] == '!';
            return d + n + 1;
        }
    }
    return 0;
}

static const char *const phnames[PH_COUNT] = {
    "_", "IY", "IH", "EY", "EH", "AE", "AA", "AO", "OW", "UH", "UW", "AH", "AX", "ER",
    "AY", "AW", "OY", "p", "b", "t", "d", "k", "g", "f", "v", "TH", "DH", "s", "z", "SH",
    "ZH", "h", "m", "n", "NG", "l", "r", "w", "y", "CH", "j", "WH"
};

const char *tts_phone_name(int ph) { return ph >= 0 && ph < PH_COUNT ? phnames[ph] : "?"; }

static int is_vowel_ph(int ph) { return ph >= PH_IY && ph <= PH_OY; }

/* parse the notation into phones; returns count */
static int parse_phones(const char *s, tts_phone *out, int max, int *has_stress)
{
    int n = 0, i;
    *has_stress = 0;
    while (*s && n < max) {
        int found = -1;
        if (*s >= '0' && *s <= '2') {
            if (n && is_vowel_ph(out[n - 1].ph)) { out[n - 1].stress = (unsigned char)(*s - '0'); *has_stress = 1; }
            s++; continue;
        }
        if (*s >= 'A' && *s <= 'Z' && s[1] >= 'A' && s[1] <= 'Z') {
            for (i = 1; i < PH_COUNT; i++)
                if (phnames[i][0] == s[0] && phnames[i][1] == s[1] && !phnames[i][2]) { found = i; break; }
            if (found > 0) { s += 2; }
        } else {
            for (i = 1; i < PH_COUNT; i++)
                if (phnames[i][0] == s[0] && !phnames[i][1]) { found = i; break; }
            s++;
            if (found < 0) found = 0;
        }
        if (found > 0) {
            out[n].ph = (unsigned char)found; out[n].stress = 0; out[n].flags = 0; out[n].word = 0;
            n++;
        } else if (found < 0 && *s) {
            /* unknown two-capital sequence: skip one char */
            s++;
        }
    }
    return n;
}

int tts_no_dict;

int tts_word_phones(const char *word, tts_phone *out, int max, int use_dict)
{
    char buf[160];
    const char *ph = 0;
    int func = 0, has_stress, n, i;
    if (use_dict && !tts_no_dict) ph = dict_lookup(word, &func);
    if (!ph) { tts_rules_word(word, buf, sizeof buf); ph = buf; }
    n = parse_phones(ph, out, max, &has_stress);
    /* doubled consonant letters are one sound ("RUNNING", "OFF") */
    for (i = 1; i < n; i++)
        if (out[i].ph == out[i - 1].ph && !is_vowel_ph(out[i].ph)) {
            memmove(out + i, out + i + 1, (n - i - 1) * sizeof *out);
            n--; i--;
        }
    if (func) {
        for (i = 0; i < n; i++) out[i].flags |= TF_FUNCWORD;
    } else if (!has_stress) {
        /* stress the first full vowel (a reduced AX only if there is nothing
         * else); in words of 3+ syllables with an unstressed-looking prefix
         * (the rules' IH/AX from BE- DE- RE-), take the next one */
        int first = -1, full = -1, nv = 0;
        for (i = 0; i < n; i++)
            if (is_vowel_ph(out[i].ph)) {
                nv++;
                if (first < 0) first = i;
                if (full < 0 && out[i].ph != PH_AX) full = i;
            }
        if (full < 0) full = first;
        if (full >= 0) out[full].stress = 1;
        (void)nv;
    }
    return n;
}

int tts_parse_notation(const char *s, tts_phone *out, int max)
{
    int hs;
    return parse_phones(s, out, max, &hs);
}
