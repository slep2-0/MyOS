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
#include <IndustryStandard/Pci.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>


#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_SUBCLASS_MASS_STORAGE_SATA 0x06 // SATA controller
#define PCI_PROGIF_AHCI 0x01 // AHCI programming interface

#ifdef __INTELLISENSE__
#define __asm__ __asm
#endif

// Recursive Mapping Definitions
#define SELF_REF_IDX 0x1FF // Reserved Recursive Slot

static void* kmemcpy(void* dest, const void* src, size_t len) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < len; i++) d[i] = s[i];
    return dest;
}

// TSS For Kernel Stack when faulting so guard pages work correctly.
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7]; // This is the Interrupt Stack Table
    uint32_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed)) TSS;

TSS gTss;
EFI_PHYSICAL_ADDRESS DoubleFaultStackPhys;
EFI_PHYSICAL_ADDRESS PageFaultStackPhys;

// Frame buffer parameters passed to kernel
typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 Width;
    UINT32 Height;
    UINT32 PixelsPerScanLine;
} GOP_PARAMS;

// Boot information passed to kernel
typedef struct {
    GOP_PARAMS* Gop;
    EFI_MEMORY_DESCRIPTOR* MemoryMap;
    UINTN MapSize;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
    UINTN AhciCount;
    UINT64* AhciBarBases;
    UINT64 KernelStackTop;
    uintptr_t Pml4Phys;
    uint16_t TssSelector;
} BOOT_INFO;

// minimal ELF64 defs
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

static inline UINT64 round_down64(UINT64 x, UINT64 a) { return x & ~(a-1); }
static inline UINT64 round_up64 (UINT64 x, UINT64 a) { return (x + (a-1)) & ~(a-1); }

_Static_assert(offsetof(BOOT_INFO, KernelStackTop) == 0x38, "KernelStackTop offset is not 0x38");

// define the GUID so the linker can satisfy &gEfiFileInfoGuid
EFI_GUID gEfiFileInfoGuid = EFI_FILE_INFO_ID;

// PTE bits
#define PTE_PRESENT (1ULL << 0)
#define PTE_RW (1ULL << 1)
#define PTE_USER (1ULL << 2)
#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)
#define PTE_ACCESSED (1ULL << 5)
#define PTE_DIRTY (1ULL << 6)
#define PTE_PS (1ULL << 7)
#define PTE_GLOBAL (1ULL << 8)
#define PTE_NX (1ULL << 63)

// Page sizes
#define PAGE_SIZE_4K 0x1000ULL
#define PAGE_SIZE_2M 0x200000ULL

// Kernel VA base (linked address)
#define KERNEL_VA_START 0xfffff80000000000ULL

// Helpers to extract indices
static inline UINTN IDX_PML4(UINT64 va) { return (va >> 39) & 0x1FF; }
static inline UINTN IDX_PDPT(UINT64 va) { return (va >> 30) & 0x1FF; }
static inline UINTN IDX_PD (UINT64 va) { return (va >> 21) & 0x1FF; }
static inline UINTN IDX_PT (UINT64 va) { return (va >> 12) & 0x1FF; }

// Globals for page tables
static UINT64* Pml4Virt = NULL;

// Allocate one page for page-table and zero it
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

static void commit_page_tables_and_load_cr3(void) {
    if (!Pml4Virt) return;
    UINT64 phys = (UINT64)(UINTN)Pml4Virt;
    AsmWriteCr3(phys);
}

// Map a single 4KiB page (virtual->physical) with pte_flags
static EFI_STATUS ensure_next_table(UINT64* parent_table, UINTN idx, UINT64** next_table_vaddr) {
    if (!(parent_table[idx] & PTE_PRESENT)) {
        // Allocate a new page. The return value is its address.
        VOID* new_table = alloc_page_table_page();
        if (!new_table) return EFI_OUT_OF_RESOURCES;
        
        // The physical address is the same as the virtual address here.
        UINT64 phys_addr = (UINT64)(UINTN)new_table;
        parent_table[idx] = phys_addr | PTE_PRESENT | PTE_RW;
        *next_table_vaddr = new_table;
    } else {
        // The table already exists, calculate its virtual address from the entry.
        *next_table_vaddr = (UINT64*)(UINTN)(parent_table[idx] & ~0xFFFULL);
    }
    return EFI_SUCCESS;
}

