#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PciIo.h>
#include <Guid/FileInfo.h>
#include <IndustryStandard/Pci.h>

#define PCI_CLASS_MASS_STORAGE           0x01
#define PCI_SUBCLASS_MASS_STORAGE_SATA   0x06  // SATA controller
#define PCI_PROGIF_AHCI                  0x01  // AHCI programming interface

// Frame buffer parameters passed to kernel
typedef struct {
  UINT64  FrameBufferBase;
  UINT64  FrameBufferSize;
  UINT32  Width;
  UINT32  Height;
  UINT32  PixelsPerScanLine;
} GOP_PARAMS;

// Boot information passed to kernel
typedef struct {
  GOP_PARAMS* Gop;
  EFI_MEMORY_DESCRIPTOR* MemoryMap;
  UINTN                  MapSize;
  UINTN                  DescriptorSize;
  UINT32                 DescriptorVersion;
  UINTN                  AhciCount;
  UINT64                 *AhciBarBases;
} BOOT_INFO;

// define the GUID so the linker can satisfy &gEfiFileInfoGuid
EFI_GUID gEfiFileInfoGuid = EFI_FILE_INFO_ID;

EFI_STATUS
EFIAPI
UefiMain(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                      Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL    *Gop;
  EFI_LOADED_IMAGE_PROTOCOL       *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SimpleFS;
  EFI_FILE_PROTOCOL               *Root, *File;
  EFI_FILE_INFO                   *FileInfo;
  UINTN                           FileInfoSize;
  UINTN                           FileSize;
  EFI_PHYSICAL_ADDRESS            KernelAddress = 0x100000;
  VOID                            *KernelBuffer;

  // 1) Locate GOP
  Status = gBS->LocateProtocol(
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  (VOID**)&Gop
                  );
  if (EFI_ERROR(Status)) {
    Print(L"GOP not found: %r\n", Status);
    return Status;
  }

  // Fill GOP_PARAMS
  GOP_PARAMS* GopParams = AllocatePool(sizeof(GOP_PARAMS));
  GopParams->FrameBufferBase    = Gop->Mode->FrameBufferBase;
  GopParams->FrameBufferSize    = Gop->Mode->FrameBufferSize;
  GopParams->Width              = Gop->Mode->Info->HorizontalResolution;
  GopParams->Height             = Gop->Mode->Info->VerticalResolution;
  GopParams->PixelsPerScanLine  = Gop->Mode->Info->PixelsPerScanLine;

  // 2) Load kernel.bin
  Status = gBS->HandleProtocol(
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID**)&LoadedImage
                  );
  if (EFI_ERROR(Status)) {
    Print(L"LoadedImageProtocol failed: %r\n", Status);
    return Status;
  }

  Status = gBS->HandleProtocol(
                  LoadedImage->DeviceHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID**)&SimpleFS
                  );
  if (EFI_ERROR(Status)) {
    Print(L"SimpleFileSystemProtocol failed: %r\n", Status);
    return Status;
  }

  Status = SimpleFS->OpenVolume(SimpleFS, &Root);
  if (EFI_ERROR(Status)) {
    Print(L"OpenVolume failed: %r\n", Status);
    return Status;
  }

  Status = Root->Open(Root, &File, L"kernel.bin", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(Status)) {
    Print(L"Open kernel.bin failed: %r\n", Status);
    return Status;
  }

  FileInfoSize = sizeof(EFI_FILE_INFO) + 200;
  FileInfo     = AllocateZeroPool(FileInfoSize);
  if (FileInfo == NULL) {
    Print(L"AllocateZeroPool(FileInfo) failed\n");
    return EFI_OUT_OF_RESOURCES;
  }

  Status = File->GetInfo(
                  File,
                  &gEfiFileInfoGuid,
                  &FileInfoSize,
                  FileInfo
                  );

  if (EFI_ERROR(Status)) {
    Print(L"GetInfo failed: %r\n", Status);
    return Status;
  }
  FileSize = FileInfo->FileSize;
  FreePool(FileInfo);

  Status = gBS->AllocatePages(
                  AllocateAddress,
                  EfiLoaderData,
                  EFI_SIZE_TO_PAGES(FileSize),
                  &KernelAddress
                  );
  if (EFI_ERROR(Status)) {
    Print(L"AllocatePages failed: %r\n", Status);
    return Status;
  }

  KernelBuffer = (VOID*)(UINTN)KernelAddress;
  Status       = File->Read(File, &FileSize, KernelBuffer);
  if (EFI_ERROR(Status)) {
    Print(L"Read kernel.bin failed: %r\n", Status);
    return Status;
  }
  File->Close(File);

  // 3) Memory map & AHCI & ExitBootServices
  UINTN                   MemMapSize = 0, MapKey;
  UINTN                   DescriptorSize;
  UINT32                  DescriptorVersion;
  EFI_MEMORY_DESCRIPTOR *MemMap = NULL;

  Status = gBS->GetMemoryMap(
                  &MemMapSize, MemMap, &MapKey,
                  &DescriptorSize, &DescriptorVersion
                  );
  if (Status != EFI_BUFFER_TOO_SMALL) {
    Print(L"GetMemoryMap failed: %r\n", Status);
    return Status;
  }

  MemMapSize += DescriptorSize * 2;
  MemMap = AllocatePool(MemMapSize);
  Status = gBS->GetMemoryMap(
                  &MemMapSize, MemMap, &MapKey,
                  &DescriptorSize, &DescriptorVersion
                  );
  if (EFI_ERROR(Status)) {
    Print(L"GetMemoryMap failed: %r\n", Status);
    return Status;
  }

  // --- AHCI Scan ---
  UINTN      handleCount;
  EFI_HANDLE *handles;

  Status = gBS->LocateHandleBuffer(
    ByProtocol,
    &gEfiPciIoProtocolGuid,
    NULL,
    &handleCount,
    &handles
    );
  
  UINTN   ahciCount = 0;
  UINT64* barBases = NULL;

  if (!EFI_ERROR(Status)) {
    barBases = AllocatePool(sizeof(UINT64) * handleCount);
    if (barBases == NULL) {
        Print(L"Failed to allocate memory for AHCI BARs\n");
        FreePool(handles);
        return EFI_OUT_OF_RESOURCES;
    }

    for (UINTN i = 0; i < handleCount; i++) {
      EFI_PCI_IO_PROTOCOL *pciIo;
      Status = gBS->HandleProtocol(
                    handles[i],
                    &gEfiPciIoProtocolGuid,
                    (VOID**)&pciIo
                    );
      if (EFI_ERROR(Status)) continue;

	UINT8 class_code[3];
	pciIo->Pci.Read(
	  pciIo,
	  EfiPciIoWidthUint8,
	  PCI_CLASSCODE_OFFSET, // 0x09
	  3,
	  &class_code
	);

	if (class_code[2] == PCI_CLASS_MASS_STORAGE &&
	    class_code[1] == PCI_SUBCLASS_MASS_STORAGE_SATA &&
	    class_code[0] == PCI_PROGIF_AHCI) {

	    // Read BAR5 (ABAR) - AHCI Base Address Register
	    UINT32 bar5_low = 0;
	    UINT32 bar5_high = 0;
	    
	    // Read the lower 32 bits of BAR5
	    Status = pciIo->Pci.Read(
	      pciIo,
	      EfiPciIoWidthUint32,
	      0x24,  // BAR5 offset
	      1,
	      &bar5_low
	    );
	    if (EFI_ERROR(Status)) {
	      continue;
	    }
	    
	    // Check if this is a 64-bit BAR (bit 2:1 = 10b means 64-bit)
	    if ((bar5_low & 0x06) == 0x04) {
	      // This is a 64-bit BAR, read the upper 32 bits
	      Status = pciIo->Pci.Read(
		pciIo,
		EfiPciIoWidthUint32,
		0x28,  // BAR5+4 offset (upper 32 bits)
		1,
		&bar5_high
	      );
	      if (EFI_ERROR(Status)) {
		continue;
	      }
	    }
	    
	    // Combine into 64-bit address and mask flags
	    UINT64 bar_address = ((UINT64)bar5_high << 32) | (bar5_low & ~0x0F);
	    
	    // Debug output
	    Print(L"AHCI Controller found: BAR5=0x%08x%08x\n", bar5_high, bar5_low & ~0x0F);
	    
	    barBases[ahciCount++] = bar_address;
	}
    }
    FreePool(handles);
  }

  // Prepare BOOT_INFO
  BOOT_INFO* BootInfo = AllocatePool(sizeof(BOOT_INFO));
  BootInfo->Gop               = GopParams;
  BootInfo->MemoryMap         = MemMap;
  BootInfo->MapSize           = MemMapSize;
  BootInfo->DescriptorSize    = DescriptorSize;
  BootInfo->DescriptorVersion = DescriptorVersion;
  BootInfo->AhciCount         = ahciCount;
  BootInfo->AhciBarBases      = barBases;

	// Get final memory map after all allocations
	UINTN FinalMemMapSize = MemMapSize;
	Status = gBS->GetMemoryMap(
		        &FinalMemMapSize, MemMap, &MapKey,
		        &DescriptorSize, &DescriptorVersion
		        );

	if (Status == EFI_BUFFER_TOO_SMALL) {
	    Print(L"Reallocating memory map buffer...\n");
	    FreePool(MemMap);
	    FinalMemMapSize += DescriptorSize * 2;
	    MemMap = AllocatePool(FinalMemMapSize);
	    
	    Status = gBS->GetMemoryMap(
		            &FinalMemMapSize, MemMap, &MapKey,
		            &DescriptorSize, &DescriptorVersion
		            );
	}

	if (EFI_ERROR(Status)) {
	    Print(L"Final GetMemoryMap failed: %r\n", Status);
	    return Status;
	}

	// Update BootInfo with final values
	BootInfo->MemoryMap = MemMap;
	BootInfo->MapSize = FinalMemMapSize;

	Status = gBS->ExitBootServices(ImageHandle, MapKey);
	if (EFI_ERROR(Status)) {
	    Print(L"ExitBootServices failed: %r\n", Status);
	    for (;;) { __asm__ volatile ("hlt"); }
	}

  // 4) Jump to kernel
  typedef void (*KERNEL_ENTRY)(BOOT_INFO*);
  KERNEL_ENTRY KernelEntry = (KERNEL_ENTRY)(UINTN)KernelAddress;
  KernelEntry(BootInfo);

  // should never return
  for (;;) { __asm__ volatile ("hlt"); }
}
