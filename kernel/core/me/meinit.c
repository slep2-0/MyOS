/*++

Module Name:

    meinit.c

Purpose:

    This translation unit contains the implementation of core system init routines (executive).

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/me.h"
#include "../../includes/mh.h"
#include "../../includes/mg.h"
#include "../../assert.h"
#include "../../includes/mt.h"

/* Register Bit Definitions */
#define CR0_MP              (1UL << 1)   // Monitor Coprocessor
#define CR0_EM              (1UL << 2)   // Emulation
#define CR0_WP              (1UL << 16)  // Write Protect
#define CR0_CD              (1UL << 30)  // Cache Disable

#define CR4_OSFXSR          (1UL << 9)   // OS FXSAVE/FXRSTOR Support
#define CR4_OSXMMEXCPT      (1UL << 10)  // OS Unmasked Exception Support
#define CR4_UMIP            (1UL << 11)  // User Mode Instruction Prevention
#define CR4_FSGSBASE        (1UL << 16)  // Enable RDFSBASE/RDGSBASE/etc
#define CR4_SMEP            (1UL << 20)  // Supervisor Mode Execution Prevention
#define CR4_SMAP            (1UL << 21)  // Supervisor Mode Access Prevention

/* CPUID Feature Bits */
#define CPUID_1_EDX_SSE     (1UL << 25)
#define CPUID_1_EDX_SSE2    (1UL << 26)
#define CPUID_7_EBX_SMEP    (1UL << 7)
#define CPUID_7_EBX_SMAP    (1UL << 20)


static void InitialiseControlRegisters(void) {
    unsigned long cr0 = __read_cr0();
    unsigned long cr4 = __read_cr4();
    unsigned int eax, ebx, ecx, edx;

    // Prepare CR0 Configuration
    cr0 |= CR0_WP; // Write Protect
#ifdef DISABLE_CACHE
    cr0 |= CR0_CD; // Cache Disable
#endif

    // Prepare CR4 Configuration
    cr4 |= CR4_UMIP; // User Mode Instruction Prevention

    // Clear Debug Registers
    for (int i = 0; i < 7; i++) __write_dr(i, 0);

    // Detect & Setup SSE/FPU Bits
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));

    int has_sse = (edx & CPUID_1_EDX_SSE) || (edx & CPUID_1_EDX_SSE2);

    if (has_sse) {
        cr0 &= ~CR0_EM; // Clear Emulation
        cr0 |= CR0_MP;  // Set Monitor Coprocessor
        cr4 |= (CR4_OSFXSR | CR4_OSXMMEXCPT); // Enable SSE/Exceptions
        cr4 |= CR4_FSGSBASE; // Enable FSGSBASE
    }
    else {
        gop_printf(COLOR_RED, "**CPU does not support SSE. Halting.**\n");
        FREEZE();
    }

    // Detect & Setup SMAP/SMEP Bits
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));

    if (ebx & CPUID_7_EBX_SMEP) cr4 |= CR4_SMEP;
    else gop_printf(COLOR_YELLOW, "SMEP not available.\n");

    if (ebx & CPUID_7_EBX_SMAP) cr4 |= CR4_SMAP;
    else gop_printf(COLOR_YELLOW, "SMAP not available.\n");

    // COMMIT REGISTERS TO CPU
    // We MUST write these before executing LDMXCSR below.
    __write_cr0(cr0);
    __write_cr4(cr4);

    // Initialize SSE Hardware
    // Now that CR4.OSFXSR is set in hardware, this instruction is valid.
    if (has_sse) {
        unsigned int mxcsr = 0x1f80;
        __asm__ volatile (
            "fninit\n\t"
            "ldmxcsr %0\n\t"
            : : "m"(mxcsr) : "memory"
            );
    }

    // Enable NX Bit.
    uint64_t EFER = __readmsr(MSR_EFER);
    EFER |= (1 << 11); // EFER.NXe
    __writemsr(MSR_EFER, EFER);
}

static void MeInitGdtTssForCurrentProcessor(void) {
    PPROCESSOR cur = MeGetCurrentProcessor();
    TSS* tss = cur->tss;
    uint64_t* gdt = cur->gdt;
    // gdt is uint64_t gdt[7];
    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFF;
    gdt[2] = 0x00CF92000000FFFF;
    gdt[3] = 0x00CFF2000000FFFF; // User Data
    gdt[4] = 0x00AFFA000000FFFF; // User Code
    uint64_t tss_base = (uint64_t)tss;
    uint32_t limit = sizeof(TSS) - 1;

    // tss entry
    kmemset(tss, 0, sizeof(TSS));
    // Stack and IST's have been moved to MeInitProcessor.
    tss->io_map_base = sizeof(TSS);
    tss->rsp0 = (uint64_t)cur->Rsp0;
    tss->ist[0] = (uint64_t)cur->IstPFStackTop; // IDT.ist = 1
    tss->ist[1] = (uint64_t)cur->IstDFStackTop; // IDT.ist = 2
    tss->ist[2] = (uint64_t)cur->IstTimerStackTop; // IDT.ist = 3
    tss->ist[3] = (uint64_t)cur->IstIpiStackTop; // IDT.ist = 4

    uint64_t tss_limit = (uint64_t)limit; // sizeof(TSS)-1
    // gdt tss descriptor
    uint64_t low = (tss_limit & 0xFFFFULL)
        | ((tss_base & 0xFFFFFFULL) << 16)
        | (0x89ULL << 40)                             // P=1, type=0x9 (available 64-bit TSS)
        | (((tss_limit >> 16) & 0xFULL) << 48)       // limit high nibble -> bits 48..51
        | (((tss_base >> 24) & 0xFFULL) << 56);      // base bits 24..31 -> bits 56..63

    // high qword
    uint64_t high = (tss_base >> 32) & 0xFFFFFFFFULL; // base >> 32 in low 32 bits of high qword

    /* copy two qwords into GDT */
    gdt[5] = low;
    gdt[6] = high;
    const int GDT_ENTRIES = 7;

    GDTPtr gdtr = { .limit = (GDT_ENTRIES * sizeof(uint64_t)) - 1, .base = (uint64_t)gdt };
    __asm__ volatile("lgdt %0" : : "m"(gdtr));
    __asm__ volatile(
        "pushq $0x08\n\t"                 /* kernel code selector */
        "leaq 1f(%%rip), %%rax\n\t"     /*load ret*/
        "pushq %%rax\n\t" /*push ret*/
        "lretq\n\t" /*return*/
        "1:\n\t"
        : : : "rax", "memory"
        );


    // tss selector is 16 bit operand
    unsigned short sel = 0x28; // index 5 * 8
    __asm__ volatile("ltr %w0" :: "r"(sel));
}

