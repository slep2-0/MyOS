#ifndef X86_MATANEL_MEMORY_H
#define X86_MATANEL_MEMORY_H

/*++

Module Name:

    mm.h

Purpose:

    This module contains the header files required for memory management (virtual, physical, PFN, VAD, etc.)

Author:

    slep (Matanel) 2025.

Revision History:

--*/

// Base Includes    z
#include <stdint.h>
#include <stdbool.h>
#include "annotations.h"
#include "macros.h"
#include "../mtstatus.h"
#include "../intrinsics/intrin.h"
#include "../intrinsics/atomic.h"

// Needed for linked list and spinlocks
#include "ms.h"
#include "core.h"
#include "efi.h"

// ------------------ HEADER SPECIFIC MACROS ------------------

#define VirtualPageSize 4096ULL // Same as each physical frame.
#define PhysicalFrameSize 4096ULL // Each physical frame.
#define KernelVaStart 0xfffff80000000000ULL
#define PhysicalMemoryOffset 0xffff880000000000ULL // Defines the offset in arithmetic for quick mapping
#define RECURSIVE_INDEX 0x1FF

// Global Declarations for signals.
extern bool MmPfnDatabaseInitialized;

#ifndef __INTELLISENSE__
/* Convert a PFN index to a PPFN_ENTRY pointer */
#define INDEX_TO_PPFN(Index) \
    (&(PfnDatabase.PfnEntries[(size_t)(Index)]))
#define PHYSICAL_TO_PPFN(PHYS) \
    (&PfnDatabase.PfnEntries[(size_t)((PHYS) / (uint64_t)PhysicalFrameSize)])
#define PTE_TO_PHYSICAL(PMMPTE) ((PMMPTE)->Value & ~0xFFFULL)
#define MI_WRITE_PTE(_PtePointer, _Va, _Pa, _Flags)                         \
do {                                                                        \
    volatile MMPTE* _pte = (volatile MMPTE*)(_PtePointer);                  \
    uint64_t _val = (((uintptr_t)(_Pa)) & ~0xFFFULL) | (uint64_t)(_Flags);  \
    _pte->Value = _val;                                                     \
    __asm__ volatile("" ::: "memory");                                      \
                                                                            \
    /* Only set PFN->PTE link if PFN database is initialized */             \
    if (MmPfnDatabaseInitialized) {                                         \
        PPFN_ENTRY _pfn = PHYSICAL_TO_PPFN(_Pa);                            \
        _pfn->Descriptor.Mapping.PteAddress = (PMMPTE)_pte;                 \
    }                                                                       \
                                                                            \
    invlpg((void*)(uintptr_t)(_Va));                                        \
} while (0)
#define PPFN_TO_INDEX(PPFN) ((size_t)((PPFN) - PfnDatabase.PfnEntries))
#define PPFN_TO_PHYSICAL_ADDRESS(PPFN) \
    ((uint64_t)((uint64_t)PPFN_TO_INDEX(PPFN) * (uint64_t)PhysicalFrameSize))
#else
#define PTE_TO_PHYSICAL(PMMPTE) (0)
#define MI_WRITE_PTE(_PtePointer, _Va, _Pa, _Flags) ((void)0)
#define PPFN_TO_INDEX(PPFN) (0)
#define PPFN_TO_PHYSICAL_ADDRESS(PPFN) (0)
#define INDEX_TO_PPFN(Index) (NULL)
#define PHYSICAL_TO_PPFN(PHYS) (NULL)
#endif

// Convert bytes to pages (rounding up)
#define BYTES_TO_PAGES(Bytes) (((Bytes) + VirtualPageSize - 1) / VirtualPageSize)
// Convert pages to bytes
#define PAGES_TO_BYTES(Pages) ((Pages) * VirtualPageSize)

#define MAX_POOL_DESCRIPTORS 7 // Allows for: 32, 64, 128, 256, 512, 1024, 2048 Bytes Per pool
#define _32KB_POOL 1
#define _64KB_POOL 2
#define _128KB_POOL 3
#define _256KB_POOL 4
#define _512KB_POOL 5
#define _1024KB_POOL 6
#define _2048KB_POOL 7
#define POOL_MIN_ALLOC 32
#define USER_VA_END 0x00007FFFFFFFFFFF
#define USER_VA_START 0x10000
// You are allowed to request bytes above max allocation, the global pool would be used.
#define POOL_MAX_ALLOC 2048
// Pool sizes
#define MI_NONPAGED_POOL_SIZE ((size_t)8ULL * 1024 * 1024 * 1024)  // 8 GiB

