# Web fonts

The page's type faces, as WOFF2 files, served from the site's `fonts/` directory:

| file | font | licence |
|---|---|---|
| `archivo-var.woff2` | Archivo (variable weight), The Archivo Project Authors | SIL Open Font License 1.1, `OFL-archivo.txt` |
| `newsreader-400.woff2`, `newsreader-400i.woff2` | Newsreader, The Newsreader Project Authors | SIL Open Font License 1.1, `OFL-newsreader.txt` |
| `ibmplexmono-400.woff2`, `ibmplexmono-600.woff2` | IBM Plex Mono, IBM Corp. | SIL Open Font License 1.1, `OFL-ibmplexmono.txt` |
| `charis-400.woff2`, `charis-400i.woff2`, `charis-700.woff2`, `charis-700i.woff2` | Charis SIL (Latin subset, from the `@fontsource/charis-sil` package), SIL International | SIL Open Font License 1.1, `OFL-charis.txt` |

The licence texts are copied from the fonts' entries in https://github.com/google/fonts (Charis SIL's from its package).

One more face is not in this directory: `pcvga.ttf`, the machine's own IBM VGA 8x16 font as a
web font (family `PC VGA`), which `web/tools/build-site.mjs` makes at build time from
`emu/fonts/vga8x16.bin` with `web/tools/pcfont.mjs` and writes next to these. Its glyphs are
VileR's (The Ultimate Oldschool PC Font Pack, CC BY-SA 4.0, see `emu/fonts/README.md`).
