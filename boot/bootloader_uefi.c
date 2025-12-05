#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h> // AsmWriteCr3
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PciIo.h>
#include <Guid/FileInfo.h>
#include <Guid/Acpi.h>
#include <IndustryStandard/Pci.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_SUBCLASS_MASS_STORAGE_SATA 0x06 
#define PCI_PROGIF_AHCI 0x01 

#ifdef __INTELLISENSE__
#define __asm__ __asm
#endif

#define SELF_REF_IDX 0x1FF 

static void* kmemcpy(void* dest, const void* src, size_t len) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < len; i++) d[i] = s[i];
    return dest;
}

// --- STRUCTS ---

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint32_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed)) TSS;

TSS gTss;
EFI_PHYSICAL_ADDRESS DoubleFaultStackPhys;
EFI_PHYSICAL_ADDRESS PageFaultStackPhys;
EFI_PHYSICAL_ADDRESS TimerStackPhys;
EFI_PHYSICAL_ADDRESS IpiStackPhys;

typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 Width;
    UINT32 Height;
    UINT32 PixelsPerScanLine;
} GOP_PARAMS;

// MODIFIED: Embedded structs instead of pointers to prevent mapping crashes
typedef struct {
    GOP_PARAMS Gop; // Embedded!

    EFI_MEMORY_DESCRIPTOR* MemoryMap;
    UINTN MapSize;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;

    UINTN AhciCount;
    UINT64 AhciBarBases[32]; // Fixed size array, no pointer!

    UINT64 KernelStackTop;
    uintptr_t Pml4Phys;
    uint16_t TssSelector;
    uintptr_t AcpiRsdpPhys;
} BOOT_INFO;

// ELF definitions
#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef struct {
    UINT8 e_ident[16];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
} Elf64_Phdr;

// --- HELPERS ---

static inline UINT64 round_down64(UINT64 x, UINT64 a) { return x & ~(a - 1); }
static inline UINT64 round_up64(UINT64 x, UINT64 a) { return (x + (a - 1)) & ~(a - 1); }

EFI_GUID gEfiFileInfoGuid = EFI_FILE_INFO_ID;

#define PTE_PRESENT (1ULL << 0)
#define PTE_RW (1ULL << 1)
#define PTE_PS (1ULL << 7)

#define PAGE_SIZE_4K 0x1000ULL
#define KERNEL_VA_START 0xfffff80000000000ULL
#define PHYS_MEM_OFFSET 0xffff880000000000ULL

static inline UINTN IDX_PML4(UINT64 va) { return (va >> 39) & 0x1FF; }
static inline UINTN IDX_PDPT(UINT64 va) { return (va >> 30) & 0x1FF; }
static inline UINTN IDX_PD(UINT64 va) { return (va >> 21) & 0x1FF; }
static inline UINTN IDX_PT(UINT64 va) { return (va >> 12) & 0x1FF; }

static UINT64* Pml4Virt = NULL;

static VOID* alloc_page_table_page(VOID) {
    EFI_PHYSICAL_ADDRESS a = 0;
    EFI_STATUS s = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &a);
    if (EFI_ERROR(s)) return NULL;
    VOID* v = (VOID*)(UINTN)a;
    ZeroMem(v, 0x1000);
    return v;
}

STATIC EFI_STATUS ensure_pml4(void) {
    if (Pml4Virt != NULL) return EFI_SUCCESS;
    VOID* v = alloc_page_table_page();
    if (!v) return EFI_OUT_OF_RESOURCES;
    Pml4Virt = (UINT64*)v;
    return EFI_SUCCESS;
}

