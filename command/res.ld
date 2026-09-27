/*
 * res.ld - layout of COMMAND.COM's resident part: like sdk/link.ld, but the
 * resident .bss is stored in the image (as zeros) and the transient's AR1
 * image (.blob) comes last, so that after start-up the resident part can
 * shrink its memory block to __resident_end.
 */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

SECTIONS
{
    . = 0;
    .text : ALIGN(4)
    {
        KEEP(*(.text._start))
        *(.text .text.* .gnu.linkonce.t.*)
        *(.glue_7) *(.glue_7t) *(.vfp11_veneer) *(.v4_bx)
    }
    .rodata : ALIGN(4) { *(.rodata .rodata.* .gnu.linkonce.r.*) }
    .ARM.extab : ALIGN(4) { *(.ARM.extab* .gnu.linkonce.armextab.*) }
    .ARM.exidx : ALIGN(4) { *(.ARM.exidx* .gnu.linkonce.armexidx.*) }
    .data : ALIGN(4) { *(.data .data.* .gnu.linkonce.d.*) }
    .bss : ALIGN(16)
    {
        LONG(0)                 /* a data statement makes it PROGBITS: stored in the image */
        __bss_start__ = .;
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end__ = .;
        __resident_end = .;
    }
    .blob : ALIGN(16) { KEEP(*(.blob)) }
    /DISCARD/ : { *(.comment) *(.note .note.*) *(.gnu.attributes) *(.init_array*) *(.fini_array*) }
}