// Corrected bootloader map_page_4k
STATIC EFI_STATUS map_page_4k(UINT64 virt, UINT64 phys, UINT64 pte_flags) {
    EFI_STATUS Status = ensure_pml4();
    if (EFI_ERROR(Status)) return Status;
    
    UINT64* pml4 = Pml4Virt;
    UINT64* pdpt;
    UINT64* pd;
    UINT64* pt;
    
    UINTN pml4_i = IDX_PML4(virt);
    UINTN pdpt_i = IDX_PDPT(virt);
    UINTN pd_i   = IDX_PD(virt);
    UINTN pt_i   = IDX_PT(virt);

    // This new helper function gets the virtual address of the next table safely.
    Status = ensure_next_table(pml4, pml4_i, &pdpt);
    if (EFI_ERROR(Status)) return Status;

    Status = ensure_next_table(pdpt, pdpt_i, &pd);
    if (EFI_ERROR(Status)) return Status;
    
    Status = ensure_next_table(pd, pd_i, &pt);
    if (EFI_ERROR(Status)) return Status;

    pt[pt_i] = (phys & ~0xFFFULL) | (pte_flags & ~PTE_PS) | PTE_PRESENT;
    return EFI_SUCCESS;
}

// Map range physical [StartAddrPhys, EndAddrPhys) to virtual starting at VirtBase
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

