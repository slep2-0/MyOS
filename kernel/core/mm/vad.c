/*++

Module Name:

    vad.c

Purpose:

    This translation unit contains the implementation of Virtual Address Descriptors (VADs) of the memory manager.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

// Dev notes: BST's left child are smaller than the node, and the right child is larger than the node (heavier)
// So: LeftChild < Node < RightChild

#include "../../includes/mm.h"
#include "../../includes/ps.h"
#include "../../assert.h"

FORCEINLINE
int
MiGetNodeHeight(
    IN  PMMVAD Node
)

/*++

    Routine description:

        This routine returns the current height of the VAD node (or -1)

    Arguments:

        Pointer to MMVAD Node.

    Return Values:

        Height of node, or -1 if invalid pointer. (0)

--*/

{
    if (!Node) return -1;
    return Node->Height;
}

FORCEINLINE
void
MiUpdateNodeHeight(
    IN  PMMVAD Node
)

/*++

    Routine description:

        Updates the node's height based on its children.

    Arguments:

        Pointer to MMVAD Node.

    Return Values:

        None.

--*/

{
    if (!Node) return;
    Node->Height = 1 + MAX(MiGetNodeHeight(Node->LeftChild), MiGetNodeHeight(Node->RightChild));
}

FORCEINLINE
int
MiGetBalanceFactor(
    IN  PMMVAD Node
)

/*++

    Routine description:

        Calculates balance factor of node in the tree.

    Arguments:

        Pointer to MMVAD Node.

    Return Values:

        Return's the nodes balance factor.

--*/

{
    if (!Node) return 0;
    // balance factor is the (height of the right - height of the left)
    return MiGetNodeHeight(Node->RightChild) - MiGetNodeHeight(Node->LeftChild);
}

static
PMMVAD 
MiAllocateVad(
    void
)

/*++

    Routine description:

        This routine allocates a VAD from the nonpaged pool and sets it up.

    Arguments:

        None.

    Return Values:

        Pointer to allocated VAD. NULL On failure.

--*/

{
    // Allocate the VAD.
    PMMVAD vad = (PMMVAD)MmAllocatePoolWithTag(NonPagedPool, sizeof(MMVAD), ' daV'); // Little endian tag.
    if (!vad) return NULL;
    
    // Initialize to zero. (including height)
    kmemset(vad, 0, sizeof(MMVAD));

    return vad;
}


static
void
MiFreeVad(
    IN PMMVAD Vad
)

/*++

    Routine description:

    Frees a VAD structure back to the non-paged pool.

    Arguments:

        Pointer to MMVAD Node.

    Return Values:

        None.

--*/

{
    return MmFreePool((void*)Vad);
}

static
PMMVAD
MiRotateRight(
    IN PMMVAD y
)

/*++
    Routine description:
        Performs a single right rotation on node y.

    Before:
             y
            / \
           x   T3
          / \
         T1  T2

    After:
             x
            / \
           T1  y
              / \
             T2  T3

    Return Values:
        
        New root of subtree.
--*/

{
    PMMVAD x = y->LeftChild;
    PMMVAD T2 = x->RightChild;
    
    // Perform rotation
    x->RightChild = y;
    y->LeftChild = T2;

    // Update parent pointers
    x->Parent = y->Parent;
    y->Parent = x;
    if (T2) T2->Parent = y;

    // Update heights (update Y before X, since the function uses X)
    MiUpdateNodeHeight(y);
    MiUpdateNodeHeight(x);

    // Return new root of subtree.
    return x;
}

static
PMMVAD
MiRotateLeft(
    IN PMMVAD x
)

/*++
    Routine description:
        Performs a single left rotation on node x.

    Before:
             x
            / \
           T1  y
              / \
             T2  T3

    After:
             y
            / \
           x   T3
          / \
         T1  T2

    Return Values:

        New root of subtree.
--*/

