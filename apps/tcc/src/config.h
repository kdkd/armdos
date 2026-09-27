/* config.h - TinyCC for ARM-DOS (replaces the one ./configure writes).
 *
 * Target: ARM-DOS (TCC_TARGET_ARMDOS: ARMv5TE, EABI, soft-float, MZ+AR1
 * .EXE output). The same sources build
 *   - TCC.EXE, which runs on ARM-DOS (host: TCC_HOST_ARMDOS, SDK's GCC), and
 *   - a Linux cross compiler used by the build and the tests.
 */
#define TCC_VERSION "0.9.28rc"
#define CONFIG_TCC_PREDEFS 1        /* tccdefs.h compiled in (tccdefs_.h) */
#define CONFIG_TCC_BCHECK 0         /* no bounds-checking runtime */
#define CONFIG_TCC_BACKTRACE 0      /* no backtrace runtime */
#define CONFIG_TCC_SEMLOCK 0        /* single-threaded */
#define CONFIG_TCC_STATIC 1         /* no dlopen() */

#ifndef TCC_TARGET_ARMDOS
# define TCC_TARGET_ARMDOS 1
#endif
#define TCC_TARGET_ARM 1
#define TCC_ARM_EABI 1

#ifdef TCC_HOST_ARMDOS
/* TCC.EXE: INCLUDE\ and LIB\ are next to the program (argv[0]) */
extern const char *tcc_armdos_dir(void);
# define CONFIG_TCCDIR tcc_armdos_dir()
#endif