// Total pages in each pool
#define NONPAGED_POOL_VA_TOTAL_PAGES (MI_NONPAGED_POOL_SIZE / VirtualPageSize)

// Bitmap QWORDs
#define NONPAGED_POOL_VA_BITMAP_QWORDS ((NONPAGED_POOL_VA_TOTAL_PAGES + 63) / 64)

// Number of pages needed for each bitmap (page-aligned)
#define MI_NONPAGED_BITMAP_PAGES_NEEDED ((NONPAGED_POOL_VA_BITMAP_QWORDS * sizeof(uint64_t) + VirtualPageSize - 1) / VirtualPageSize)

// Alignment helper
#define ALIGN_UP(x, align) (((uintptr_t)(x) + ((align)-1)) & ~((uintptr_t)((align)-1)))

// Bitmap memory allocations (physical pages)
#define MI_NONPAGED_BITMAP_BASE  ALIGN_UP(LK_KERNEL_END, VirtualPageSize)
#define MI_NONPAGED_BITMAP_END   (MI_NONPAGED_BITMAP_BASE + MI_NONPAGED_BITMAP_PAGES_NEEDED * VirtualPageSize)

// Pool virtual address ranges (page-aligned)
#define MI_NONPAGED_POOL_BASE    ALIGN_UP(MI_NONPAGED_BITMAP_END, VirtualPageSize)
#define MI_NONPAGED_POOL_END     (MI_NONPAGED_POOL_BASE + MI_NONPAGED_POOL_SIZE)

#define MI_VAD_SEARCH_START MI_NONPAGED_POOL_END
#define MI_VAD_SEARCH_END   (uintptr_t)-1

// Address Manipulation And Checks
#define MI_IS_CANONICAL_ADDR(va) \
({ \
    uint64_t _va = (uint64_t)(va); \
    uint64_t _mask = ~((1ULL << 48) - 1); /* bits 63:48 */ \
    ((_va & _mask) == 0 || (_va & _mask) == _mask); \
})


#define PFN_ERROR UINT64_T_MAX

// ------------------ TYPE DEFINES ------------------
typedef uint64_t PAGE_INDEX;

// ------------------ ENUMERATORS ------------------

typedef enum _PFN_STATE {
    PfnStateActive,         // Actively mapped in a process (RefCount > 0)
    PfnStateStandby,        // Clean, in RAM, not mapped (RefCount == 0)
    PfnStateModified,       // Dirty, in RAM, not mapped (RefCount == 0)
    PfnStateFree,           // Contents are garbage (RefCount == 0)
    PfnStateZeroed,         // Contents are all zeros (RefCount == 0)
    PfnStateTransition,     // Locked for I/O (e.g being paged in/out)
    PfnStateBad             // Unusable (hardware error)
} PFN_STATE;

// Page FLAGS (attributes that can be combined)
typedef enum _PFN_FLAGS {
    PFN_FLAG_NONE = 0,
    PFN_FLAG_NONPAGED = (1U << 0),    // This PFN holds a nonpaged virtual address (not backed by a file), BIT 3 must NOT be set if this bit is active.
    PFN_FLAG_COPY_ON_WRITE = (1U << 1), // This is a COW page
    PFN_FLAG_MAPPED_FILE = (1U << 2), // Backed by a file (not swap)
    PFN_FLAG_LOCKED_FOR_IO = (1U << 3)  // Page is pinned for DMA, etc.
} PFN_FLAGS;

typedef enum _VAD_FLAGS {
    VAD_FLAG_NONE = 0,
    VAD_FLAG_READ = (1U << 0),
    VAD_FLAG_WRITE = (1U << 1),
    VAD_FLAG_EXECUTE = (1U << 2),
    VAD_FLAG_PRIVATE = (1U << 3),     // Private (backed by swap file, like pagefile.mtsys)
    VAD_FLAG_MAPPED_FILE = (1U << 4), // Backed by a file (lets say data.mtdll)
    VAD_FLAG_COPY_ON_WRITE = (1U << 5)
} VAD_FLAGS;

