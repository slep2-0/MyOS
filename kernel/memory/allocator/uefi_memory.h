#ifndef UEFI_MEMORY_H
#define UEFI_MEMORY_H

// Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// global
typedef struct _BLOCK_DEVICE BLOCK_DEVICE;

// Minimal UEFI memory descriptor for kernel use
// Matches UEFI spec EFI_MEMORY_DESCRIPTOR up to PhysicalStart, NumberOfPages, Type

typedef struct _EFI_MEMORY_DESCRIPTOR {
    uint32_t Type;         // What this memory region is used for
    uint32_t Pad;          // Alignment / padding
    uint64_t PhysicalStart; // Physical start address of the region
    uint64_t VirtualStart;  // Virtual start (usually 0 during boot)
    uint64_t NumberOfPages; // Size of the region in pages (usually 4 KB)
    uint64_t Attribute;     // Flags (cacheable, runtime, etc.)
} EFI_MEMORY_DESCRIPTOR;

typedef struct _GOP_PARAMS {
    uint64_t FrameBufferBase;
    uint64_t FrameBufferSize;
    uint32_t Width;               // Visible width in pixels
    uint32_t Height;              // Visible height in pixels
    uint32_t PixelsPerScanLine;   // Actual pixels per row in framebuffer (stride)
} GOP_PARAMS;

typedef struct _BOOT_INFO {
    GOP_PARAMS* Gop;
    EFI_MEMORY_DESCRIPTOR* MemoryMap;
    size_t                    MapSize;       
    size_t                    DescriptorSize;
    uint32_t                  DescriptorVersion;
    size_t                  AhciCount;
    uint64_t*                 AhciBarBases;
    uint64_t KernelStackTop;
    uintptr_t Pml4Phys;
    uint16_t TssSelector;
} BOOT_INFO;
#ifndef _MSC_VER 
_Static_assert(sizeof(BOOT_INFO) == 80, "Size of BOOT_INFO doesn't equal 72 bytes. Update the struct.");
_Static_assert(offsetof(BOOT_INFO, TssSelector) == 0x48, "TssSelector offset is not 0x48");
#endif

// Memory types (we only need ConventionalMemory here)
#define EfiReservedMemoryType          0
#define EfiLoaderCode                  1
#define EfiLoaderData                  2
#define EfiBootServicesCode            3 /// USABLE MEMORY
#define EfiBootServicesData            4 /// USABLE MEMORY
#define EfiRuntimeServicesCode         5
#define EfiRuntimeServicesData         6
#define EfiConventionalMemory          7 /// USABLE MEMORY
// ... other types omitted for brevity

extern BOOT_INFO boot_info_local;

#endif // UEFI_MEMORY_H
