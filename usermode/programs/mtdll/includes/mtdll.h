#pragma once
// mtdll.h


// never thought id get here.
// This header is reserved for internal syscall use, for external use, use the MatanelOS.h

// First we must define basic types.
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Then kernel types
#include "mtstatus.h"
#include "accessrights.h"
#include "pstypes.h"
#include "core.h"

// Basic definitions.
typedef int32_t HANDLE, * PHANDLE;
typedef uint32_t ACCESS_MASK;
#define IN // Takes REQUIRED INPUT
#define OUT // Supplies REQUIRED OUTPUT
#define _In_Opt // Takes OPTIONAL INPUT if given.
#define _Out_Opt // OPTIONALLY Supplies OUTPUT if given.
#define MtCurrentProcess() -1 // Special handle signifying current process.
#define MtCurrentThread() -2 // Special handle signifying current thread.

/// This example is using the legacy kernel structures.
/// Usage: CONTAINING_RECORD(ptr, struct, ptr_member)
/// Example: 
/// CTX_FRAME* ctxframeptr = 0x1234; // Hypothetical address of the pointer.
/// Thread* threadAssociated = CONTAINING_RECORD(ctxframeptr, Thread, ctx); // Note that ctx is the member name for CTX_FRAME in the Thread struct.
#ifndef CONTAINING_RECORD
#define CONTAINING_RECORD(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

static
inline
void
InitializeListHead(
    PDOUBLY_LINKED_LIST Head
)

{
    Head->Flink = Head;
    Head->Blink = Head;
}

// ->>>> CRASHES IN THESE FUNCTIONS USUALLY BECAUSE INITIALIZELISTHEAD WASNT USED ON THE DOUBLY LINKED LIST !!!!!!!

static
inline
void
InsertTailList(
    PDOUBLY_LINKED_LIST Head,
    PDOUBLY_LINKED_LIST Entry
)

{
    PDOUBLY_LINKED_LIST Blink;
    // The last element is the one before Head (circular list style)
    Blink = Head->Blink;
    Entry->Flink = Head;  // New entry points forward to Head
    Entry->Blink = Blink; // New entry points back to old last node
    Blink->Flink = Entry; // Old last node points forward to new entry
    Head->Blink = Entry;  // Head points back to new entry
}

static
inline
void
InsertHeadList(
    PDOUBLY_LINKED_LIST Head,
    PDOUBLY_LINKED_LIST Entry
)
{
    PDOUBLY_LINKED_LIST First;

    // The first element is the one after Head (circular list)
    First = Head->Flink;

    Entry->Flink = First; // Entry -> next = old first
    Entry->Blink = Head;  // Entry -> prev = head

    First->Blink = Entry; // old first -> prev = entry
    Head->Flink = Entry;  // head -> next = entry
}

static
inline
PDOUBLY_LINKED_LIST
RemoveHeadList(
    PDOUBLY_LINKED_LIST Head
)

{
    PDOUBLY_LINKED_LIST Entry;
    PDOUBLY_LINKED_LIST Flink;

    Entry = Head->Flink;
    if (Entry == Head) {
        // List is empty
        return NULL;
    }

    Flink = Entry->Flink;
    Head->Flink = Flink;
    Flink->Blink = Head;

    // Clear links
    Entry->Flink = Entry->Blink = NULL;
    return Entry;
}

static
inline
void
RemoveEntryList(
    PDOUBLY_LINKED_LIST Entry
)
{
    PDOUBLY_LINKED_LIST Flink;
    PDOUBLY_LINKED_LIST Blink;

    Flink = Entry->Flink;
    Blink = Entry->Blink;

    /* Normal (minimal) unlink — identical to Windows' RemoveEntryList */
    Blink->Flink = Flink;
    Flink->Blink = Blink;

    // Sanitize the removed entry so it doesn't look valid
    Entry->Flink = Entry;
    Entry->Blink = Entry;
}


// should change to protection type, and add allocation type like MEM_TOP_DOWN
typedef enum _USER_ALLOCATION_TYPE {
    PAGE_EXECUTE_READ = 0x10, // PRESENT
    PAGE_EXECUTE_READWRITE = 0x20, // PRESENT | RW
    PAGE_READWRITE = 0x30, // PRESENT | RW | NX
    PAGE_READONLY = 0x40 // PRESENT | NX
} USER_ALLOCATION_TYPE;

// System calls. (TODO mtdll.mtdll, funny name)
MTSTATUS
MtAllocateVirtualMemory(
    IN HANDLE Process,
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t NumberOfBytes,
    IN uint8_t AllocationType
);

MTSTATUS
MtOpenProcess(
    IN uint32_t ProcessId,
    OUT PHANDLE ProcessHandle,
    IN ACCESS_MASK DesiredAccess
);

MTSTATUS
MtTerminateProcess(
    IN HANDLE ProcessHandle,
    IN MTSTATUS ExitStatus
);

MTSTATUS
MtReadFile(
    IN HANDLE FileHandle,
    IN uint64_t FileOffset,
    OUT void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesRead
);

MTSTATUS
MtWriteFile(
    IN HANDLE FileHandle,
    IN uint64_t FileOffset,
    IN void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesWritten
);

MTSTATUS
MtCreateFile(
    IN const char* path,
    IN ACCESS_MASK DesiredAccess,
    OUT PHANDLE FileHandleOut
);

MTSTATUS
MtClose(
    IN HANDLE hObject
);

MTSTATUS
MtTerminateThread(
    IN HANDLE ThreadHandle,
    IN MTSTATUS ExitStatus
);