{
    PMMVAD y = x->RightChild;
    PMMVAD T2 = y->LeftChild;

    // Perform rotation
    y->LeftChild = x;
    x->RightChild = T2;

    // Update parent pointers
    y->Parent = x->Parent;
    x->Parent = y;
    if (T2) T2->Parent = x;

    // Update heights (update X before Y, since the function uses Y)
    MiUpdateNodeHeight(x);
    MiUpdateNodeHeight(y);

    // Return new root of subtree.
    return y;
}

static
PMMVAD
MiFindMinimumVad(
    IN PMMVAD Node
)

/*++

    Routine description:

        Finds the node with the minimum value (StartVa) in a given sub-tree.

    Arguments:

        Pointer to MMVAD Node.

    Return Values:

        None.

--*/

{
    PMMVAD current = Node;
    while (current && current->LeftChild != NULL) {
        current = current->LeftChild;
    }
    return current;
}

static
bool
MiCheckVadOverlap(
    IN PMMVAD Root,
    IN uintptr_t StartVa,
    IN uintptr_t EndVa
)

/*++

    Routine description:

        Checks if a proposed new VAD (defined by StartVA and EndVA) overlaps with any existing VADs in the tree.

    Arguments:

        Pointer to MMVAD Node.
        Start Virtual Address.
        Ending Virtual Address.

    Return Values:

        True if there is an overlap, false otherwise.

--*/

{
    PMMVAD Node = Root;
    while (Node) {
        // Check for overlap
        // A overlaps B if A.start < B.End AND > B.start
        if (StartVa <= Node->EndVa && EndVa >= Node->StartVa) {
            return true;
        }

        // If the new VAD is entirely before the current one
        if (EndVa < Node->StartVa) {
            Node = Node->LeftChild; // Only need to check left
        }

        // If the new VAD is entirely after the current one
        else if (StartVa > Node->EndVa) {
            Node = Node->RightChild; // Only need to check right
        }

        // Overlaps.
        else {
            return true;
        }
    }

    // No overlap found.
    return false;
}

PMMVAD
MiFindVad(
    IN  PEPROCESS Process,
    IN  uintptr_t VirtualAddress
)

/*++

    Routine description:

        Finds a VAD that contains the given virtual address in it.

    Arguments:

        [IN]    PMMVAD Root - Root of the VAD Tree of the process.
        [IN]    uintptr_t VirtualAddress - Virtual address to check for.

    Return Values:

        Returns the VAD if found, NULL otherwise.

--*/

{
    // Acquire the reading lock for the process.
    MsAcquirePushLockShared(&Process->VadLock);

    PMMVAD current = Process->VadRoot;

    while (current) {
        // Is the virtual address BEFORE this VAD
        if (VirtualAddress < current->StartVa) {
            current = current->LeftChild;
        }

        // Is the virtual address AFTER this VAD
        else if (VirtualAddress > current->EndVa) {
            current = current->RightChild;
        }

        // Then, it must be inside of this VAD.
        else {
            MsReleasePushLockShared(&Process->VadLock);
            return current;
        }
    }

    // Not found.
    MsReleasePushLockShared(&Process->VadLock);
    return NULL;
}

static
PMMVAD
MiInsertVadNode(
    IN PMMVAD Node,
    IN PMMVAD NewVad
)

/*++

    Routine description:

        Inserts the NewVad into Node using AVL insert.

    Arguments:

        Node to insert to.
        Vad to insert.

    Return Values:

        New root of subtree.

--*/

