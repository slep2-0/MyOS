OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_start)

SECTIONS
{
    /* — Kernel placement at 0xfffff80000000000 — */
    . = 0xfffff80000000000;
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

    .aptrampoline : ALIGN(16) {
        /* keep the section and all input .aptrampoline pieces produced by objcopy */
        KEEP(*(.aptrampoline))
    }

    PROVIDE(bss_start = .);
    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    PROVIDE(bss_end = .);

    /* — PML4 (4 KiB) — */
    . = ALIGN(0x1000);
    PROVIDE(__pml4_start = .);
    .pml4 (NOLOAD) : ALIGN(0x1000) {
        . = . + 4096;
    }

    /* — Page directory and tables are now a single pool — */
    . = ALIGN(0x1000);
    PROVIDE(__pt_start = .);
    .page_tables (NOLOAD) : ALIGN(0x1000) {
        /* Reserve space for PDs and PTs. e.g., 128 KiB */
        . = . + (32 * 4096);
    }
    /* --- IMPORTANT: End the page table pool HERE --- */
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
