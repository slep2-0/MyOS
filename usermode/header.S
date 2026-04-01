.section .mtheader,"a"
    .global mtheader
mtheader:
    .ascii  "MTE\0"             /* 4 bytes magic */
    .quad   __image_base        /* Preferred image base */
    .quad   __entry_rva         /* Entry RVA */
    .quad   __text_rva          /* Text RVA */
    .quad   __text_size         /* Text Size */
    .quad   __data_rva          /* Data RVA */
    .quad   __data_size         /* Data Size */
    .quad   __bss_size          /* Bss Size */

    /* Exports */
    .quad   __mtexports_rva
    .quad   __mtexports_table_size  /* Total byte size */

    /* Relocations */
    .quad   __reloc_rva
    .quad   __reloc_size

    /* Imports */
    .quad   __mtimports_rva
    .quad   __mtimports_size

    /* pad to 80 bytes */
    .space  (128 - (.-mtheader))