{
    // Found the best spot to insert
    if (!Node) return NewVad;

    // Recursive step
    if (NewVad->StartVa < Node->StartVa) {
        PMMVAD newLeft = MiInsertVadNode(Node->LeftChild, NewVad);
        Node->LeftChild = newLeft;
        if (newLeft) newLeft->Parent = Node;
    }
    else {
        // No duplicates or overlaps, the caller should handle that before calling.
        PMMVAD newRight = MiInsertVadNode(Node->RightChild, NewVad);
        Node->RightChild = newRight;
        if (newRight) newRight->Parent = Node;
    }

    // Update height
    MiUpdateNodeHeight(Node);

    // Get balance, and rebalance the tree if needed.
    int balance = MiGetBalanceFactor(Node);

    // Left heavy tree
    if (balance < -1) {
        // Left left
        if (NewVad->StartVa < Node->LeftChild->StartVa) {
            return MiRotateRight(Node);
        }
        // Left right
        else {
            Node->LeftChild = MiRotateLeft(Node->LeftChild);
            return MiRotateRight(Node);
        }
    }

    // Right heavy tree
    if (balance > 1) {
        // Right right
        if (NewVad->StartVa > Node->RightChild->StartVa) {
            return MiRotateLeft(Node);
        }
        // Right left
        else {
            Node->RightChild = MiRotateRight(Node->RightChild);
            return MiRotateLeft(Node);
        }
    }

    // Return the (probably new) root of the this subtree.
    return Node;
}

static
PMMVAD
MiDeleteVadNode(
    IN  PMMVAD Root,
    IN  PMMVAD VadToDelete
)

/*++

    Routine description:

        Delets VadToDelete from Root.

    Arguments:

        Root to delete from
        Vad to delete.

    Return Values:

        New root of subtree.

--*/

{
    if (Root == NULL) {
        // Shouldn't happen if logic is correct.
        return NULL;
    }

    // Find the node
    if (VadToDelete->StartVa < Root->StartVa) {
        Root->LeftChild = MiDeleteVadNode(Root->LeftChild, VadToDelete);
    }
    else if (VadToDelete->StartVa > Root->StartVa) {
        Root->RightChild = MiDeleteVadNode(Root->RightChild, VadToDelete);
    }
    else {
        // Node with 0 or 1 child.
        if (Root->LeftChild == NULL || Root->RightChild == NULL) {
            PMMVAD temp = Root->LeftChild ? Root->LeftChild : Root->RightChild;

            // No childs.
            if (!temp) {
                // This node (root) is what we want to delete.
                // We return NULL to our parent, who will set its child ptr to null. (and free it from memory)
                return NULL;
            }
            // One child
            else {
                // The child takes our place.
                temp->Parent = Root->Parent;
                return temp; // Return the child to our parent.
            }
        }
        // Node with 2 children
        else {
            PMMVAD successor = MiFindMinimumVad(Root->RightChild);

            // Save Root's tree links
            PMMVAD oldLeft = Root->LeftChild;
            PMMVAD oldParent = Root->Parent;

            // Copy all of successor's data (data + tree links)
            kmemcpy(Root, successor, sizeof(MMVAD));

            // Restore Root's original tree links
            Root->LeftChild = oldLeft;
            Root->Parent = oldParent;

            // Update parent pointers for Root's children
            if (Root->LeftChild) Root->LeftChild->Parent = Root;
            if (Root->RightChild) Root->RightChild->Parent = Root; // successor's right

            // Now delete the original successor
            Root->RightChild = MiDeleteVadNode(Root->RightChild, successor);
        }
    }

    // Update height.
    MiUpdateNodeHeight(Root);

    // Get balance and rebalance if needed.
    int balance = MiGetBalanceFactor(Root);

    // Left Heavy
    if (balance < -1) {
        // Left-Left
        if (MiGetBalanceFactor(Root->LeftChild) <= 0) {
            return MiRotateRight(Root);
        }
        // Left-Right
        else {
            Root->LeftChild = MiRotateLeft(Root->LeftChild);
            return MiRotateRight(Root);
        }
    }

    // Right Heavy
    if (balance > 1) {
        // Right-Right
        if (MiGetBalanceFactor(Root->RightChild) >= 0) {
            return MiRotateLeft(Root);
        }
        // Right-Left
        else {
            Root->RightChild = MiRotateRight(Root->RightChild);
            return MiRotateLeft(Root);
        }
    }

    return Root;
}

