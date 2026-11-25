ENTRY(_start)
MEMORY { IRAM (rwx) : ORIGIN = 0xD0020010, LENGTH = 0x3ff0 }
SECTIONS {
  .entry : { KEEP(*(.text.entry)) } > IRAM
  .vectors ALIGN(32) : { KEEP(*(.vectors)) } > IRAM
  .text : { *(.text.start) *(.text.ddr_stub) *(.text*) } > IRAM
  .rodata : { *(.rodata*) } > IRAM
  .data : { *(.data*) } > IRAM
  . = ALIGN(4);
  .bss (NOLOAD) : { . = ALIGN(4); __bss_start = .; *(.bss*) *(COMMON); . = ALIGN(4); __bss_end = .; } > IRAM
  .stack (NOLOAD) : { . = ALIGN(8); . += 2048; __stack_top = .; } > IRAM
  ASSERT(_start == 0xD0020010, "incorrect USB entry")
  ASSERT((vectors & 31) == 0, "unaligned VBAR")
  ASSERT(__stack_top <= 0xD0024000, "SRAM image/stack too large")
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
