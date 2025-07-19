/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		Dynamic Memory Allocation Header.
 */

#ifndef X86_DYNAMIC_MEM_H
#define X86_DYNAMIC_MEM_H
#include "../../kernel.h"

#ifndef PHYS_MEM_SIZE
 // Total physical memory (upto 3.9GB)
#define PHYS_MEM_SIZE (128 * 1024 * 1024)  // 128 MiB
#endif

 // Dynamic Memory allocation.
#define E820_MAX 32

#ifdef _MSC_VER
#pragma pack(push, 1)
typedef struct _E820_ENTRY {
#else
typedef struct __attribute__((packed)) _E820_ENTRY {
#endif
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_attrs;
} E820_ENTRY;
#ifdef _MSC_VER
#pragma pack(pop)
#endif

/* E820 Explanation: */

// E820 is the memory map provided by the BIOS (via INT 15/EAX=0xE820), telling us which physical ranges are usable ram, and which are reserved.
// We store all of the entries in a small real-mode buffer, then when we get to protected mode (and our C-code here)
// we build a bitmap of all 4KB frames up to our max (PHYS_MEM_SIZE), mark every bit as reserved (1) initially, then clear bits for each E820 "type 1" region.
// Types: 1 - usable ram, 2 - reserved (bios or hardware), 3 - ACPI Reclaimable (acpi tables, we can claim these after parsing them), 4 - ACPI NVS - non volatile sleep data, don't claim, 5 - bad ram - known faulty ram addresses.
// alloc_frame and free_frame scan that bitmap and see whats free, and by so return to us the physical frame.
// however, it's not reliable, as in many times testing the dynamic allocation, it failed and bugchecked me (thank god I implemented bugchecking or it would be a total mess)
// The reason for that, is that E820 gave me the end of the memory, higher than the phys mem size set by the kernel (hardcoded value).
// So if it did that, the functions thought they had more usable memory, so they cleared it, but that gave a page fault, since they went over the usable ram of the machine.

extern uint16_t e820_count;
extern E820_ENTRY e820_buf[E820_MAX];

#define FRAME_SIZE 4096ULL // this is also the page size.
#define MAX_FRAMES ( PHYS_MEM_SIZE / FRAME_SIZE )

static uint8_t frame_bitmap[MAX_FRAMES / 8];
// 1 bit per frame, 0 = free, 1 = reserved.

// Set the frame as reserved.
// Set bit 2 to 1, to mark frame as reserved.
static inline void set_frame(size_t frame) {
    if (frame >= MAX_FRAMES) {
#ifdef _MSC_VER // supress intellisense error.
        bugcheck_system(NULL, SEVERE_MACHINE_CHECK, 0xBADF00D, true);
#else
        bugcheck_system(NULL, 18, 0xBADF00D, true);
#endif
    }
    frame_bitmap[frame / 8] |= (1 << (frame % 8));
}

// Set the frame as free.
// Set bit 2 to 0, to mark frame as free.
static inline void clear_frame(size_t frame) {
    frame_bitmap[frame / 8] &= ~(1 << (frame % 8));
}

// Return true if reserved, false if free.
// Divide the frame gotten by 8 to get byte index, then mask bit 2 to return if it's set or not, bit set - reserved, not set (0) - free frame.
static inline bool test_frame(size_t frame) {
    return frame_bitmap[frame / 8] & (1 << (frame % 8));
}

void frame_bitmap_init(void);
void* alloc_frame(void);
void free_frame(void* p);

#endif