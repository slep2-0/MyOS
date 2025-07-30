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
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
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
} BOOT_INFO;
#ifndef _MSC_VER 
_Static_assert(sizeof(BOOT_INFO) == 56, "Size of BOOT_INFO doesn't equal 56 bytes. Update the struct.");
#endif

// Memory types (we only need ConventionalMemory here)
#define EfiReservedMemoryType          0
#define EfiLoaderCode                  1
#define EfiLoaderData                  2
#define EfiBootServicesCode            3 /// USABLE MEMORY (thank you reactOS)
#define EfiBootServicesData            4 /// USABLE MEMORY (thank you reactOS)
#define EfiRuntimeServicesCode         5
#define EfiRuntimeServicesData         6
#define EfiConventionalMemory          7 /// USABLE MEMORY
// ... other types omitted for brevity

extern BOOT_INFO boot_info_local;

#endif // UEFI_MEMORY_H
