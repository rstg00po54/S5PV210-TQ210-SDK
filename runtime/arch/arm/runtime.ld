ENTRY(_start)
MEMORY { DDR (rwx) : ORIGIN = 0x24000000, LENGTH = 16M }
SECTIONS
{
    .text : {
        KEEP(*(.text.entry))
        . = ALIGN(32);
        KEEP(*(.text.vectors))
        *(.text*) *(.rodata*)
        . = ALIGN(4);
        __text_end = .;
    } > DDR
    .data : { *(.data*) . = ALIGN(4); } > DDR
    __image_file_end = .;
    .bss (NOLOAD) : {
        . = ALIGN(16384);
        __bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > DDR
    .stacks (NOLOAD) : {
        . = ALIGN(8);
        . += 2048; __fiq_stack_top = .;
        . += 2048; __irq_stack_top = .;
        . += 2048; __abt_stack_top = .;
        . += 2048; __und_stack_top = .;
        . += 2048; __sys_stack_top = .;
        . += 8192; __svc_stack_top = .;
    } > DDR
    __runtime_end = .;
    ASSERT(_start == 0x24000000, "wrong entry")
    ASSERT((vectors & 31) == 0, "VBAR alignment")
    ASSERT(__text_end <= 0x24100000, "code exceeds executable section")
    ASSERT(__runtime_end <= 0x25000000, "runtime exceeds reserved DDR")
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