STATIC EFI_STATUS FindAcpiRsdp(OUT uintptr_t* RsdpAddress) {
    EFI_GUID Acpi20Guid = gEfiAcpi20TableGuid;
    for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
        EFI_GUID* g = &gST->ConfigurationTable[i].VendorGuid;
        if (CompareGuid(g, &Acpi20Guid)) {
            if (gST->ConfigurationTable[i].VendorTable != NULL) {
                *RsdpAddress = (uintptr_t)gST->ConfigurationTable[i].VendorTable;
                return EFI_SUCCESS;
            }
        }
    }
    return EFI_NOT_FOUND;
}

static void commit_page_tables_and_load_cr3(void) {
    if (!Pml4Virt) return;
    UINT64 phys = (UINT64)(UINTN)Pml4Virt;
    AsmWriteCr3(phys);
}

static EFI_STATUS ensure_next_table(UINT64* parent_table, UINTN idx, UINT64** next_table_vaddr) {
    if (!(parent_table[idx] & PTE_PRESENT)) {
        VOID* new_table = alloc_page_table_page();
        if (!new_table) return EFI_OUT_OF_RESOURCES;
        UINT64 phys_addr = (UINT64)(UINTN)new_table;
        parent_table[idx] = phys_addr | PTE_PRESENT | PTE_RW;
        *next_table_vaddr = new_table;
    }
    else {
        *next_table_vaddr = (UINT64*)(UINTN)(parent_table[idx] & ~0xFFFULL);
    }
    return EFI_SUCCESS;
}

STATIC EFI_STATUS map_page_4k(UINT64 virt, UINT64 phys, UINT64 pte_flags) {
    EFI_STATUS Status = ensure_pml4();
    if (EFI_ERROR(Status)) return Status;

    UINT64* pml4 = Pml4Virt;
    UINT64* pdpt; UINT64* pd; UINT64* pt;

    Status = ensure_next_table(pml4, IDX_PML4(virt), &pdpt);
    if (EFI_ERROR(Status)) return Status;

    Status = ensure_next_table(pdpt, IDX_PDPT(virt), &pd);
    if (EFI_ERROR(Status)) return Status;

    Status = ensure_next_table(pd, IDX_PD(virt), &pt);
    if (EFI_ERROR(Status)) return Status;

    pt[IDX_PT(virt)] = (phys & ~0xFFFULL) | (pte_flags & ~PTE_PS) | PTE_PRESENT;
    return EFI_SUCCESS;
}

