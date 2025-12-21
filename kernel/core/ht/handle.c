/*++

Module Name:

    handle.c

Purpose:

    This module contains the implementation of MatanelOS's Handle Table.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/ht.h"
#include "../../includes/ob.h"
#include "../../includes/ps.h"
#include "../../assert.h"

// ->>>>> The handle table handles are accessed in pageable memory, we cannot be at DISPATCH_LEVEL or above.
// Since we also use push locks.

DOUBLY_LINKED_LIST HandleTableList;
PUSH_LOCK HandleTableLock;

static
PHANDLE_TABLE_ENTRY
HtpLookupEntry(
    IN  PHANDLE_TABLE Table,
    IN  HANDLE Handle
)

/*++

    Routine description:

       Lookups the entry in the given handle table from supplied handle.

    Arguments:

        [IN] PHANDLE_TABLE Table - Pointer to the handle table.
        [IN] HANDLE Handle - The handle.

    Return Values:

        The entry on success, or NULL on invalid table/handle.

--*/

{
    if (!Table || !Handle || ((uint64_t)Handle & 3)) return NULL;

    uint64_t TableCode = Table->TableCode;
    uint64_t Level = TableCode & TABLE_LEVEL_MASK;
    void* TableBase = (void*)(TableCode & ~TABLE_LEVEL_MASK);

    // Handles are multiples of 4, so we divide by 4 to get actual index
    uint64_t Index = (uint64_t)(Handle) >> 2;

    if (Level == 0) {
        // Direct Array
        PHANDLE_TABLE_ENTRY Entries = (PHANDLE_TABLE_ENTRY)TableBase;
        return &Entries[Index];
    }
    else if (Level == 1) {
        // TableBase points to an array of pointers to pages
        // We need to find WHICH page, and then WHICH entry in that page.
        uint64_t MaxEntriesPerLevel = LOW_LEVEL_ENTRIES;
        uint64_t PageIndex = Index / MaxEntriesPerLevel;
        uint64_t EntryIndex = Index % MaxEntriesPerLevel;

        PHANDLE_TABLE_ENTRY* PageTable = (PHANDLE_TABLE_ENTRY*)TableBase;
        PHANDLE_TABLE_ENTRY ActualPage = PageTable[PageIndex];
        if (ActualPage) {
            return &ActualPage[EntryIndex];
        }
    }

    // We dont really support millions of handles, so. (level 2 support needed.)
    return NULL;
}

PHANDLE_TABLE
HtCreateHandleTable(
    IN  PEPROCESS Process
)

/*++

    Routine description:

        Create a handle table for the given process.

    Arguments:

        [IN] PEPROCESS Process - Pointer to process.

    Return Values:

        Pointer to table on success, or NULL on failure.

--*/

{
    PHANDLE_TABLE Table = MmAllocatePoolWithTag(NonPagedPool, sizeof(HANDLE_TABLE), 'bTtH'); // HtTb - Handle Table.
    
    // Allocate the first page of the entries (level 0) (switched to paged pool now)
    PHANDLE_TABLE_ENTRY Level0 = MmAllocatePoolWithTag(PagedPool, VirtualPageSize, 'egaP'); // Page
    if (!Level0) {
        MmFreePool(Table);
        return NULL;
    }
    
    // Initialize the free list in the new page.
    for (uint64_t i = 1; i < LOW_LEVEL_ENTRIES - 1; i++) {
        Level0[i].NextFreeTableEntry = (i + 1) * 4; // Store as a handle value.
    }
    Level0[LOW_LEVEL_ENTRIES - 1].NextFreeTableEntry = 0; // End of the list.
    // The first index is NULL, always.
    Level0[0].Object = NULL;

    Table->TableCode = (uint64_t)Level0; // Level is 0, so bottom bits are 0
    Table->FirstFreeHandle = 4;
    Table->QuotaProcess = Process;
    Table->TableLock.Value = 0;

    return Table;
}

static 
PHANDLE_TABLE_ENTRY 
HtpAllocateAndInitHandlePage(
    IN  PHANDLE_TABLE Table,
    IN  uint32_t BaseHandleIndex
)