typedef enum _PAGE_FLAGS {
    PAGE_PRESENT = 1 << 0,  // Bit 0
    // 0 = page not present (access causes page fault)
    // 1 = page is present, MMU translates virtual addresses

    PAGE_RW = 1 << 1,  // Bit 1
    // 0 = read-only
    // 1 = read/write

    PAGE_USER = 1 << 2,  // Bit 2
    // 0 = supervisor (kernel) only
    // 1 = user-mode access allowed

    PAGE_PWT = 0x8,     // Bit 3
    // Page Write-Through
    // 0 = write-back caching
    // 1 = write-through caching

    PAGE_PCD = 0x10,    // Bit 4
    // Page Cache Disable
    // 0 = cacheable
    // 1 = cache disabled

    PAGE_ACCESSED = 0x20,    // Bit 5
    // Set by CPU when page is read or written

    PAGE_DIRTY = 0x40,    // Bit 6
    // Set by CPU when page is written to

    PAGE_PS = 0x80,    // Bit 7
    // Page Size
    // 0 = normal 4KB page
    // 1 = large page (4MB in PDE, 2MB in PTE for PAE/long mode)

    PAGE_GLOBAL = 0x100,   // Bit 8
    // Global page
    // Not flushed from TLB on CR3 reload

    PAGE_NX = (1ULL << 63) // Bit 63
    // No-Execute region
    // Execution cannot happen in this page.
} PAGE_FLAGS;

typedef enum _POOL_TYPE {
    NonPagedPool = 0,                 // Non-pageable kernel pool (instant map, available at all IRQLs)
    PagedPool = 1,                    // Pageable pool (can only be used when IRQL < DISPATCH_LEVEL.
    NonPagedPoolCacheAligned = 2,     // Non-paged, cache-aligned (UNIMPLEMENTED)
    PagedPoolCacheAligned = 3,        // Paged, cache-aligned (UNIMPLEMENTED)
    NonPagedPoolNx = 4,               // Non-paged, non-executable (NX) (UNIMPLEMENTED)
    PagedPoolNx = 5,                  // Paged, non-executable (UNIMPLEMENTED)
    // No MustSucceeds, these are a bad concept, handle errors gracefully.
} POOL_TYPE;

typedef enum _FAULT_OPERATION {
    ReadOperation = 0,
    WriteOperation = 2,
    ExecuteOperation = 10,
} FAULT_OPERATION, *PFAULT_OPERATION;

typedef enum _PRIVILEGE_MODE {
    KernelMode = 0,
    UserMode = 1
} PRIVILEGE_MODE, * PPRIVILEGE_MODE;

// ------------------ STRUCTURES ------------------

typedef struct _MMPTE
{
    union
    {
        uint64_t Value; // Raw 64-bit PTE value

        //
        // Hardware format when the page is present in memory
        //
        struct
        {
            uint64_t Present : 1;         // 1 = Present
            uint64_t Write : 1;           // Writable
            uint64_t User : 1;            // User-accessible
            uint64_t WriteThrough : 1;    // Write-through cache
            uint64_t CacheDisable : 1;    // Disable caching
            uint64_t Accessed : 1;        // Set by CPU when accessed
            uint64_t Dirty : 1;           // Set by CPU when written
            uint64_t LargePage : 1;       // Large page flag (2MB/1GB)
            uint64_t Global : 1;          // Global TLB entry
            uint64_t CopyOnWrite : 1;     // Software: copy-on-write
            uint64_t Prototype : 1;       // Software: prototype PTE (section)
            uint64_t Reserved0 : 1;       // Unused or software-available
            uint64_t PageFrameNumber : 40;// Physical page frame number
            uint64_t Reserved1 : 11;      // Reserved by hardware
            uint64_t NoExecute : 1;       // NX bit
        } Hard;

        //
        // Software format when not present
        // (Paged out / transition / pagefile / prototype)
        //
        struct
        {
            uint64_t Present : 1;            // 0 = Not present
            uint64_t Write : 1;              // Meaning depends on context
            uint64_t Transition : 1;         // 1 = Page is in transition (has PFN)
            uint64_t Prototype : 1;          // 1 = Prototype PTE (mapped section)
            uint64_t PageFile : 1;           // 1 = Paged to disk (pagefile)
            uint64_t Reserved : 7;           // Software-defined bits
            uint64_t PageFrameNumber : 32;   // Pagefile offset or PFN (if transition)
            uint64_t SoftwareFlags : 20;     // e.g. protection mask, pool type
            uint64_t NoExecute : 1;          // NX still meaningful in transition
        } Soft;
    };
} MMPTE, * PMMPTE;

