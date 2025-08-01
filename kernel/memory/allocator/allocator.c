#include "allocator.h"
#include "../../bugcheck/bugcheck.h"
#include "../memory.h"
 
static uint8_t frame_bitmap[MAX_FRAMES / 8];

static inline void set_frame(size_t frame) {
    tracelast_func("set_frame");
    enforce_max_irql(PASSIVE_LEVEL);
    if (frame >= MAX_FRAMES) {
        bugcheck_system(NULL, NULL, FRAME_LIMIT_REACHED, 0, false);
    }
    frame_bitmap[frame / 8] |= (uint8_t)(1 << (frame % 8));
}

static inline void clear_frame(size_t frame) {
    tracelast_func("clear_frame");
    enforce_max_irql(PASSIVE_LEVEL);
    if (frame < MAX_FRAMES) {
        frame_bitmap[frame / 8] &= (uint8_t)~(1 << (frame % 8));
    }
}

static inline bool test_frame(size_t frame) {
    tracelast_func("test_frame");
    enforce_max_irql(PASSIVE_LEVEL);
    return (frame < MAX_FRAMES) && (frame_bitmap[frame / 8] & (1 << (frame % 8)));
}

void frame_bitmap_init(void) {
    tracelast_func("frame_bitmap_init");
    enforce_max_irql(PASSIVE_LEVEL);
    // 1. mark all frames reserved
    kmemset(frame_bitmap, 0xFF, sizeof(frame_bitmap));

    // 2. reserve kernel pages
    uintptr_t k_start = (uintptr_t)&kernel_start;
    uintptr_t k_end = (uintptr_t)&kernel_end;
    size_t first = k_start / FRAME_SIZE;
    size_t last = (k_end + FRAME_SIZE - 1) / FRAME_SIZE;
    for (size_t f = first; f < last; ++f) {
        set_frame(f);
    }

    // 3. clear usable frames from UEFI map
    size_t entry_count = boot_info_local.MapSize / boot_info_local.DescriptorSize;
    for (size_t i = 0; i < entry_count; ++i) {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)
            ((uint8_t*)boot_info_local.MemoryMap + i * boot_info_local.DescriptorSize);
        if (desc->Type == EfiConventionalMemory) {
            uintptr_t base = desc->PhysicalStart;
            uint64_t pages = desc->NumberOfPages;
            for (uint64_t p = 0; p < pages; ++p) {
                size_t frame = (base / FRAME_SIZE) + p;
                if (frame >= first && frame < last) continue;
                clear_frame(frame);
            }
        }
    }
}

static uint8_t* next_pt = (uint8_t*)& __pt_start;

// Early‐boot frame allocator:
void* alloc_frame(void) {
    tracelast_func("alloc_frame");
    enforce_max_irql(PASSIVE_LEVEL);
    // If we still have reserved pages, carve from there
    if (next_pt + FRAME_SIZE <= (uint8_t*)&__pt_end) {
        void* phys = next_pt;
        next_pt += FRAME_SIZE;
        return phys;
    }

    // Otherwise fall back on the bitmap (for heap allocations)
    for (size_t frame = 0; frame < MAX_FRAMES; ++frame) {
        if (!(frame_bitmap[frame / 8] & (1 << (frame % 8)))) {
            // mark and return
            frame_bitmap[frame / 8] |= (1 << (frame % 8));
            return (void*)(frame * FRAME_SIZE);
        }
    }
    return NULL;
}

void free_frame(void* p) {
    tracelast_func("free_frame");
    enforce_max_irql(PASSIVE_LEVEL);
    size_t frame = (uintptr_t)p / FRAME_SIZE;
    clear_frame(frame);
}
