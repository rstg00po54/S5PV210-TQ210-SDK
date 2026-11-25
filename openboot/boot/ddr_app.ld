ENTRY(_start)
MEMORY { DDR (rwx) : ORIGIN = 0x24000000, LENGTH = 0x01000000 }
SECTIONS {
    .text : { KEEP(*(.text.entry)) *(.text*) *(.rodata*) . = ALIGN(4); } > DDR
    .data : { *(.data*) . = ALIGN(4); } > DDR
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } > DDR
    ASSERT(_start == 0x24000000, "incorrect DDR entry")
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