typedef struct _PFN_ENTRY {
    volatile uint32_t RefCount;     // Atomic Reference Count
    uint8_t State;                  // PFN_STATE of this Page.
    uint8_t Flags;                  // Bitfield of PFN_FLAGS
    // The Descriptor of the PFN (contains mapping data, the doubly linked list, and file offset, all that depend on the State)
    union {
        // State: PfnStateFree, PfnStateZeroed, 
        // PfnStateStandby, PfnStateModified (Used when - INACTIVE)
        struct _DOUBLY_LINKED_LIST ListEntry;

        // State: PfnStateActive (this is the reverse mapping information) (Used when - ACTIVE, IN USE)
        struct {
            struct _MMVAD* Vad;  // Pointer to VAD in memory. (might not always be in use)
            PMMPTE PteAddress; // Pointer to PTE in memory. (is always valid when in use)
        } Mapping;

        // State: PfnStateStandby or PfnStateModified (for file backed pages) (Used when - SEMI-ACTIVE, PAGED TO DISK, NOT IN CURRENT USE)
        uintptr_t FileOffset;

    } Descriptor;
} PFN_ENTRY, *PPFN_ENTRY;

// Memory manager
typedef struct _MM_PFN_LIST {
    struct _DOUBLY_LINKED_LIST ListEntry;       // List Head
    volatile uint64_t Count;                    // Number of pages in this list.
    SPINLOCK PfnListLock;                       // Spinlock for each PFN List to ensure atomicity.
} MM_PFN_LIST;

typedef struct _MM_PFN_DATABASE {
    PPFN_ENTRY PfnEntries;  // Pointer to base of the PFN_ENTRY array.
    size_t TotalPageCount;  // Total count of pages in the PFN database.
    SPINLOCK PfnDatabaseLock; // Global spinlock for adding/popping memory.

    // Page lists
    MM_PFN_LIST FreePageList;   // Pages with garbage data.
    MM_PFN_LIST ZeroedPageList; // Pages pre-filled with zeros for optimization purposes.
    MM_PFN_LIST StandbyPageList; // Clean pages, candidates for reuse. (used for loading processes fast)
    MM_PFN_LIST ModifiedPageList; // Dirty pages, must be written to disk for backing.
    MM_PFN_LIST BadPageList;    // List of bad memory pages

    // Statistics
    volatile size_t AvailablePages; // Free + Zeroed + Standby
    volatile size_t TotalReserved;  // Kernel, drivers, etc.
} MM_PFN_DATABASE;

typedef struct _MMVAD {
    uintptr_t StartVa; // Starting Virtual Page Number.
    uintptr_t EndVa;   // Ending Virtual Page Number.
    uint32_t Flags;     // VAD_FLAGS Bitfield
    
    // VAD Are per process, stored in an AVL.
    struct _MMVAD* LeftChild;
    struct _MMVAD* RightChild;
    struct _MMVAD* Parent;

    // Height of the node in the tree.
    int Height;

    // If VAD_FLAG_MAPPED_FILE bit is set.
    struct _FILE_OBJECT* File;  // Pointer to file object.
    uint64_t FileOffset;    // Offset into the file this region starts in.

    // Pointer to owner process.
    struct _EPROCESS* OwningProcess;
} MMVAD, *PMMVAD;

typedef struct _POOL_HEADER
{
    uint32_t PoolCanary; // Must always be equal to - 'BEKA'
    union
    {
        // When the block is FREE, it's part of a list.
        SINGLE_LINKED_LIST FreeListEntry;

        // When the block is ALLOCATED, we store actual metadata info.
        struct
        {
            uint16_t BlockSize;  // Size of this block
            uint16_t PoolIndex;  // Index of the slab it came from
        };
    } Metadata;
    uint32_t PoolTag; // Tag of pool. (default - 'ADIR')
} POOL_HEADER, * PPOOL_HEADER;

typedef struct _POOL_DESCRIPTOR {
    SINGLE_LINKED_LIST FreeListHead;    // Head of the free list
    size_t BlockSize;                   // The size of the block + header (so if this is a 32 byte slab, it would be (32 + sizeof(POOL_HEADER))
    volatile uint64_t FreeCount;        // Number of blocks on the free list
    volatile uint64_t TotalBlocks;      // Total blocks ever allocated (statistics)
    SPINLOCK PoolLock;                  // Spinlock for this specific pool descriptor.
} POOL_DESCRIPTOR, *PPOOL_DESCRIPTOR;

// ------------------ FUNCTIONS ------------------

extern MM_PFN_DATABASE PfnDatabase; // Database defined in 'pfn.c'

// general functions

// Memory Set.
FORCEINLINE
void* 
kmemset (
    void* dest, int64_t val, uint64_t len
) 
{
    uint8_t* ptr = dest;
    for (size_t i = 0; i < (size_t)len; i++) {
        ptr[i] = (uint8_t)val;
    }
    return dest;
}

