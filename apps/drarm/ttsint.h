/* ttsint.h - internals shared by the speech engine's files */
#ifndef DRARM_TTSINT_H
#define DRARM_TTSINT_H

enum { TK_WORD, TK_PHON, TK_PUNCT };

typedef struct {
    unsigned char kind;
    unsigned char nodict;   /* read by the letter-to-sound rules only */
    char text[30];
    short src;           /* byte offset in the source text */
} tts_token;

int tts_tokenise(const char *s, tts_token *out, int max);

#endif
