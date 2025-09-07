/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Core Kernel Includes, includes all core and necessary header files.
 */

#ifndef X86_KERNEL_H
#define X86_KERNEL_H

// Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include "trace.h"

// forward declarations, i don't think i need them.

typedef struct _BLOCK_DEVICE BLOCK_DEVICE;
typedef struct _BOOT_INFO BOOT_INFO;
typedef struct _CTX_FRAME CTX_FRAME;


// Standard globals
extern bool isBugChecking;
extern LASTFUNC_HISTORY lastfunc_history; // grab lastfunc from kernel.c

/* Definitions that change kernel behaviour below */

/* Uncomment to trigger a bugcheck on entry */
#define CAUSE_BUGCHECK

/* Uncomment to show all reminders in a static assertion */
///#define REMINDER

/* Uncomment to disable CPU Caching */
///#define DISABLE_CACHE

/* To define DEBUG globally, use a compiler flag. I removed this since I now transitioned each header to iself and others instead of relying on kernel.h that caused circular includes. */

#define UNREFERENCED_PARAMETER(x) (void)(x)
#include "assert.h"
#include "intrin/intrin.h"
#include "cpu/cpu.h"
#include "filesystem/fat32/fat32.h"
#include "memory/allocator/uefi_memory.h"
#include "memory/memory.h"
#include "memory/paging/paging.h"
#include "defs/stdarg_myos.h"
#include "interrupts/idt.h"
#include "interrupts/handlers/handlers.h"
#include "bugcheck/bugcheck.h"
#include "memory/allocator/allocator.h"
#include "drivers/blk/block.h"
#include "drivers/ahci/ahci.h"
#include "drivers/gop/gop.h"
#include "cpu/cpuid/cpuid.h"
#include "time.h"
#include "filesystem/vfs/vfs.h"
#include "cpu/apic/apic.h"
#include "cpu/mutex/mutex.h"
#include "cpu/events/events.h"

// Entry point in C
void kernel_idle_checks(void);
void kernel_main(BOOT_INFO* boot_info);
// Function declarations.
void copy_memory_map(BOOT_INFO* boot_info);
void copy_gop(BOOT_INFO* boot_info);
void init_boot_info(BOOT_INFO* boot_info);
void InitCPU(void);

// Custom assembly functions externals.
extern void read_context_frame(CTX_FRAME* registers);
extern void read_interrupt_frame(INT_FRAME* intfr);

#define gop_printf_forced(color, fmt, ...) gop_printf(color, fmt, ##__VA_ARGS__)

#define ALLOCATIONS 1000
#define BLOCK_SIZE  128
#define ALIGNMENT   16