// Memory copy  
FORCEINLINE
void* 
kmemcpy(
    void* dest, const void* src, size_t len
) 
{
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < len; i++) d[i] = s[i];
    return dest;
}

FORCEINLINE
int 
kmemcmp(
    const void* s1, const void* s2, size_t n
) 
{
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i])
            return (int)(p1[i] - p2[i]);
    }
    return 0;
}

FORCEINLINE
void
MiReloadTLBs(
    void
)

// Reloads CR3 to flush all TLBs (slow flush)

{
    __write_cr3(__read_cr3());
}

FORCEINLINE
FAULT_OPERATION
MiRetrieveOperationFromErrorCode(
    uint64_t ErrorCode
)

{
    FAULT_OPERATION operation = -1;
    if (ErrorCode & (1 << 4)) {
        operation = ExecuteOperation; // Execute (NX Fault) (nx bit set, and CPU executed an instruction there)
    }
    else if (ErrorCode & 1) {
        operation = WriteOperation; // Write fault (read only page \ not present)
    }
    else {
        operation = ReadOperation; // Read Fault (not present?)
    }
    
    return operation;
}

FORCEINLINE
uint64_t
MiRetrieveLastFaultyAddress(
    void
)

{
    return __read_cr2();
}

// module: pfn.c

MTSTATUS
MiInitializePfnDatabase(
    IN  PBOOT_INFO BootInfo
);

PAGE_INDEX
MiRequestPhysicalPage(
    IN  PFN_STATE ListType
);

void
MiReleasePhysicalPage(
    IN  PAGE_INDEX PfnIndex
);

void*
MmAllocateContigiousMemory(
    IN  size_t NumberOfBytes,
    IN  uint64_t HighestAcceptableAddress
);

void
MmFreeContigiousMemory(
    IN  void* BaseAddress,
    IN  size_t NumberOfBytes
);

// module: map.c

PMMPTE
MiGetPtePointer(
    IN  uintptr_t va
);

PAGE_INDEX
MiTranslatePteToPfn(
    IN  PMMPTE pte
);

uintptr_t
MiTranslateVirtualToPhysical(
    IN void* VirtualAddress
);

void
MiUnmapPte(
    IN  PMMPTE pte
);

bool
MmIsAddressPresent(
    IN  uintptr_t VirtualAddress
);

// module: hypermap.c

void*
MiMapPageInHyperspace(
    IN  uint64_t PfnIndex,
    OUT  PIRQL OldIrql
);

void
MiUnmapHyperSpaceMap(
    IN  IRQL OldIrql
);

// module: pool.c

MTSTATUS
MiInitializePoolSystem(
    void
);

// ONLY NONPAGEDPOOL IMPLEMENTED.
void*
MmAllocatePoolWithTag(
    IN enum _POOL_TYPE PoolType,
    IN  size_t NumberOfBytes,
    IN  uint32_t Tag
);

void
MmFreePool(
    IN  void* buf
);


// module: vad.c

MTSTATUS
MmAllocateVirtualMemory(
    IN PEPROCESS Process,
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t NumberOfBytes,
    IN VAD_FLAGS VadFlags
);

MTSTATUS
// TODO Free with explicit size, split vad if needed.
MmFreeVirtualMemory(
    IN PEPROCESS Process,
    IN void* BaseAddress
);

PMMVAD
MiFindVad(
    IN  PMMVAD Root,
    IN  uintptr_t VirtualAddress
);

uintptr_t
MmFindFreeAddressSpace(
    IN  PEPROCESS Process,
    IN  size_t NumberOfBytes,
    IN  uintptr_t SearchStart,
    IN  uintptr_t SearchEnd    // exclusive
);

// module: va.c

bool
MiInitializePoolVaSpace(
    void
);

uintptr_t
MiAllocatePoolVa(
    IN  POOL_TYPE PoolType,
    IN  size_t NumberOfBytes
);

void
MiFreePoolVaContiguous(
    IN  uintptr_t va,
    IN  size_t NumberOfBytes,
    IN  POOL_TYPE PoolType
);

// module: fault.c

MTSTATUS
MmAccessFault(
    IN  uint64_t FaultBits,
    IN  uint64_t VirtualAddress,
    IN  PRIVILEGE_MODE PreviousMode,
    IN  PTRAP_FRAME TrapFrame
);

bool
MmInvalidAccessAllowed(
    void
);

// module: oom.c

// TODO OOM KILLER, TO USE WHEN 0 PHYSICAL MEMORY IS AVAILABLE, AND PAGING TO DISK EVEN FAILED.

#endif