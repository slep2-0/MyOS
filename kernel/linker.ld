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
    
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame)
    }
	
	/* Define symbol marking the end of the kernel binary */
	PROVIDE(kernel_end = .);
}