// Read ELF64 e_entry if present; returns 0 on malformed/non-ELF
static UINT64 get_elf_entry_if_present(VOID* buf, UINTN size) {
    if (size < 0x40) return 0;
    unsigned char* b = (unsigned char*)buf;
    if (b[0] == 0x7f && b[1] == 'E' && b[2] == 'L' && b[3] == 'F' && b[4] == 2) {
        return *(UINT64*)((UINT8*)buf + 0x18);
    }
    return 0;
}
STATIC EFI_STATUS map_elf_segments(VOID* KernelBuffer, UINTN FileSize) {
    Print(L"[map_elf_segments] called FileSize=0x%lx\n", FileSize);
    if (FileSize < sizeof(Elf64_Ehdr)) {
        Print(L"[map_elf_segments] file too small for ELF header\n");
        // Halt so you can inspect output
        Print(L"[map_elf_segments] HALTING (not ELF)\n");
        for (;;) { __asm__ volatile("hlt"); }
    }

    Elf64_Ehdr* eh = (Elf64_Ehdr*)KernelBuffer;
    if (!(eh->e_ident[0] == 0x7f && eh->e_ident[1] == 'E' && eh->e_ident[2] == 'L' && eh->e_ident[3] == 'F' && eh->e_ident[4] == 2)) {
        Print(L"[map_elf_segments] not ELF64, doing fallback map_range\n");
        EFI_STATUS s = map_range((UINTN)KernelBuffer, (UINTN)KernelBuffer + FileSize, (UINTN)KERNEL_VA_START, PTE_PRESENT | PTE_RW);
        Print(L"[map_elf_segments] fallback map_range returned %r\n", s);
        Print(L"[map_elf_segments] HALTING (fallback)\n");
        for (;;) { __asm__ volatile("hlt"); }
    }

    Print(L"[map_elf_segments] ELF64 detected. phnum=%u phoff=0x%lx\n", eh->e_phnum, eh->e_phoff);
    Elf64_Phdr* ph = (Elf64_Phdr*)((UINT8*)eh + eh->e_phoff);
    for (UINT16 i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) {
            Print(L" PH %u: type=%u (skipped)\n", i, ph[i].p_type);
            continue;
        }

        UINT64 seg_vstart = ph[i].p_vaddr;
        UINT64 seg_filesz = ph[i].p_filesz;
        UINT64 seg_memsz = ph[i].p_memsz;
        UINT64 seg_off = ph[i].p_offset;
        UINT64 page_vstart = round_down64(seg_vstart, PAGE_SIZE_4K);
        UINT64 page_vend = round_up64(seg_vstart + seg_memsz, PAGE_SIZE_4K);

        Print(L" PH %u: v=0x%lx - 0x%lx filesz=0x%lx memsz=0x%lx off=0x%lx\n", i, seg_vstart, seg_vstart + seg_memsz, seg_filesz, seg_memsz, seg_off);

        // choose flags from p_flags (make kernel pages supervisor-only)
        UINT64 flags = PTE_PRESENT;
        if (ph[i].p_flags & PF_W) flags |= PTE_RW;

        for (UINT64 v = page_vstart; v < page_vend; v += PAGE_SIZE_4K) {
            // allocate a physical page for this virtual page
            EFI_PHYSICAL_ADDRESS phys_page = 0;
            EFI_STATUS s = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &phys_page);
            if (EFI_ERROR(s)) {
                Print(L" [PH %u] AllocatePages failed: %r\n", i, s);
                Print(L" HALTING\n");
                for (;;) { __asm__ volatile("hlt"); }
            }

            VOID* dest = (VOID*)(UINTN)phys_page;
            ZeroMem(dest, PAGE_SIZE_4K);

            // compute where (if anywhere) in the file this page's bytes come from
            INT64 seg_page_off = (INT64)v - (INT64)seg_vstart;
            UINT64 copy_dest_off = 0;
            UINT64 copy_src_off = 0;
            UINT64 copy_len = 0;

            if (seg_page_off < 0) {
                copy_dest_off = (UINT64)(-seg_page_off);
                if (seg_filesz > 0) {
                    if (seg_filesz > copy_dest_off) {
                        copy_len = seg_filesz - copy_dest_off;
                        if (copy_len > PAGE_SIZE_4K - copy_dest_off) copy_len = PAGE_SIZE_4K - copy_dest_off;
                        copy_src_off = seg_off;
                    } else {
                        copy_len = 0;
                    }
                }
            } else {
                if ((UINT64)seg_page_off < seg_filesz) {
                    copy_dest_off = 0;
                    copy_src_off = seg_off + (UINT64)seg_page_off;
                    if (copy_src_off < FileSize) {
                        UINT64 max_copy_from_file = FileSize - copy_src_off;
                        UINT64 rem_in_filesz = seg_filesz - (UINT64)seg_page_off;
                        copy_len = (max_copy_from_file < rem_in_filesz) ? max_copy_from_file : rem_in_filesz;
                        if (copy_len > PAGE_SIZE_4K) copy_len = PAGE_SIZE_4K;
                    } else {
                        copy_len = 0;
                    }
                } else {
                    copy_len = 0;
                }
            }

            if (copy_len > 0) {
                CopyMem((UINT8*)dest + copy_dest_off, (UINT8*)KernelBuffer + copy_src_off, copy_len);
            }

            EFI_STATUS ms = map_page_4k(v, (UINT64)phys_page, flags);
            if (EFI_ERROR(ms)) {
                Print(L" [PH %u] map_page_4k(0x%lx->0x%lx) failed: %r\n", i, v, phys_page, ms);
                Print(L" HALTING\n");
                for (;;) { __asm__ volatile("hlt"); }
            }

            // print what we did for this page
            Print(L" Mapped page VA 0x%lx -> phys 0x%lx flags=0x%lx copy_len=0x%lx dest_off=0x%lx\n", v, (UINT64)phys_page, flags, (UINT64)copy_len, (UINT64)copy_dest_off);
        }
    }

    Print(L"[map_elf_segments] finished mapping all PT_LOAD pages.\n");
    return EFI_SUCCESS;
}

