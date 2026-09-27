ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.start) *(.text*) *(.rodata*) *(.glue_7*) *(.vfp11_veneer) *(.v4_bx) }
  .data : { *(.data*) }
  . = ALIGN(4);
  __bss_start = .;
  .bss : { *(.bss*) *(COMMON) }
  . = ALIGN(4);
  __bss_end = .;
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
