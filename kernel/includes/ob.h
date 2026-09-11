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
#include "ht.h"

// --------------- STRUCTURES ---------------

// Forward declare the object header
struct _OBJECT_HEADER;

#define MT_OB_MAX_REPARSE_ATTEMPTS 32u

// Object lookup/open attributes.
#define MT_OBJ_CASE_INSENSITIVE 0x00000001u
#define MT_OBJ_OPEN_LINK        0x00000002u
#define MT_OBJ_VALID_ATTRIBUTES (MT_OBJ_CASE_INSENSITIVE | MT_OBJ_OPEN_LINK)

typedef struct _OB_PARSE_RESULT {
    void* Object;
    char* ReparsePath;
    size_t ReparsePathLength;
} OB_PARSE_RESULT, *POB_PARSE_RESULT;

// Function pointer types for object callbacks
typedef void (*OB_DELETE_METHOD)(void* Object);
typedef void (*OB_CLOSE_METHOD)(void* Object, void* Process, uint64_t Handle);
typedef void (*OB_DUMP_METHOD)(void* Object);
typedef MTSTATUS (*OB_PARSE_METHOD)(
    IN void* ParseObject,
    IN const char* RemainingPath,
    IN size_t RemainingPathLength,
    IN void* ParseContext,
    OUT POB_PARSE_RESULT Result
);

// Defines how a specific type of object behaves.
// This struct mimics standard Windows OBJECT_TYPE_INITIALIZER
typedef struct _OBJECT_TYPE_INITIALIZER {
    POOL_TYPE PoolType;          // NonPagedPool vs PagedPool
    // Standard Callbacks
    uint32_t ValidAccessRights;
    OB_DUMP_METHOD DumpProcedure;
    OB_DELETE_METHOD DeleteProcedure;
    OB_CLOSE_METHOD CloseProcedure; // Maybe will be used.
    OB_PARSE_METHOD ParseProcedure;
} OBJECT_TYPE_INITIALIZER, * POBJECT_TYPE_INITIALIZER;

typedef struct _OBJECT_TYPE {
    DOUBLY_LINKED_LIST TypeList;     // Global list of all types
    char Name[32];                   // "Process", "Thread", "Mutant"
    uint32_t TotalNumberOfObjects;   // Statistics
    uint32_t TotalNumberOfHandles;   // Statistics
    OBJECT_TYPE_INITIALIZER TypeInfo; // Routine (init & del & dbg) information for this object, along with which pool type is it for allocations, and valid access rights for a handle for this object. 
} OBJECT_TYPE, * POBJECT_TYPE;

struct _OBJECT_HEADER_NAME_INFO;

// Object-header flags.
#define MT_OB_FLAG_PERMANENT_OBJECT 0x00000001u

// Object header (it is aligned to 16 bytes, to avoid bugs)
typedef struct _OBJECT_HEADER {
    uint64_t PointerCount; // Number of kernel pointers referencing this object.
    uint64_t HandleCount;  // Number of user handles open
    volatile void* NextToFree; // Link used by the deferred-deletion stack.
    POBJECT_TYPE Type;  // Pointer to type definition.
    uint32_t Flags;
    struct _OBJECT_HEADER_NAME_INFO* NameInfo; // Nullable pointer that contains the name information of this object (if present)
} __attribute__((aligned(16))) OBJECT_HEADER, *POBJECT_HEADER;
_Static_assert(sizeof(OBJECT_HEADER) % 16 == 0, "OBJECT_HEADER must be 16-byte aligned");

// Macros for arithemetic
#define OBJECT_TO_OBJECT_HEADER(o) \
    ((POBJECT_HEADER)((char*)(o) - sizeof(OBJECT_HEADER)))

#define OBJECT_HEADER_TO_OBJECT(h) \
    ((void*)((char*)(h) + sizeof(OBJECT_HEADER)))

// Directory types

#define MT_OB_DIRECTORY_HASH_BUCKET_SIZE 37 // Prime number

// OBJECT_HEADER_NAME_INFO.QueryReferences state.
#define MT_OB_NAME_INFO_REMOVING          0x80000000u
#define MT_OB_NAME_INFO_REFERENCE_MASK    0x7FFFFFFFu
#define MT_OB_NAME_INFO_INITIAL_REFERENCES 1u

typedef struct _OBJECT_DIRECTORY_ENTRY {
    // Pointer to next entry in the same bucket
    struct _OBJECT_DIRECTORY_ENTRY* Next;

    // Pointer to object represented by this entry
    // This points to the object pointer itself, NOT to its header.
    void* Object;

    // Cached hash value of the object name
    uint32_t HashValue;
} OBJECT_DIRECTORY_ENTRY, *POBJECT_DIRECTORY_ENTRY;

typedef struct _OBJECT_DIRECTORY {
    // Array of entry pointers, each entry is one chain.
    POBJECT_DIRECTORY_ENTRY HashBuckets[MT_OB_DIRECTORY_HASH_BUCKET_SIZE];

    // Push lock that protects all buckets
    PUSH_LOCK Lock;
} OBJECT_DIRECTORY, *POBJECT_DIRECTORY;

typedef struct _OBJECT_HEADER_NAME_INFO {
    POBJECT_DIRECTORY ParentDirectory;
    char* Name;
    size_t NameLength;
    volatile uint32_t QueryReferences;
} OBJECT_HEADER_NAME_INFO, *POBJECT_HEADER_NAME_INFO;

