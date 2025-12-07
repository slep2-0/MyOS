/*++

Module Name:

    ob.h

Purpose:

    This module contains the header files & prototypes required for the Object Manager implementation of MatanelOS.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#ifndef X86_MATANEL_OB_H
#define X86_MATANEL_OB_H

#include "core.h"
#include "me.h"

// --------------- STRUCTURES ---------------

// Forward declare the object header
struct _OBJECT_HEADER;

// Function pointer types for object callbacks
typedef void (*OB_DELETE_METHOD)(void* Object);
typedef void (*OB_CLOSE_METHOD)(void* Object, void* Process, uint64_t Handle);
typedef void (*OB_DUMP_METHOD)(void* Object);

// Defines how a specific type of object behaves.
// This struct mimics standard Windows OBJECT_TYPE_INITIALIZER
typedef struct _OBJECT_TYPE_INITIALIZER {
    POOL_TYPE PoolType;          // NonPagedPool vs PagedPool
    // Standard Callbacks
    uint32_t ValidAccessRights;
    OB_DUMP_METHOD DumpProcedure;
    OB_DELETE_METHOD DeleteProcedure;
    OB_CLOSE_METHOD CloseProcedure; // Maybe will be used.
} OBJECT_TYPE_INITIALIZER, * POBJECT_TYPE_INITIALIZER;

typedef struct _OBJECT_TYPE {
    DOUBLY_LINKED_LIST TypeList;     // Global list of all types
    char Name[32];                   // "Process", "Thread", "Mutant"
    uint32_t TotalNumberOfObjects;   // Statistics
    uint32_t TotalNumberOfHandles;   // Statistics
    OBJECT_TYPE_INITIALIZER TypeInfo; // Routine (init & del & dbg) information for this object
} OBJECT_TYPE, * POBJECT_TYPE;

// Object header (it is aligned to 16 bytes, to avoid bugs)
typedef struct _OBJECT_HEADER {
    uint64_t PointerCount; // Number of kernel pointers referencing this object.
    union {
        uint64_t HandleCount;  // Number of user handles open (future)
        volatile void* NextToFree; // If object is deferred for deletion, NextToFree is used instead of HandleCount.
    };
    POBJECT_TYPE Type;  // Pointer to type definition.
    uint32_t Flags;
} __attribute__((aligned(16))) OBJECT_HEADER, *POBJECT_HEADER;
_Static_assert(sizeof(OBJECT_HEADER) % 16 == 0, "OBJECT_HEADER must be 16-byte aligned");

// Macros for arithemetic
#define OBJECT_TO_OBJECT_HEADER(o) \
    ((POBJECT_HEADER)((char*)(o) - sizeof(OBJECT_HEADER)))

#define OBJECT_HEADER_TO_OBJECT(h) \
    ((void*)((char*)(h) + sizeof(OBJECT_HEADER)))

// --------------- FUNCTIONS ---------------

void ObInitialize(void);

MTSTATUS ObCreateObjectType(
    IN char* TypeName,
    IN POBJECT_TYPE_INITIALIZER ObjectTypeInitializer,
    OUT POBJECT_TYPE* ObjectType
);

void* ObCreateObject(
    IN POBJECT_TYPE ObjectType,
    IN uint32_t ObjectSize
    //_In_Opt char* Name - When files arrive, i'll uncomment this.
);

bool
ObReferenceObject(
    IN  void* Object
);

void ObDereferenceObject(
    IN  void* Object
);

#endif