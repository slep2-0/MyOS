/*++

Module Name:

    ht.h

Purpose:

    This module contains the header files & prototypes required for the Handle Table implementation of MatanelOS.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#ifndef X86_MATANEL_HT_H
#define X86_MATANEL_HT_H

#include "core.h"
#include "ms.h"

// --------------- STRUCTURES ---------------

typedef struct _HANDLE_TABLE_ENTRY {
    union {
        void* Object;       // [USED IF ALLOCATED] Pointer to the Kernel Object
        uint64_t Value;     // Generic value access
    };
    union {
        uint32_t GrantedAccess;       // [USED IF ALLOCATED] Access Mask (Read, Write, etc.)
        uint32_t NextFreeTableEntry;  // [USED IF FREE] Index of the next empty slot
    };
} HANDLE_TABLE_ENTRY, *PHANDLE_TABLE_ENTRY;

#define LOW_LEVEL_ENTRIES   (VirtualPageSize / sizeof(HANDLE_TABLE_ENTRY)) // 256 entries per page
#define TABLE_LEVEL_MASK    3

typedef struct _HANDLE_TABLE {
    // Linked list of next table (if any)
    DOUBLY_LINKED_LIST TableList;
    SPINLOCK TableLock;

    // Storage
    uint64_t TableCode;     // Pointer | Level
    PEPROCESS QuotaProcess;

    // Free List Logic
    PHANDLE_TABLE_ENTRY LastFreeHandleEntry;
    uint32_t FirstFreeHandle;    // Index of first free handle, or 0 if none.
    uint32_t NextHandleNeedingPool;
    uint32_t HandleCount;
} HANDLE_TABLE, *PHANDLE_TABLE;

// --------------- TYPE DEFINES ---------------

typedef int32_t HANDLE, * PHANDLE;

// --------------- FUNCTIONS ---------------

typedef uint32_t ACCESS_MASK;

void*
HtGetObject(
    PHANDLE_TABLE Table,
    HANDLE Handle,
    PHANDLE_TABLE_ENTRY* OutEntry
);

void
HtDeleteHandle(
    PHANDLE_TABLE Table,
    HANDLE Handle
);

HANDLE
HtCreateHandle(
    PHANDLE_TABLE Table,
    void* Object,
    uint32_t Access
);

PHANDLE_TABLE
HtCreateHandleTable(
    IN  PEPROCESS Process
);

void
HtDeleteHandleTable(
    IN PHANDLE_TABLE Table
);

#endif