/*++

    Routine description:

        Creates a HANDLE_TABLE_ENTRY for the given Table (does not insert).

    Arguments:

        [IN]    PHANDLE_TABLE Table - The table to create the entry for.
        [IN]    uint32_t BaseHandleIndex - The base index to create the starting handle table entry for. (e.g if last index was 16, then supply it, :))

    Return Values:

        Pointer to allocated table entry on success, or NULL on failure.

--*/

{
    PHANDLE_TABLE_ENTRY NewPage = MmAllocatePoolWithTag(PagedPool, VirtualPageSize, 'egaP');
    if (!NewPage) return NULL;

    // Link all entries in this new page together
    uint32_t i;
    for (i = 0; i < LOW_LEVEL_ENTRIES - 1; i++) {
        // Calculate the actual handle value for the *next* entry
        NewPage[i].NextFreeTableEntry = (BaseHandleIndex + i + 1) * 4;
    }

    // The last entry in this new page points to the CURRENT FirstFreeHandle.
    NewPage[LOW_LEVEL_ENTRIES - 1].NextFreeTableEntry = Table->FirstFreeHandle;
    NewPage[0].Object = NULL;

    return NewPage;
}

static
void 
HtpExpandTable(
    PHANDLE_TABLE Table
)

/*++

    Routine description:

        Expands a HANDLE_TABLE to its next intended level.

    Arguments:

        [IN]    PHANDLE_TABLE Table - The table to expand.

    Return Values:

        None.

    Notes:

        Any level above 1 is not supported.

--*/

{
    uint64_t TableCode = Table->TableCode;
    uint64_t CurrentLevel = TableCode & TABLE_LEVEL_MASK;
    void* TableBase = (void*)(TableCode & ~TABLE_LEVEL_MASK);

    PHANDLE_TABLE_ENTRY NewFreePage = NULL;
    uint32_t NewBaseIndex = 0;

    //
    // Case 1: Promoting from Level 0 to Level 1
    //
    if (CurrentLevel == 0) {
        // Allocate the "Directory" page (holds pointers, not entries)
        PHANDLE_TABLE_ENTRY* Directory = MmAllocatePoolWithTag(PagedPool, VirtualPageSize, 'riD');
        if (!Directory) return; // OOM

        // The existing Level 0 page becomes the first entry in the directory
        Directory[0] = (PHANDLE_TABLE_ENTRY)TableBase;

        // Allocate a NEW Level 0 page for the second slot
        NewBaseIndex = LOW_LEVEL_ENTRIES; // The index starts where the first page left off
        NewFreePage = HtpAllocateAndInitHandlePage(Table, NewBaseIndex);

        if (!NewFreePage) {
            MmFreePool(Directory);
            return;
        }

        Directory[1] = NewFreePage;

        // Update TableCode: Pointer to Directory | Level 1
        Table->TableCode = ((uint64_t)Directory) | 1;

        // Update the free list to point to the start of our new page
        // (NewFreePage[0] corresponds to NewBaseIndex)
        Table->FirstFreeHandle = NewBaseIndex * 4;
    }
    //
    // Case 2: Already Level 1, need to add a new page
    //
    else if (CurrentLevel == 1) {
        PHANDLE_TABLE_ENTRY* Directory = (PHANDLE_TABLE_ENTRY*)TableBase;

        // Find the first empty slot in the directory
        uint32_t DirectoryIndex = 0;
        for (DirectoryIndex = 0; DirectoryIndex < LOW_LEVEL_ENTRIES; DirectoryIndex++) {
            if (Directory[DirectoryIndex] == NULL) break;
        }

        if (DirectoryIndex >= LOW_LEVEL_ENTRIES) {
            // Level 1 is full, no level 2 yet.
            goto Level2Setup;
        }

        // Calculate the Handle Index base for this new page
        NewBaseIndex = DirectoryIndex * LOW_LEVEL_ENTRIES;

        // Allocate the new page
        NewFreePage = HtpAllocateAndInitHandlePage(Table, NewBaseIndex);
        if (!NewFreePage) return;

        // Link it into the directory
        Directory[DirectoryIndex] = NewFreePage;

        // Update Free List
        Table->FirstFreeHandle = NewBaseIndex * 4;
    }

Level2Setup:
    return;
}

