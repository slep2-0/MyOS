/*
 * PROJECT:      MatanelOS
 * LICENSE:      GPLv3
 * PURPOSE:      Fundamental ABI types shared by public and native APIs.
 */

#ifndef MATANELOS_SHARED_MTTYPES_H
#define MATANELOS_SHARED_MTTYPES_H

#include <stddef.h>
#include <stdint.h>
#include "mte.h"

#define MT_INVALID_TLS_INDEX UINT64_MAX

// User defined types that are stable and should not change between versions wildely, only extended.
// Meaning the structs offsets may stay the same (layout), but only extended from the bottom if it is really needed.
typedef int32_t HANDLE, *PHANDLE;
typedef uint32_t ACCESS_MASK;

/*
 * Intrusive list layout shared by the kernel and the user-mode loader ABI.
 * List manipulation remains private to each side; only the node layout is
 * shared here.
 */
typedef struct _DOUBLY_LINKED_LIST {
    struct _DOUBLY_LINKED_LIST* Blink;
    struct _DOUBLY_LINKED_LIST* Flink;
} DOUBLY_LINKED_LIST, *PDOUBLY_LINKED_LIST;

/*
 * Process and thread environment layouts.
 *
 * These structures are allocated and initialized by the kernel, then read and
 * maintained by MTDLL. Their existing fields must not be reordered. New fields
 * may only be appended after both sides have been updated together.
 */
typedef struct _MT_MODULE_INFO {
    char FullPath[256];
    uint64_t Size;
    void* Base;
} MT_MODULE_INFO, *PMT_MODULE_INFO;

typedef struct _MTDLL_BASIC_TYPES {
    MT_MODULE_INFO PrimaryExecutable;
    MT_MODULE_INFO Mtdll;
    uint64_t EpochCreation;
} MTDLL_BASIC_TYPES, *PMTDLL_BASIC_TYPES;

typedef enum _LDR_MODULE_STATE {
    LdrModuleLoading,
    LdrModuleLoaded,
    LdrModuleUnloading
} LDR_MODULE_STATE;

typedef struct _PROCESS_INFORMATION {
    HANDLE ProcessHandle;
    HANDLE ThreadHandle;
    uint32_t ProcessId;
    uint32_t ThreadId;
} PROCESS_INFORMATION, * PPROCESS_INFORMATION;

typedef struct _LDR_DATA_TABLE_ENTRY {
    void* EntryPoint;
    void* Base;
    bool Pinned; // FreeLibrary cannot be executed on this module if true.
    DOUBLY_LINKED_LIST DependencyListHead; // Lists of dependencies this module has reference counts to.
    uint64_t SizeOfImage;
    LDR_MODULE_STATE State;
    uint32_t ReferenceCount;
    char FullName[256];
    uint64_t LoadTime;
    DOUBLY_LINKED_LIST LoadedModuleList;
    uint64_t TlsIndex;               // Process-local index used by __tls_get_addr.
    MTE_TLS_DIRECTORY TlsDirectory;  // Validated copy of this module's TLS metadata.
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

typedef struct _LDR_DEPENDENCY_ENTRY {
    DOUBLY_LINKED_LIST ListEntry;
    PLDR_DATA_TABLE_ENTRY Module;
} LDR_DEPENDENCY_ENTRY, * PLDR_DEPENDENCY_ENTRY;

typedef struct _PEB_LDR_DATA {
    HANDLE LoaderLock; // Mutex lock for creating DLLs
    DOUBLY_LINKED_LIST LoadedModuleList;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

typedef struct _MT_PROCESS_PARAMETERS {
    uint32_t Size;
    uint32_t Flags;

    char* ImagePath;
    uint64_t ImagePathLength;

    char* CommandLine;
    uint64_t CommandLineLength;

    char* CurrentDirectory;
    uint64_t CurrentDirectoryLength;

    char* Environment;
    uint64_t EnvironmentSize;

    int32_t ArgumentCount;
    char** ArgumentVector;
} MT_PROCESS_PARAMETERS, *PMT_PROCESS_PARAMETERS;

typedef struct _PEB {
    uint8_t BeingDebugged;
    void* ImageBase;
    PEB_LDR_DATA LoaderData;
    void* ProcessHeap;
    uint64_t NextTlsIndex;
    PMT_PROCESS_PARAMETERS ProcessParameters;
} PEB, *PPEB;

struct _EXCEPTION_REGISTRATION_RECORD;

typedef struct _MT_TIB {
    struct _EXCEPTION_REGISTRATION_RECORD* ExceptionList;
    void* StackBase;
    void* StackLimit;
} MT_TIB, *PMT_TIB;

typedef struct _TEB {
    MT_TIB MtTib;
    uint64_t UniqueProcessId;
    uint64_t UniqueThreadId;
    PPEB ProcessEnvironmentBlock;
    int32_t LastErrorValue;
    int32_t LastStatusValue;
    void* StaticTlsAllocation; // Original allocation containing executable static TLS.
    void* ThreadPointer;       // FS base used by the current thread.
    void** TlsSlots;           // Module-indexed array of allocated TLS block addresses.
    uint64_t TlsSlotCount;     // Number of valid entries in TlsSlots.
} TEB, *PTEB;

#define MtCurrentProcess() ((HANDLE)-1)
#define MtCurrentThread()  ((HANDLE)-2)

typedef enum _USER_PROTECTION_TYPE {
    PAGE_EXECUTE_READ = 0x10,
    PAGE_EXECUTE_READWRITE = 0x20,
    PAGE_READWRITE = 0x30,
    PAGE_READONLY = 0x40,
    PAGE_NOACCESS = 0x50
} USER_PROTECTION_TYPE;

typedef enum _FREE_TYPE {
    MEM_RELEASE = 0,
    MEM_DECOMMIT = 1
} FREE_TYPE;

typedef struct _MEMORY_BASIC_INFORMATION {
    void* BaseAddress;
    size_t RegionSize;
    USER_PROTECTION_TYPE Protection;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

typedef uint32_t (*THREAD_START_ROUTINE)(void* Argument);

#endif /* MATANELOS_SHARED_MTTYPES_H */
