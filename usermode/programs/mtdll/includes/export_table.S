/* exports_table.S — Fixed RVA calculation using Image Base literal */
#define IMAGE_BASE 0x100000

/* * Helper Macro for GNU Assembler (GAS)
 * We subtract IMAGE_BASE to get the RVA (Relative Virtual Address).
 * The linker handles (SymbolAddr - Constant).
 */
.macro EXPORT sym, str
    .section .mtexports.str, "a"
    __name_\sym:
        .asciz "\str"
    
    .section .mtexports, "a"
    /* Name RVA */
    .quad (__name_\sym - IMAGE_BASE)
    /* Function RVA */
    .quad (\sym - IMAGE_BASE)
.endm

/* --- Exports List --- */

/* string.c */
EXPORT strchr, "strchr"
EXPORT strncat, "strncat"
EXPORT strncmp, "strncmp"
EXPORT strcmp, "strcmp"
EXPORT strncpy, "strncpy"
EXPORT strcpy, "strcpy"
EXPORT strlen, "strlen"

/* thread.c */
EXPORT TerminateThread, "TerminateThread"
EXPORT CreateThread, "CreateThread"
EXPORT CreateRemoteThread, "CreateRemoteThread"

/* process.c */
EXPORT OpenProcess, "OpenProcess"
EXPORT TerminateProcess, "TerminateProcess"

/* memory.c */
EXPORT VirtualAlloc, "VirtualAlloc"
EXPORT VirtualAllocEx, "VirtualAllocEx"
EXPORT VirtualQuery, "VirtualQuery"
EXPORT VirtualQueryEx, "VirtualQueryEx"
EXPORT VirtualProtect, "VirtualProtect"
EXPORT VirtualProtectEx, "VirtualProtectEx"
EXPORT VirtualFree, "VirtualFree"
EXPORT VirtualFreeEx, "VirtualFreeEx"

/* file.c */
EXPORT CreateFile, "CreateFile"
EXPORT WriteFile, "WriteFile"
EXPORT ReadFile, "ReadFile"

/* procldr.c */
EXPORT LdrInitializeProcess, "LdrInitializeProcess"

/* thrdldr.c */
EXPORT LdrInitializeThread, "LdrInitializeThread"

/* error.c */
EXPORT GetLastError, "GetLastError"
EXPORT SetLastError, "SetLastError"

/* apcdispatch.asm */
EXPORT MeUserApcDispatcher, "MeUserApcDispatcher"

/* synch.c */
EXPORT Sleep, "Sleep"
EXPORT WaitForSingleObject, "WaitForSingleObject"

/* print.c */
EXPORT printf, "printf"