HANDLE
HtCreateHandle(
    PHANDLE_TABLE Table,
    void* Object,
    uint32_t Access
)

/*++

    Routine description:

        Creates a HANDLE for specified Object. (Ob)

    Arguments:

        [IN]    PHANDLE_TABLE Table - The table to insert the handle in.
        [IN]    void* Object - The Object to create the handle for.
        [IN]    uint32_t Access - The maximum access the handle should have.

    Return Values:

        The HANDLE number, or MT_INVALID_HANDLE on new handle allocation failure.

--*/

{
    // Acquire exclusive push lock, we are modifying the table.
    // Since we are in pageable memory, we can wait and access it even.
    MsAcquirePushLockExclusive(&Table->TableLock);

    // Is there a free handle in the list?
    if (Table->FirstFreeHandle == 0)
    {

        // Expand table, no free handles.
        HtpExpandTable(Table);

        // Check again. If it is STILL 0, expansion failed (OOM).
        if (Table->FirstFreeHandle == 0) {
            MsReleasePushLockExclusive(&Table->TableLock);
            return MT_INVALID_HANDLE; // Return NULL/0
        }
    }

    // Pop from Free List
    uint32_t FreeIndex = Table->FirstFreeHandle;
    PHANDLE_TABLE_ENTRY Entry = HtpLookupEntry(Table, (HANDLE)FreeIndex);

    // Sanity check (Should never happen if FirstFreeHandle != 0)
    if (!Entry) {
        MsReleasePushLockExclusive(&Table->TableLock);
        return MT_INVALID_HANDLE;
    }

    // Update head of free list
    Table->FirstFreeHandle = Entry->NextFreeTableEntry;

    // Setup the Entry
    Entry->Object = Object;
    Entry->GrantedAccess = Access;
    MsReleasePushLockExclusive(&Table->TableLock);

    return (HANDLE)FreeIndex;
}

void 
HtDeleteHandle(
    PHANDLE_TABLE Table,
    HANDLE Handle
)

/*++

    Routine description:

        Delets a HANDLE from the table.

    Arguments:

        [IN]    PHANDLE_TABLE Table - The table to delete the handle from.
        [IN]    HANDLE Handle - The handle to delete.

    Return Values:

        None.

--*/

{
    MsAcquirePushLockExclusive(&Table->TableLock);

    // Validate Handle
    // Ensure it's not 0 (if 0 is invalid) and is a multiple of 4
    if (!Handle || ((uint64_t)Handle & 3)) {
        MsReleasePushLockExclusive(&Table->TableLock);
        return;
    }

    // Lookup the entry
    PHANDLE_TABLE_ENTRY Entry = HtpLookupEntry(Table, Handle);

    // Check if entry is actually in use
    if (!Entry || !Entry->Object) {
        // Handle is already free or invalid
        MsReleasePushLockExclusive(&Table->TableLock);
        return;
    }

    // 4. Invalidate the Entry
    Entry->Object = NULL;
    Entry->GrantedAccess = 0;

    // Push onto Free List (LIFO - Stack)
    // The current head of the list becomes the next for this entry.
    Entry->NextFreeTableEntry = Table->FirstFreeHandle;
    // This entry becomes the new head.
    Table->FirstFreeHandle = (uint32_t)Handle;
    MsReleasePushLockExclusive(&Table->TableLock);
}

void* 
HtGetObject (
    IN  PHANDLE_TABLE Table, 
    IN  HANDLE Handle,
    _Out_Opt PHANDLE_TABLE_ENTRY* OutEntry
)

/*++

    Routine description:

        Retrieves the object for the specified Handle.

    Arguments:

        [IN]    PHANDLE_TABLE Table - The table to enumerate the handle in.
        [IN]    HANDLE Handle - The Object's handle.
        [OUT OPTIONAL]    PHANDLE_TABLE_ENTRY* OutEntry - The table entry for the handle.

    Return Values:

        The Object found for the handle.

--*/

