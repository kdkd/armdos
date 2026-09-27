/*
 * link.ld - ARM-DOS program layout (ARCH.md section 8).
 *
 * Everything is linked at address 0 and relocated by the DOS loader, which
 * adds the load base (PSP+0x100) to every word listed in the EXE's relocation
 * table (made by elf2exe.mjs from the R_ARM_ABS32 relocations kept by -Wl,-q).
 *
 *   0             .text   (_start first)
 *                 .rodata, .ARM.extab, .ARM.exidx, init/fini arrays
 *                 .data
 *   __image_end   ------- end of the bytes stored in the file
 *   __bss_start__ .bss (+COMMON)
 *   __bss_end__ = end = _end
 *                 [stack: stack_size bytes, reserved by the loader]
 *                 [heap:  from the stack top to the end of the DOS block]
 */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(dos_entry)

SECTIONS
{
    . = 0;

    /* the list of lists comes first, so that its "segment" (LoL >> 4) is where
       ARMDOS.SYS starts: MEM splits IO.SYS / ARMDOS.SYS there, as on DOS 4 */
    .lol : ALIGN(16)
    {
        KEEP(*(.lol))
        . = ALIGN(16);
    }

    .text : ALIGN(4)
    {
        KEEP(*(.devhdr))            /* a DEVICE= driver's header comes first (ARCH.md 14) */
        KEEP(*(.text.dos_entry))
        *(.text.unlikely .text.unlikely.*)
        *(.text.startup .text.startup.*)
        *(.text .text.* .gnu.linkonce.t.*)
        *(.glue_7) *(.glue_7t) *(.vfp11_veneer) *(.v4_bx)
        KEEP(*(.init))
        KEEP(*(.fini))
    }

    .rodata : ALIGN(4)
    {
        *(.rodata .rodata.* .gnu.linkonce.r.*)
    }

    .ARM.extab : ALIGN(4)
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    }

    .ARM.exidx : ALIGN(4)
    {
        PROVIDE_HIDDEN(__exidx_start = .);
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        PROVIDE_HIDDEN(__exidx_end = .);
    }

    .preinit_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    }
    .init_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    }
    .fini_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    }

    .data : ALIGN(4)
    {
        __data_start__ = .;
        *(.data .data.* .gnu.linkonce.d.*)
        . = ALIGN(8);
        __data_end__ = .;
        __image_end = .;
    }

    .bss (NOLOAD) : ALIGN(8)
    {
        __bss_start__ = .;
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end__ = .;
        /* defined inside the section so they are section-relative (and
         * relocated), not absolute */
        end = .;
        _end = .;
        __end__ = .;
    }

    /* Things a loaded DOS program never needs. */
    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
        *(.gnu.attributes)
    }
    /* No .got/.plt/.dynamic: programs are static, non-PIC. If one appears,
     * elf2exe.mjs rejects the GOT relocations that come with it. */
}
