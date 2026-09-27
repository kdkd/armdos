/* tts.h - DRARM's text-to-speech engine: text normalisation, NRL letter-to-sound
 * rules, a small prosody model and a Klatt-style formant synthesiser producing
 * 8-bit unsigned PCM at 11025 Hz. Portable C (builds on the host for tests).
 *
 *   tts_settings(pitch, speed, tone, volume);     0..9 each, 5 = default
 *   tts_begin("Hello there.");                     parse + plan the utterance
 *   while ((n = tts_generate(buf, sizeof buf)) > 0) play(buf, n);
 *
 * Word marks (tts_word_*) tell a caller which sample each word starts at, so
 * text can be revealed as it is spoken.
 */
#ifndef DRARM_TTS_H
#define DRARM_TTS_H

#define TTS_RATE 11025

/* phoneme codes */
enum {
    PH_PAUSE = 0,
    PH_IY, PH_IH, PH_EY, PH_EH, PH_AE, PH_AA, PH_AO, PH_OW, PH_UH, PH_UW,
    PH_AH, PH_AX, PH_ER, PH_AY, PH_AW, PH_OY,
    PH_P, PH_B, PH_T, PH_D, PH_K, PH_G, PH_F, PH_V, PH_TH, PH_DH,
    PH_S, PH_Z, PH_SH, PH_ZH, PH_HH, PH_M, PH_N, PH_NG, PH_L, PH_R,
    PH_W, PH_Y, PH_CH, PH_JH, PH_WH,
    PH_COUNT
};

/* One phoneme of the planned utterance. */
typedef struct {
    unsigned char ph;
    unsigned char stress;     /* 0 none, 1 primary, 2 secondary */
    unsigned char flags;      /* TF_* below */
    unsigned char word;       /* word index (for marks) */
} tts_phone;

#define TF_WORDSTART  0x01
#define TF_WORDEND    0x02
#define TF_PHRASEEND  0x04    /* last phone before , . ? ! ; : */
#define TF_QUESTION   0x08    /* phrase ends in '?' */
#define TF_FUNCWORD   0x10    /* unstressed function word */
#define TF_FINAL      0x20    /* sentence end (. ? !) */

/* ---- settings ---- */
void tts_settings(int pitch, int speed, int tone, int volume);
void tts_get_settings(int *pitch, int *speed, int *tone, int *volume);

/* ---- text -> phonemes ---- */
/* Normalise text (numbers, abbreviations, symbols) into upper-case words and
 * punctuation. Returns the length written to out. */
int  tts_normalise(const char *in, char *out, int outsz);
/* Translate one upper-case word with the NRL rules (no dictionary) into the
 * rule notation ("hEHlOW"). */
int  tts_rules_word(const char *word, char *out, int outsz);
/* Word (dictionary first, then rules) -> phoneme codes with stress. Returns count. */
int  tts_word_phones(const char *word, tts_phone *out, int max, int use_dict);
/* The phoneme string of a whole text in readable form ("HH EH1 L OW ..."). */
int  tts_text_phonemes(const char *text, char *out, int outsz);
const char *tts_phone_name(int ph);

/* Spell mode: each letter is spoken by name (used for the name prompt). */
extern int tts_spell_mode;
/* Words (space separated) always read by the rules alone, never the
 * dictionary or the acronym speller - the doctor's naive reading of a name. */
void tts_rules_words(const char *words);
/* No dictionary for words (the doctor's naive reading of a name). */
extern int tts_no_dict;
/* Monotone: every vowel at the base pitch - no declination, stress or phrase contours (a
 * flat, machine voice: TERM's "wopr" voice for WarGames' WOPR). */
extern int tts_monotone;

/* ---- synthesis ---- */
int  tts_begin(const char *text);          /* returns the number of samples planned */
int  tts_generate(unsigned char *buf, int max);   /* returns samples written; 0 = done */
long tts_total_samples(void);
/* word marks: for word i, its first sample and the byte offset in the text
 * passed to tts_begin */
int  tts_word_count(void);
long tts_word_sample(int i);
int  tts_word_offset(int i);

#endif
