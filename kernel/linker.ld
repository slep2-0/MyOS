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
	
	/* * The Exception Table Section 
     * We align it to 8 bytes because it contains 64-bit pointers.
     */
    . = ALIGN(8);
    __start_ex_table = .;
    __ex_table : {
        *(__ex_table)
    }
    __stop_ex_table = .;

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

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame)
    }

    /* — End of kernel image — */
    PROVIDE(kernel_end = .);
    PROVIDE(kernel_length = kernel_end - kernel_start);
}