#define MAX_VAD_DEPTH 64 // Usually enough for a 64-bit tree

static
uintptr_t
MiFindGap(
    IN  PEPROCESS Process,
    IN  size_t NumberOfBytes,
    IN  uintptr_t SearchStart,
    IN  uintptr_t SearchEnd    // exclusive
)

/*++

    Routine description:

        Finds a VA gap in the VAD Tree using an iterative in-order traversal. (does NOT claim the gap)

    Arguments:

        [IN] PMMVAD Root - The ROOT of the VAD Tree.
        [IN] size_t NumberOfBytes - The size of the gap needed.
        [IN] uintptr_t SearchStart - Inclusive start of the search range.
        [IN] uintptr_t SearchEnd   - Exclusive end of the search range.

    Return Values:

        Start of VA that has enough bytes for 'size'. 0 If gap isn't found.

--*/
{
    if (SearchStart >= SearchEnd) return 0;                // invalid range
    if (NumberOfBytes == 0) return 0;                     // no zero-sized allocations
    if (SearchStart == 0) return 0;                       // defensive: we don't expect VA 0

    PMMVAD vadStack[MAX_VAD_DEPTH];
    int stackTop = -1;

    // Acquire the reading lock.
    MsAcquirePushLockShared(&Process->VadLock);

    PMMVAD current = Process->VadRoot;
    size_t size_needed = ALIGN_UP(NumberOfBytes, VirtualPageSize);

    // Start from one byte before SearchStart so ALIGN_UP(lastEndVa + 1, page) == aligned SearchStart
    uintptr_t lastEndVa = SearchStart - 1;

    stackTop = -1; // Reset stack (you already use this global stack)

    while (current != NULL || stackTop != -1) {
        // Go all the way left, pushing nodes onto the stack
        while (current != NULL) {
            if (stackTop + 1 >= MAX_VAD_DEPTH) {
                // Tree is too deep (shouldn't happen if we balanced it though)
                MsReleasePushLockShared(&Process->VadLock);
                return 0;
            }
            vadStack[++stackTop] = current;
            current = current->LeftChild;
        }

        // Pop the next in-order node
        current = vadStack[stackTop--];

        // If this VAD is entirely before our search range, skip it.
        if (current->EndVa < SearchStart) {
            // Still update lastEndVa so gaps before SearchStart are ignored
            if (current->EndVa > lastEndVa) lastEndVa = current->EndVa;
            current = current->RightChild;
            continue;
        }

        // If this VAD starts at/after the search end, we can check the final gap and exit.
        if (current->StartVa >= SearchEnd) {
            uintptr_t gapStart = ALIGN_UP(lastEndVa + 1, VirtualPageSize);

            // Overflow check: gapStart + size_needed must not wrap
            if (gapStart <= (uintptr_t)-1 - (size_needed - 1)) {
                if (gapStart + size_needed <= SearchEnd) {
                    MsReleasePushLockShared(&Process->VadLock);
                    return gapStart;
                }
            }
            MsReleasePushLockShared(&Process->VadLock);
            return 0;
        }

        // Normal case: VAD intersects our search range in some way.
        // We compute gapStart relative to lastEndVa, but it must also be >= SearchStart.
        uintptr_t gapStart = ALIGN_UP(lastEndVa + 1, VirtualPageSize);
        if (gapStart < SearchStart) gapStart = ALIGN_UP(SearchStart, VirtualPageSize);

        // If gapStart is strictly before this VAD's StartVa, we have candidate gap.
        if (gapStart < current->StartVa) {
            // check overflow and fit into both current VAD and SearchEnd
            if (gapStart <= (uintptr_t)-1 - (size_needed - 1)) {
                uintptr_t gapEndExclusive = gapStart + size_needed;
                // must fit before current VAD and before SearchEnd (SearchEnd is exclusive)
                if (gapEndExclusive <= current->StartVa && gapEndExclusive <= SearchEnd) {
                    MsReleasePushLockShared(&Process->VadLock);
                    return gapStart;
                }
            }
        }

        // Update lastEndVa to cover this VAD. ensure monotonicity.
        if (current->EndVa > lastEndVa) lastEndVa = current->EndVa;

        // Move to right subtree
        current = current->RightChild;
    }

    // After traversing entire tree, check the gap between lastEndVa and SearchEnd (exclusive)
    uintptr_t finalGapStart = ALIGN_UP(lastEndVa + 1, VirtualPageSize);
    if (finalGapStart < SearchStart) finalGapStart = ALIGN_UP(SearchStart, VirtualPageSize);

    if (finalGapStart <= (uintptr_t)-1 - (size_needed - 1)) {
        if (finalGapStart + size_needed <= SearchEnd) {
            MsReleasePushLockShared(&Process->VadLock);
            return finalGapStart;
        }
    }

    // No gap found anywhere
    MsReleasePushLockShared(&Process->VadLock);
    return 0;
}

