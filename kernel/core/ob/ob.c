/*++

Module Name:

    ob.c

Purpose:

    This translation unit contains the implementation of the object manager.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/ob.h"
#include "../../includes/mg.h"
#include "../../includes/md.h"
#include "../../assert.h"
#include "../../includes/ps.h"

// Global list of types (for debugging/enumeration)
DOUBLY_LINKED_LIST ObTypeDirectoryList;
SPINLOCK ObGlobalLock;
volatile void* ObpReaperList = NULL;


DPC ObpReaperDpc;

void ObInitialize (
    void
) 

/*++

    Routine description:

        Initializes the Object Manager of the kernel.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    ObGlobalLock.locked = false;
    InitializeListHead(&ObTypeDirectoryList);
    // Initialize the DPC here, not at the ObpDefer function, as it would overwrite.
    MeInitializeDpc(&ObpReaperDpc, ReapOb, NULL, MEDIUM_PRIORITY);
    gop_printf(COLOR_RED, "Its address (&ObpReaperDpc.DeferredRoutine): %p | VS What it points: %p\n", &ObpReaperDpc.DeferredRoutine, ObpReaperDpc.DeferredRoutine);
}

MTSTATUS ObCreateObjectType(
    IN char* TypeName,
    IN POBJECT_TYPE_INITIALIZER ObjectTypeInitializer,
    OUT POBJECT_TYPE* ReturnedObjectType
) 

/*++

    Routine description:

        Creates an object type for the specified object in the kernel subsystem.

    Arguments:

        [IN]    char* TypeName - The name of the object subsystem that will be created for.
        [IN]    POBJECT_TYPE_INITIALIZER ObjectTypeInitializer - The initializer for each object created by ObCreateObject that defines how it should be created & its attributes.
        [OUT]   POBJECT_TYPE* ReturnedObjectType - The returned object type, used to identify this type of object initialization.

    Return Values:

        MTSTATUS Status codes:

            MT_INVALID_PARAM: Invalid parameter, one of them is NULL.
            MT_NO_MEMORY: No memory is available to create the object type.
            MT_SUCCESS: Successfully created the object type.

--*/

{
    if (!TypeName || !ObjectTypeInitializer || !ReturnedObjectType) {
        return MT_INVALID_PARAM;
    }

    // Allocate the Type Object itself.
    POBJECT_TYPE NewType = (POBJECT_TYPE)MmAllocatePoolWithTag(NonPagedPool, sizeof(OBJECT_TYPE), 'epyT'); // Type
    if (!NewType) return MT_NO_MEMORY;

    // Initialize the Type Object
    kmemset(NewType, 0, sizeof(OBJECT_TYPE));
    kstrncpy(NewType->Name, TypeName, 32);

    // Copy the initializer into the object
    kmemcpy(&NewType->TypeInfo, ObjectTypeInitializer, sizeof(OBJECT_TYPE_INITIALIZER));

    // Link it into the global list
    IRQL oldIrql;
    MsAcquireSpinlock(&ObGlobalLock, &oldIrql);
    InsertTailList(&ObTypeDirectoryList, &NewType->TypeList);
    MsReleaseSpinlock(&ObGlobalLock, oldIrql);

    // 5. Return the pointer
    *ReturnedObjectType = NewType;
    return MT_SUCCESS;
}

void* ObCreateObject(
    IN POBJECT_TYPE ObjectType,
    IN uint32_t ObjectSize
    //_In_Opt char* Name - When files arrive, i'll uncomment this.
) 

/*++

    Routine description:

       Creates an object for the specified object type subsystem.

    Arguments:

        [IN]    POBJECT_TYPE ObjectType - The object type to create the object for.
        [IN]    uint32_t ObjectBodySize - The size of the object in bytes to create.

    Return Values:

        Pointer to object, or NULL on failure.

--*/

