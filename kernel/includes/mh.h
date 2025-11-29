#ifndef X86_MATANEL_HAL_H
#define X86_MATANEL_HAL_H

/*++

Module Name:

    mh.h

Purpose:

    This module contains the header files & prototypes required for the hardware abstraction layer of MatanelOS.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "core.h"
#include "annotations.h"
#include "macros.h"
#include <cpuid.h>
#define IDT_ENTRIES        256

#include "mm.h"

// ------------------ ENUMERATORS ------------------

//** Exception Definitions **/
typedef enum _CPU_EXCEPTIONS {
	EXCEPTION_DIVIDE_BY_ZERO,
	EXCEPTION_SINGLE_STEP,
	EXCEPTION_NON_MASKABLE_INTERRUPT,
	EXCEPTION_BREAKPOINT,
	EXCEPTION_OVERFLOW,
	EXCEPTION_BOUNDS_CHECK,
	EXCEPTION_INVALID_OPCODE,
	EXCEPTION_NO_COPROCESSOR,
	EXCEPTION_DOUBLE_FAULT,
	EXCEPTION_COPROCESSOR_SEGMENT_OVERRUN,
	EXCEPTION_INVALID_TSS,
	EXCEPTION_SEGMENT_SELECTOR_NOTPRESENT,
	EXCEPTION_STACK_SEGMENT_OVERRUN,
	EXCEPTION_GENERAL_PROTECTION_FAULT,
	EXCEPTION_PAGE_FAULT,
	EXCEPTION_RESERVED,
	EXCEPTION_FLOATING_POINT_ERROR,
	EXCEPTION_ALIGNMENT_CHECK,
	EXCEPTION_SEVERE_MACHINE_CHECK,
} CPU_EXCEPTIONS;

/** Interrupt Definitions **/
typedef enum _INTERRUPT_LIST {
	TIMER_INTERRUPT = 32,
	KEYBOARD_INTERRUPT = 33,
	ATA_INTERRUPT = 46,
	LAPIC_INTERRUPT = 0xEF,
	LAPIC_SIV_INTERRUPT = 0xFF,
	LAPIC_ACTION_VECTOR = 0xDE
} INTERRUPT_LIST;

typedef enum _CPU_ACTION {
	CPU_ACTION_STOP = 0,
	CPU_ACTION_PRINT_ID = 1,
	CPU_ACTION_PERFORM_TLB_SHOOTDOWN = 2,
	CPU_ACTION_WRITE_DEBUG_REGS = 3,
	CPU_ACTION_CLEAR_DEBUG_REGS = 4,
    CPU_ACTION_DO_DEFERRED_ROUTINES = 5,
} CPU_ACTION;

enum MADT_TYPES {
    MADT_LAPIC = 0,
    MADT_IOAPIC = 1,
    MADT_INTERUPT_SOURCE_OVERRIDE = 2,
    MADT_NON_MASKABLE_INTERRUPT = 4,
    MADT_X2APIC = 9
};

