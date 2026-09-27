ENTRY(_vectors)
MEMORY { ROM (rx) : ORIGIN = 0xFFF00000, LENGTH = 1M
         RAM (rw) : ORIGIN = 0x00080000, LENGTH = 64K }
SECTIONS {
  .header 0xFFF00000 : { *(.header) } > ROM
  .text 0xFFF00100 : { *(.text*) *(.rodata*) *(.glue_7*) . = ALIGN(4); } > ROM
  .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > RAM AT > ROM
  __data_load = LOADADDR(.data);
  .vectors 0xFFFF0000 : { *(.vectors) } > ROM
  .pad 0xFFFFFFFC : { LONG(0xFFFFFFFF) } > ROM
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > RAM
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
