/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Dynamic Memory Allocation Implementation
 */
#include "allocator.h"

uint16_t e820_count = 0;
E820_ENTRY e820_buf[E820_MAX];

void set_frame(size_t frame) {
    if (frame >= MAX_FRAMES) {
#ifdef _MSC_VER // supress intellisense error.
        bugcheck_system(NULL, SEVERE_MACHINE_CHECK, 0xBADF00D, true);
#else
        bugcheck_system(NULL, 18, 0xBADF00D, true);
#endif
    }
    frame_bitmap[frame / 8] |= (uint8_t)((1 << (frame % 8)));
}

void* alloc_frame(void) {
    for (size_t frame = 0; frame < MAX_FRAMES; frame++) {
        if (!test_frame(frame)) {
            set_frame(frame);
            return (void*)((uintptr_t)frame * (uintptr_t)FRAME_SIZE);
        }
    }
    return NULL;
}

void free_frame(void* p) {
    size_t frame = ((uintptr_t)p) / FRAME_SIZE;
    clear_frame(frame);
}

void frame_bitmap_init(void) {
    // mark *all* of the frames as reserved initially.
    kmemset(frame_bitmap, 0xFF, sizeof(frame_bitmap));

    // for each e820 entry of type 1 (usable memory), clear those bits.
    // First reserve kernel pages
    uintptr_t kernelstart = (uintptr_t)&kernel_start;
    uintptr_t kernelend = (uintptr_t)&kernel_end;

    size_t frame_zero = kernelstart / FRAME_SIZE;
    size_t frame_end = (kernelend + FRAME_SIZE - 1) / FRAME_SIZE;
    for (size_t frame = frame_zero; frame < frame_end; frame++) {
        if (frame >= MAX_FRAMES) {
#ifdef _MSC_VER
            bugcheck_system(NULL, SEVERE_MACHINE_CHECK, 0xCAFEBABE, true);
#else
            bugcheck_system(NULL, 18, 0xCAFEBABE, true);
#endif
        }
        set_frame(frame);
    }

    // Now mark usable memory
    for (int i = 0; i < e820_count; i++) {
        E820_ENTRY e = e820_buf[i];
        if (e.type != 1) continue;

        uint32_t start = (uint32_t)e.base / FRAME_SIZE;
        uint32_t end = (uint32_t)(e.base + e.length) / FRAME_SIZE;

        // i hate e820 protocol, it reported more memory than max frames, so i got bugchecks over and over for page faults, until i setup safe guards to bugcheck if its over max frames, then I set it so if the end is higher than max frames, just cap it.
        // smh.
        if (end > MAX_FRAMES) {
            end = MAX_FRAMES;
        }

        for (uint32_t frame = start; frame < end; frame++) {
            if (frame >= MAX_FRAMES) {
#ifdef _MSC_VER
                bugcheck_system(NULL, SEVERE_MACHINE_CHECK, 0xDEADBEEF, true);
#else
                bugcheck_system(NULL, 18, 0xDEADBEEF, true);
#endif
            }
            // DO NOT TAMPER WITH KERNEL FRAMES.
            if (frame >= frame_zero && frame < frame_end) {
                continue;
            }
            if (frame < MAX_FRAMES) {
                clear_frame(frame); // this won’t undo kernel frames, since they're already marked reserved
            }
        }
    }
}