{
    // 1. Calculate size
    size_t ActualSize = sizeof(OBJECT_HEADER) + ObjectSize;

    // Allocate memory for the header.
    POBJECT_HEADER Header = (POBJECT_HEADER)MmAllocatePoolWithTag(ObjectType->TypeInfo.PoolType, ActualSize, 'bObO'); // Ob Object, not bobo, lol.
    if (!Header) return NULL;

    Header->Type = ObjectType;
    Header->PointerCount = 1; // Start with 1 reference

    // Update stats in the Type object
    InterlockedIncrementU32((volatile uint32_t*)&ObjectType->TotalNumberOfObjects);

    // Return Body
    return OBJECT_HEADER_TO_OBJECT(Header);
}

bool 
ObReferenceObject(
    IN  void* Object
) 

/*++

    Routine description:

       References the Object given.

    Arguments:

        [IN]    void* Object - The Object to increment reference count for.

    Return Values:

        True if reference succeded, false otherwise (object dying/dead).

--*/

{
    if (!Object) return false;
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);

    uint64_t OldCount = Header->PointerCount;
    while (1) {
        if (OldCount == 0) return false; // Object is dying or dead

        uint64_t NewCount = InterlockedCompareExchangeU64(
            (volatile uint64_t*)&Header->PointerCount,
            OldCount + 1,
            OldCount
        );

        if (NewCount == OldCount) return true;
        OldCount = NewCount;
    }
}

MTSTATUS
ObReferenceObjectByPointer(
    IN  void* Object,
    IN  POBJECT_TYPE DesiredType
)

/*++

    Routine description:

       References the Object given by its pointer.

    Arguments:

        [IN]    void* Object - The Object to increment reference count for.
        [IN]    POBJECT_TYPE DesiredType - The type we EXPECT the Object to be (PsProcessType, PsThreadType, etc..)

    Return Values:

        MT_SUCCESS if reference succeeded.
        MT_TYPE_MISMATCH if DesiredType isn't the Object's actual OBJECT_TYPE.
        MT_INVALID_PARAM if Object is NULL.
        MT_OBJECT_DELETED if Object is deleted / ongoing deletion.

        MT_BETTER_THAN_WINDOWS if (true)

--*/

{
    if (!Object) return MT_INVALID_PARAM;

    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);

    // If the caller expects a process but gets a thread or a file, we say no no bye bye.
    if (DesiredType != NULL && Header->Type != DesiredType) {
        return MT_TYPE_MISMATCH;
    }

    // We reference it.
    if (ObReferenceObject(Object)) {
        return MT_SUCCESS;
    }

    // Object is RIP, we return.
    return MT_OBJECT_DELETED;
}

MTSTATUS
ObReferenceObjectByHandle(
    IN HANDLE Handle,
    IN uint32_t DesiredAccess,
    IN POBJECT_TYPE DesiredType,
    OUT void** Object,
    _Out_Opt PHANDLE_TABLE_ENTRY HandleInformation
)

/*++

    Routine description:

       References the Object given by its given handle.

    Arguments:

        [IN]    HANDLE Handle - The handle to reference the object for.
        [IN]    uint32_t DesiredAccess - The access rights requested for the object.
        [IN]    POBJECT_TYPE DesiredType - The type we EXPECT the Object to be (PsProcessType, PsThreadType, etc..)
        [OUT]   void** Object - The pointer to the object expected.
        [OUT OPTIONAL]  PHANDLE_TABLE_ENTRY HandleInformation - Information about the handle given if MT_SUCCESS is returned.

    Return Values:

        MT_SUCCESS if reference succeeded.
        MT_INVALID_HANDLE if the HANDLE is simply invalid (doesn't exist, or table doesnt exist)
        MT_TYPE_MISMATCH if DesiredType isn't the Object's actual OBJECT_TYPE.
        MT_INVALID_PARAM if Object is NULL.
        MT_OBJECT_DELETED if Object is deleted / ongoing deletion.
        MT_ACCESS_DENIED if the desired access does not meet the access rights of the Object.

        MT_BETTER_THAN_WINDOWS if (true)

--*/