{
    void* Object = NULL;

    // We can acquire a shareed push lock since we are strictly reading and not writing to the table contents.
    MsAcquirePushLockShared(&Table->TableLock);

    PHANDLE_TABLE_ENTRY Entry = HtpLookupEntry(Table, Handle);

    // Check if valid and allocated
    if (Entry && Entry->Object) {
        Object = Entry->Object;
    }

    MsReleasePushLockShared(&Table->TableLock);
    if (Entry) {
        if (OutEntry) *OutEntry = Entry;
    }
    return Object;
}

void
HtDeleteHandleTable(
    IN PHANDLE_TABLE Table
)

/*++

    Routine description:

        Deletes the handle table allocated. (Dereferences any objects that are still alive, frees directory and other)

    Arguments:

        [IN]    PHANDLE_TABLE Table - The table to delete.

    Return Values:

        None.

--*/

{
    // We just free all of the levels and the table itself.
    if (!Table) return;

    // Grab the table lock.
    MsAcquirePushLockExclusive(&Table->TableLock);

    uint64_t TableCode = Table->TableCode;
    uint64_t Level = TableCode & TABLE_LEVEL_MASK;
    void* TableBase = (void*)(TableCode & ~TABLE_LEVEL_MASK);

    if (Level == 0) {
        // Single contigious page of entries
        PHANDLE_TABLE_ENTRY Entries = (PHANDLE_TABLE_ENTRY)TableBase;
        if (Entries) {
            // Walk and dereference any live objects that are alive.
            for (uint64_t i = 0; i < LOW_LEVEL_ENTRIES; i++) {
                void* Object = Entries[i].Object;
                if (Object) {
                    Entries[i].Object = NULL;
                    // Decrement handle count atomically
                    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
                    InterlockedDecrementIfNotZero((volatile uint64_t*)Header->HandleCount);
                    ObDereferenceObject(Object);
                }
            }
        }

        // No more live handles — release lock and free the page
        MsReleasePushLockExclusive(&Table->TableLock);
        if (Entries) MmFreePool(Entries);
    }

    else if (Level == 1) {
        // Directory of page pointers.
        PHANDLE_TABLE_ENTRY* Directory = (PHANDLE_TABLE_ENTRY*)TableBase;
        if (Directory) {
            // Walk every allocated page.
            for (uint64_t dir = 0; dir < LOW_LEVEL_ENTRIES; dir++) {
                PHANDLE_TABLE_ENTRY Page = Directory[dir];
                if (!Page) continue;

                for (uint64_t i = 0; i < LOW_LEVEL_ENTRIES; i++) {
                    void* Object = Page[i].Object;
                    if (Object) {
                        Page[i].Object = NULL;
                        // Decrement handle count atomically
                        POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
                        InterlockedDecrementIfNotZero((volatile uint64_t*)Header->HandleCount);
                        ObDereferenceObject(Object);
                    }
                }

                // Free this page of handles.
                MmFreePool(Page);
            }
        }

        // Release push lock and free the directory itself.
        MsReleasePushLockExclusive(&Table->TableLock);
        if (Directory) MmFreePool(Directory);
    }

    else {
        // Unsupported level, release lock and get out.
        assert(false, "Unsupported level encountered on handle table free.");
        MsReleasePushLockExclusive(&Table->TableLock);
    }

    // Finally, free our table itself.
    MmFreePool(Table);
}

void
HtClose(
    IN HANDLE Handle
)

/*++

    Routine description:

       Decrements handle count of object, Dereferences the object associated with the handle, then deletes the handle.

    Arguments:

        [IN]    HANDLE Handle - The handle to close.

    Return Values:

        None.

--*/

{
    PHANDLE_TABLE Table = PsGetCurrentProcess()->ObjectTable;

    // First get the object for the handle
    void* Object = HtGetObject(Table, Handle, NULL);
    if (!Object) return;

    // Remove the handle from the table first
    HtDeleteHandle(Table, Handle);

    // Decrement handle count atomically
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    InterlockedDecrementU64((volatile uint64_t*)&Header->HandleCount);

    // Dereference the object
    ObDereferenceObject(Object);
}