static EFI_STATUS patch_kernel_image_with_tss(void* kernel_buf, UINTN kernel_size,
                                              uint64_t tss_base, uint32_t tss_limit,
                                              uint16_t *out_selector) {
    if (!kernel_buf || kernel_size < 8) return EFI_INVALID_PARAMETER;

    const uint64_t pattern[5] = {
        0x0000000000000000ULL,
        0x00AF9A000000FFFFULL,   // kernel code
        0x00CF92000000FFFFULL,   // kernel data
        0x00AFFA000000FFFFULL,   // user code
        0x00CFF2000000FFFFULL    // user data
    };

    // Build TSS descriptor QWORDs (64-bit-safe)
    uint64_t low = ((uint64_t)(tss_limit & 0xFFFFULL))
                 | ((uint64_t)(tss_base & 0xFFFFFFULL) << 16)
                 | (0x0089ULL << 40)   /* type=0x9 + P=1 -> 0x89 */
                 | ((uint64_t)(tss_limit & 0xF0000ULL) << 32)
                 | ((uint64_t)(tss_base & 0xFF000000ULL) << 32);
    uint64_t high = (uint64_t)(tss_base >> 32);

    uint8_t *scan = (uint8_t*)kernel_buf;
    uint8_t *scan_end = scan + kernel_size;
    const size_t pat_bytes = sizeof(pattern); // 5 * 8 = 40

    for (uint8_t *p = scan; p + pat_bytes <= scan_end; p += 8) {
        bool match = true;
        for (int i = 0; i < 5; ++i) {
            uint64_t val;
            // safe unaligned read from file bytes
            kmemcpy(&val, p + i * 8, sizeof(uint64_t));
            if (val != pattern[i]) { match = false; break; }
        }
        if (!match) continue;

        // Found gdt_start in the file image at offset (p - scan).
        uint8_t *gdt_tss_addr_in_file = p + 5 * 8;
        // Overwrite the two qwords inside the ELF file image (so later mapping copies them)
        kmemcpy(gdt_tss_addr_in_file, &low, sizeof(uint64_t));
        kmemcpy(gdt_tss_addr_in_file + 8, &high, sizeof(uint64_t));

        // The kernel's selector index is 5 (0..4 were the five entries), so selector = index * 8
        uint16_t selector = (uint16_t)(5 * 8); // 0x28
        *out_selector = selector;
        return EFI_SUCCESS;
    }

    return EFI_NOT_FOUND;
}