{
    // Set initially to NULL. (to overwrite stack default if uninitialized)
    *Object = NULL;

    // Get the handle table from current process (requesting process)
    PEPROCESS Process = PsGetCurrentProcess();
    if (!Process || !Process->ObjectTable) return MT_INVALID_HANDLE;

    // Lookup in the handle table.
    PHANDLE_TABLE_ENTRY OutHandleEntry = NULL;
    void* RetrievedObject = HtGetObject(Process->ObjectTable, Handle, &OutHandleEntry);
    if (!RetrievedObject) return MT_INVALID_HANDLE;

    // Get the header.
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(RetrievedObject);

    // Lets check if the type matches
    if (DesiredType && Header->Type != DesiredType) {
        // Invalid type.
        return MT_TYPE_MISMATCH;
    }

    // Check access.
    if ((OutHandleEntry->GrantedAccess & DesiredAccess) != DesiredAccess) {
        // Access is invalid.
        return MT_ACCESS_DENIED;
    }

    // Wow!! It is all good!!, reference it.
    ObReferenceObject(RetrievedObject);
    *Object = RetrievedObject;
    if (HandleInformation) *HandleInformation = *OutHandleEntry;
    return MT_SUCCESS;
}

MTSTATUS
ObCreateHandleForObject(
    IN void* Object,
    IN ACCESS_MASK DesiredAccess,
    OUT PHANDLE ReturnedHandle
)

/*++

    Routine description:

       Creates a handle in the current process's handle table for the specified Object.

    Arguments:

        [IN]    void* Object - The object to create the handle for.
        [IN]    ACCESS_MASK DesiredAccess - The maximum access the handle should have.
        [OUT]   PHANDLE ReturnedHandle - The returned handle for the object if success.

    Return Values:

        MTSTATUS Status Codes:

            MT_SUCCESS - Successful.
            MT_INVALID_STATE - No handle table for current process.
            MT_INVALID_CHECK - HtCreateHandle returned MT_INVALID_HANDLE.
--*/

{
    // Acquire the current Process Handle Table.
    PHANDLE_TABLE HandleTable = PsGetCurrentProcess()->ObjectTable;
    if (!HandleTable) return MT_INVALID_STATE;

    // Create the handle.
    HANDLE Handle = HtCreateHandle(HandleTable, Object, DesiredAccess);
    if (Handle == MT_INVALID_HANDLE) return MT_INVALID_CHECK;

    // Return success.
    *ReturnedHandle = Handle;
    return MT_SUCCESS;
}

static
void
ObpDeferObjectDeletion(
    IN POBJECT_HEADER Header
)

/*++

    Routine description:

       Defers object deletion to a DPC, to ensure no use after free.

    Arguments:

        [IN]    POBJECT_HEADER Header - The object header to defer deletion for.

    Return Values:

        None.

--*/

{
    volatile void* Entry;
    do {
        // Get the current entry.
        Entry = ObpReaperList;

        // Link our object to the linked list.
        Header->NextToFree = Entry;
        // Update the list
    } while (InterlockedCompareExchangePointer(&ObpReaperList, Header, (void*)Entry) != Entry);

    if (!Entry) {
        // Looks like a DPC hasn't been queued yet, lets do so!
        MeInsertQueueDpc(&ObpReaperDpc, NULL, NULL);
    }
}

void ObDereferenceObject(
    IN  void* Object
) 

/*++

    Routine description:

       Dereferences the Object given.

    Arguments:

        [IN]    void* Object - The Object to decrement reference count for.

    Return Values:

        None.

    Notes:

        On reference count 0, object is deleted using type initializer routine.

--*/

{
    if (!Object) return;
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);

    uint64_t NewCount = InterlockedDecrementU64((volatile uint64_t*)&Header->PointerCount);

    if (NewCount == 0) {
        // Get the type initializer for the object.
        POBJECT_TYPE Type = Header->Type;

#ifdef DEBUG
        // First call debug callback if exists
        if (Type->TypeInfo.DumpProcedure) Type->TypeInfo.DumpProcedure(Object);
#endif

        // Call Delete Callback if it exists
        if (Type->TypeInfo.DeleteProcedure) Type->TypeInfo.DeleteProcedure(Object);

        // Update Stats
        InterlockedDecrementU32((volatile uint32_t*)&Type->TotalNumberOfObjects);
        // Free Memory
        gop_printf(COLOR_RED, "Freeing the header\n");
        //ObpDeferObjectDeletion(Header);
        MmFreePool(Header);
    }
}