#pragma once

// pstypes.h - user mode and kernel compatible structs and general types.
#include "core.h"

typedef enum _PROCESS_FLAGS {
    ProcessBreakOnTermination = (1 << 0),
    ProcessBeingTerminated = (1 << 1),
    ProcessBeingDeleted = (1 << 2),
} PROCESS_FLAGS;

typedef struct _MT_MODULE_INFO {
    char FullPath[256];
    uint64_t Size;
    void* Base;
} MT_MODULE_INFO;

typedef struct _MTDLL_BASIC_TYPES {
    MT_MODULE_INFO PrimaryExecutable;
    MT_MODULE_INFO Mtdll;
    uint64_t EpochCreation;
} MTDLL_BASIC_TYPES, * PMTDLL_BASIC_TYPES;

typedef struct _LDR_DATA_TABLE_ENTRY {
    void* EntryPoint; // Entry point of module.
    void* Base; // Base address of module. (start address, not entrypoint, like offset 0 of a file)
    uint64_t SizeOfImage; // Size of the loaded module in bytes.
    char FullName[256]; // Path of loaded module (including file and extension).
    uint64_t LoadTime; // Epoch timestamp of time module loaded.

    // The list entry itself.
    DOUBLY_LINKED_LIST LoadedModuleList; // Doubly linked list of LDR_DATA_TABLE_ENTRY
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

typedef struct _PEB_LDR_DATA {
    DOUBLY_LINKED_LIST LoadedModuleList; // Doubly linked list of LDR_DATA_TABLE_ENTRY
} PEB_LDR_DATA, * PPEB_LDR_DATA;

typedef struct _PEB {
    uint8_t  BeingDebugged;          // Flag set if process is being debugged
    void* ImageBase; // Pointer of executable entry point in memory.
    PEB_LDR_DATA LoaderData;
} PEB, * PPEB;

typedef struct _MT_TIB {
    void* ExceptionList; // SEH Chain.
    void* StackBase; // The base of this thread's stack.
    void* StackLimit; // The maximum address of the stack (any pushes beyond here are guard pages)
} MT_TIB, * PMT_TIB;

typedef struct _TEB {
    MT_TIB MtTib; // GS:[0] should point here.
    uint64_t UniqueProcessId; // Current ID of this thread's process.
    uint64_t UniqueThreadId; // Current ID of this thread.
    PPEB ProcessEnvironmentBlock; // Pointer to this thread's process's PEB.
    int32_t LastErrorValue; // The last error that the thread's has done in an operation (failed function, illegal instruction)
    int32_t LastStatusValue; // Internal MTSTATUS Values.
} TEB, * PTEB;

typedef void* THREAD_PARAMETER;
typedef void (*ThreadEntry)(THREAD_PARAMETER);