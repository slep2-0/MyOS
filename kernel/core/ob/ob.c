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
#include "../../includes/mt.h"

// Global list of types (for debugging/enumeration)
DOUBLY_LINKED_LIST ObTypeDirectoryList;
SPINLOCK ObGlobalLock;
static volatile void* ObpReaperList = NULL;
static EVENT ObpReaperEvent;

static void
ObpIncrementTypeCounter(
    IN volatile uint32_t* Counter,
    IN void* Owner
)
{
    uint32_t Current = InterlockedLoadAcquire(Counter);
    for (;;) {
        if (Current == UINT32_MAX) {
            MeBugCheckEx(MEMORY_OVERFLOW_DETECTION, (void*)Counter,
                Owner, RETADDR(0), NULL);
        }

        uint32_t Observed = InterlockedCompareExchangeU32(
            Counter,
            Current + 1,
            Current
        );
        if (Observed == Current) return;
        Current = Observed;
    }
}

static void
ObpDecrementTypeCounter(
    IN volatile uint32_t* Counter,
    IN void* Owner
)
{
    uint32_t Current = InterlockedLoadAcquire(Counter);
    for (;;) {
        if (Current == 0) {
            MeBugCheckEx(MEMORY_DOUBLE_FREE, (void*)Counter,
                Owner, RETADDR(0), NULL);
        }

        uint32_t Observed = InterlockedCompareExchangeU32(
            Counter,
            Current - 1,
            Current
        );
        if (Observed == Current) return;
        Current = Observed;
    }
}

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

    // Initialize event
    MsInitializeEvent(&ObpReaperEvent, DispatcherSynchronizationEvent, false);
}

static void ObpReaperThread(void)
{
    for (;;) {
        MsWaitForSingleObject(
            &ObpReaperEvent,
            KernelMode,
            false,
            MT_INFINITE
        );

        POBJECT_HEADER Header = (POBJECT_HEADER)
            InterlockedExchangePointer(&ObpReaperList, NULL);
        while (Header) {
            POBJECT_HEADER Next = (POBJECT_HEADER)Header->NextToFree;
            ObDeleteObject(Header);
            Header = Next;
        }
    }
}

void ObInitializeReaperThread(void)
{
    PETHREAD ReaperThread = NULL;
    MTSTATUS Status = PsCreateSystemThread((ThreadEntry)ObpReaperThread,
        NULL, LOW_TIMESLICE_TICKS, &ReaperThread);
    if (MT_FAILURE(Status)) {
        MeBugCheckEx(PSWORKER_INIT_FAILED, (void*)(uintptr_t)Status,
            &ObpReaperEvent, NULL, NULL);
    }

    ReaperThread->WorkerThread = true;
}

MTSTATUS ObCreateObjectType(
    IN const char* TypeName,
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

    // Return the pointer
    *ReturnedObjectType = NewType;
    return MT_SUCCESS;
}