static EFI_STATUS map_range(UINTN StartAddrPhys, UINTN EndAddrPhys, UINTN VirtBase, UINT64 flags) {
    UINTN p = StartAddrPhys;
    UINTN v = VirtBase;
    for (; p < EndAddrPhys; p += PAGE_SIZE_4K, v += PAGE_SIZE_4K) {
        EFI_STATUS s = map_page_4k((UINT64)v, (UINT64)p, flags);
        if (EFI_ERROR(s)) return s;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS map_range_identity(UINTN StartAddr, UINTN EndAddr, UINT64 flags) {
    UINTN p = StartAddr;
    for (; p < EndAddr; p += PAGE_SIZE_4K) {
        EFI_STATUS s = map_page_4k((UINT64)p, (UINT64)p, flags);
        if ((EFI_ERROR(s))) return s;
    }
    return EFI_SUCCESS;
}

static UINT64 get_elf_entry_if_present(VOID* buf, UINTN size) {
    if (size < 0x40) return 0;
    unsigned char* b = (unsigned char*)buf;
    if (b[0] == 0x7f && b[1] == 'E' && b[2] == 'L' && b[3] == 'F' && b[4] == 2) {
        return *(UINT64*)((UINT8*)buf + 0x18);
    }
    return 0;
}

STATIC EFI_STATUS map_elf_segments(VOID* KernelBuffer, UINTN FileSize) {
    Elf64_Ehdr* eh = (Elf64_Ehdr*)KernelBuffer;
    if (!(eh->e_ident[0] == 0x7f && eh->e_ident[1] == 'E' && eh->e_ident[2] == 'L' && eh->e_ident[3] == 'F' && eh->e_ident[4] == 2)) {
        return map_range((UINTN)KernelBuffer, (UINTN)KernelBuffer + FileSize, (UINTN)KERNEL_VA_START, PTE_PRESENT | PTE_RW);
    }

    Elf64_Phdr* ph = (Elf64_Phdr*)((UINT8*)eh + eh->e_phoff);
    for (UINT16 i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;

        UINT64 seg_vstart = ph[i].p_vaddr;
        UINT64 seg_memsz = ph[i].p_memsz;
        UINT64 seg_filesz = ph[i].p_filesz;
        UINT64 seg_off = ph[i].p_offset;

        UINT64 page_vstart = round_down64(seg_vstart, PAGE_SIZE_4K);
        UINT64 page_vend = round_up64(seg_vstart + seg_memsz, PAGE_SIZE_4K);

        UINT64 flags = PTE_PRESENT;
        if (ph[i].p_flags & PF_W) flags |= PTE_RW;

        for (UINT64 v = page_vstart; v < page_vend; v += PAGE_SIZE_4K) {
            EFI_PHYSICAL_ADDRESS phys_page = 0;
            gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &phys_page);
            VOID* dest = (VOID*)(UINTN)phys_page;
            ZeroMem(dest, PAGE_SIZE_4K);

            INT64 seg_page_off = (INT64)v - (INT64)seg_vstart;
            UINT64 copy_len = 0;
            UINT64 copy_src_off = 0;
            UINT64 copy_dest_off = 0;

            if (seg_page_off < 0) {
                copy_dest_off = (UINT64)(-seg_page_off);
                if (seg_filesz > copy_dest_off) copy_len = seg_filesz - copy_dest_off;
                if (copy_len > PAGE_SIZE_4K - copy_dest_off) copy_len = PAGE_SIZE_4K - copy_dest_off;
                copy_src_off = seg_off;
            }
            else {
                if ((UINT64)seg_page_off < seg_filesz) {
                    copy_src_off = seg_off + (UINT64)seg_page_off;
                    UINT64 rem = seg_filesz - (UINT64)seg_page_off;
                    copy_len = (rem > PAGE_SIZE_4K) ? PAGE_SIZE_4K : rem;
                }
            }

            if (copy_len > 0) CopyMem((UINT8*)dest + copy_dest_off, (UINT8*)KernelBuffer + copy_src_off, copy_len);
            map_page_4k(v, (UINT64)phys_page, flags);
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS patch_kernel_image_with_tss(void* kernel_buf, UINTN kernel_size,
    uint64_t tss_base, uint32_t tss_limit,
    uint16_t* out_selector) {
    if (!kernel_buf || kernel_size < 8) return EFI_INVALID_PARAMETER;
    const uint64_t pattern[5] = { 0, 0x00AF9A000000FFFFULL, 0x00CF92000000FFFFULL, 0x00AFFA000000FFFFULL, 0x00CFF2000000FFFFULL };

    uint8_t* scan = (uint8_t*)kernel_buf;
    for (uint8_t* p = scan; p + 40 <= scan + kernel_size; p += 8) {
        bool match = true;
        for (int i = 0; i < 5; ++i) {
            uint64_t val;
            kmemcpy(&val, p + i * 8, sizeof(uint64_t));
            if (val != pattern[i]) { match = false; break; }
        }
        if (!match) continue;

        uint64_t low = ((uint64_t)(tss_limit & 0xFFFFULL)) | ((uint64_t)(tss_base & 0xFFFFFFULL) << 16) | (0x0089ULL << 40) | ((uint64_t)(tss_limit & 0xF0000ULL) << 32) | ((uint64_t)(tss_base & 0xFF000000ULL) << 32);
        uint64_t high = (uint64_t)(tss_base >> 32);

        kmemcpy(p + 40, &low, 8);
        kmemcpy(p + 48, &high, 8);
        *out_selector = (uint16_t)(5 * 8);
        return EFI_SUCCESS;
    }
    return EFI_NOT_FOUND;
}

// Helper to prevent the "Rug Pull" crash
STATIC EFI_STATUS MapUefiMemory(EFI_MEMORY_DESCRIPTOR* Map, UINTN MapSize, UINTN DescriptorSize) {
    EFI_MEMORY_DESCRIPTOR* d = Map;
    for (UINTN i = 0; i < MapSize / DescriptorSize; i++) {
        // Map everything except free conventional memory.
        // This ensures the Stack, the Loader Code, and runtime data are present in the new table.
        if (d->Type != EfiConventionalMemory && d->Type != EfiUnusableMemory && d->NumberOfPages > 0) {
            UINT64 Start = round_down64(d->PhysicalStart, PAGE_SIZE_4K);
            UINT64 End = round_up64(d->PhysicalStart + (d->NumberOfPages * PAGE_SIZE_4K), PAGE_SIZE_4K);
            map_range_identity((UINTN)Start, (UINTN)End, PTE_PRESENT | PTE_RW);
        }
        d = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)d + DescriptorSize);
    }
    return EFI_SUCCESS;
}

// --- MAIN ---

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable) {
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop;
    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* SimpleFS;
    EFI_FILE_PROTOCOL* Root, * File;
    EFI_FILE_INFO* FileInfo;
    UINTN FileSize;
    EFI_PHYSICAL_ADDRESS KernelAddress = 0x100000;
    VOID* KernelBuffer;

    // 1) Load GOP
    Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&Gop);
    if (EFI_ERROR(Status)) return Status;

    GOP_PARAMS GopParamsLocal;
    GopParamsLocal.FrameBufferBase = Gop->Mode->FrameBufferBase;
    GopParamsLocal.FrameBufferSize = Gop->Mode->FrameBufferSize;
    GopParamsLocal.Width = Gop->Mode->Info->HorizontalResolution;
    GopParamsLocal.Height = Gop->Mode->Info->VerticalResolution;
    GopParamsLocal.PixelsPerScanLine = Gop->Mode->Info->PixelsPerScanLine;

    // 2) Load File
    gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage);
    gBS->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&SimpleFS);
    SimpleFS->OpenVolume(SimpleFS, &Root);
    Root->Open(Root, &File, L"kernel.elf", EFI_FILE_MODE_READ, 0);

    UINTN FileInfoSize = sizeof(EFI_FILE_INFO) + 512;
    FileInfo = AllocateZeroPool(FileInfoSize);
    File->GetInfo(File, &gEfiFileInfoGuid, &FileInfoSize, FileInfo);
    FileSize = FileInfo->FileSize;
    FreePool(FileInfo);

    gBS->AllocatePages(AllocateAddress, EfiLoaderData, EFI_SIZE_TO_PAGES(FileSize), &KernelAddress);
    KernelBuffer = (VOID*)(UINTN)KernelAddress;
    File->Read(File, &FileSize, KernelBuffer);
    File->Close(File);

    // 3) AHCI Scan
    UINTN handleCount;
    EFI_HANDLE* handles;
    UINTN ahciCount = 0;
    UINT64 barBases[32]; // Local buffer
    gBS->LocateHandleBuffer(ByProtocol, &gEfiPciIoProtocolGuid, NULL, &handleCount, &handles);

    for (UINTN i = 0; i < handleCount; i++) {
        EFI_PCI_IO_PROTOCOL* pciIo;
        if (!EFI_ERROR(gBS->HandleProtocol(handles[i], &gEfiPciIoProtocolGuid, (VOID**)&pciIo))) {
            UINT8 class_code[3];
            pciIo->Pci.Read(pciIo, EfiPciIoWidthUint8, PCI_CLASSCODE_OFFSET, 3, &class_code);
            if (class_code[2] == PCI_CLASS_MASS_STORAGE && class_code[1] == PCI_SUBCLASS_MASS_STORAGE_SATA && class_code[0] == PCI_PROGIF_AHCI) {
                UINT32 bar5_low = 0, bar5_high = 0;
                pciIo->Pci.Read(pciIo, EfiPciIoWidthUint32, 0x24, 1, &bar5_low);
                if ((bar5_low & 0x06) == 0x04) pciIo->Pci.Read(pciIo, EfiPciIoWidthUint32, 0x28, 1, &bar5_high);
                if (ahciCount < 32) barBases[ahciCount++] = ((UINT64)bar5_high << 32) | (bar5_low & ~0x0F);
            }
        }
    }
    FreePool(handles);

    // 4) Allocations (Stacks & TSS)
    EFI_PHYSICAL_ADDRESS StackPhysBase = 0;
    const UINTN StackPages = 8;
    gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, StackPages, &StackPhysBase);
    const UINT64 StackVirtTop = StackPhysBase + PHYS_MEM_OFFSET + (StackPages * PAGE_SIZE_4K);

    gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &DoubleFaultStackPhys);
    gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &PageFaultStackPhys);
    gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &TimerStackPhys);
    gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &IpiStackPhys);

    // 5) Build Mappings
    map_range((UINTN)StackPhysBase, (UINTN)(StackPhysBase + StackPages * PAGE_SIZE_4K), (UINTN)(StackPhysBase + PHYS_MEM_OFFSET), PTE_PRESENT | PTE_RW);
    map_range_identity((UINTN)&gTss, (UINTN)&gTss + sizeof(TSS), PTE_PRESENT | PTE_RW);
    map_range_identity((UINTN)DoubleFaultStackPhys, (UINTN)DoubleFaultStackPhys + 4096, PTE_PRESENT | PTE_RW);
    map_range_identity((UINTN)PageFaultStackPhys, (UINTN)PageFaultStackPhys + 4096, PTE_PRESENT | PTE_RW);
    map_range_identity((UINTN)TimerStackPhys, (UINTN)TimerStackPhys + 4096, PTE_PRESENT | PTE_RW);
    map_range_identity((UINTN)IpiStackPhys, (UINTN)IpiStackPhys + 4096, PTE_PRESENT | PTE_RW);

    // 6) TSS Init
    ZeroMem(&gTss, sizeof(TSS));
    gTss.ist[0] = (uint64_t)PageFaultStackPhys + 4096;
    gTss.ist[1] = (uint64_t)DoubleFaultStackPhys + 4096;
    gTss.ist[2] = (uint64_t)TimerStackPhys + 4096;
    gTss.ist[3] = (uint64_t)IpiStackPhys + 4096;

    uint16_t selector = 0;
    patch_kernel_image_with_tss(KernelBuffer, FileSize, (uint64_t)(uintptr_t)&gTss, sizeof(TSS), &selector);

    // 7) Map Kernel & PHYS_MEM_OFFSET
    map_elf_segments(KernelBuffer, FileSize);
    map_range(0x10000, 0x100000000ULL, PHYS_MEM_OFFSET + 0x10000, PTE_PRESENT | PTE_RW);

    if (GopParamsLocal.FrameBufferSize > 0) {
        map_range_identity(round_down64(GopParamsLocal.FrameBufferBase, PAGE_SIZE_4K),
            round_up64(GopParamsLocal.FrameBufferBase + GopParamsLocal.FrameBufferSize, PAGE_SIZE_4K),
            PTE_PRESENT | PTE_RW);
    }

    uintptr_t acpi_rsdp_addr = 0;
    FindAcpiRsdp(&acpi_rsdp_addr);

    // --- EXIT BOOT SERVICES SEQUENCE ---

    // A) Allocate BootInfo
    EFI_PHYSICAL_ADDRESS BootInfoPhys = 0;
    gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &BootInfoPhys);
    BOOT_INFO* BootInfo = (BOOT_INFO*)(UINTN)BootInfoPhys;

    // Fill BootInfo (By Value)
    BootInfo->Gop = GopParamsLocal; // Copy Struct
    BootInfo->AhciCount = ahciCount;
    for (UINTN i = 0; i < 32; i++) BootInfo->AhciBarBases[i] = (i < ahciCount) ? barBases[i] : 0;
    BootInfo->KernelStackTop = StackVirtTop;
    BootInfo->Pml4Phys = (UINT64)(UINTN)Pml4Virt;
    BootInfo->TssSelector = selector;
    BootInfo->AcpiRsdpPhys = acpi_rsdp_addr;

    // B) Allocate Final Map Buffer (With huge padding)
    UINTN FinalMapSize = 0, MapKey, FinalDescriptorSize;
    UINT32 FinalDescriptorVersion;
    gBS->GetMemoryMap(&FinalMapSize, NULL, &MapKey, &FinalDescriptorSize, &FinalDescriptorVersion);
    FinalMapSize += PAGE_SIZE_4K * 16; // 64KB Padding

    EFI_PHYSICAL_ADDRESS MemMapPhys = 0;
    gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, EFI_SIZE_TO_PAGES(FinalMapSize), &MemMapPhys);
    EFI_MEMORY_DESCRIPTOR* FinalMemMap = (EFI_MEMORY_DESCRIPTOR*)(UINTN)MemMapPhys;

    // C) Map all UEFI Memory (The "Rug Pull" Fix)
    // We grab the map one last time to ensure we catch the BootInfo and FinalMemMap allocations we just made.
    UINTN TempMapSize = FinalMapSize;
    gBS->GetMemoryMap(&TempMapSize, FinalMemMap, &MapKey, &FinalDescriptorSize, &FinalDescriptorVersion);

    // Identity map all valid regions. 
    // This is SAFE because we are NOT inside the ExitBootServices loop yet.
    MapUefiMemory(FinalMemMap, TempMapSize, FinalDescriptorSize);

    // Map the BootInfo and Map buffer specifically just to be paranoid
    map_range_identity((UINTN)BootInfo, (UINTN)BootInfo + sizeof(BOOT_INFO), PTE_PRESENT | PTE_RW);
    map_range_identity((UINTN)FinalMemMap, (UINTN)FinalMemMap + FinalMapSize, PTE_PRESENT | PTE_RW);

    // Recursion
    Pml4Virt[SELF_REF_IDX] = (UINT64)(UINTN)Pml4Virt | PTE_PRESENT | PTE_RW;

    // D) Exit Boot Services Loop
    // NO ALLOCATIONS ALLOWED HERE.
    Status = EFI_INVALID_PARAMETER;
    while (EFI_ERROR(Status)) {
        TempMapSize = FinalMapSize;
        Status = gBS->GetMemoryMap(&TempMapSize, FinalMemMap, &MapKey, &FinalDescriptorSize, &FinalDescriptorVersion);
        if (EFI_ERROR(Status)) break;
        Status = gBS->ExitBootServices(ImageHandle, MapKey);
    }

    if (EFI_ERROR(Status)) {
        for (;;) __asm__ volatile("hlt");
    }

    // E) Finalize BootInfo
    BootInfo->MemoryMap = FinalMemMap;
    BootInfo->MapSize = TempMapSize;
    BootInfo->DescriptorSize = FinalDescriptorSize;
    BootInfo->DescriptorVersion = FinalDescriptorVersion;

    // F) Jump
    commit_page_tables_and_load_cr3();

    UINT64 entry_va = get_elf_entry_if_present(KernelBuffer, FileSize);
    if (entry_va == 0) entry_va = KERNEL_VA_START;

    typedef void (*KERNEL_ENTRY)(BOOT_INFO*);
    KERNEL_ENTRY KernelEntry = (KERNEL_ENTRY)(UINTN)entry_va;

    KernelEntry(BootInfo);

    for (;;) { __asm__ volatile ("hlt"); }
    return EFI_SUCCESS;
}