// PUBLIC API

// Wrapper
uintptr_t
MmFindFreeAddressSpace(
    IN  PEPROCESS Process,
    IN  size_t NumberOfBytes,
    IN  uintptr_t SearchStart,
    IN  uintptr_t SearchEnd    // exclusive
)

{
    if (Process && NumberOfBytes) {
        return MiFindGap(Process, NumberOfBytes, SearchStart, SearchEnd);
    }
    return 0;
}

MTSTATUS
MmAllocateVirtualMemory(
    IN PEPROCESS Process,
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t NumberOfBytes,
    IN VAD_FLAGS VadFlags
)

/*++

    Routine description:

        Allocates virtual memory (paged) for the process.

    Arguments:

        [IN]    PEPROCESS Process - Process to allocate memory for
        [IN OPTIONAL | OUT OPTIONAL] [PTR_TO_PTR]   void** BaseAddress - The base address to allocate memory starting from if supplied. If NULL, a free gap is chosen and used by NumberOfBytes, and *baseAddress is set to the found start of gap.
        [IN]    size_t NumberOfBytes - The amount in virtual memory to allocate.
        [IN]    uint32_t VadFlags - The VAD Flags to supply for the allocation (file backed?)

    Return Values:

        Various MTSTATUS Status codes.

--*/

{
    // Calculate pages needed

    uintptr_t StartVa; 
    PRIVILEGE_MODE PreviousMode = MeGetPreviousMode();
    if (BaseAddress) {
        if (PreviousMode == UserMode) __stac();
        StartVa = (uintptr_t)*BaseAddress;
        if (PreviousMode == UserMode) __clac();
    }
    else {
        return MT_INVALID_PARAM;
    }
    size_t Pages = BYTES_TO_PAGES(NumberOfBytes);
    uintptr_t EndVa = StartVa + PAGES_TO_BYTES(Pages) - 1;
    MTSTATUS status = MT_GENERAL_FAILURE; // Default to failure
    bool checkForOverlap = true;

    if (!StartVa) {
        // We need to determine if the allocation is for a system process or a user process.
        // Its + 1 because its exclusive (so we want the actual end of the page, not excluding the last one)
        StartVa = MiFindGap(Process, NumberOfBytes, USER_VA_START, (uintptr_t)USER_VA_END + 1);
        if (!StartVa) return MT_NOT_FOUND;
        
        // Update the newly found address.
        if (BaseAddress) {
            if (PreviousMode == UserMode) __stac();
            *BaseAddress = (void*)StartVa;
            if (PreviousMode == UserMode) __clac();
        }
        
        // No need to check for an overlap as if we found a sufficient gap, there is guranteed to be no overlap.
        checkForOverlap = false;

        // Calculate the end VA.
        EndVa = StartVa + PAGES_TO_BYTES(Pages) - 1;
    } 

    // Acquire rundown protection for process
    if (!MsAcquireRundownProtection(&Process->ProcessRundown)) {
        return MT_INVALID_STATE;
    }

    // Acquire lock for this process VAD tree.
    MsAcquirePushLockExclusive(&Process->VadLock);

    // Check for overlap
    if (checkForOverlap && MiCheckVadOverlap(Process->VadRoot, StartVa, EndVa)) {
        status = MT_CONFLICTING_ADDRESSES;
        goto cleanup;
    }

    // Allocate and initialize new VAD.
    PMMVAD newVad = MiAllocateVad();
    if (!newVad) {
        status = MT_NO_RESOURCES;
        goto cleanup;
    }

    newVad->StartVa = StartVa;
    newVad->EndVa = EndVa;
    newVad->Flags = VadFlags;
    newVad->OwningProcess = Process;

    // TODO init file info if VAD_FLAG_MAPPED_FILE is set. (TODO FILE PAGING)

    // Insert the VAD into the the process's tree.
    Process->VadRoot = MiInsertVadNode(Process->VadRoot, newVad);
    status = MT_SUCCESS;
    goto cleanup;

cleanup:
    MsReleaseRundownProtection(&Process->ProcessRundown);
    MsReleasePushLockExclusive(&Process->VadLock);
    return status;
}

