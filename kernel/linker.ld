/* linker.ld - Fixed version */
ENTRY(_start)

SECTIONS
{
    . = 0x10000;
    
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
	
	/* Align to page before creating page directory */
	. = ALIGN(0x1000);
	PROVIDE(__pd_start = .);
	.page_directory (NOLOAD) : ALIGN(0x1000) {
		. = . + 4096;  /* Reserve 4 KiB for the page directory */
	}

	/* Reserve space for 32 page tables (4 KiB each = 128 KiB total) */
	. = ALIGN(0x1000);
	PROVIDE(__pt_start = .);
	.page_tables (NOLOAD) : ALIGN(0x1000) {
		. = . + (32 * 4096); /* 32 PTs for 128 MiB */
	}
	PROVIDE(__pt_end = .);

	/* Mark end of page-tables region */
	. = ALIGN(0x1000);
	__pt_end = .;
	
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame)
    }
	
	/* Define symbol marking the end of the kernel binary */
	PROVIDE(kernel_end = .);
}