/* imports_table.S */
/* IMAGE_BASE is defined in the compiler flags for mtexe makefiles. */

.macro MT_IMPORT lib, func
    /* --- 1. The Strings --- */
    .section .mtimports.str, "a"
    __str_mod_\func:
        .asciz "\lib"
    __str_func_\func:
        .asciz "\func"

    /* --- 2. The Function Pointer Variable --- */
    .section .data
    .global \func
    .align 8
    \func:
        .quad 0  /* Loader will overwrite this with the real absolute address */

    /* --- 3. The Import Table Entry --- */
    .section .mtimports, "a"
    .align 8
    __import_entry_\func:
        /* Preprocessor will replace IMAGE_BASE with the value from the Makefile */
        .quad (__str_mod_\func - IMAGE_BASE)   /* module_name RVA */
        .quad (__str_func_\func - IMAGE_BASE)  /* function_name RVA */
        .quad (\func - IMAGE_BASE)             /* func_ptr_addr RVA */
.endm

/* ========================== */
/* IMPORTS LIST               */
/* ========================== */

/* TODO Make it only when the process uses an import, it requests its library, because this is useless right now */
/* But for that we need compiler/linker support.  */

/* String Ops */
MT_IMPORT "mtdll.mtdll", strchr
MT_IMPORT "mtdll.mtdll", strncat
MT_IMPORT "mtdll.mtdll", strncmp
MT_IMPORT "mtdll.mtdll", strcmp
MT_IMPORT "mtdll.mtdll", strncpy
MT_IMPORT "mtdll.mtdll", strcpy
MT_IMPORT "mtdll.mtdll", strlen

/* Threading */
MT_IMPORT "mtdll.mtdll", TerminateThread
MT_IMPORT "mtdll.mtdll", CreateThread
MT_IMPORT "mtdll.mtdll", CreateRemoteThread

/* Processes */
MT_IMPORT "mtdll.mtdll", OpenProcess
MT_IMPORT "mtdll.mtdll", TerminateProcess

/* Memory */
MT_IMPORT "mtdll.mtdll", VirtualAlloc
MT_IMPORT "mtdll.mtdll", VirtualAllocEx
MT_IMPORT "mtdll.mtdll", VirtualQuery
MT_IMPORT "mtdll.mtdll", VirtualQueryEx
MT_IMPORT "mtdll.mtdll", VirtualProtect
MT_IMPORT "mtdll.mtdll", VirtualProtectEx
MT_IMPORT "mtdll.mtdll", VirtualFree
MT_IMPORT "mtdll.mtdll", VirtualFreeEx

/* File I/O */
MT_IMPORT "mtdll.mtdll", CreateFile
MT_IMPORT "mtdll.mtdll", WriteFile
MT_IMPORT "mtdll.mtdll", ReadFile
MT_IMPORT "mtdll.mtdll", printf

/* Error Handling */
MT_IMPORT "mtdll.mtdll", GetLastError
MT_IMPORT "mtdll.mtdll", SetLastError

/* Synchronization */
MT_IMPORT "mtdll.mtdll", Sleep
MT_IMPORT "mtdll.mtdll", WaitForSingleObject