MTSTATUS
MmIsAddressRangeFree(
    PEPROCESS Process,
    uintptr_t StartVa,
    uintptr_t EndVa
)

{
    return MiCheckVadOverlap(Process->VadRoot, StartVa, EndVa);
}

MTSTATUS
MmFreeVirtualMemory(
    IN PEPROCESS Process,
    IN void* BaseAddress
)

/*++

    Routine description:

        Releases virtual memory allocated by MmAllocateVirtualMemory.

    Arguments:

        [IN]    PEPROCESS Process - Process to allocate memory for
        [IN]    void* BaseAddress - The base address to release memory, supplied from/to MmAllocateVirtualMemory.

    Return Values:

        Various MTSTATUS Status code.

--*/

{
    MTSTATUS status = MT_GENERAL_FAILURE;
    uintptr_t va = (uintptr_t)BaseAddress;

    // Acquire rundown protection
    if (!MsAcquireRundownProtection(&Process->ProcessRundown)) {
        return MT_INVALID_STATE;
    }

    // Acquire VAD lock
    MsAcquirePushLockExclusive(&Process->VadLock);

    PMMVAD VadToFree = MiFindVad(Process, va);

    // Check if its the valid VAD and if the base address is the start of the VAD region.
    if (VadToFree == NULL || VadToFree->StartVa != va) {
        status = MT_INVALID_PARAM;
        goto cleanup;
    }

    // Unmap all PTEs and physical pages from VAD.
    for (uintptr_t virtualaddr = VadToFree->StartVa; virtualaddr <= VadToFree->EndVa; virtualaddr += VirtualPageSize) {
        // Get the PTE pointer for the current VA.
        PMMPTE pte = MiGetPtePointer(virtualaddr);
        // Atomically unmap the PTE.
        MiUnmapPte(pte);
        // Grab the PFN Number from the (now replaced) PTE. (in PresentSet, PageFrameNumber is the physical address, not the PFN index, our MiUnmapPte function replaced that)
        PAGE_INDEX pfn = pte->Soft.PageFrameNumber;
        // Release the PFN back to MM.
        MiReleasePhysicalPage(pfn);
    }

    // Delete the VAD from the tree.
    Process->VadRoot = MiDeleteVadNode(Process->VadRoot, VadToFree);
    // Free the VAD struct itself (from kernel's nonpagedpool memory, its not a double free)
    MiFreeVad(VadToFree);

    // Set status.
    status = MT_SUCCESS;
    goto cleanup;

cleanup:
    MsReleaseRundownProtection(&Process->ProcessRundown);
    MsReleasePushLockExclusive(&Process->VadLock);
    return status;
}