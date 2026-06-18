ENTRY(_start)

SECTIONS
{
    . = 1M;
    .multiboot : ALIGN(4K) { *(.multiboot) }
    .text      : ALIGN(4K) { *(.text) }
    .data      : ALIGN(4K) { *(.data) }
    .bss       : ALIGN(4K) { *(.bss) }
}
