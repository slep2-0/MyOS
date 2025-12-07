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
#include "../../assert.h"

// Global list of types (for debugging/enumeration)
DOUBLY_LINKED_LIST ObTypeDirectoryList;
SPINLOCK ObGlobalLock;

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

        True if reference succeded, false otherwise (object dyind/dead).

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

volatile void* ObpReaperList = NULL;

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
        DPC* DpcAllocated = MmAllocatePoolWithTag(NonPagedPool, sizeof(DPC), 'pRbO'); // Ob Reaper
        assert(DpcAllocated != NULL);
        if (!DpcAllocated) return;
        MeInitializeDpc(DpcAllocated, ReapOb, NULL, MEDIUM_PRIORITY);
        MeInsertQueueDpc(DpcAllocated, (void*)ObpReaperList, NULL);
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
        ObpDeferObjectDeletion(Header);
    }
}