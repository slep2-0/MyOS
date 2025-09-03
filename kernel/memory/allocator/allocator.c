#include "allocator.h"
#include "../../bugcheck/bugcheck.h"
#include "../memory.h"

static uint8_t* frame_bitmap = NULL;
static size_t total_frames = 0;

static inline void set_frame(size_t frame) {
    tracelast_func("set_frame");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    if (frame >= total_frames) {
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        MtBugcheck(&ctx, NULL, FRAME_LIMIT_REACHED, 0, false);
    }
    frame_bitmap[frame / 8] |= (uint8_t)(1 << (frame % 8));
}

static inline void clear_frame(size_t frame) {
    tracelast_func("clear_frame");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    if (frame < total_frames) {
        frame_bitmap[frame / 8] &= (uint8_t)~(1 << (frame % 8));
    }
}

static inline bool test_frame(size_t frame) {
    tracelast_func("test_frame");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    return (frame < total_frames) && (frame_bitmap[frame / 8] & (1 << (frame % 8)));
}

static uint64_t get_total_memory_size(const BOOT_INFO* boot_info) {
    uint64_t highest_addr = 0;

    // Calculate the number of entries in the memory map
    size_t entry_count = boot_info->MapSize / boot_info->DescriptorSize;

    // Get a pointer to the first descriptor
    EFI_MEMORY_DESCRIPTOR* desc = boot_info->MemoryMap;

    for (size_t i = 0; i < entry_count; i++) {
        // Calculate the end address of the current memory region.
        uint64_t region_end = desc->PhysicalStart + (desc->NumberOfPages * FRAME_SIZE);

        // If this region ends at a higher address, update our maximum.
        if (region_end > highest_addr) highest_addr = region_end;

        desc = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)desc + boot_info->DescriptorSize);
    }

    return highest_addr;
}

void frame_bitmap_init(void) {
    tracelast_func("frame_bitmap_init");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);

    // 1. Calculate the total memory size and required bitmap size
    uint64_t total_memory = get_total_memory_size(&boot_info_local);
    total_frames = (total_memory + FRAME_SIZE - 1) / FRAME_SIZE; // round up
    size_t bitmap_size = (total_frames + 7) / 8;

    // 2. Find a place in physical memory for our bitmap
    size_t entry_count = boot_info_local.MapSize / boot_info_local.DescriptorSize;
    EFI_MEMORY_DESCRIPTOR* desc = boot_info_local.MemoryMap;
    uintptr_t bitmap_phys = 0;
    for (size_t i = 0; i < entry_count; ++i) {
        // Look for a usable region that's big enough
        if (classify(desc->Type) && (desc->NumberOfPages * FRAME_SIZE) >= bitmap_size) {
            // We found a spot! Use its physical start address.
            bitmap_phys = desc->PhysicalStart;
            frame_bitmap = (uint8_t*)(bitmap_phys + PHYS_MEM_OFFSET);
            break;
        }
        desc = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)desc + boot_info_local.DescriptorSize);
    }

    if (!bitmap_phys) {
        MtBugcheck(NULL, NULL, STACK_SEGMENT_OVERRUN, 0, false);
        return;
    }

    if (frame_bitmap == NULL) {
        // This is a catastrophic failure. We couldn't find anywhere to put the bitmap.
        MtBugcheck(NULL, NULL, FRAME_BITMAP_CREATION_FAILURE, 0, false);
        return;
    }


    // 3. Initialize the bitmap
    // Mark all frames as used/reserved initially
    kmemset(frame_bitmap, 0xFF, bitmap_size);

    // 4. Mark the bitmap's OWN frames as used! (Crucial!)
    size_t bitmap_pages = (bitmap_size + FRAME_SIZE - 1) / FRAME_SIZE;
    size_t bitmap_base_frame = bitmap_phys / FRAME_SIZE; // physical here
    for (size_t i = 0; i < bitmap_pages; i++) {
        set_frame(bitmap_base_frame + i);
    }

    // 5. Clear usable frames based on the rest of the memory map
    desc = boot_info_local.MemoryMap;
    for (size_t i = 0; i < entry_count; ++i) {
        if (desc->Type == EfiConventionalMemory) {
            uintptr_t base = desc->PhysicalStart;
            uint64_t pages = desc->NumberOfPages;
            for (uint64_t p = 0; p < pages; ++p) {
                size_t frame_idx = (base / FRAME_SIZE) + p;

                // Don't free the frames we are using for the bitmap itself!
                if (frame_idx >= ((uintptr_t)bitmap_phys / FRAME_SIZE) &&
                    frame_idx < (((uintptr_t)bitmap_phys / FRAME_SIZE) + bitmap_pages)) {
                    continue;
                }

                clear_frame(frame_idx);
            }
        }
        desc = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)desc + boot_info_local.DescriptorSize);
    }
}

// Early‐boot frame allocator:
uintptr_t alloc_frame(void) {
    tracelast_func("alloc_frame");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    /// Removed reserved pages use, for safeguarding against memory corruption within the kernel.
    for (size_t frame = 0; frame < total_frames; ++frame) {
        if (!(frame_bitmap[frame / 8] & (1 << (frame % 8)))) {
            // mark and return
            frame_bitmap[frame / 8] |= (1 << (frame % 8));
            return (uintptr_t)(frame * FRAME_SIZE);
        }
    }
    return 0;
}

void free_frame(uintptr_t p) {
    tracelast_func("free_frame");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    size_t frame = (uintptr_t)p / FRAME_SIZE;
    clear_frame(frame);
}
