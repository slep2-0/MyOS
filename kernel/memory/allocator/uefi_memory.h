#ifndef UEFI_MEMORY_H
#define UEFI_MEMORY_H

#include "../../kernel.h"

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
    size_t                    MapSize;        // Changed from UINTN to size_t
    size_t                    DescriptorSize; // Changed from UINTN to size_t
    uint32_t                  DescriptorVersion;
} BOOT_INFO;

// Memory types (we only need ConventionalMemory here)
#define EfiReservedMemoryType          0
#define EfiLoaderCode                  1
#define EfiLoaderData                  2
#define EfiBootServicesCode            3
#define EfiBootServicesData            4
#define EfiRuntimeServicesCode         5
#define EfiRuntimeServicesData         6
#define EfiConventionalMemory          7
// ... other types omitted for brevity

extern BOOT_INFO boot_info_local;

#endif // UEFI_MEMORY_H
