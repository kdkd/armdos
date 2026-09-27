/* ARM/AT BIOS ROM: 1 MB at 0xFFF00000, ARM high vectors at 0xFFFF0000,
   data/bss/stacks in the first 64 KB of extended memory (the "HMA") */
ENTRY(reset_entry)
MEMORY
{
    ROM (rx)  : ORIGIN = 0xFFF00000, LENGTH = 0xF0000
    VEC (rx)  : ORIGIN = 0xFFFF0000, LENGTH = 0x10000
    HMA (rwx) : ORIGIN = 0x00100000, LENGTH = 0x10000
}
SECTIONS
{
    .text : { *(.text.boot) *(.text*) *(.rodata*) . = ALIGN(4); } > ROM
    .ARM.exidx : { *(.ARM.exidx*) } > ROM
    __data_load = LOADADDR(.data);
    .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > HMA AT > ROM
    .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > HMA
    __abt_stack_top = 0x00105000;
    __und_stack_top = 0x00104800;
    __irq_stack_top = 0x00106000;
    __svc_stack_top = 0x00110000;
    .vectors : { KEEP(*(.vectors)) } > VEC
    /DISCARD/ : { *(.comment) *(.note*) *(.ARM.attributes) }
}
ASSERT(__bss_end <= 0x00104000, "BIOS data overflows into its stacks")