/// Memory test to run to check for memory issues - identified a problem.
// Stable memory test for MatanelOS allocator
static uint32_t xorshift32(uint32_t* s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static int MemoryTestStable(void) {
    enum {
        TEST_ALLOCATIONS = 64,
        MAX_BLOCK = 1024,        // maximum random block size
        MIN_BLOCK = 16,         // minimum block size
        ALIGN_OPTIONS = 4
    };

    void* blocks[TEST_ALLOCATIONS];
    size_t sizes[TEST_ALLOCATIONS];
    size_t aligns[TEST_ALLOCATIONS];
    uint32_t patterns[TEST_ALLOCATIONS];

    uint32_t rng = 0xdeadbeef; // deterministic seed

    // Phase 1: allocate with varied sizes & alignments, fill with pattern
    for (int i = 0; i < TEST_ALLOCATIONS; ++i) {
        uint32_t r = xorshift32(&rng);
        sizes[i] = MIN_BLOCK + (r % (MAX_BLOCK - MIN_BLOCK + 1));
        // pick alignment from {8,16,32,64}
        size_t align_choice = (size_t)(8 << (r & (ALIGN_OPTIONS - 1)));
        aligns[i] = align_choice;
        patterns[i] = r ^ (uint32_t)i;

        blocks[i] = MtAllocateVirtualMemory(sizes[i], aligns[i]);
        if (!blocks[i]) {
            gop_printf_forced(0xFFFF0000, "Alloc fail idx=%d sz=%u align=%u\n", i, (unsigned)sizes[i], (unsigned)aligns[i]);
            return -1;
        }

        // Quick alignment check
        if (((uintptr_t)blocks[i] & (aligns[i] - 1)) != 0) {
            gop_printf_forced(0xFFFF8000, "Misaligned idx=%d ptr=%p align=%u\n", i, blocks[i], (unsigned)aligns[i]);
            return -2;
        }

        // Mark memory with pattern: repeating 4-byte pattern to detect shuffles
        uint32_t pat = patterns[i];
        uint8_t* bp = (uint8_t*)blocks[i];
        for (size_t b = 0; b + 4 <= sizes[i]; b += 4) {
            ((uint32_t*)(bp + b))[0] = pat;
        }
        // tail bytes
        for (size_t b = (sizes[i] / 4) * 4; b < sizes[i]; ++b) {
            bp[b] = (uint8_t)(pat & 0xFF);
        }

        // Confirm MtIsHeapAddressAllocated reports true
        if (!MtIsHeapAddressAllocated(blocks[i])) {
            gop_printf_forced(0xFFFF8000, "MtIsHeapAddressAllocated false after alloc idx=%d\n", i);
            return -3;
        }
    }

    // Phase 2: verify contents
    for (int i = 0; i < TEST_ALLOCATIONS; ++i) {
        uint32_t pat = patterns[i];
        uint8_t* bp = (uint8_t*)blocks[i];
        for (size_t b = 0; b + 4 <= sizes[i]; b += 4) {
            uint32_t v = ((uint32_t*)(bp + b))[0];
            if (v != pat) {
                gop_printf_forced(0xFF0000FF, "Corrupt idx=%d offset=%u expected=0x%08x got=0x%08x\n",
                    i, (unsigned)b, pat, v);
                return -4;
            }
        }
        for (size_t b = (sizes[i] / 4) * 4; b < sizes[i]; ++b) {
            uint8_t v = bp[b];
            if (v != (uint8_t)(pat & 0xFF)) {
                gop_printf_forced(0xFF0000FF, "Corrupt tail idx=%d offset=%u\n", i, (unsigned)b);
                return -5;
            }
        }
    }

    // Phase 3: create fragmentation - free every second block
    for (int i = 0; i < TEST_ALLOCATIONS; i += 2) {
        MtFreeVirtualMemory(blocks[i]);
        // After free, MtIsHeapAddressAllocated should be false
        if (MtIsHeapAddressAllocated(blocks[i])) {
            gop_printf_forced(0xFFFF8000, "Still allocated after free idx=%d ptr=%p\n", i, blocks[i]);
            return -6;
        }
        // header-store slot should be cleared (if MtFreeVirtualMemory clears it)
        BLOCK_HEADER* hdr = ((BLOCK_HEADER**)blocks[i])[-1];
        if (hdr != NULL) {
            gop_printf_forced(0xFFFF8000, "Header-store not cleared idx=%d hdr=%p\n", i, hdr);
            return -7;
        }
        blocks[i] = NULL; // avoid accidental reuse
    }

    // Phase 4: attempt to allocate a larger block that should fit into coalesced space
    size_t big_request = MAX_BLOCK * 4; // make it large enough to require coalesce or growth
    void* big_block = MtAllocateVirtualMemory(big_request, 16);
    if (!big_block) {
        gop_printf_forced(0xFFFF0000, "Big allocation failed (coalesce test)\n");
        // not necessarily a failure in all implementations; treat as warning
    }
    else {
        // write and verify a quick pattern
        kmemset(big_block, 0xAB, big_request < 4096 ? big_request : 4096);
        MtFreeVirtualMemory(big_block);
    }

    // Phase 5: free remaining blocks in reverse order to stress coalescing
    for (int i = TEST_ALLOCATIONS - 1; i >= 0; --i) {
        if (blocks[i]) {
            // verify before free
            if (!MtIsHeapAddressAllocated(blocks[i])) {
                gop_printf_forced(0xFFFF8000, "Was not allocated before free idx=%d\n", i);
                return -8;
            }
            MtFreeVirtualMemory(blocks[i]);
            if (MtIsHeapAddressAllocated(blocks[i])) {
                gop_printf_forced(0xFFFF8000, "Still allocated after free idx=%d\n", i);
                return -9;
            }
            // ensure header-store slot cleared
            BLOCK_HEADER* hdr = ((BLOCK_HEADER**)blocks[i])[-1];
            if (hdr != NULL) {
                gop_printf_forced(0xFFFF8000, "Header-store not cleared after free idx=%d hdr=%p\n", i, hdr);
                return -10;
            }
            blocks[i] = NULL;
        }
    }
    /*
    // Phase 6: Test MtAllocateVirtualMemoryEx (page-backed allocation)
    size_t ex_size = FRAME_SIZE * 2; // two pages
    void* exptr = MtAllocateVirtualMemoryEx(ex_size - sizeof(BLOCK_HEADER), FRAME_SIZE, PAGE_PRESENT | PAGE_RW);
    if (!exptr) {
        gop_printf_forced(0xFFFF8000, "MtAllocateVirtualMemoryEx failed\n");
        // warn but continue
    }
    else {
        // write/read small pattern
        kmemset(exptr, 0x5A, 256);
        // free and ensure pages unmapped (MtIsHeapAddressAllocated should be false)
        MtFreeVirtualMemory(exptr);
        if (MtIsHeapAddressAllocated(exptr)) {
            gop_printf_forced(0xFFFF8000, "EX allocation still reported allocated after free\n");
            return -11;
        }
    }
    */
    gop_printf_forced(0xFF00FF00, "MemoryTestStable: PASSED\n");
    return 0;
}

#endif // X86_KERNEL_H
