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

static inline uintptr_t align_up(uintptr_t addr, size_t align) {
    // This is a standard and fast way to perform alignment.
    return (addr + align - 1) & ~(align - 1);
}

void frame_bitmap_init(void) {
    tracelast_func("frame_bitmap_init");

    // 1. Calculate total memory and the required size for our bitmap
    uint64_t total_memory = get_total_memory_size(&boot_info_local);
    total_frames = (total_memory + FRAME_SIZE - 1) / FRAME_SIZE;
    size_t bitmap_size = (total_frames + 7) / 8; // 1 bit per frame

    // 2. Find the physical end of the kernel image
    // The linker gives us the VIRTUAL address of kernel_end. We must subtract
    // the higher-half offset to get the corresponding PHYSICAL address.
    uintptr_t kernel_end_phys = (uintptr_t)&kernel_end - PHYS_MEM_OFFSET;

    // The first safe place to put the bitmap is after the kernel, aligned up.
    uintptr_t potential_bitmap_start = align_up(kernel_end_phys, FRAME_SIZE);

    // 3. Find a suitable physical memory region for the bitmap
    uintptr_t bitmap_phys_addr = 0;
    size_t entry_count = boot_info_local.MapSize / boot_info_local.DescriptorSize;
    EFI_MEMORY_DESCRIPTOR* desc = boot_info_local.MemoryMap;

    for (size_t i = 0; i < entry_count; ++i) {
        uintptr_t region_start = desc->PhysicalStart;
        uint64_t region_pages = desc->NumberOfPages;
        uintptr_t region_end = region_start + (region_pages * FRAME_SIZE);

        // First, check the region our kernel is in. This is the best place.
        if (desc->Type == EfiLoaderData) {
            // Check if this region actually contains our kernel and has space after it.
            if (potential_bitmap_start >= region_start && region_end > potential_bitmap_start) {
                uint64_t available_space = region_end - potential_bitmap_start;
                if (available_space >= bitmap_size) {
                    // Perfect! We found space right after the kernel.
                    bitmap_phys_addr = potential_bitmap_start;
                    break; // Stop searching, this is the ideal location.
                }
            }
        }

        // If we haven't found a spot yet, look for any other large enough conventional region.
        if (bitmap_phys_addr == 0 && desc->Type == EfiConventionalMemory) {
            if ((region_pages * FRAME_SIZE) >= bitmap_size) {
                // Ensure this region doesn't conflict with the kernel itself.
                if (region_end <= kernel_end_phys || region_start >= kernel_end_phys) {
                    bitmap_phys_addr = region_start;
                    // We don't break here, because finding space in EfiLoaderData is still preferred.
                    // This just becomes our backup option.
                }
            }
        }

        desc = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)desc + boot_info_local.DescriptorSize);
    }

    // If after checking all memory regions we still have no place, bugcheck.
    if (bitmap_phys_addr == 0) {
        MtBugcheck(NULL, NULL, FRAME_BITMAP_CREATION_FAILURE, 0, false);
        return; // Unreachable
    }

    // 4. Map the physical address to its virtual address and initialize
    frame_bitmap = (uint8_t*)(bitmap_phys_addr + PHYS_MEM_OFFSET);

    // Mark all frames as used (1s) initially.
    kmemset(frame_bitmap, 0xFF, bitmap_size);

    // 5. Mark the bitmap's own frames as used in the bitmap
    // This prevents the allocator from handing out the memory that the bitmap itself uses.
    size_t bitmap_pages = (bitmap_size + FRAME_SIZE - 1) / FRAME_SIZE;
    size_t bitmap_base_frame = bitmap_phys_addr / FRAME_SIZE;
    for (size_t i = 0; i < bitmap_pages; i++) {
        set_frame(bitmap_base_frame + i);
    }

    // 6. Now, clear the bits for all conventional (usable) memory regions
    desc = boot_info_local.MemoryMap;
    for (size_t i = 0; i < entry_count; ++i) {
        if (desc->Type == EfiConventionalMemory) {
            uintptr_t base = desc->PhysicalStart;
            uint64_t pages = desc->NumberOfPages;

            for (uint64_t p = 0; p < pages; ++p) {
                size_t frame_idx = (base / FRAME_SIZE) + p;

                // CRITICAL: Do not free the frames used by the bitmap itself!
                if (frame_idx >= bitmap_base_frame && frame_idx < (bitmap_base_frame + bitmap_pages)) {
                    continue;
                }

                // Also, as a safeguard, don't free the first 1MiB. It's often used by BIOS/legacy hardware.
                if (frame_idx * FRAME_SIZE < 0x100000) {
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
