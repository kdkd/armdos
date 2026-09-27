/* IO.SYS: resident part linked at 0x700 (0070:0000); SYSINIT (init_*.o)
   linked to run at 0x90100 and copied there by start.S.  The resident .bss
   occupies the place where SYSINIT's code sits in the file. */
ENTRY(_start)
SECTIONS
{
    . = 0x700;
    .text : {
        KEEP(*(.text.start))
        EXCLUDE_FILE(*init_*.o) *(.text .text.*)
        EXCLUDE_FILE(*init_*.o) *(.rodata .rodata.*)
        . = ALIGN(4);
    }
    .data : {
        EXCLUDE_FILE(*init_*.o) *(.data .data.*)
        . = ALIGN(16);
    }
    __res_file_end = .;
    .bss (NOLOAD) : {
        __bss_start = .;
        EXCLUDE_FILE(*init_*.o) *(.bss .bss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
    __res_end = __bss_end;

    .init 0x90100 : AT(__res_file_end) {
        __init_start = .;
        *init_*.o(.text .text.* .rodata .rodata.* .data .data.*)
        . = ALIGN(4);
        __init_end = .;
    }
    __init_load = LOADADDR(.init);
    .initbss (NOLOAD) : {
        __initbss_start = .;
        *init_*.o(.bss .bss.* COMMON)
        . = ALIGN(4);
        __initbss_end = .;
    }

    /DISCARD/ : { *(.comment) *(.note*) *(.ARM.attributes) *(.ARM.exidx*) }
}
ASSERT(__initbss_end <= 0x9E000, "SYSINIT too big for its region")
ASSERT(__res_file_end + SIZEOF(.init) < 0x40000, "IO.SYS too big")