enum {
    CPUID_FEAT_ECX_SSE3 = 1 << 0,
    CPUID_FEAT_ECX_PCLMUL = 1 << 1,
    CPUID_FEAT_ECX_DTES64 = 1 << 2,
    CPUID_FEAT_ECX_MONITOR = 1 << 3,
    CPUID_FEAT_ECX_DS_CPL = 1 << 4,
    CPUID_FEAT_ECX_VMX = 1 << 5,
    CPUID_FEAT_ECX_SMX = 1 << 6,
    CPUID_FEAT_ECX_EST = 1 << 7,
    CPUID_FEAT_ECX_TM2 = 1 << 8,
    CPUID_FEAT_ECX_SSSE3 = 1 << 9,
    CPUID_FEAT_ECX_CID = 1 << 10,
    CPUID_FEAT_ECX_SDBG = 1 << 11,
    CPUID_FEAT_ECX_FMA = 1 << 12,
    CPUID_FEAT_ECX_CX16 = 1 << 13,
    CPUID_FEAT_ECX_XTPR = 1 << 14,
    CPUID_FEAT_ECX_PDCM = 1 << 15,
    CPUID_FEAT_ECX_PCID = 1 << 17,
    CPUID_FEAT_ECX_DCA = 1 << 18,
    CPUID_FEAT_ECX_SSE4_1 = 1 << 19,
    CPUID_FEAT_ECX_SSE4_2 = 1 << 20,
    CPUID_FEAT_ECX_X2APIC = 1 << 21,
    CPUID_FEAT_ECX_MOVBE = 1 << 22,
    CPUID_FEAT_ECX_POPCNT = 1 << 23,
    CPUID_FEAT_ECX_TSC = 1 << 24,
    CPUID_FEAT_ECX_AES = 1 << 25,
    CPUID_FEAT_ECX_XSAVE = 1 << 26,
    CPUID_FEAT_ECX_OSXSAVE = 1 << 27,
    CPUID_FEAT_ECX_AVX = 1 << 28,
    CPUID_FEAT_ECX_F16C = 1 << 29,
    CPUID_FEAT_ECX_RDRAND = 1 << 30,
    CPUID_FEAT_ECX_HYPERVISOR = 1 << 31,

    CPUID_FEAT_EDX_FPU = 1 << 0,
    CPUID_FEAT_EDX_VME = 1 << 1,
    CPUID_FEAT_EDX_DE = 1 << 2,
    CPUID_FEAT_EDX_PSE = 1 << 3,
    CPUID_FEAT_EDX_TSC = 1 << 4,
    CPUID_FEAT_EDX_MSR = 1 << 5,
    CPUID_FEAT_EDX_PAE = 1 << 6,
    CPUID_FEAT_EDX_MCE = 1 << 7,
    CPUID_FEAT_EDX_CX8 = 1 << 8,
    CPUID_FEAT_EDX_APIC = 1 << 9,
    CPUID_FEAT_EDX_SEP = 1 << 11,
    CPUID_FEAT_EDX_MTRR = 1 << 12,
    CPUID_FEAT_EDX_PGE = 1 << 13,
    CPUID_FEAT_EDX_MCA = 1 << 14,
    CPUID_FEAT_EDX_CMOV = 1 << 15,
    CPUID_FEAT_EDX_PAT = 1 << 16,
    CPUID_FEAT_EDX_PSE36 = 1 << 17,
    CPUID_FEAT_EDX_PSN = 1 << 18,
    CPUID_FEAT_EDX_CLFLUSH = 1 << 19,
    CPUID_FEAT_EDX_DS = 1 << 21,
    CPUID_FEAT_EDX_ACPI = 1 << 22,
    CPUID_FEAT_EDX_MMX = 1 << 23,
    CPUID_FEAT_EDX_FXSR = 1 << 24,
    CPUID_FEAT_EDX_SSE = 1 << 25,
    CPUID_FEAT_EDX_SSE2 = 1 << 26,
    CPUID_FEAT_EDX_SS = 1 << 27,
    CPUID_FEAT_EDX_HTT = 1 << 28,
    CPUID_FEAT_EDX_TM = 1 << 29,
    CPUID_FEAT_EDX_IA64 = 1 << 30,
    CPUID_FEAT_EDX_PBE = 1 << 31
};

// ------------------ STRUCTURES ------------------

#pragma pack(push, 1)
typedef struct _IDT_PTR {
	uint16_t limit;
	uint64_t base;
} IDT_PTR;

typedef struct _IDT_ENTRY_64 {
	uint16_t offset_low;
	uint16_t selector;
	uint8_t  ist;
	uint8_t  type_attr;
	uint16_t offset_mid;
	uint32_t offset_high;
	uint32_t zero;
} IDT_ENTRY64;
#pragma pack(pop)

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

typedef struct _SMP_BOOTINFO {
    uint64_t magic;
    uint64_t kernel_pml4_phys;  // from boot_info_local.Pml4Phys
    uint64_t ap_entry_virt;     // kernel virtual address of ap_main()
    uint32_t cpu_count;
    uint32_t lapic_base;
} SMP_BOOTINFO;

