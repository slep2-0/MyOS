#include "allocator.h"
#include "../../bugcheck/bugcheck.h"
#include "../memory.h"
 
static uint8_t frame_bitmap[MAX_FRAMES / 8];

static inline void set_frame(size_t frame) {
    tracelast_func("set_frame");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    if (frame >= MAX_FRAMES) {
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
    if (frame < MAX_FRAMES) {
        frame_bitmap[frame / 8] &= (uint8_t)~(1 << (frame % 8));
    }
}

static inline bool test_frame(size_t frame) {
    tracelast_func("test_frame");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    return (frame < MAX_FRAMES) && (frame_bitmap[frame / 8] & (1 << (frame % 8)));
}

void frame_bitmap_init(void) {
    tracelast_func("frame_bitmap_init");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
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
        if (classify(desc->Type)) {
            uintptr_t base = desc->PhysicalStart;
            uint64_t pages = desc->NumberOfPages;
            for (uint64_t p = 0; p < pages; ++p) {
                size_t frame = (base / FRAME_SIZE) + p;
                if (frame >= first && frame < last) continue;
                clear_frame(frame);
            }
        }
    }

    // 4. reserve the page‑tables region itself
    uintptr_t pt_base = (uintptr_t)&__pt_start;
    uintptr_t pt_end = (uintptr_t)&__pt_end;
    size_t    first_pt_frame = pt_base / FRAME_SIZE;
    size_t    last_pt_frame = (pt_end + FRAME_SIZE - 1) / FRAME_SIZE;
    for (size_t f = first_pt_frame; f < last_pt_frame; ++f) {
        set_frame(f);
    }
}

// Early‐boot frame allocator:
uintptr_t alloc_frame(void) {
    tracelast_func("alloc_frame");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    /// Removed reserved pages use, for safeguarding against memory corruption within the kernel.
    for (size_t frame = 0; frame < MAX_FRAMES; ++frame) {
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