EFI_STATUS EFIAPI UefiMain(
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE* SystemTable
) {
    // --- STAGE 1: GATHER ALL INFORMATION USING UEFI BOOT SERVICES ---
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* Gop;
    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* SimpleFS;
    EFI_FILE_PROTOCOL* Root, * File;
    EFI_FILE_INFO* FileInfo;
    UINTN FileInfoSize;
    UINTN FileSize;
    EFI_PHYSICAL_ADDRESS KernelAddress = 0x100000;
    VOID* KernelBuffer;

    // 1) Locate GOP
    Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&Gop);
    if (EFI_ERROR(Status)) {
        Print(L"GOP not found: %r\n", Status);
        return Status;
    }

    GOP_PARAMS* GopParams = AllocatePool(sizeof(GOP_PARAMS));
    if (!GopParams) return EFI_OUT_OF_RESOURCES;
    GopParams->FrameBufferBase = Gop->Mode->FrameBufferBase;
    GopParams->FrameBufferSize = Gop->Mode->FrameBufferSize;
    GopParams->Width = Gop->Mode->Info->HorizontalResolution;
    GopParams->Height = Gop->Mode->Info->VerticalResolution;
    GopParams->PixelsPerScanLine = Gop->Mode->Info->PixelsPerScanLine;

    // 2) Load kernel.elf from disk
    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage);
    if (EFI_ERROR(Status)) {
        Print(L"LoadedImageProtocol failed: %r\n", Status);
        return Status;
    }

    Status = gBS->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&SimpleFS);
    if (EFI_ERROR(Status)) {
        Print(L"SimpleFileSystemProtocol failed: %r\n", Status);
        return Status;
    }

    Status = SimpleFS->OpenVolume(SimpleFS, &Root);
    if (EFI_ERROR(Status)) {
        Print(L"OpenVolume failed: %r\n", Status);
        return Status;
    }

    Status = Root->Open(Root, &File, L"kernel.elf", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        Print(L"Open kernel.elf failed: %r\n", Status);
        return Status;
    }

    FileInfoSize = sizeof(EFI_FILE_INFO) + 512;
    FileInfo = AllocateZeroPool(FileInfoSize);
    if (FileInfo == NULL) {
        Print(L"AllocateZeroPool(FileInfo) failed\n");
        return EFI_OUT_OF_RESOURCES;
    }

    Status = File->GetInfo(File, &gEfiFileInfoGuid, &FileInfoSize, FileInfo);
    if (EFI_ERROR(Status)) {
        Print(L"GetInfo failed: %r\n", Status);
        return Status;
    }

    FileSize = FileInfo->FileSize;
    FreePool(FileInfo);

    Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, EFI_SIZE_TO_PAGES(FileSize), &KernelAddress);
    if (EFI_ERROR(Status)) {
        Print(L"AllocatePages failed: %r\n", Status);
        return Status;
    }

    KernelBuffer = (VOID*)(UINTN)KernelAddress;
    Status = File->Read(File, &FileSize, KernelBuffer);
    if (EFI_ERROR(Status)) {
        Print(L"Read kernel.elf failed: %r\n", Status);
        return Status;
    }

    File->Close(File);

    // 3) Get initial memory map to build our page tables
    UINTN MemMapSize = 0, MapKey;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
    EFI_MEMORY_DESCRIPTOR* MemMap = NULL;

    gBS->GetMemoryMap(&MemMapSize, MemMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    MemMapSize += DescriptorSize * 2;
    MemMap = AllocatePool(MemMapSize);
    if (!MemMap) return EFI_OUT_OF_RESOURCES;
    Status = gBS->GetMemoryMap(&MemMapSize, MemMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (EFI_ERROR(Status)) {
        Print(L"Initial GetMemoryMap failed: %r\n", Status);
        return Status;
    }

    // 4) Scan for AHCI controllers
    UINTN handleCount;
    EFI_HANDLE* handles;
    Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiPciIoProtocolGuid, NULL, &handleCount, &handles);
    UINTN ahciCount = 0;
    UINT64* barBases = NULL;
    if (!EFI_ERROR(Status)) {
        barBases = AllocatePool(sizeof(UINT64) * handleCount);
        if (barBases == NULL) { FreePool(handles); return EFI_OUT_OF_RESOURCES; }

        for (UINTN i = 0; i < handleCount; i++) {
            EFI_PCI_IO_PROTOCOL* pciIo;
            if (EFI_ERROR(gBS->HandleProtocol(handles[i], &gEfiPciIoProtocolGuid, (VOID**)&pciIo))) continue;
            UINT8 class_code[3];
            pciIo->Pci.Read(pciIo, EfiPciIoWidthUint8, PCI_CLASSCODE_OFFSET, 3, &class_code);
            if (class_code[2] == PCI_CLASS_MASS_STORAGE && class_code[1] == PCI_SUBCLASS_MASS_STORAGE_SATA && class_code[0] == PCI_PROGIF_AHCI) {
                UINT32 bar5_low = 0, bar5_high = 0;
                pciIo->Pci.Read(pciIo, EfiPciIoWidthUint32, 0x24, 1, &bar5_low);
                if ((bar5_low & 0x06) == 0x04) {
                    pciIo->Pci.Read(pciIo, EfiPciIoWidthUint32, 0x28, 1, &bar5_high);
                }
                barBases[ahciCount++] = ((UINT64)bar5_high << 32) | (bar5_low & ~0x0F);
            }
        }
        FreePool(handles);
    }

    // 5) Allocate the stack for our kernel.
    EFI_PHYSICAL_ADDRESS StackPhysBase = 0;
    const UINTN StackPages = 8; // 8 Pages = 32KiB stack, a big and reasonable one for the kernel.
    const UINT64 StackVirtTop = 0xfffff80000900000ULL; // Arbitrary high-half addr.

    Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, StackPages, &StackPhysBase);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to allocate pages for kernel stack: %r\n", Status);
        return Status;
    }

    // Map the physical stack pages to the virtual address
    Status = map_range((UINTN)StackPhysBase, (UINTN)(StackPhysBase + StackPages * PAGE_SIZE_4K), (UINTN)(StackVirtTop - StackPages * PAGE_SIZE_4K), PTE_PRESENT | PTE_RW);
    if (EFI_ERROR(Status)) { Print(L"Failed to map kernel stack: %r\n", Status); return Status; }

        // 7.5) Create the TSS for the kernel handlers (PF and DF)
    Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &DoubleFaultStackPhys);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to allocate double fault stack: %r\n", Status);
        return Status;
    }

    // Allocate a page for the Page Fault stack
    Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &PageFaultStackPhys);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to allocate page fault stack: %r\n", Status);
        return Status;
    }

    // Map the TSS and IST stacks for the kernel
    Status = map_range_identity((UINTN)&gTss, (UINTN)&gTss + sizeof(TSS), PTE_PRESENT | PTE_RW);
    if (EFI_ERROR(Status)) { Print(L"Failed to map TSS: %r\n", Status); return Status; }

    Status = map_range_identity((UINTN)DoubleFaultStackPhys, (UINTN)DoubleFaultStackPhys + 4096, PTE_PRESENT | PTE_RW);
    if (EFI_ERROR(Status)) { Print(L"Failed to map DF stack: %r\n", Status); return Status; }

    Status = map_range_identity((UINTN)PageFaultStackPhys, (UINTN)PageFaultStackPhys + 4096, PTE_PRESENT | PTE_RW);
    if (EFI_ERROR(Status)) { Print(L"Failed to map PF stack: %r\n", Status); return Status; }

    // Populate the TSS. Stacks grow down, so the pointer is at the top of the allocated page.
    ZeroMem(&gTss, sizeof(TSS));
    gTss.ist[0] = (uint64_t)PageFaultStackPhys + 4096; // IST1 for page faults
    gTss.ist[1] = (uint64_t)DoubleFaultStackPhys + 4096; // IST2 for double faults

    // Set the TSS Selector
    uint16_t selector = 0;
    Status = patch_kernel_image_with_tss(KernelBuffer, FileSize, (uint64_t)(uintptr_t)&gTss, sizeof(TSS), &selector);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to patch kernel GDT with TSS descriptor: %r\n", Status);
        return Status;
    }

    // 6) Build the page tables in memory, but DO NOT activate them yet.
    Status = map_elf_segments(KernelBuffer, FileSize);
    if (EFI_ERROR(Status)) { Print(L"map_elf_segments failed: %r\n", Status); return Status; }

    const UINT64 PHYS_MEM_OFFSET = 0xffff880000000000ULL;
    const UINT64 MEM_TO_MAP = 0x100000000ULL; // 4 GiB
    const UINT64 UNMAPPED_LOW_MEM_SIZE = 0x10000; // Reserve low 64KB to catch NULL pointer bugs

    // --- FIX: Map physical memory starting after the unmapped region ---
    // The virtual base is also offset, preserving the Virt = Phys + Offset relationship.
    Status = map_range(UNMAPPED_LOW_MEM_SIZE, MEM_TO_MAP, PHYS_MEM_OFFSET + UNMAPPED_LOW_MEM_SIZE, PTE_PRESENT | PTE_RW);
    if (EFI_ERROR(Status)) { Print(L"Failed to map physical memory offset: %r\n", Status); return Status; }


    UINT8* iter = (UINT8*)MemMap;
    for (UINTN i = 0; i < MemMapSize / DescriptorSize; ++i) {
        EFI_MEMORY_DESCRIPTOR* d = (EFI_MEMORY_DESCRIPTOR*)iter;
        // Only identity map memory that is NOT conventional (free) or unusable.
        // This maps loader code/data, runtime services, ACPI data, and MMIO.
        // Also, skip mapping the NULL page to help catch NULL pointer bugs.
        if (d->Type != EfiConventionalMemory && d->Type != EfiUnusableMemory && d->PhysicalStart != 0) {
            if (d->NumberOfPages > 0) {
                Status = map_range_identity((UINTN)d->PhysicalStart, (UINTN)(d->PhysicalStart + d->NumberOfPages * 4096), PTE_PRESENT | PTE_RW);
                if (EFI_ERROR(Status)) { Print(L"Identity map failed for desc %u: %r\n", i, Status); return Status; }
            }
        }
        iter += DescriptorSize;
    }
    FreePool(MemMap);

    // 6.5) Page into the mapping all of the BOOT_INFO ptrs so that they won't fail in the kernel.
    UINT64 fb_size = GopParams->FrameBufferSize;
    UINT64 fb_base = GopParams->FrameBufferBase;
    if (fb_size > 0 && fb_base != 0) {
        Print(L"[GOP] Mapping GOP framebuffer now, Base: %p\n", fb_base);
        UINT64 fb_start = round_down64(fb_base, PAGE_SIZE_4K);
        UINT64 fb_end = round_up64(fb_base + fb_size, PAGE_SIZE_4K);
        Status = map_range_identity((UINTN)fb_start, (UINTN)fb_end, PTE_PRESENT | PTE_RW);
        if (EFI_ERROR(Status)) {
            Print(L"[GOP-ERROR] Unable to map framebuffer... halting. STATUS: %r\n", Status);
            for (;;) { __asm__ volatile ("hlt"); }
        }
    }

    // --- STAGE 2: EXIT BOOT SERVICES AND JUMP TO KERNEL ---
    // 7) Allocate BOOT_INFO structure BEFORE exiting boot services
    BOOT_INFO* BootInfo = AllocatePool(sizeof(BOOT_INFO));
    if (!BootInfo) return EFI_OUT_OF_RESOURCES;
    BootInfo->Gop = GopParams;
    BootInfo->AhciCount = ahciCount;
    BootInfo->AhciBarBases = barBases;

    // 8) Get the FINAL memory map and exit boot services.
    EFI_MEMORY_DESCRIPTOR* FinalMemMap = NULL;
    UINTN FinalMapSize = 0, FinalMapKey, FinalDescriptorSize;
    UINT32 FinalDescriptorVersion;

    Status = EFI_INVALID_PARAMETER;
    while (Status == EFI_INVALID_PARAMETER) {
        FinalMapSize = 0;
        Status = gBS->GetMemoryMap(&FinalMapSize, NULL, &FinalMapKey, &FinalDescriptorSize, &FinalDescriptorVersion);
        if (Status != EFI_BUFFER_TOO_SMALL) { Print(L"GetMemoryMap size failed: %r\n", Status); return Status; }
        FinalMapSize += FinalDescriptorSize * 2;
        if (FinalMemMap != NULL) FreePool(FinalMemMap);
        FinalMemMap = AllocatePool(FinalMapSize);
        if (FinalMemMap == NULL) return EFI_OUT_OF_RESOURCES;
        Status = gBS->GetMemoryMap(&FinalMapSize, FinalMemMap, &FinalMapKey, &FinalDescriptorSize, &FinalDescriptorVersion);
        if (EFI_ERROR(Status)) { Print(L"Final GetMemoryMap failed: %r\n", Status); return Status; }

        // Identity map the BOOT_INFO struct so when the kernel touches it, it won't get a PAGE FAULT.
        Status = map_range_identity((UINTN)BootInfo, (UINTN)BootInfo + sizeof(BOOT_INFO), PTE_PRESENT | PTE_RW);
        if (EFI_ERROR(Status)) { Print(L"Failed to map BootInfo: %r\n", Status); return Status; }
        Status = map_range_identity((UINTN)FinalMemMap, (UINTN)FinalMemMap + FinalMapSize, PTE_PRESENT | PTE_RW);
        if (EFI_ERROR(Status)) { Print(L"Failed to map FinalMemMap: %r\n", Status); return Status; }

        UINT64 phys = (UINT64)(UINTN)Pml4Virt;

        Pml4Virt[SELF_REF_IDX] = phys | PTE_PRESENT | PTE_RW;

        // Fill in the rest of the BootInfo struct with the final map details
        BootInfo->MemoryMap = FinalMemMap;
        BootInfo->MapSize = FinalMapSize;
        BootInfo->DescriptorSize = FinalDescriptorSize;
        BootInfo->DescriptorVersion = FinalDescriptorVersion;
        BootInfo->KernelStackTop = StackVirtTop;
        BootInfo->Pml4Phys = phys;
        BootInfo->TssSelector = selector;

        Status = gBS->ExitBootServices(ImageHandle, FinalMapKey);
    }

    if (EFI_ERROR(Status)) { Print(L"ExitBootServices failed fatally: %r\n", Status); return Status; }

    // UEFI IS NOW GONE. WE ARE ON OUR OWN. NO MORE gBS OR Print()

    // 9) Activate our new page tables.
    commit_page_tables_and_load_cr3();

    // 10) Determine kernel entry point
    UINT64 entry_va = get_elf_entry_if_present(KernelBuffer, FileSize);
    if (entry_va == 0) entry_va = KERNEL_VA_START;

    // 11) Jump to kernel!
    typedef void (*KERNEL_ENTRY)(BOOT_INFO*);
    KERNEL_ENTRY KernelEntry = (KERNEL_ENTRY)(UINTN)entry_va;
    KernelEntry(BootInfo);

    // Should never be reached
    for (;;) { __asm__ volatile ("hlt"); }
    return EFI_SUCCESS;
}