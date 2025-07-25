#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Guid/FileInfo.h>           // brings in gEfiFileInfoGuid

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
  GOP_PARAMS*               Gop;
  EFI_MEMORY_DESCRIPTOR*    MemoryMap;
  UINTN                     MapSize;
  UINTN                     DescriptorSize;
  UINT32                    DescriptorVersion;
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
  EFI_STATUS                       Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL    *Gop;
  EFI_LOADED_IMAGE_PROTOCOL       *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SimpleFS;
  EFI_FILE_PROTOCOL               *Root, *File;
  EFI_FILE_INFO                   *FileInfo;
  UINTN                            FileInfoSize;
  UINTN                            FileSize;
  EFI_PHYSICAL_ADDRESS             KernelAddress = 0x100000;
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

  // ← here’s the only change:
  Status = File->GetInfo(
             File,
             &gEfiFileInfoGuid,      // use the EDK2‐provided GUID
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

  // 3) Memory map & ExitBootServices
  UINTN                  MemMapSize = 0, MapKey;
  UINTN                  DescriptorSize;
  UINT32                 DescriptorVersion;
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

  // Prepare BOOT_INFO
  BOOT_INFO* BootInfo = AllocatePool(sizeof(BOOT_INFO));
  BootInfo->Gop               = GopParams;
  BootInfo->MemoryMap         = MemMap;
  BootInfo->MapSize           = MemMapSize;
  BootInfo->DescriptorSize    = DescriptorSize;
  BootInfo->DescriptorVersion = DescriptorVersion;

  Status = gBS->ExitBootServices(ImageHandle, MapKey);
  if (EFI_ERROR(Status)) {
    Print(L"ExitBootServices failed: %r\n", Status);
    return Status;
  }

  // 4) Jump to kernel
  typedef void (*KERNEL_ENTRY)(BOOT_INFO*);
  KERNEL_ENTRY KernelEntry = (KERNEL_ENTRY)(UINTN)KernelAddress;
  KernelEntry(BootInfo);

  // never returns
  for (;;) { __asm__ volatile ("hlt"); }
}
