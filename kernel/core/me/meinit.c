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
#include "../../includes/mg.h"

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
}

void
MeInitializeProcessor(
    IN PPROCESSOR CPU
)

/*++

    Routine description:

        Initializes the current PROCESSOR struct to default values.

    Arguments:

        [IN]    PPROCESSOR CPU - Pointer to current PROCESSOR struct.

    Return Values:

        None.

    Note:

        This routine is ran by every CPU on the system on its startup.

--*/

{
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
}