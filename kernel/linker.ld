ENTRY(_start)

SECTIONS
{
    /* — Kernel placement at 0x10000 — */
    . = 0x10000;
    PROVIDE(kernel_start = .);

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    /* — Page directory (4 KiB) — */
    . = ALIGN(0x1000);
    PROVIDE(__pd_start = .);
    .page_directory (NOLOAD) : ALIGN(0x1000) {
        . = . + 4096;
    }

    /* — Page tables (32 × 4 KiB = 128 KiB) — */
    . = ALIGN(0x1000);
    PROVIDE(__pt_start = .);
    .page_tables (NOLOAD) : ALIGN(0x1000) {
        . = . + (32 * 4096);
    }

    /* — Mark end of pagetables — */
    . = ALIGN(0x1000);
    PROVIDE(__pt_end = .);

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame)
    }

    /* — End of kernel image — */
    PROVIDE(kernel_end = .);
    PROVIDE(kernel_length = kernel_end - kernel_start);
}