typedef struct __attribute__((packed)) _GDTEntry64 {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} GDTEntry64;

typedef struct __attribute__((packed)) _GDTPtr {
    uint16_t limit;
    uint64_t base;
} GDTPtr;

typedef struct __attribute__((packed)) _TSS {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7]; // This is the Interrupt Stack Table
    uint32_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} TSS;

typedef void (*DebugCallback)(void*);

typedef struct _DEBUG_REGISTERS {
    uint64_t dr7;
    uint64_t address;
    DebugCallback callback;
} DEBUG_REGISTERS;

typedef struct _PAGE_PARAMETERS {
    uint64_t addressToInvalidate;
} PAGE_PARAMETERS;

typedef struct _IPI_PARAMS {
    struct _DEBUG_REGISTERS debugRegs;
    struct _PAGE_PARAMETERS pageParams;
} IPI_PARAMS;

// ------------------ MACROS ------------------
#define AP_TRAMP_PHYS 0x7000ULL
#define AP_TRAMP_SIZE 0x1000UL   // single page
#define AP_TRAMP_APMAIN_OFFSET 0x1000ULL
#define AP_TRAMP_PML4_OFFSET 0x2000ULL
#define AP_TRAMP_CPUS_OFFSET 0x2500ULL
#define MAX_CPUS 32
#define LAPIC_ID 0x020
#define SMP_MAGIC 0x4D4154414E454C00 // MATANEL\0
#define IST_SIZE (16*1024) // 16 KiB
#define IST_ALIGNMENT 16
// Vendor strings from CPUs.
#define CPUID_VENDOR_AMD           "AuthenticAMD"
#define CPUID_VENDOR_AMD_OLD       "AMDisbetter!" // Early engineering samples of AMD K5 processor
#define CPUID_VENDOR_INTEL         "GenuineIntel"
#define CPUID_VENDOR_VIA           "VIA VIA VIA "
#define CPUID_VENDOR_TRANSMETA     "GenuineTMx86"
#define CPUID_VENDOR_TRANSMETA_OLD "TransmetaCPU"
#define CPUID_VENDOR_CYRIX         "CyrixInstead"
#define CPUID_VENDOR_CENTAUR       "CentaurHauls"
#define CPUID_VENDOR_NEXGEN        "NexGenDriven"
#define CPUID_VENDOR_UMC           "UMC UMC UMC "
#define CPUID_VENDOR_SIS           "SiS SiS SiS "
#define CPUID_VENDOR_NSC           "Geode by NSC"
#define CPUID_VENDOR_RISE          "RiseRiseRise"
#define CPUID_VENDOR_VORTEX        "Vortex86 SoC"
#define CPUID_VENDOR_AO486         "MiSTer AO486"
#define CPUID_VENDOR_AO486_OLD     "GenuineAO486"
#define CPUID_VENDOR_ZHAOXIN       "  Shanghai  "
#define CPUID_VENDOR_HYGON         "HygonGenuine"
#define CPUID_VENDOR_ELBRUS        "E2K MACHINE "

// Vendor strings from hypervisors.
#define CPUID_VENDOR_QEMU          "TCGTCGTCGTCG"
#define CPUID_VENDOR_KVM           " KVMKVMKVM  "
#define CPUID_VENDOR_VMWARE        "VMwareVMware"
#define CPUID_VENDOR_VIRTUALBOX    "VBoxVBoxVBox"
#define CPUID_VENDOR_XEN           "XenVMMXenVMM"
#define CPUID_VENDOR_HYPERV        "Microsoft Hv"
#define CPUID_VENDOR_PARALLELS     " prl hyperv "
#define CPUID_VENDOR_PARALLELS_ALT " lrpepyh vr " // Sometimes Parallels incorrectly encodes "prl hyperv" as "lrpepyh vr" due to an endianness mismatch.
#define CPUID_VENDOR_BHYVE         "bhyve bhyve "
#define CPUID_VENDOR_QNX           " QNXQVMBSQG "

/// ------------------ FUNCTIONS ------------------

