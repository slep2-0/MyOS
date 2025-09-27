/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      ACPI (Advanced Configuration and Power Interface) function prototypes, primitives, structures.
 * DEVELOPER:	 https://github.com/slep2-0
 */

#ifndef X86_ACPI_H
#define X86_ACPI_H

#include <stdint.h>
#include <stdbool.h>
#include "../../mtstatus.h"
#include "../uefi_memory.h"

/*STRUCTURES****************************************************************/

typedef struct _RSDP_Descriptor {
    char     Signature[8];
    uint8_t  Checksum;
    char     OemId[6];
    uint8_t  Revision;
    uint32_t RsdtAddress; // legacy 32bit.
    // acpi 2.0 fields
    uint32_t Length;
    uint64_t XsdtAddress; // The one we use.
    uint8_t  ExtendedChecksum;
    uint8_t  Reserved[3];
} __attribute__((packed)) RSDP_Descriptor;

typedef struct _ACPI_SDT_HEADER {
    char     Signature[4];
    uint32_t Length;
    uint8_t  Revision;
    uint8_t  Checksum;
    char     OemId[6];
    char     OemTableId[8];
    uint32_t OemRevision;
    uint32_t CreatorId;
    uint32_t CreatorRevision;
} __attribute__((packed)) ACPI_SDT_HEADER;

typedef struct _XSDT {
    struct _ACPI_SDT_HEADER h;
    uint64_t Entries[]; // Array of 64-bit physical addresses to other tables
} __attribute__((packed)) XSDT;

typedef struct _GenericAddressStructure
{
    uint8_t AddressSpace;
    uint8_t BitWidth;
    uint8_t BitOffset;
    uint8_t AccessSize;
    uint64_t Address;
} __attribute__((packed)) GenericAddressStructure;

typedef struct _FADT
{
    struct   _ACPI_SDT_HEADER h;
    uint32_t FirmwareCtrl;
    uint32_t Dsdt;

    // field used in ACPI 1.0; no longer in use, for compatibility only
    uint8_t  Reserved;

    uint8_t  PreferredPowerManagementProfile;
    uint16_t SCI_Interrupt;
    uint32_t SMI_CommandPort;
    uint8_t  AcpiEnable;
    uint8_t  AcpiDisable;
    uint8_t  S4BIOS_REQ;
    uint8_t  PSTATE_Control;
    uint32_t PM1aEventBlock;
    uint32_t PM1bEventBlock;
    uint32_t PM1aControlBlock;
    uint32_t PM1bControlBlock;
    uint32_t PM2ControlBlock;
    uint32_t PMTimerBlock;
    uint32_t GPE0Block;
    uint32_t GPE1Block;
    uint8_t  PM1EventLength;
    uint8_t  PM1ControlLength;
    uint8_t  PM2ControlLength;
    uint8_t  PMTimerLength;
    uint8_t  GPE0Length;
    uint8_t  GPE1Length;
    uint8_t  GPE1Base;
    uint8_t  CStateControl;
    uint16_t WorstC2Latency;
    uint16_t WorstC3Latency;
    uint16_t FlushSize;
    uint16_t FlushStride;
    uint8_t  DutyOffset;
    uint8_t  DutyWidth;
    uint8_t  DayAlarm;
    uint8_t  MonthAlarm;
    uint8_t  Century;

    // reserved in ACPI 1.0; used since ACPI 2.0+
    uint16_t BootArchitectureFlags;

    uint8_t  Reserved2;
    uint32_t Flags;

    // 12 byte structure; see below for details
    GenericAddressStructure ResetReg;

    uint8_t  ResetValue;
    uint8_t  Reserved3[3];

    // 64bit pointers - Available on ACPI 2.0+
    uint64_t                X_FirmwareControl;
    uint64_t                X_Dsdt;

    GenericAddressStructure X_PM1aEventBlock;
    GenericAddressStructure X_PM1bEventBlock;
    GenericAddressStructure X_PM1aControlBlock;
    GenericAddressStructure X_PM1bControlBlock;
    GenericAddressStructure X_PM2ControlBlock;
    GenericAddressStructure X_PMTimerBlock;
    GenericAddressStructure X_GPE0Block;
    GenericAddressStructure X_GPE1Block;
} __attribute__((packed)) FADT;

enum MADT_TYPES {
    MADT_LAPIC = 0,
    MADT_IOAPIC = 1,
    MADT_INTERUPT_SOURCE_OVERRIDE = 2,
    MADT_NON_MASKABLE_INTERRUPT = 4,
    MADT_X2APIC = 9
};

typedef struct _MADT {
    struct _ACPI_SDT_HEADER h;
    uint32_t lapicAddress;
    uint32_t flags;
} __attribute__((packed)) MADT;

typedef struct {
    uint8_t Type;          // 0
    uint8_t Length;        // 8
    uint8_t AcpiProcessorId;
    uint8_t ApicId;
    uint32_t Flags;        // Bit 0 = enabled, Bit 1 = online-capable
} __attribute__((packed)) MADT_LOCAL_APIC;

typedef struct {
    uint8_t Type;           // 1
    uint8_t Length;         // 12
    uint8_t IoApicId;
    uint8_t Reserved;       // must be 0
    uint32_t IoApicAddress; // physical address
    uint32_t GlobalSystemInterruptBase;
} __attribute__((packed)) MADT_IO_APIC;

typedef struct {
    uint8_t Type;            // 2
    uint8_t Length;          // 10
    uint8_t Bus;             // 0 = ISA
    uint8_t Source;          // IRQ source
    uint32_t GlobalSystemInterrupt;
    uint16_t Flags;
} __attribute__((packed)) MADT_INTERRUPT_OVERRIDE;

typedef struct {
    uint8_t Type;     // 4
    uint8_t Length;   // 6
    uint8_t AcpiProcessorId; // 0xFF = all processors
    uint16_t Flags;
    uint8_t Lint;     // LINTn pin (0 or 1)
} __attribute__((packed)) MADT_NMI;

typedef struct {
    uint8_t Type;         // 9
    uint8_t Length;       // 16
    uint16_t Reserved;
    uint32_t X2ApicId;
    uint32_t Flags;       // same as local APIC flags
    uint32_t AcpiProcessorUid;
} __attribute__((packed)) MADT_LOCAL_X2APIC;

/*FUNCTIONS***********************************************************/

/// <summary>
/// Parse the LAPIC Tables in the MADT.
/// </summary>
/// <param name="*buffer">[OUT] Pointer to buffer of APICs</param>
/// <param name="maxCPUs">[IN] MAX CPUs supported by the system.</param>
/// <param name="*cpuCount">[OUT] Pointer to CPU Count</param>
/// <param name="*lapicAddress">[OUT] Pointer to LAPIC Address variable</param>
/// <returns>MTSTATUS Status Code</returns>
MTSTATUS ParseLAPICs(uint8_t* buffer, size_t maxCPUs, uint32_t* cpuCount, uint32_t* lapicAddress);

MTSTATUS InitializeACPI(void);

/// <summary>
/// The following function will send a REBOOT signal to the ACPI Register, use when everything is halted (SMP CPUs), unmounted (FileSystems), and practically idle (Kernel Core stuff).
/// </summary>
/// <remarks>If it returns, that means a failure in rebooting has happened (including not initializing acpi beforehand)</remarks>
void MtACPIRebootComputer(void);

#endif