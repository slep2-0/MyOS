.section .mtheader,"a"
    .global mtheader
mtheader:
    .ascii  "MTE\0"             /* 4 bytes magic */
    .quad   __image_base        /* Preferred image base */
    .quad   __entry_rva         /* Entry RVA */
    .quad   __text_rva          /* Text RVA */
    .quad   __text_size         /* Text Size */
    .quad   __data_rva          /* Data RVA (ADDED) */
    .quad   __data_size         /* Data Size */
    .quad   __bss_size          /* Bss Size */

    /* pad to 64 bytes */
    .space  (64 - (.-mtheader))