MTSTATUS
ObCreateObject(
    IN POBJECT_TYPE ObjectType,
    IN uint32_t ObjectSize,
    OUT void** ObjectCreated
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
    if (!ObjectType || !ObjectCreated) return MT_INVALID_PARAM;
    *ObjectCreated = NULL;

    // 1. Calculate size
    size_t ActualSize = sizeof(OBJECT_HEADER) + ObjectSize;

    // Allocate memory for the header.
    POBJECT_HEADER Header = (POBJECT_HEADER)MmAllocatePoolWithTag(ObjectType->TypeInfo.PoolType, ActualSize, 'bObO'); // Ob Object, not bobo, lol.
    if (!Header) return MT_NO_MEMORY;

    // Object delete routines must be able to consume a partially initialized
    // body after any constructor failure. Pool contents are not an initializer.
    kmemset(Header, 0, ActualSize);
    Header->Type = ObjectType;
    Header->PointerCount = 1; // Start with 1 reference
    Header->HandleCount = 0;

    // Update stats in the Type object
    ObpIncrementTypeCounter(
        (volatile uint32_t*)&ObjectType->TotalNumberOfObjects,
        ObjectType
    );

    // Return Body
    *ObjectCreated = OBJECT_HEADER_TO_OBJECT(Header);
    return MT_SUCCESS;
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

    uint64_t expected = InterlockedLoad((volatile uint64_t*)&Header->PointerCount);

    for (;;) {
        if (expected == 0) {
            // object is dying or dead
            return false;
        }

        if (expected == UINT64_MAX) {
            MeBugCheckEx(
                MEMORY_OVERFLOW_DETECTION,
                Header,
                (void*)(uintptr_t)expected,
                Header->Type,
                Object
            );
        }

        uint64_t desired = expected + 1;
        // attempt swap: if fail, expected is updated by the function with the new current value
        if (InterlockedCompareExchangeU64_bool((volatile uint64_t*)&Header->PointerCount, desired, &expected)) {
            return true;
        }
        // loop with updated expected
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
    if (Header->Type != DesiredType) {
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
ObOpenObjectByPointer(
    IN void* Object,
    IN POBJECT_TYPE ObjectType,
    IN ACCESS_MASK DesiredAccess,
    OUT PHANDLE Handle
)

{
    if (!Handle) return MT_INVALID_PARAM;

    // Assume failure.
    *Handle = 0;

    // Reference the object.
    MTSTATUS Status = ObReferenceObjectByPointer(Object, ObjectType);
    if (MT_FAILURE(Status)) return Status;

    // Create handle.
    Status = ObCreateHandleForObject(Object, DesiredAccess, Handle);

    // Drop the temporary reference in all cases.
    ObDereferenceObject(Object);

    return Status;
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
    if (!Object) return MT_INVALID_PARAM;

    // Set initially to NULL. (to overwrite stack default if uninitialized)
    *Object = NULL;
    MTSTATUS Status = MT_INVALID_HANDLE;

    // Check for special handles
    if (Handle < 0) {
        if (Handle == MtCurrentProcess()) {
            if (!DesiredType || DesiredType == PsProcessType) {
                PEPROCESS CurrentProcess = PsGetCurrentProcess();

                // Check if caller wants handle information
                if (HandleInformation) {
                    HandleInformation->GrantedAccess = MT_PROCESS_ALL_ACCESS;
                    HandleInformation->Object = CurrentProcess;
                }

                if (ObReferenceObject(CurrentProcess)) {
                    *Object = CurrentProcess;
                    Status = MT_SUCCESS;
                }
                else {
                    Status = MT_OBJECT_DELETED;
                }
            }
            else {
                Status = MT_TYPE_MISMATCH;
            }
        }
        else if (Handle == MtCurrentThread()) {
            if (!DesiredType || DesiredType == PsThreadType) {
                PETHREAD CurrentThread = PsGetCurrentThread();

                // Check if caller wants handle information
                if (HandleInformation) {
                    HandleInformation->GrantedAccess = MT_THREAD_ALL_ACCESS;
                    HandleInformation->Object = CurrentThread;
                }

                if (ObReferenceObject(CurrentThread)) {
                    *Object = CurrentThread;
                    Status = MT_SUCCESS;
                }
                else {
                    Status = MT_OBJECT_DELETED;
                }
            }
            else {
                Status = MT_TYPE_MISMATCH;
            }
        }

        return Status;
    }


    // Get the handle table from current process (requesting process)
    PEPROCESS Process = PsGetCurrentProcess();
    if (!Process || !Process->ObjectTable) return MT_INVALID_HANDLE;

    // Lookup in the handle table.
    HANDLE_TABLE_ENTRY EntryInformation;
    void* RetrievedObject = HtReferenceObject(Process->ObjectTable, Handle,
        &EntryInformation);
    if (!RetrievedObject) return MT_INVALID_HANDLE;

    // Get the header.
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(RetrievedObject);

    // Lets check if the type matches
    if (DesiredType && Header->Type != DesiredType) {
        // Invalid type.
        ObDereferenceObject(RetrievedObject);
        return MT_TYPE_MISMATCH;
    }

    if (DesiredType &&
        (DesiredAccess & ~DesiredType->TypeInfo.ValidAccessRights) != 0) {
        ObDereferenceObject(RetrievedObject);
        return MT_ACCESS_DENIED;
    }

    // Check access.
    if ((EntryInformation.GrantedAccess & DesiredAccess) != DesiredAccess) {
        // Access is invalid.
        ObDereferenceObject(RetrievedObject);
        return MT_ACCESS_DENIED;
    }

    // HtReferenceObject took the reference while the table was locked.
    *Object = RetrievedObject;
    if (HandleInformation) *HandleInformation = EntryInformation;
    return MT_SUCCESS;
}

void
ObIncrementHandleCount(
    IN void* Object
)
{
    if (!Object) {
        MeBugCheckEx(NULL_POINTER_DEREFERENCE, RETADDR(0), NULL, NULL, NULL);
    }

    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    if (!Header->Type) {
        MeBugCheckEx(MEMORY_CORRUPT_HEADER, Header, Object,
            RETADDR(0), NULL);
    }

    uint64_t Current = InterlockedLoadAcquire(
        (volatile uint64_t*)&Header->HandleCount
    );
    for (;;) {
        if (Current == UINT64_MAX) {
            MeBugCheckEx(MEMORY_OVERFLOW_DETECTION, Header,
                (void*)(uintptr_t)Current, Header->Type, Object);
        }

        uint64_t Observed = InterlockedCompareExchangeU64(
            (volatile uint64_t*)&Header->HandleCount,
            Current + 1,
            Current
        );
        if (Observed == Current) break;
        Current = Observed;
    }

    ObpIncrementTypeCounter(
        (volatile uint32_t*)&Header->Type->TotalNumberOfHandles,
        Header->Type
    );
}

void
ObDecrementHandleCount(
    IN void* Object
)
{
    if (!Object) {
        MeBugCheckEx(NULL_POINTER_DEREFERENCE, RETADDR(0), NULL, NULL, NULL);
    }

    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    if (!Header->Type) {
        MeBugCheckEx(MEMORY_CORRUPT_HEADER, Header, Object,
            RETADDR(0), NULL);
    }

    uint64_t Current = InterlockedLoadAcquire(
        (volatile uint64_t*)&Header->HandleCount
    );
    for (;;) {
        if (Current == 0) {
            MeBugCheckEx(MEMORY_DOUBLE_FREE, Header, Object,
                RETADDR(0), NULL);
        }

        uint64_t Observed = InterlockedCompareExchangeU64(
            (volatile uint64_t*)&Header->HandleCount,
            Current - 1,
            Current
        );
        if (Observed == Current) break;
        Current = Observed;
    }

    ObpDecrementTypeCounter(
        (volatile uint32_t*)&Header->Type->TotalNumberOfHandles,
        Header->Type
    );
}

MTSTATUS
ObCreateHandleForObject(
    IN void* Object,
    IN ACCESS_MASK DesiredAccess,
    OUT PHANDLE ReturnedHandle
)

/*++

    Routine description:

       Creates a handle in the current process's handle table for the specified Object and references the object.

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
    // Acquire the object table.
    PHANDLE_TABLE ObjectTable = PsGetCurrentProcess()->ObjectTable;
    if (!ObjectTable) return MT_INVALID_ADDRESS;

    // Use the extended function.
    return ObCreateHandleForObjectEx(Object, DesiredAccess, ReturnedHandle, ObjectTable);
}

MTSTATUS
ObCreateHandleForObjectEx(
    IN void* Object,
    IN ACCESS_MASK DesiredAccess,
    OUT PHANDLE ReturnedHandle,
    IN PHANDLE_TABLE ObjectTable
)

/*++

    Routine description:

       Creates a handle in the specified handle table for the specified Object.
       This also references the object since a handle is created for it.

    Arguments:

        [IN]    void* Object - The object to create the handle for.
        [IN]    ACCESS_MASK DesiredAccess - The maximum access the handle should have.
        [OUT]   PHANDLE ReturnedHandle - The returned handle for the object if success.
        [IN]    PHANDLE_TABLE ObjectTable - The handle table to insert the newly created handle in.

    Return Values:

        MTSTATUS Status Codes:

            MT_SUCCESS - Successful.
            MT_INVALID_ADDRESS - No handle table for process has been given.
            MT_INVALID_CHECK - HtCreateHandle returned MT_INVALID_HANDLE.
--*/

{
    if (!ObjectTable || !Object || !ReturnedHandle) return MT_INVALID_ADDRESS;
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    if ((DesiredAccess & ~Header->Type->TypeInfo.ValidAccessRights) != 0) {
        return MT_ACCESS_DENIED;
    }

    // Establish the handle's ownership before publishing the table entry. A
    // concurrent close of a guessed handle value must never see an entry whose
    // object reference and handle count do not exist yet.
    if (!ObReferenceObject(Object)) return MT_OBJECT_DELETED;
    ObIncrementHandleCount(Object);

    HANDLE Handle = HtCreateHandle(ObjectTable, Object, DesiredAccess);
    if (Handle == MT_INVALID_HANDLE) {
        ObDecrementHandleCount(Object);
        ObDereferenceObject(Object);
        return MT_INVALID_CHECK;
    }

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

       Defers object deletion to a passive-level worker thread.

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
        // Link our object  to the linked list.
        Header->NextToFree = Entry;
        // Update the list
    } while (InterlockedCompareExchangePointer(&ObpReaperList, Header, (void*)Entry) != Entry);

    // Synchronization events coalesce multiple notifications. If the worker
    // is already draining a captured list, this leaves the event signaled for
    // the next pass.
    MsSetEvent(&ObpReaperEvent);
}

void ObDeleteObject(
    IN POBJECT_HEADER Header
)

{
    if (!Header || Header->HandleCount != 0 || Header->PointerCount != 0 ||
        !Header->Type) {
        MeBugCheckEx(MEMORY_CORRUPT_HEADER, Header,
            Header ? (void*)(uintptr_t)Header->PointerCount : NULL,
            Header ? (void*)(uintptr_t)Header->HandleCount : NULL,
            RETADDR(0));
    }

    // Get the type initializer for the object.
    POBJECT_TYPE Type = Header->Type;

#ifdef DEBUG
    // First call debug callback if exists
    if (Type->TypeInfo.DumpProcedure) Type->TypeInfo.DumpProcedure(OBJECT_HEADER_TO_OBJECT(Header));
#endif

    // Call Delete Callback if it exists
    if (Type->TypeInfo.DeleteProcedure) Type->TypeInfo.DeleteProcedure(OBJECT_HEADER_TO_OBJECT(Header));

    // Update Stats
    ObpDecrementTypeCounter(
        (volatile uint32_t*)&Type->TotalNumberOfObjects,
        Type
    );

    // Free Memory
    gop_printf(COLOR_RED, "Freeing the header\n");
    MmFreePool(Header);
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

    uint64_t Current = InterlockedLoadAcquire(
        (volatile uint64_t*)&Header->PointerCount
    );
    uint64_t NewCount;
    for (;;) {
        if (Current == 0) {
            MeBugCheckEx(MEMORY_DOUBLE_FREE, Header, RETADDR(0), NULL, NULL);
        }

        NewCount = Current - 1;
        uint64_t Observed = InterlockedCompareExchangeU64(
            (volatile uint64_t*)&Header->PointerCount,
            NewCount,
            Current
        );
        if (Observed == Current) break;
        Current = Observed;
    }

    if (NewCount == 0) {
        if (InterlockedLoadAcquire(
            (volatile uint64_t*)&Header->HandleCount
        ) != 0) {
            MeBugCheckEx(MEMORY_CORRUPT_HEADER, Header,
                (void*)(uintptr_t)Header->HandleCount, RETADDR(0), NULL);
        }

        ObpDeferObjectDeletion(Header);
    }
}
