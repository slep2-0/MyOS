#include <Uefi.h>
#include <Guid/Acpi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PciIo.h>
#include <Protocol/SimpleFileSystem.h>

EFI_SYSTEM_TABLE* gST;
EFI_BOOT_SERVICES* gBS;

EFI_GUID gEfiGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
EFI_GUID gEfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
EFI_GUID gEfiPciIoProtocolGuid = EFI_PCI_IO_PROTOCOL_GUID;
EFI_GUID gEfiSimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
EFI_GUID gEfiAcpi20TableGuid = EFI_ACPI_20_TABLE_GUID;

EFI_STATUS
EFIAPI
UefiMain(
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE* SystemTable
);

VOID*
EFIAPI
CopyMem(
    OUT VOID* DestinationBuffer,
    IN CONST VOID* SourceBuffer,
    IN UINTN Length
)
{
    UINT8* Destination = (UINT8*)DestinationBuffer;
    CONST UINT8* Source = (CONST UINT8*)SourceBuffer;

    for (UINTN Index = 0; Index < Length; Index++) {
        Destination[Index] = Source[Index];
    }

    return DestinationBuffer;
}

VOID*
EFIAPI
ZeroMem(
    OUT VOID* Buffer,
    IN UINTN Length
)
{
    UINT8* Bytes = (UINT8*)Buffer;

    for (UINTN Index = 0; Index < Length; Index++) {
        Bytes[Index] = 0;
    }

    return Buffer;
}

BOOLEAN
EFIAPI
CompareGuid(
    IN CONST GUID* Guid1,
    IN CONST GUID* Guid2
)
{
    CONST UINT8* Left = (CONST UINT8*)Guid1;
    CONST UINT8* Right = (CONST UINT8*)Guid2;

    for (UINTN Index = 0; Index < sizeof(GUID); Index++) {
        if (Left[Index] != Right[Index]) {
            return FALSE;
        }
    }

    return TRUE;
}

VOID*
EFIAPI
AllocateZeroPool(
    IN UINTN AllocationSize
)
{
    VOID* Buffer = NULL;

    if (AllocationSize == 0 || gBS == NULL) {
        return NULL;
    }

    EFI_STATUS Status = gBS->AllocatePool(
        EfiLoaderData,
        AllocationSize,
        &Buffer
    );
    if (EFI_ERROR(Status)) {
        return NULL;
    }

    return ZeroMem(Buffer, AllocationSize);
}

VOID
EFIAPI
FreePool(
    IN VOID* Buffer
)
{
    if (Buffer != NULL && gBS != NULL) {
        gBS->FreePool(Buffer);
    }
}

UINTN
EFIAPI
AsmWriteCr3(
    UINTN Cr3
)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(Cr3) : "memory");
    return Cr3;
}

EFI_STATUS
EFIAPI
EfiMain(
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE* SystemTable
)
{
    if (SystemTable == NULL || SystemTable->BootServices == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    gST = SystemTable;
    gBS = SystemTable->BootServices;
    return UefiMain(ImageHandle, SystemTable);
}
