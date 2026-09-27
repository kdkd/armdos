/* refkeen_config.h - ReflectionHLE build configuration for ARM-DOS
 * (replaces the CMake-generated header): one game, one version,
 * Keen Dreams EGA v1.13 (shareware), little-endian ARM, no threads,
 * no launcher, no SDL. */
#ifndef REFKEEN_CONFIG_H
#define REFKEEN_CONFIG_H

#define REFKEEN_ARCH_LITTLE_ENDIAN 1
#define REFKEEN_HAS_VER_KDREAMS 1
#define REFKEEN_VER_KDREAMS 1
#define REFKEEN_PLATFORM_ARMDOS 1

#endif