void APMain(void);
void MhInitializeSMP(uint8_t* apic_list, uint32_t cpu_count, uint32_t lapicAddress);
void MhSendActionToCpusAndWait(CPU_ACTION action, IPI_PARAMS parameter);

extern int smp_cpu_count;

// Didn't get rename yet.
void set_idt_gate(int n, unsigned long int handler);
void install_idt(void);
void init_interrupts(void);

void lapic_init_cpu(void);                    // call once on BSP early
void lapic_enable(void);
uint32_t lapic_mmio_read(uint32_t off);
void lapic_mmio_write(uint32_t off, uint32_t val);
void lapic_eoi(void);
// lapic spurious interrupt vector, protects against faulty interrupts.
void lapic_init_siv(void);
// send IPI to APIC id 
// apic_id - APICId of the CPU.
// vector - IDT Vector number
// flags - specified cpu flags, 0 for none.
void lapic_send_ipi(uint8_t apic_id, uint8_t vector, uint32_t flags);
int init_lapic_timer(uint32_t hz);           // calibrate + start periodic timer at `hz` (returns 0 on success)
void pit_sleep_ms(uint32_t ms);
void lapic_timer_calibrate(void);

extern bool checkcpuid(void);

// Get CPU Model number
static inline int getCpuModel(void) {
    int ebx, unused;
    __cpuid(0, unused, ebx, unused, unused);
    return ebx;
}

// Check for APIC Availability
FORCEINLINE
bool checkApic(void) {
    unsigned int eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    return edx & (1 << 9); // bit 9 = APIC
}

FORCEINLINE 
void getCpuName(char* name) {
    unsigned int regs[4];
    char* p = name;

    for (unsigned int i = 0; i < 3; i++) {
        __cpuid(0x80000002 + i, regs[0], regs[1], regs[2], regs[3]);
        kmemcpy(p, regs, sizeof(regs));
        p += sizeof(regs);
    }
    *p = '\0'; // Null-terminate
}

void 
MhHandleInterrupt (
	IN	int vec_num,
	IN	PTRAP_FRAME trap
);

void MiLapicInterrupt(
	bool schedulerEnabled,
	PTRAP_FRAME trap
);

void MiBreakpoint(
	PTRAP_FRAME trap
);

NORETURN
void
MiNonMaskableInterrupt(
	PTRAP_FRAME trap
);

void
MiDivideByZero(
	PTRAP_FRAME trap
);

void
MiDebugTrap(
	PTRAP_FRAME trap
);

NORETURN
void
MiDoubleFault(
	IN  PTRAP_FRAME trap
);

void 
MiInterprocessorInterrupt(
	void
);

void
MiPageFault(
	IN  PTRAP_FRAME trap
);

void
MiInvalidTss(
	IN	PTRAP_FRAME trap
);

void 
MiOverflow(
	PTRAP_FRAME trap
);

void 
MiBoundsCheck(
	PTRAP_FRAME trap
);

void 
MiInvalidOpcode(
	PTRAP_FRAME trap
);

void 
MiNoCoprocessor(
	PTRAP_FRAME trap
);

void 
MiCoprocessorSegmentOverrun(
	PTRAP_FRAME trap
);

void 
MiInvalidTss(
	PTRAP_FRAME trap
);
void 
MiSegmentSelectorNotPresent(
	PTRAP_FRAME trap
);

void 
MiStackSegmentOverrun(
	PTRAP_FRAME trap
);

void 
MiGeneralProtectionFault(
	PTRAP_FRAME trap
);

void 
MiFloatingPointError(
	PTRAP_FRAME trap
);

void
MiAlignmentCheck(
	PTRAP_FRAME trap
);

void 
MiMachineCheck(
	PTRAP_FRAME trap
);

void
MhRequestSoftwareInterrupt(
    IN IRQL RequestIrql
);

MTSTATUS MhInitializeACPI(void);
MTSTATUS MhParseLAPICs(uint8_t* buffer, size_t maxCPUs, uint32_t* cpuCount, uint32_t* lapicAddress);

void
MhRebootComputer(
    void
);

#endif