extern IDT_ENTRY64 IDT[];
extern IDT_PTR  PIDT;

void
MeInitializeProcessor(
    IN PPROCESSOR CPU,
    IN bool InitializeStandardRoutine,
    IN bool AreYouAP
)

/*++

    Routine description:

        Initializes the current PROCESSOR struct to default values.

    Arguments:

        [IN]    PPROCESSOR CPU - Pointer to current PROCESSOR struct.
        [IN]    bool InitializeStandardRoutine - Boolean value indicating if to initialize the TSS & GDT & New IDT of system.
        [IN]    bool AreYouAP - Boolean value indicating the caller of this function is an AP Processor and not the BSP.

    Return Values:

        None.

    Note:

        This routine is ran by every CPU on the system on its startup.

--*/

{
    if (InitializeStandardRoutine && !AreYouAP) goto StartInit; // If we are BSP, and we want to run the routines, go to them immediately and skip the basic init. If we are AP, we do basic init and start routines anyway.
    // Initialize the CR registers.
    InitialiseControlRegisters();

    CPU->self = CPU;
    CPU->currentIrql = PASSIVE_LEVEL;
    CPU->schedulerEnabled = NULL; // since NULL is 0, it would be false.
    CPU->currentThread = NULL;
    CPU->readyQueue.head = CPU->readyQueue.tail = NULL;
    // Initialize the DPC Lock & list head.
    CPU->DpcData.DpcLock.locked = 0;
    InitializeListHead(&CPU->DpcData.DpcListHead);

    // Initialize DPC Fields.
    CPU->MaximumDpcQueueDepth = 4; // Baseline.
    CPU->MinimumDpcRate = 1000; // 1000 DPCs per second baseline (TODO DPC Throttling)
    CPU->DpcRequestRate = 0; // Initialized to zero.
    CPU->DpcRoutineActive = false;
    CPU->DpcInterruptRequested = false;

    // Initialize system calls.
    MtSetupSyscall();

    if (!InitializeStandardRoutine && !AreYouAP) return; // If we are BSP, and we do not want to run the routines below, return. If we are AP, we run it none the less.

StartInit: {
    // Initialize CPU RSP0 and IST Stacks.
    // RSP0 Is used on anything else that the IST already own.
    // If we have an IST for IDT 14 (Page Fault), RSP0 will not be taken.
    // If we don't RSP0 will be taken.
    // RSP0 Is also taken in syscall instructions, but it is immediately replaced by ITHREAD.KernelStack.

    // Create RSP0 and ISTs for processor.
    void* Rsp0 = MiCreateKernelStack(false);
    void* IstPf = MiCreateKernelStack(true);
    void* IstDf = MiCreateKernelStack(true);
    void* IstIpi = MiCreateKernelStack(false);
    void* IstTimer = MiCreateKernelStack(false);
    bool exists = (IstTimer && IstIpi && IstDf && IstPf && Rsp0) != 0;
    assert(exists == true);
    CPU->Rsp0 = Rsp0;
    CPU->IstPFStackTop = IstPf;
    CPU->IstDFStackTop = IstDf;
    CPU->IstIpiStackTop = IstIpi;
    CPU->IstTimerStackTop = IstTimer;

    // Create new GDT and TSS For Processor.
    // Allocate TSS.
    void* tss = MmAllocatePoolWithTag(NonPagedPool, sizeof(TSS), ' ssT'); // If fails on here, check alignment (16 byte)
    CPU->tss = tss;

    // Allocate GDT.
    uint64_t* gdt = MmAllocatePoolWithTag(NonPagedPool, sizeof(uint64_t) * 7, ' TDG');
    CPU->gdt = gdt;

    MeInitGdtTssForCurrentProcessor();

    // ISTs
    IDT[14].ist = 1; // First one is page fault.
    IDT[8].ist = 2; // Second one is double fault.
    IDT[VECTOR_CLOCK].ist = 3; // Third one is the LAPIC Timer.
    IDT[VECTOR_IPI].ist = 4; // Fourth one is the LAPIC IPI.

    // Reload IDT with set stacks.
    __lidt(&PIDT);
    }
}