typedef struct _OBJECT_SYMBOLIC_LINK {
    // TargetPath has to be NULL terminated
    // it stores the path like "\Device\HarddiskVolume0"
    // Links own namespace still lives in NameInfo.
    char* TargetPath;
    size_t TargetPathLength;
} OBJECT_SYMBOLIC_LINK, *POBJECT_SYMBOLIC_LINK;

// --------------- FUNCTIONS ---------------

extern POBJECT_TYPE PsProcessType;
extern POBJECT_TYPE PsThreadType;
extern POBJECT_TYPE MmSectionType;
extern POBJECT_TYPE ObDirectoryType;
extern POBJECT_TYPE ObSymbolicLinkType;
extern POBJECT_DIRECTORY ObAchtungNamedObjectsDirectory;
extern POBJECT_DIRECTORY ObDeviceDirectory;

MTSTATUS ObInitialize(void);
void ObInitializeReaperThread(void);

MTSTATUS ObpInitializeDirectoryType(void);
MTSTATUS ObpInitializeSymbolicLinkType(void);

MTSTATUS ObCreateObjectType(
    IN const char* TypeName,
    IN POBJECT_TYPE_INITIALIZER ObjectTypeInitializer,
    OUT POBJECT_TYPE* ObjectType
);

// object manager private delete name info in pobject header structure which is the header
// it returns void because void is emptiness and more emo stuff
void ObpDeleteNameInfo(IN POBJECT_HEADER Header);

// TODO SIDS FOR AccessMode FOR PRIVILEGE MODE
// IF ITS KERNEL MODE - WE SKIP ACCESS CHECK
// IF ITS USER MODE - WE CHECK SID FOR THE OBJECT TYPE, LIKE IS THIS SID ALLOWED TO OPEN THE OBJECT IN ObOpenObjectByPointer?
// sorry caps :)
MTSTATUS
ObCreateObject(
    IN POBJECT_TYPE ObjectType,
    IN uint32_t ObjectSize,
    OUT void** ObjectCreated
    //_In_Opt char* Name - When files arrive, i'll uncomment this.
);

MTSTATUS
ObCreateHandleForObject(
    IN void* Object,
    IN ACCESS_MASK DesiredAccess,
    OUT PHANDLE ReturnedHandle
);

MTSTATUS
ObCreateHandleForObjectEx(
    IN void* Object,
    IN ACCESS_MASK DesiredAccess,
    OUT PHANDLE ReturnedHandle,
    IN PHANDLE_TABLE ObjectTable
);

bool
ObReferenceObject(
    IN  void* Object
);

MTSTATUS
ObReferenceObjectByPointer(
    IN  void* Object,
    IN  POBJECT_TYPE DesiredType
);

MTSTATUS
ObReferenceObjectByHandle(
    IN HANDLE Handle,
    IN uint32_t DesiredAccess,
    IN POBJECT_TYPE DesiredType,
    OUT void** Object,
    _Out_Opt PHANDLE_TABLE_ENTRY HandleInformation
);

MTSTATUS
ObOpenObjectByPointer(
    IN void* Object,
    IN POBJECT_TYPE ObjectType,
    IN ACCESS_MASK DesiredAccess,
    OUT PHANDLE Handle
);

void ObDereferenceObject(
    IN  void* Object
);

void ObIncrementHandleCount(
    IN void* Object
);

void ObDecrementHandleCount(
    IN void* Object
);

void ObDeleteObject(
    IN POBJECT_HEADER Header
);

MTSTATUS
ObpCreateNameInfo(
    IN POBJECT_HEADER Header,
    IN const char* Name,
    IN size_t NameLength
);

POBJECT_HEADER_NAME_INFO
ObpReferenceNameInfo(
    IN POBJECT_HEADER Header
);

void
ObpDereferenceNameInfo(
    IN POBJECT_HEADER_NAME_INFO NameInfo
);

MTSTATUS
ObpCreateRootDirectory(
    void
);

MTSTATUS
ObpCreateAchtungNamedObjectsDirectory(
    void
);

MTSTATUS
ObpCreateDeviceDirectory(
    void
);

MTSTATUS
ObpLookupObjectPath(
    IN POBJECT_DIRECTORY StartingDirectory,
    IN const char* Path,
    IN size_t PathLength,
    IN uint32_t Attributes,
    IN void* ParseContext,
    OUT void** Object
);

MTSTATUS
ObpInsertNamedObject(
    IN void* Object,
    IN POBJECT_DIRECTORY StartingDirectory,
    IN const char* Path,
    IN size_t PathLength,
    IN uint32_t Attributes
);

MTSTATUS
ObOpenObjectByName(
    IN POBJECT_DIRECTORY StartingDirectory,
    IN const char* Path,
    IN size_t PathLength,
    IN uint32_t Attributes,
    IN void* ParseContext,
    IN POBJECT_TYPE ExpectedType,
    IN ACCESS_MASK DesiredAccess,
    OUT PHANDLE Handle
);

MTSTATUS
ObCreateSymbolicLink(
    IN POBJECT_DIRECTORY StartingDirectory,
    IN const char* Path,
    IN size_t PathLength,
    IN uint32_t Attributes,
    IN const char* TargetPath,
    IN size_t TargetPathLength,
    OUT POBJECT_SYMBOLIC_LINK* SymbolicLinkObject
);

#endif
