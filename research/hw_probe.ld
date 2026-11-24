ENTRY(_start)
SECTIONS {
    . = 0x23e00000;
    .text : { KEEP(*(.text.start)) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    . = ALIGN(64);
    .bss (NOLOAD) : {
        __bss_start = .; *(.bss*) *(COMMON); . = ALIGN(4); __bss_end = .;
    }
    . = ALIGN(8);
    .stack (NOLOAD) : { . += 4096; __stack_top = .; }
    ASSERT(__stack_top <= 0x23e10000, "probe exceeds reserved 64 KiB")
    /DISCARD/ : { *(.comment) *(.note*) *(.ARM.exidx*) *(.ARM.extab*) }
}
