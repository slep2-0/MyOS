/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Core Kernel Entry Point for MatanelOS.
 */

#include "assert.h"
#include "kernel.h"
#include "includes/exception.h"
#ifndef _MSC_VER
_Static_assert(sizeof(void*) == 8, "This Kernel is 64 bit only! The 32bit version is deprecated.");
#endif

#define MT_STRESS_MODE_NORMAL      0
#define MT_STRESS_MODE_COLD_BOOT   1
#define MT_STRESS_MODE_RANDOMIZED  2
#define MT_STRESS_MODE_EXCEPTION_CHAIN 3

#ifndef MT_STRESS_MODE
#define MT_STRESS_MODE MT_STRESS_MODE_NORMAL
#endif

#ifndef MT_STRESS_AUTOMATION
#define MT_STRESS_AUTOMATION 0
#endif

#ifndef MT_STRESS_DURATION_SECONDS
#define MT_STRESS_DURATION_SECONDS 1800
#endif

#if MT_STRESS_MODE < MT_STRESS_MODE_NORMAL || \
    MT_STRESS_MODE > MT_STRESS_MODE_EXCEPTION_CHAIN
#error "MT_STRESS_MODE is invalid"
#endif

/**
Global variables initialization
**/

/*
Kernel Specific
*/
bool isBugChecking = false;
bool allApsInitialized = false;
PROCESSOR cpu0; // In UP Mode - Will be the place the CPU struct lives permanently, however in SMP mode, the struct transfers to cpus[my_id] after initializing SMP.

/*
Boot Parameters
*/
GOP_PARAMS gop_local;
BOOT_INFO boot_info_local;

/*
AHCI Specifications
*/
#define MAX_AHCI_CONTROLLERS 32
uint64_t ahci_bases_local[MAX_AHCI_CONTROLLERS];


/**
Ended
**/


#define MAX_MEMORY_MAP_SIZE 0x8000  // 32 KB, enough for ~512 descriptors (this shouldn't be used, since we init the PFN db with the ptr from original UEFI, but eh, whatevs)

static EFI_MEMORY_DESCRIPTOR memory_map_copy[MAX_MEMORY_MAP_SIZE / sizeof(EFI_MEMORY_DESCRIPTOR)];

void copy_memory_map(BOOT_INFO* boot_info) {
    if (!boot_info || !boot_info->MemoryMap) return;
    if (boot_info->MapSize > MAX_MEMORY_MAP_SIZE) {
        // handle error, memory map too big
        MeBugCheck(MEMORY_MAP_SIZE_OVERRUN);
    }

    // Copy the entire memory map into the static buffer
    kmemcpy(memory_map_copy, boot_info->MemoryMap, boot_info->MapSize);

    boot_info_local.MemoryMap = memory_map_copy;
    boot_info_local.MapSize = boot_info->MapSize;
    boot_info_local.DescriptorSize = boot_info->DescriptorSize;
    boot_info_local.DescriptorVersion = boot_info->DescriptorVersion;
}

void copy_gop(BOOT_INFO* boot_info) {
    if (!boot_info || !boot_info->Gop.FrameBufferBase) return;

    // Copy the GOP data to a local global variable
    gop_local = (boot_info->Gop);

    // Update all relevant pointers to point to the local copy
    boot_info_local.Gop = gop_local;
}


void init_boot_info(BOOT_INFO* boot_info) {
    if (!boot_info) return;

    copy_memory_map(boot_info);
    copy_gop(boot_info);
    if (boot_info->AhciCount > MAX_AHCI_CONTROLLERS) {
        MeBugCheck(BAD_AHCI_COUNT);
    }
    for (uint32_t i = 0; i < boot_info->AhciCount; i++) {
        ahci_bases_local[i] = boot_info->AhciBarBases[i];
    }
    // Copy the local array into local boot info.
    kmemcpy(boot_info_local.AhciBarBases, ahci_bases_local, sizeof(ahci_bases_local));
    boot_info_local.AhciCount = boot_info->AhciCount;
    boot_info_local.KernelStackTop = boot_info->KernelStackTop;
    boot_info_local.Pml4Phys = boot_info->Pml4Phys;
    boot_info_local.AcpiRsdpPhys = boot_info->AcpiRsdpPhys;
}

static inline bool interrupts_enabled(void) {
    unsigned long flags;
    __asm__ __volatile__("pushfq; popq %0"
        : "=r"(flags)
        :
        : "memory", "cc");
    return (flags & (1UL << 9)) != 0; // IF is bit 9
}

void kernel_idle_checks(void) {
    gop_printf(0xFF000FF0, "Reached the idle thread!\n");
    // Reaching the idle thread with interrupts off means something did not have the RFLAGS IF Bit set.
    if (!interrupts_enabled()) {
        gop_printf(COLOR_RED, "**Interrupts aren't enabled..\n Stack Trace:\n");
        FREEZE();
    }
    while (1) {
        if (MeGetCurrentProcessor()->ZombieThread) {
            // Schedule is a restore-only primitive. Capture a fresh idle
            // continuation before using it to reap the previous thread.
            MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
        }
        __hlt();
        //Schedule();
    }
}

static void MeCreateInitialUserModeProcess(void) {
    gop_printf(COLOR_OLIVE, "Starting initial user mode process.\n");
    HANDLE hProcess = MT_INVALID_HANDLE;
    MTSTATUS status = PsCreateProcess("terminateMyself.mtexe", &hProcess, MT_PROCESS_ALL_ACCESS, 0);
    if (MT_FAILURE(status)) {
        gop_printf(COLOR_RED, "Failed to create initial user process: %x\n", status);
        return;
    }

    // Always free handles, important.
    status = HtClose(hProcess);
    if (MT_FAILURE(status)) {
        gop_printf(COLOR_RED, "Failed to close initial process handle: %x\n", status);
    }
}

// All CPUs
uint8_t apic_list[MAX_CPUS];
uint32_t cpu_count = 0;
uint32_t lapicAddress;
bool smpInitialized;

/// Stack cookies detect any overwritten function frame; they do not prove that
/// the stack reached its guard page. Complete exhaustion is guarded separately
/// by MiCreateKernelStack's unmapped page.
#ifdef DEBUG
// Stack Canary GCC
volatile uintptr_t __stack_chk_guard;

static
void
MiInitializeStackCookie(void)
{
    uint64_t Candidate = 0;

    for (int Attempt = 0; Attempt < 64; Attempt++) {
        if (__rdrand64(&Candidate)) {
            break;
        }
    }

    if (Candidate == 0) {
        Candidate = __rdtsc();
    }

    if (Candidate == 0) {
        Candidate = 0xDEADC0DEDEADC0DE;
    }

    __stack_chk_guard = Candidate;
}

__attribute__((noreturn))
void __stack_chk_fail(void) {
    __cli();
    PETHREAD Thread = PsGetCurrentThread();
    void* SavedRsp = Thread
        ? (void*)(uintptr_t)Thread->InternalThread.TrapRegisters.rsp
        : NULL;
    MeBugCheckEx(KERNEL_STACK_COOKIE_CORRUPTION,
        (void*)__builtin_return_address(0),
        (void*)(uintptr_t)__read_rsp(),
        Thread,
        SavedRsp);
}
#endif

// TODO allocate dynamically (use PsCreateProcess)
EPROCESS PsInitialSystemProcess;

static void InitSystemProcess(void) {
    // TODO Setup system process like PsCreateProcess, and modify the func.
    kmemset(&PsInitialSystemProcess, 0, sizeof(EPROCESS));
    PsInitialSystemProcess.PID = 4; // Initial PID, reserved.
    PsInitialSystemProcess.ParentProcess = 0; // No creator process
    kstrncpy(PsInitialSystemProcess.ImageName, "mtoskrnl.mtexe", sizeof(PsInitialSystemProcess.ImageName)); // Name for the process
    PsInitialSystemProcess.priority = 0; // TODO
    PsInitialSystemProcess.InternalProcess.PageDirectoryPhysical = __read_cr3(); // The PML4 of the system process, is our kernel PML4.
    PsInitialSystemProcess.CreationTime = MeGetEpoch();
    PsInitialSystemProcess.MainThread = MeGetCurrentProcessor()->idleThread; // The main thread for the SYSTEM process is the BSP's idle thread.
    InitializeListHead(&PsInitialSystemProcess.AllThreads);
    PsInitialSystemProcess.ObjectTable = HtCreateHandleTable(&PsInitialSystemProcess);
    if (!PsInitialSystemProcess.ObjectTable) {
        MeBugCheckEx(MEMORY_LIMIT_REACHED, &PsInitialSystemProcess,
            (void*)(uintptr_t)RETADDR(0), NULL, NULL);
    }

    PsInitialSystemProcess.Flags = ProcessBreakOnTermination;
    MsInitializeDispatcherHeader(&PsInitialSystemProcess.InternalProcess.Header, 0, DispatcherProcess);
    PsInitialSystemProcess.ExitStatus = MT_PENDING;

    // Initialize the push locks
    MsInitializePushLock(&PsInitialSystemProcess.ProcessLock);
    MsInitializePushLock(&PsInitialSystemProcess.ThreadListLock);
    MsInitializePushLock(&PsInitialSystemProcess.AddressSpaceLock);
    MsInitializePushLock(&PsInitialSystemProcess.VadLock);
}

extern uint8_t bss_start;
extern uint8_t bss_end;

static void DbgCallback(void* vinfo) {
    DBG_CALLBACK_INFO* info = (DBG_CALLBACK_INFO*)vinfo;
    gop_printf(COLOR_RED, "**->>>>> RIP %p TOUCHED THE GLOBAL STACK CANARY!**\n", (void*)(uintptr_t)info->trap->rip);
    FREEZE_OTHER_CPUS();
    FREEZE();
}

#define STRESS2_PROGRESS_TARGET       1000000ULL
#define STRESS2_PUBLISH_INTERVAL      4096ULL
#define STRESS2_TIMEOUT_TICKS         (5ULL * TICK_HZ)
#define STRESS2_DPC_REQUEUE_PER_CPU   10000U
#define STRESS2_TARGET_CPU_COUNT      4U
#ifndef STRESS_SUITE_DPC_DURATION_SECONDS
#define STRESS_SUITE_DPC_DURATION_SECONDS 10ULL
#endif
#define STRESS2C_PROGRESS_SECONDS     60ULL
#define STRESS2C_CLOCK_STALL_SECONDS  2ULL

typedef enum _STRESS2_DPC_FAILURE_STAGE {
    Stress2DpcInvalidExecutionContext = 1,
    Stress2DpcSelfRequeueFailed,
    Stress2DpcExecutionOvershoot,
    Stress2DpcQueueNotIdle,
    Stress2DpcDuplicateAdmissionFailure,
    Stress2DpcRemovalFailure,
    Stress2DpcRemovedButExecuted,
    Stress2DpcRaceOutcomeInvalid,
    Stress2DpcUnexpectedProcessorCount,
    Stress2DpcRetirementTimeout,
    Stress2DpcTopologyFailure,
    Stress2DpcEnduranceStall,
    Stress2DpcClockStall,
    Stress2DpcTscCalibrationFailure
} STRESS2_DPC_FAILURE_STAGE;

typedef enum _STRESS2C_PHASE {
    Stress2CPhasePreparing = 1,
    Stress2CPhaseInserting,
    Stress2CPhaseRequesting,
    Stress2CPhaseRemoving,
    Stress2CPhaseWaiting
} STRESS2C_PHASE;

static volatile uint64_t Stress2Counter1;
static volatile uint64_t Stress2Counter2;
static volatile uint64_t Stress2StartTick;
static volatile uint32_t Stress2ControllerClaimed;
static volatile bool Stress2BusyActive;
static volatile bool Stress2SuiteComplete;
static volatile uint32_t Stress2DpcExecutions;
static volatile uint32_t Stress2DpcExecutionTarget;
static volatile bool Stress2CEnduranceActive;
static volatile uint64_t Stress2CHeartbeat;
static volatile uint64_t Stress2CWatchdogHeartbeat;
static volatile uint64_t Stress2CWatchdogTsc;
static volatile uint64_t Stress2CIteration;
static volatile uint32_t Stress2CCpuNumber;
static volatile uint32_t Stress2COperation;
static volatile uint32_t Stress2CPhase;
static uint64_t Stress2CTscTicksPerSecond;
static DPC Stress2Dpc;

NORETURN
static void
Stress2DpcBugCheck(
    STRESS2_DPC_FAILURE_STAGE Stage,
    void* Detail1,
    void* Detail2,
    void* Detail3
)
{
    // DPC_EXECUTE_FAILURE: P1 is the test stage; P2-P4 are stage details.
    MeBugCheckEx(
        DPC_EXECUTE_FAILURE,
        (void*)(uintptr_t)Stage,
        Detail1,
        Detail2,
        Detail3
    );
}

static void
Stress2ValidateDpcTopology(
    PPROCESSOR Cpu,
    bool ExpectQueued
)
{
    IRQL OldIrql;
    MeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    MsAcquireSpinlockAtDpcLevel(&Cpu->DpcData.DpcLock);

    PDOUBLY_LINKED_LIST Head = &Cpu->DpcData.DpcListHead;
    PDOUBLY_LINKED_LIST Entry = &Stress2Dpc.DpcListEntry;
    uint32_t QueueDepth = Cpu->DpcData.DpcQueueDepth;
    void* DpcData = (void*)Stress2Dpc.DpcData;

    bool HeadValid = Head->Flink != NULL &&
        Head->Blink != NULL &&
        Head->Flink->Blink == Head &&
        Head->Blink->Flink == Head;
    bool EntryValid = Entry->Flink != NULL &&
        Entry->Blink != NULL &&
        Entry->Flink->Blink == Entry &&
        Entry->Blink->Flink == Entry;
    bool MembershipValid = ExpectQueued
        ? DpcData == &Cpu->DpcData && QueueDepth == 1 &&
            Head->Flink == Entry && Head->Blink == Entry && EntryValid
        : DpcData == NULL && QueueDepth == 0 &&
            Head->Flink == Head && Head->Blink == Head;

    MsReleaseSpinlockFromDpcLevel(&Cpu->DpcData.DpcLock);
    MeLowerIrql(OldIrql);

    if (!HeadValid || !MembershipValid) {
        uintptr_t QueueState = (uintptr_t)QueueDepth |
            ((uintptr_t)ExpectQueued << 32) |
            ((uintptr_t)HeadValid << 33) |
            ((uintptr_t)EntryValid << 34);
        Stress2DpcBugCheck(
            Stress2DpcTopologyFailure,
            (void*)(uintptr_t)Cpu->ID,
            DpcData,
            (void*)QueueState
        );
    }
}

static void
Stress2DpcRoutine(
    DPC* Dpc,
    void* DeferredContext,
    void* SystemArgument1,
    void* SystemArgument2
)
{
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    PPROCESSOR CurrentCpu = MeGetCurrentProcessor();
    uint32_t ExpectedCpu = Dpc->CpuNumber;
    uintptr_t ContextState = (uintptr_t)MeGetCurrentIrql() |
        ((uintptr_t)MeAreInterruptsEnabled() << 8);

    if (Dpc != &Stress2Dpc ||
        CurrentCpu->ID != ExpectedCpu ||
        MeGetCurrentIrql() != DISPATCH_LEVEL ||
        !MeAreInterruptsEnabled()) {
        Stress2DpcBugCheck(
            Stress2DpcInvalidExecutionContext,
            (void*)(uintptr_t)CurrentCpu->ID,
            (void*)(uintptr_t)ExpectedCpu,
            (void*)ContextState
        );
    }

    // Retirement unlinks the DPC and clears DpcData before invoking us.
    Stress2ValidateDpcTopology(CurrentCpu, false);

    uint32_t Execution = InterlockedIncrementU32(&Stress2DpcExecutions);
    uint32_t Target = InterlockedLoadAcquire(
        &Stress2DpcExecutionTarget
    );

    if (Execution < Target) {
        if (!MeInsertQueueDpc(Dpc, NULL, NULL)) {
            Stress2DpcBugCheck(
                Stress2DpcSelfRequeueFailed,
                (void*)(uintptr_t)ExpectedCpu,
                (void*)(uintptr_t)Execution,
                (void*)(uintptr_t)Target
            );
        }
    }
    else if (Execution > Target) {
        Stress2DpcBugCheck(
            Stress2DpcExecutionOvershoot,
            (void*)(uintptr_t)ExpectedCpu,
            (void*)(uintptr_t)Execution,
            (void*)(uintptr_t)Target
        );
    }

    // A self-requeue owns exactly one target-queue entry; the final callback
    // must leave no membership behind.
    Stress2ValidateDpcTopology(CurrentCpu, Execution < Target);
}

static void
Stress2ValidateDpcIdle(
    PPROCESSOR TargetCpu
)
{
    IRQL OldIrql;
    MeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    MsAcquireSpinlockAtDpcLevel(&TargetCpu->DpcData.DpcLock);

    void* DpcData = (void*)Stress2Dpc.DpcData;
    uint32_t QueueDepth = TargetCpu->DpcData.DpcQueueDepth;
    bool QueueEmpty = IsListEmpty(&TargetCpu->DpcData.DpcListHead);

    MsReleaseSpinlockFromDpcLevel(&TargetCpu->DpcData.DpcLock);
    MeLowerIrql(OldIrql);

    if (DpcData != NULL || QueueDepth != 0 || !QueueEmpty) {
        uintptr_t QueueState = (uintptr_t)QueueDepth |
            ((uintptr_t)!QueueEmpty << 32);
        Stress2DpcBugCheck(
            Stress2DpcQueueNotIdle,
            (void*)(uintptr_t)TargetCpu->ID,
            DpcData,
            (void*)QueueState
        );
    }
}

static void
Stress2RequestTargetDpc(
    PPROCESSOR TargetCpu
)
{
    if (TargetCpu == MeGetCurrentProcessor()) {
        MeRequestCurrentDpcInterrupt();
        return;
    }

    IPI_PARAMS IpiParams = { 0 };
    MhSendActionToSpecificCpuAndWait(
        TargetCpu,
        CPU_ACTION_REQUEST_DPC,
        IpiParams
    );
}

static void
Stress2WaitForDpcIdle(
    PPROCESSOR TargetCpu,
    uint32_t ExpectedExecutions
)
{
    uint64_t StartTick = InterlockedLoadAcquire(
        &MeSystemTickCount
    );

    for (;;) {
        uint32_t Executions = InterlockedLoadAcquire(
            &Stress2DpcExecutions
        );
        PDPC CurrentDpc = InterlockedLoadAcquire(
            &TargetCpu->CurrentDeferredRoutine
        );
        bool RoutineActive = InterlockedLoadAcquire(
            &TargetCpu->DpcRoutineActive
        );
        bool InterruptRequested = InterlockedLoadAcquire(
            &TargetCpu->DpcInterruptRequested
        );

        if (Executions == ExpectedExecutions &&
            CurrentDpc == NULL &&
            !RoutineActive &&
            !InterruptRequested) {
            break;
        }

        uint64_t CurrentTick = InterlockedLoadAcquire(
            &MeSystemTickCount
        );
        if (CurrentTick - StartTick > STRESS2_TIMEOUT_TICKS) {
            Stress2DpcBugCheck(
                Stress2DpcRetirementTimeout,
                (void*)(uintptr_t)TargetCpu->ID,
                (void*)(uintptr_t)Executions,
                (void*)(uintptr_t)ExpectedExecutions
            );
        }
        __pause();
    }

    Stress2ValidateDpcIdle(TargetCpu);
    Stress2ValidateDpcTopology(TargetCpu, false);
}

static void
Stress2CalibrateTsc(void)
{
    uint64_t TicksPerMillisecond = MhGetTscTicksPerMillisecond();

    if (TicksPerMillisecond == 0 ||
        TicksPerMillisecond > UINT64_MAX / 1000ULL) {
        Stress2DpcBugCheck(
            Stress2DpcTscCalibrationFailure,
            (void*)(uintptr_t)TicksPerMillisecond,
            NULL,
            NULL
        );
    }

    Stress2CTscTicksPerSecond = TicksPerMillisecond * 1000ULL;
}

static void
Stress2CPublishPhase(
    uint64_t Iteration,
    uint32_t CpuNumber,
    uint32_t Operation,
    STRESS2C_PHASE Phase
)
{
    InterlockedStoreRelease(&Stress2CIteration, Iteration);
    InterlockedStoreRelease(&Stress2CCpuNumber, CpuNumber);
    InterlockedStoreRelease(&Stress2COperation, Operation);
    InterlockedStoreRelease(&Stress2CPhase, (uint32_t)Phase);
}

static void
Stress2CCheckWatchdog(void)
{
    if (!InterlockedLoadAcquire(&Stress2CEnduranceActive)) {
        return;
    }

    uint64_t CurrentTsc = __rdtsc();
    uint64_t Heartbeat = InterlockedLoadAcquire(
        &Stress2CHeartbeat
    );
    uint64_t ObservedHeartbeat = InterlockedLoadAcquire(
        &Stress2CWatchdogHeartbeat
    );

    if (Heartbeat != ObservedHeartbeat) {
        InterlockedStoreRelease(
            &Stress2CWatchdogHeartbeat,
            Heartbeat
        );
        InterlockedStoreRelease(
            &Stress2CWatchdogTsc,
            CurrentTsc
        );
        return;
    }

    uint64_t LastProgressTsc = InterlockedLoadAcquire(
        &Stress2CWatchdogTsc
    );
    uint64_t StallCycles = 5ULL * Stress2CTscTicksPerSecond;
    if (CurrentTsc - LastProgressTsc <= StallCycles) {
        return;
    }

    uint64_t Iteration = InterlockedLoadAcquire(
        &Stress2CIteration
    );
    uint32_t CpuNumber = InterlockedLoadAcquire(
        &Stress2CCpuNumber
    );
    uint32_t Operation = InterlockedLoadAcquire(
        &Stress2COperation
    );
    uint32_t Phase = InterlockedLoadAcquire(
        &Stress2CPhase
    );
    uintptr_t Location = (uintptr_t)CpuNumber |
        ((uintptr_t)Operation << 8) |
        ((uintptr_t)Phase << 16);

    Stress2DpcBugCheck(
        Stress2DpcEnduranceStall,
        (void*)Location,
        (void*)(uintptr_t)Iteration,
        (void*)(uintptr_t)Heartbeat
    );
}

static void
Stress2RunDpcEndurance(
    uint32_t ProcessorCount,
    uint64_t DurationSeconds
)
{
    uint64_t StartTsc = __rdtsc();
    uint64_t ProgressCycles = Stress2CTscTicksPerSecond *
        STRESS2C_PROGRESS_SECONDS;
    uint64_t DurationCycles = Stress2CTscTicksPerSecond * DurationSeconds;
    uint64_t ClockStallCycles = Stress2CTscTicksPerSecond *
        STRESS2C_CLOCK_STALL_SECONDS;
    uint64_t NextProgressTsc = StartTsc + ProgressCycles;
    uint64_t LastClockTick = InterlockedLoadAcquire(
        &MeSystemTickCount
    );
    uint64_t LastClockProgressTsc = StartTsc;
    uint64_t Iterations = 0;

    Stress2CHeartbeat = 0;
    Stress2CWatchdogHeartbeat = 0;
    Stress2CWatchdogTsc = StartTsc;
    Stress2CPublishPhase(0, 0, 0, Stress2CPhasePreparing);
    InterlockedStoreRelease(&Stress2CEnduranceActive, true);
    gop_printf(
        COLOR_GREEN,
        "STRESS 2C START (%llu seconds, combined-suite window)\n",
        (unsigned long long)DurationSeconds
    );

    for (;;) {
        uint64_t CurrentTsc = __rdtsc();
        uint64_t CurrentTick = InterlockedLoadAcquire(
            &MeSystemTickCount
        );

        if (CurrentTick != LastClockTick) {
            LastClockTick = CurrentTick;
            LastClockProgressTsc = CurrentTsc;
        }
        else if (CurrentTsc - LastClockProgressTsc > ClockStallCycles) {
            Stress2DpcBugCheck(
                Stress2DpcClockStall,
                (void*)(uintptr_t)CurrentTick,
                (void*)(uintptr_t)(CurrentTsc - LastClockProgressTsc),
                (void*)(uintptr_t)MeGetCurrentProcessor()->ID
            );
        }

        uint64_t ElapsedCycles = CurrentTsc - StartTsc;
        if (ElapsedCycles >= DurationCycles) {
            break;
        }

        uint32_t CpuNumber = (uint32_t)(Iterations % ProcessorCount);
        uint32_t Operation = (uint32_t)(
            (Iterations / ProcessorCount) % 3U
        );
        PPROCESSOR TargetCpu = MeGetProcessorBlock((uint8_t)CpuNumber);
        uint32_t Before = InterlockedLoadAcquire(
            &Stress2DpcExecutions
        );

        Stress2CPublishPhase(
            Iterations,
            CpuNumber,
            Operation,
            Stress2CPhasePreparing
        );
        MeSetTargetProcessorDpc(&Stress2Dpc, CpuNumber);
        Stress2ValidateDpcIdle(TargetCpu);

        if (Operation == 0) {
            // Hold one DPC queued, reject a duplicate, then execute it.
            InterlockedStoreRelease(
                &Stress2DpcExecutionTarget,
                Before + 1
            );
            Stress2CPublishPhase(
                Iterations,
                CpuNumber,
                Operation,
                Stress2CPhaseInserting
            );
            bool Inserted = MeInsertQueueDpc(&Stress2Dpc, NULL, NULL);
            bool Duplicate = MeInsertQueueDpc(&Stress2Dpc, NULL, NULL);
            if (!Inserted || Duplicate) {
                Stress2DpcBugCheck(
                    Stress2DpcDuplicateAdmissionFailure,
                    (void*)(uintptr_t)CpuNumber,
                    (void*)(uintptr_t)Inserted,
                    (void*)(uintptr_t)Duplicate
                );
            }
            Stress2CPublishPhase(
                Iterations,
                CpuNumber,
                Operation,
                Stress2CPhaseRequesting
            );
            Stress2RequestTargetDpc(TargetCpu);
            Stress2CPublishPhase(
                Iterations,
                CpuNumber,
                Operation,
                Stress2CPhaseWaiting
            );
            Stress2WaitForDpcIdle(TargetCpu, Before + 1);
        }
        else if (Operation == 1) {
            // Remove before requesting retirement; execution is forbidden.
            InterlockedStoreRelease(
                &Stress2DpcExecutionTarget,
                Before
            );
            Stress2CPublishPhase(
                Iterations,
                CpuNumber,
                Operation,
                Stress2CPhaseInserting
            );
            bool Inserted = MeInsertQueueDpc(&Stress2Dpc, NULL, NULL);
            Stress2CPublishPhase(
                Iterations,
                CpuNumber,
                Operation,
                Stress2CPhaseRemoving
            );
            bool Removed = MeRemoveQueueDpc(&Stress2Dpc);
            bool RemovedTwice = MeRemoveQueueDpc(&Stress2Dpc);
            if (!Inserted || !Removed || RemovedTwice) {
                uintptr_t RemoveFlags = (Inserted ? 1U : 0U) |
                    (Removed ? 2U : 0U) |
                    (RemovedTwice ? 4U : 0U);
                Stress2DpcBugCheck(
                    Stress2DpcRemovalFailure,
                    (void*)(uintptr_t)CpuNumber,
                    (void*)RemoveFlags,
                    (void*)(uintptr_t)Iterations
                );
            }
            Stress2CPublishPhase(
                Iterations,
                CpuNumber,
                Operation,
                Stress2CPhaseWaiting
            );
            Stress2WaitForDpcIdle(TargetCpu, Before);
        }
        else {
            // Request retirement, then race removal against the target CPU.
            InterlockedStoreRelease(
                &Stress2DpcExecutionTarget,
                Before + 1
            );
            Stress2CPublishPhase(
                Iterations,
                CpuNumber,
                Operation,
                Stress2CPhaseInserting
            );
            bool Inserted = MeInsertQueueDpc(&Stress2Dpc, NULL, NULL);
            if (!Inserted) {
                Stress2DpcBugCheck(
                    Stress2DpcRaceOutcomeInvalid,
                    (void*)(uintptr_t)CpuNumber,
                    (void*)(uintptr_t)Before,
                    (void*)(uintptr_t)Iterations
                );
            }

            Stress2CPublishPhase(
                Iterations,
                CpuNumber,
                Operation,
                Stress2CPhaseRequesting
            );
            Stress2RequestTargetDpc(TargetCpu);
            Stress2CPublishPhase(
                Iterations,
                CpuNumber,
                Operation,
                Stress2CPhaseRemoving
            );
            bool Removed = MeRemoveQueueDpc(&Stress2Dpc);
            uint32_t Expected = Removed ? Before : Before + 1;
            Stress2CPublishPhase(
                Iterations,
                CpuNumber,
                Operation,
                Stress2CPhaseWaiting
            );
            Stress2WaitForDpcIdle(TargetCpu, Expected);

            uint32_t After = InterlockedLoadAcquire(
                &Stress2DpcExecutions
            );
            if (After != Expected) {
                Stress2DpcBugCheck(
                    Stress2DpcRaceOutcomeInvalid,
                    (void*)(uintptr_t)CpuNumber,
                    (void*)(uintptr_t)After,
                    (void*)(uintptr_t)Expected
                );
            }
        }

        Iterations++;
        InterlockedIncrementU64(&Stress2CHeartbeat);
        if (CurrentTsc >= NextProgressTsc) {
            gop_printf(
                COLOR_GREEN,
                "STRESS 2C progress %llu/%llu seconds (%llu iterations)\n",
                (unsigned long long)(
                    ElapsedCycles / Stress2CTscTicksPerSecond
                ),
                (unsigned long long)DurationSeconds,
                (unsigned long long)Iterations
            );
            NextProgressTsc += ProgressCycles;
        }
    }

    InterlockedStoreRelease(&Stress2CEnduranceActive, false);
    gop_printf(
        COLOR_GREEN,
        "STRESS 2C PASS (%llu iterations, %u executions)\n",
        (unsigned long long)Iterations,
        Stress2DpcExecutions
    );
}

static void
Stress2RunDpcTests(void)
{
    gop_printf(COLOR_GREEN, "STRESS 2B TIMER PREEMPTION PASS\n");

    uint32_t ProcessorCount = MeGetActiveProcessorCount();
    if (ProcessorCount == 0 || ProcessorCount > STRESS2_TARGET_CPU_COUNT) {
        Stress2DpcBugCheck(
            Stress2DpcUnexpectedProcessorCount,
            (void*)(uintptr_t)ProcessorCount,
            (void*)(uintptr_t)STRESS2_TARGET_CPU_COUNT,
            NULL
        );
    }

    for (uint32_t CpuNumber = 0;
         CpuNumber < ProcessorCount;
         CpuNumber++) {
        PPROCESSOR TargetCpu = MeGetProcessorBlock((uint8_t)CpuNumber);
        uint32_t CpuStart = InterlockedLoadAcquire(
            &Stress2DpcExecutions
        );

        MeSetTargetProcessorDpc(&Stress2Dpc, CpuNumber);
        Stress2ValidateDpcIdle(TargetCpu);

        // LOW_PRIORITY holds the first insertion in the target queue until the
        // explicit request, making duplicate rejection deterministic on APs.
        uint32_t RequeueTarget = CpuStart + STRESS2_DPC_REQUEUE_PER_CPU;
        InterlockedStoreRelease(
            &Stress2DpcExecutionTarget,
            RequeueTarget
        );
        bool FirstInsert = MeInsertQueueDpc(&Stress2Dpc, NULL, NULL);
        bool DuplicateInsert = MeInsertQueueDpc(&Stress2Dpc, NULL, NULL);
        if (!FirstInsert || DuplicateInsert) {
            Stress2DpcBugCheck(
                Stress2DpcDuplicateAdmissionFailure,
                (void*)(uintptr_t)CpuNumber,
                (void*)(uintptr_t)FirstInsert,
                (void*)(uintptr_t)DuplicateInsert
            );
        }

        Stress2RequestTargetDpc(TargetCpu);
        Stress2WaitForDpcIdle(TargetCpu, RequeueTarget);

        uint32_t ExecutionsBeforeRemove = InterlockedLoadAcquire(
            &Stress2DpcExecutions
        );
        bool RemoveInsert = MeInsertQueueDpc(&Stress2Dpc, NULL, NULL);
        bool Removed = MeRemoveQueueDpc(&Stress2Dpc);
        bool RemovedTwice = MeRemoveQueueDpc(&Stress2Dpc);
        if (!RemoveInsert || !Removed || RemovedTwice) {
            uintptr_t RemoveFlags = (RemoveInsert ? 1U : 0U) |
                (Removed ? 2U : 0U) |
                (RemovedTwice ? 4U : 0U);
            Stress2DpcBugCheck(
                Stress2DpcRemovalFailure,
                (void*)(uintptr_t)CpuNumber,
                (void*)RemoveFlags,
                &Stress2Dpc
            );
        }
        Stress2WaitForDpcIdle(TargetCpu, ExecutionsBeforeRemove);

        uint32_t ExecutionsAfterRemove = InterlockedLoadAcquire(
            &Stress2DpcExecutions
        );
        if (ExecutionsAfterRemove != ExecutionsBeforeRemove) {
            Stress2DpcBugCheck(
                Stress2DpcRemovedButExecuted,
                (void*)(uintptr_t)CpuNumber,
                (void*)(uintptr_t)ExecutionsBeforeRemove,
                (void*)(uintptr_t)ExecutionsAfterRemove
            );
        }

        uint32_t RaceStart = ExecutionsAfterRemove;
        InterlockedStoreRelease(
            &Stress2DpcExecutionTarget,
            RaceStart + 1
        );
        bool RaceInsert = MeInsertQueueDpc(&Stress2Dpc, NULL, NULL);
        if (!RaceInsert) {
            Stress2DpcBugCheck(
                Stress2DpcRaceOutcomeInvalid,
                (void*)(uintptr_t)CpuNumber,
                (void*)(uintptr_t)RaceStart,
                NULL
            );
        }
        Stress2RequestTargetDpc(TargetCpu);
        bool RaceRemoved = MeRemoveQueueDpc(&Stress2Dpc);
        uint32_t ExpectedRaceEnd = RaceRemoved ? RaceStart : RaceStart + 1;
        Stress2WaitForDpcIdle(TargetCpu, ExpectedRaceEnd);

        uint32_t RaceEnd = InterlockedLoadAcquire(
            &Stress2DpcExecutions
        );
        bool RaceOutcomeValid = RaceRemoved
            ? RaceEnd == RaceStart
            : RaceEnd == RaceStart + 1;
        if (!RaceInsert || !RaceOutcomeValid) {
            uintptr_t RaceFlags = (RaceInsert ? 1U : 0U) |
                (RaceRemoved ? 2U : 0U);
            Stress2DpcBugCheck(
                Stress2DpcRaceOutcomeInvalid,
                (void*)(uintptr_t)CpuNumber,
                (void*)(uintptr_t)RaceEnd,
                (void*)RaceFlags
            );
        }

        gop_printf(
            COLOR_GREEN,
            "STRESS 2B CPU %u DPC PASS (%u executions)\n",
            CpuNumber,
            RaceEnd - CpuStart
        );
    }

    gop_printf(
        COLOR_GREEN,
        "STRESS 2B PASS (CPUs 0-%u)\n",
        ProcessorCount - 1U
    );

    Stress2RunDpcEndurance(
        ProcessorCount,
        STRESS_SUITE_DPC_DURATION_SECONDS
    );
}

static void
Stress2CheckProgress(void)
{
    uint64_t Counter1 = InterlockedLoadAcquire(&Stress2Counter1);
    uint64_t Counter2 = InterlockedLoadAcquire(&Stress2Counter2);

    if (Counter1 >= STRESS2_PROGRESS_TARGET &&
        Counter2 >= STRESS2_PROGRESS_TARGET) {
        if (InterlockedCompareExchangeU32(
            &Stress2ControllerClaimed,
            1,
            0
        ) == 0) {
            Stress2RunDpcTests();
            InterlockedStoreRelease(&Stress2BusyActive, false);
            InterlockedStoreRelease(&Stress2SuiteComplete, true);
        }
        Stress2CCheckWatchdog();
        return;
    }

    uint64_t CurrentTick = InterlockedLoadAcquire(
        &MeSystemTickCount
    );
    uint64_t StartTick = InterlockedLoadAcquire(
        &Stress2StartTick
    );
    if (CurrentTick - StartTick > STRESS2_TIMEOUT_TICKS) {
        MeBugCheckEx(
            SCHEDULER_FAILURE,
            (void*)(uintptr_t)Counter1,
            (void*)(uintptr_t)Counter2,
            (void*)(uintptr_t)(CurrentTick - StartTick),
            RETADDR(0)
        );
    }
}

static void
Stress2BusyThread1(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);
    uint64_t LocalCounter = 0;

    while (InterlockedLoadAcquire(&Stress2BusyActive)) {
        LocalCounter++;
        if ((LocalCounter % STRESS2_PUBLISH_INTERVAL) == 0) {
            InterlockedStoreRelease(
                &Stress2Counter1,
                LocalCounter
            );
            Stress2CheckProgress();
        }
    }
}

static void
Stress2BusyThread2(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);
    uint64_t LocalCounter = 0;

    while (InterlockedLoadAcquire(&Stress2BusyActive)) {
        LocalCounter++;
        if ((LocalCounter % STRESS2_PUBLISH_INTERVAL) == 0) {
            InterlockedStoreRelease(
                &Stress2Counter2,
                LocalCounter
            );
            Stress2CheckProgress();
        }
    }
}

#define STRESS3_IMMEDIATE_ITERATIONS 1000U
#define STRESS3_SIGNAL_ITERATIONS    10000U
#define STRESS3_TIMEOUT_ITERATIONS   100U
#define STRESS3_RACE_ITERATIONS      1000U
#define STRESS3_MAX_REGISTRATION_RETRIES 1000U
#define STRESS3_TIMEOUT_MS           (5ULL * TICK_MS)
#define STRESS3_RACE_TIMEOUT_MS      TICK_MS
#define STRESS3_WATCHDOG_SECONDS     5ULL

typedef enum _STRESS3_WAIT_MODE {
    Stress3WaitForSignal = 1,
    Stress3WaitForTimeout,
    Stress3WaitForSignalTimeoutRace
} STRESS3_WAIT_MODE;

typedef enum _STRESS3_FAILURE_STAGE {
    Stress3InvalidImmediateStatus = 1,
    Stress3WaiterStartTimeout,
    Stress3RegistrationTimeout,
    Stress3CompletionTimeout,
    Stress3UnexpectedCompletionStatus,
    Stress3CompletionCountMismatch,
    Stress3EventRegistrationLeak,
    Stress3WaitBlockCleanupFailure,
    Stress3TimerRegistrationLeak,
    Stress3SetEventFailure,
    Stress3ClockProgressTimeout,
    Stress3ProtocolFailure,
    Stress3UnexpectedProcessorCount
} STRESS3_FAILURE_STAGE;

static EVENT Stress3Event;
static PETHREAD Stress3WaiterThread;
static volatile uint64_t Stress3RequestEpoch;
static volatile uint64_t Stress3StartedEpoch;
static volatile uint64_t Stress3CompletedEpoch;
static volatile uint64_t Stress3CompletionCount;
static volatile bool Stress3Active;
static volatile uint32_t Stress3RequestedMode;
static volatile MTSTATUS Stress3CompletionStatus;

NORETURN
static void
Stress3BugCheck(
    STRESS3_FAILURE_STAGE Stage,
    void* Detail1,
    void* Detail2,
    void* Detail3
)
{
    // WAIT_STATE_FAILURE: P1 is the stage; P2-P4 are stage details.
    MeBugCheckEx(
        WAIT_STATE_FAILURE,
        (void*)(uintptr_t)Stage,
        Detail1,
        Detail2,
        Detail3
    );
}

static bool
Stress3WatchdogExpired(
    uint64_t StartTsc
)
{
    return __rdtsc() - StartTsc >
        STRESS3_WATCHDOG_SECONDS * Stress2CTscTicksPerSecond;
}

static void
Stress3WaitForEpoch(
    volatile uint64_t* Value,
    uint64_t Expected,
    STRESS3_FAILURE_STAGE TimeoutStage
)
{
    uint64_t StartTsc = __rdtsc();

    for (;;) {
        uint64_t Observed = InterlockedLoadAcquire(Value);
        if (Observed == Expected) {
            return;
        }
        if (Observed > Expected) {
            Stress3BugCheck(
                Stress3ProtocolFailure,
                (void*)(uintptr_t)Expected,
                (void*)(uintptr_t)Observed,
                (void*)Value
            );
        }
        if (Stress3WatchdogExpired(StartTsc)) {
            Stress3BugCheck(
                TimeoutStage,
                (void*)(uintptr_t)Expected,
                (void*)(uintptr_t)Observed,
                (void*)Value
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static bool
Stress3WaitForRegistration(
    uint64_t Epoch
)
{
    PITHREAD Thread = &Stress3WaiterThread->InternalThread;
    uint64_t StartTsc = __rdtsc();

    for (;;) {
        uint32_t WaitStatus = InterlockedLoadAcquire(
            &Thread->WaitStatus
        );
        THREAD_STATE ThreadState = InterlockedLoadAcquire(
            &Thread->ThreadState
        );
        void* WaitObject = InterlockedLoadAcquire(
            &Thread->WaitBlock.Object
        );
        WAIT_REASON WaitReason = InterlockedLoadAcquire(
            &Thread->WaitBlock.WaitReason
        );

        if (WaitStatus == MT_PENDING &&
            (ThreadState == THREAD_BLOCKING ||
             ThreadState == THREAD_BLOCKED) &&
            WaitObject == &Stress3Event.Header &&
            WaitReason == WaitReasonDispatcherObject) {
            return true;
        }

        uint64_t Completed = InterlockedLoadAcquire(
            &Stress3CompletedEpoch
        );
        if (Completed == Epoch) {
            return false;
        }
        if (Completed > Epoch || Stress3WatchdogExpired(StartTsc)) {
            uintptr_t State = (uintptr_t)ThreadState |
                ((uintptr_t)WaitStatus << 8) |
                ((uintptr_t)WaitReason << 40);
            Stress3BugCheck(
                Stress3RegistrationTimeout,
                (void*)(uintptr_t)Epoch,
                (void*)State,
                WaitObject
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress3WaitForClockTick(
    uint64_t TargetTick,
    uint64_t Epoch
)
{
    uint64_t StartTsc = __rdtsc();

    while (InterlockedLoadAcquire(&MeSystemTickCount) <
           TargetTick) {
        if (Stress3WatchdogExpired(StartTsc)) {
            Stress3BugCheck(
                Stress3ClockProgressTimeout,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)TargetTick,
                (void*)(uintptr_t)InterlockedLoadAcquire(
                    &MeSystemTickCount
                )
            );
        }
        __pause();
    }
}

static void
Stress3ValidateCleanWait(
    uint64_t Epoch
)
{
    IRQL EventIrql;
    MsAcquireSpinlock(&Stress3Event.Header.Lock, &EventIrql);
    PDOUBLY_LINKED_LIST WaitListHead =
        &Stress3Event.Header.WaitListHead;
    bool EventQueueEmpty = IsListEmpty(WaitListHead);
    PDOUBLY_LINKED_LIST FirstEntry = WaitListHead->Flink;
    PDOUBLY_LINKED_LIST LastEntry = WaitListHead->Blink;
    Stress3Event.Header.SignalState = 0;
    MsReleaseSpinlock(&Stress3Event.Header.Lock, EventIrql);

    if (!EventQueueEmpty) {
        Stress3BugCheck(
            Stress3EventRegistrationLeak,
            (void*)(uintptr_t)Epoch,
            FirstEntry,
            LastEntry
        );
    }

    PITHREAD Thread = &Stress3WaiterThread->InternalThread;
    void* WaitObject = InterlockedLoadAcquire(
        &Thread->WaitBlock.Object
    );
    WAIT_REASON WaitReason = InterlockedLoadAcquire(
        &Thread->WaitBlock.WaitReason
    );
    uint64_t WakeupTime = InterlockedLoadAcquire(
        &Thread->WaitBlock.WakeupTime
    );
    if (WaitObject != NULL ||
        WaitReason != WaitReasonNone ||
        WakeupTime != 0) {
        Stress3BugCheck(
            Stress3WaitBlockCleanupFailure,
            WaitObject,
            (void*)(uintptr_t)WaitReason,
            (void*)(uintptr_t)WakeupTime
        );
    }

    PDOUBLY_LINKED_LIST TimerEntry =
        &Thread->WaitBlock.TimerListEntry;
    IRQL TimerIrql;
    MeRaiseIrql(CLOCK_LEVEL, &TimerIrql);
    MsAcquireSpinlockAtDpcLevel(&MsTimerQueueLock);
    bool TimerEntryIsolated = TimerEntry->Flink == TimerEntry &&
        TimerEntry->Blink == TimerEntry;
    MsReleaseSpinlockFromDpcLevel(&MsTimerQueueLock);
    MeLowerIrql(TimerIrql);

    if (!TimerEntryIsolated) {
        Stress3BugCheck(
            Stress3TimerRegistrationLeak,
            (void*)(uintptr_t)Epoch,
            TimerEntry->Flink,
            TimerEntry->Blink
        );
    }
}

static bool
Stress3RunWait(
    STRESS3_WAIT_MODE Mode,
    uint64_t Epoch,
    MTSTATUS* CompletionStatusOut
)
{
    Stress3ValidateCleanWait(Epoch - 1);

    InterlockedStoreRelease(
        &Stress3RequestedMode,
        (uint32_t)Mode
    );
    InterlockedStoreRelease(&Stress3RequestEpoch, Epoch);

    Stress3WaitForEpoch(
        &Stress3StartedEpoch,
        Epoch,
        Stress3WaiterStartTimeout
    );
    bool Registered = Stress3WaitForRegistration(Epoch);

    if (!Registered && Mode != Stress3WaitForSignalTimeoutRace) {
        Stress3BugCheck(
            Stress3RegistrationTimeout,
            (void*)(uintptr_t)Epoch,
            (void*)(uintptr_t)Mode,
            (void*)(uintptr_t)InterlockedLoadAcquire(
                &Stress3CompletionStatus
            )
        );
    }

    if (Registered && Mode == Stress3WaitForSignalTimeoutRace) {
        uint64_t WakeupTime = InterlockedLoadAcquire(
            &Stress3WaiterThread->InternalThread.WaitBlock.WakeupTime
        );
        Stress3WaitForClockTick(WakeupTime, Epoch);
    }

    if (Registered && Mode != Stress3WaitForTimeout) {
        MTSTATUS Status = MsSetEvent(&Stress3Event);
        if (MT_FAILURE(Status)) {
            Stress3BugCheck(
                Stress3SetEventFailure,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Mode,
                (void*)(uintptr_t)Status
            );
        }
    }

    Stress3WaitForEpoch(
        &Stress3CompletedEpoch,
        Epoch,
        Stress3CompletionTimeout
    );

    MTSTATUS CompletionStatus = InterlockedLoadAcquire(
        &Stress3CompletionStatus
    );
    uint64_t CompletionCount = InterlockedLoadAcquire(
        &Stress3CompletionCount
    );

    bool StatusValid = Mode == Stress3WaitForSignal
        ? CompletionStatus == MT_SUCCESS
        : Mode == Stress3WaitForTimeout
            ? CompletionStatus == MT_TIMEOUT
            : CompletionStatus == MT_SUCCESS ||
                CompletionStatus == MT_TIMEOUT;
    if (!Registered && CompletionStatus != MT_TIMEOUT) {
        StatusValid = false;
    }
    if (!StatusValid) {
        Stress3BugCheck(
            Stress3UnexpectedCompletionStatus,
            (void*)(uintptr_t)Epoch,
            (void*)(uintptr_t)Mode,
            (void*)(uintptr_t)CompletionStatus
        );
    }
    if (CompletionCount != Epoch) {
        Stress3BugCheck(
            Stress3CompletionCountMismatch,
            (void*)(uintptr_t)Epoch,
            (void*)(uintptr_t)CompletionCount,
            (void*)(uintptr_t)Mode
        );
    }

    Stress3ValidateCleanWait(Epoch);
    *CompletionStatusOut = CompletionStatus;
    return Registered;
}

static void
Stress3Waiter(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);
    uint64_t LocalEpoch = 0;

    while (InterlockedLoadAcquire(&Stress3Active)) {
        uint64_t Epoch = InterlockedLoadAcquire(
            &Stress3RequestEpoch
        );
        if (Epoch == LocalEpoch) {
            MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
            continue;
        }
        if (Epoch != LocalEpoch + 1) {
            Stress3BugCheck(
                Stress3ProtocolFailure,
                (void*)(uintptr_t)LocalEpoch,
                (void*)(uintptr_t)Epoch,
                NULL
            );
        }

        STRESS3_WAIT_MODE Mode = (STRESS3_WAIT_MODE)InterlockedLoadAcquire(
            &Stress3RequestedMode
        );
        uint64_t Timeout = Mode == Stress3WaitForSignal
            ? MT_INFINITE
            : Mode == Stress3WaitForTimeout
                ? STRESS3_TIMEOUT_MS
                : STRESS3_RACE_TIMEOUT_MS;

        InterlockedStoreRelease(&Stress3StartedEpoch, Epoch);
        MTSTATUS Status = MsWaitForSingleObject(
            &Stress3Event,
            KernelMode,
            false,
            Timeout
        );
        InterlockedStoreRelease(
            &Stress3CompletionStatus,
            Status
        );
        InterlockedIncrementU64(&Stress3CompletionCount);
        InterlockedStoreRelease(
            &Stress3CompletedEpoch,
            Epoch
        );
        LocalEpoch = Epoch;
    }
}

static void
Stress3Controller(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);
    gop_printf(
        COLOR_GREEN,
        "STRESS 3A START (event/timer waits, %u CPUs)\n",
        MeGetActiveProcessorCount()
    );

    for (uint32_t Index = 0;
         Index < STRESS3_IMMEDIATE_ITERATIONS;
         Index++) {
        MTSTATUS Status = MsSetEvent(&Stress3Event);
        if (MT_FAILURE(Status)) {
            Stress3BugCheck(
                Stress3SetEventFailure,
                (void*)(uintptr_t)Index,
                NULL,
                (void*)(uintptr_t)Status
            );
        }
        Status = MsWaitForSingleObject(
            &Stress3Event,
            KernelMode,
            false,
            MT_INFINITE
        );
        if (Status != MT_SUCCESS) {
            Stress3BugCheck(
                Stress3InvalidImmediateStatus,
                (void*)(uintptr_t)Index,
                (void*)(uintptr_t)MT_SUCCESS,
                (void*)(uintptr_t)Status
            );
        }

        Status = MsWaitForSingleObject(
            &Stress3Event,
            KernelMode,
            false,
            0
        );
        if (Status != MT_TIMEOUT) {
            Stress3BugCheck(
                Stress3InvalidImmediateStatus,
                (void*)(uintptr_t)Index,
                (void*)(uintptr_t)MT_TIMEOUT,
                (void*)(uintptr_t)Status
            );
        }
    }
    gop_printf(COLOR_GREEN, "STRESS 3A immediate waits PASS (%u each)\n",
        STRESS3_IMMEDIATE_ITERATIONS);

    uint64_t Epoch = 0;
    for (uint32_t Index = 0;
         Index < STRESS3_SIGNAL_ITERATIONS;
         Index++) {
        MTSTATUS CompletionStatus;
        if (!Stress3RunWait(
                Stress3WaitForSignal,
                ++Epoch,
                &CompletionStatus
            )) {
            Stress3BugCheck(
                Stress3ProtocolFailure,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Stress3WaitForSignal,
                (void*)(uintptr_t)CompletionStatus
            );
        }
        if (((Index + 1U) % 1000U) == 0) {
            gop_printf(
                COLOR_GREEN,
                "STRESS 3A blocked signal progress %u/%u\n",
                Index + 1U,
                STRESS3_SIGNAL_ITERATIONS
            );
        }
    }
    gop_printf(COLOR_GREEN, "STRESS 3A blocked signal PASS (%u waits)\n",
        STRESS3_SIGNAL_ITERATIONS);

    for (uint32_t Index = 0;
         Index < STRESS3_TIMEOUT_ITERATIONS;
         Index++) {
        MTSTATUS CompletionStatus;
        if (!Stress3RunWait(
                Stress3WaitForTimeout,
                ++Epoch,
                &CompletionStatus
            )) {
            Stress3BugCheck(
                Stress3ProtocolFailure,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Stress3WaitForTimeout,
                (void*)(uintptr_t)CompletionStatus
            );
        }
    }
    gop_printf(COLOR_GREEN, "STRESS 3A timeout PASS (%u waits)\n",
        STRESS3_TIMEOUT_ITERATIONS);

    uint32_t SignalWins = 0;
    uint32_t TimeoutWins = 0;
    uint32_t RegistrationRetries = 0;
    for (uint32_t Index = 0; Index < STRESS3_RACE_ITERATIONS;) {
        MTSTATUS Status;
        bool TimedSample = Stress3RunWait(
            Stress3WaitForSignalTimeoutRace,
            ++Epoch,
            &Status
        );
        if (!TimedSample) {
            RegistrationRetries++;
            if (RegistrationRetries > STRESS3_MAX_REGISTRATION_RETRIES) {
                Stress3BugCheck(
                    Stress3RegistrationTimeout,
                    (void*)(uintptr_t)RegistrationRetries,
                    (void*)(uintptr_t)Epoch,
                    (void*)(uintptr_t)Status
                );
            }
            continue;
        }
        if (Status == MT_SUCCESS) {
            SignalWins++;
        }
        else {
            TimeoutWins++;
        }
        Index++;
        if ((Index % 100U) == 0) {
            gop_printf(
                COLOR_GREEN,
                "STRESS 3A race progress %u/%u\n",
                Index,
                STRESS3_RACE_ITERATIONS
            );
        }
    }
    gop_printf(
        COLOR_GREEN,
        "STRESS 3A same-tick race PASS (%u signal, %u timeout, %u retries)\n",
        SignalWins,
        TimeoutWins,
        RegistrationRetries
    );
    gop_printf(COLOR_GREEN, "STRESS 3A PASS (%llu completions)\n",
        (unsigned long long)Stress3CompletionCount);
}

#define STRESS4_REPEAT_ITERATIONS 1000U
#define STRESS4_REPEAT_MS         1ULL
#define STRESS4_WATCHDOG_SECONDS  5ULL

typedef enum _STRESS4_FAILURE_STAGE {
    Stress4UnexpectedStatus = 1,
    Stress4EarlyWake,
    Stress4TimerRegistrationLeak,
    Stress4WaitBlockCleanupFailure,
    Stress4UnexpectedWaitStatus,
    Stress4UnexpectedThreadState,
    Stress4OwnerMismatch,
    Stress4LargeIntervalArithmetic,
    Stress4WatchdogTimeout,
    Stress4UnexpectedProcessorCount
} STRESS4_FAILURE_STAGE;

static volatile uint64_t Stress4HeartbeatTsc;
static volatile uint64_t Stress4CompletionCount;
static volatile bool Stress4WatchdogActive;
static uint64_t Stress4LateWakeCount;
static uint64_t Stress4MaximumOvershoot;

NORETURN
static void
Stress4BugCheck(
    STRESS4_FAILURE_STAGE Stage,
    void* Detail1,
    void* Detail2,
    void* Detail3
)
{
    // WAIT_STATE_FAILURE: P1 is the stage; P2-P4 are stage details.
    MeBugCheckEx(
        WAIT_STATE_FAILURE,
        (void*)(uintptr_t)Stage,
        Detail1,
        Detail2,
        Detail3
    );
}

static void
Stress4PublishHeartbeat(void)
{
    InterlockedStoreRelease(&Stress4HeartbeatTsc, __rdtsc());
}

static uint64_t
Stress4MillisecondsToTicks(
    uint64_t Milliseconds
)
{
    uint64_t Ticks = Milliseconds / TICK_MS;
    if (Milliseconds % TICK_MS) {
        Ticks++;
    }
    return Ticks;
}

static bool
Stress4TimerEntryIsIsolated(
    PITHREAD Thread
)
{
    PDOUBLY_LINKED_LIST Entry = &Thread->WaitBlock.TimerListEntry;
    IRQL OldIrql;

    MeRaiseIrql(CLOCK_LEVEL, &OldIrql);
    MsAcquireSpinlockAtDpcLevel(&MsTimerQueueLock);
    bool Isolated = Entry->Flink == Entry && Entry->Blink == Entry;
    MsReleaseSpinlockFromDpcLevel(&MsTimerQueueLock);
    MeLowerIrql(OldIrql);
    return Isolated;
}

static uint64_t
Stress4RunSleep(
    uint64_t Milliseconds,
    uint64_t Iteration
)
{
    PITHREAD Thread = MeGetCurrentThread();
    uint64_t ExpectedTicks = Stress4MillisecondsToTicks(Milliseconds);
    uint64_t StartTick = InterlockedLoadAcquire(
        &MeSystemTickCount
    );

    Stress4PublishHeartbeat();
    MTSTATUS Status = MsDelayExecution(KernelMode, false, Milliseconds);
    uint64_t EndTick = InterlockedLoadAcquire(
        &MeSystemTickCount
    );
    Stress4PublishHeartbeat();

    if (Status != MT_SUCCESS) {
        Stress4BugCheck(
            Stress4UnexpectedStatus,
            (void*)(uintptr_t)Iteration,
            (void*)(uintptr_t)Milliseconds,
            (void*)(uintptr_t)Status
        );
    }

    uint64_t ElapsedTicks = EndTick - StartTick;
    if (Milliseconds != 0 && ElapsedTicks < ExpectedTicks) {
        Stress4BugCheck(
            Stress4EarlyWake,
            (void*)(uintptr_t)Milliseconds,
            (void*)(uintptr_t)ExpectedTicks,
            (void*)(uintptr_t)ElapsedTicks
        );
    }

    if (!Stress4TimerEntryIsIsolated(Thread)) {
        PDOUBLY_LINKED_LIST Entry = &Thread->WaitBlock.TimerListEntry;
        Stress4BugCheck(
            Stress4TimerRegistrationLeak,
            (void*)(uintptr_t)Iteration,
            Entry->Flink,
            Entry->Blink
        );
    }

    void* WaitObject = InterlockedLoadAcquire(
        &Thread->WaitBlock.Object
    );
    WAIT_REASON WaitReason = InterlockedLoadAcquire(
        &Thread->WaitBlock.WaitReason
    );
    uint64_t WakeupTime = InterlockedLoadAcquire(
        &Thread->WaitBlock.WakeupTime
    );
    if (WaitObject != NULL ||
        WaitReason != WaitReasonNone ||
        WakeupTime != 0) {
        Stress4BugCheck(
            Stress4WaitBlockCleanupFailure,
            WaitObject,
            (void*)(uintptr_t)WaitReason,
            (void*)(uintptr_t)WakeupTime
        );
    }

    if (Milliseconds != 0 &&
        InterlockedLoadAcquire(&Thread->WaitStatus) != MT_TIMEOUT) {
        Stress4BugCheck(
            Stress4UnexpectedWaitStatus,
            (void*)(uintptr_t)Iteration,
            (void*)(uintptr_t)Milliseconds,
            (void*)(uintptr_t)InterlockedLoadAcquire(
                &Thread->WaitStatus
            )
        );
    }

    THREAD_STATE ThreadState = InterlockedLoadAcquire(
        &Thread->ThreadState
    );
    if (ThreadState != THREAD_RUNNING) {
        Stress4BugCheck(
            Stress4UnexpectedThreadState,
            (void*)(uintptr_t)Iteration,
            (void*)(uintptr_t)Milliseconds,
            (void*)(uintptr_t)ThreadState
        );
    }

    PPROCESSOR Owner = InterlockedLoadAcquire(
        &Thread->ActiveProcessor
    );
    if (Owner != MeGetCurrentProcessor()) {
        Stress4BugCheck(
            Stress4OwnerMismatch,
            (void*)(uintptr_t)Iteration,
            Owner,
            MeGetCurrentProcessor()
        );
    }

    if (Milliseconds != 0 && ElapsedTicks > ExpectedTicks) {
        uint64_t Overshoot = ElapsedTicks - ExpectedTicks;
        Stress4LateWakeCount++;
        if (Overshoot > Stress4MaximumOvershoot) {
            Stress4MaximumOvershoot = Overshoot;
        }
    }

    InterlockedIncrementU64(&Stress4CompletionCount);
    return ElapsedTicks;
}

static void
Stress4ValidateLargeIntervalArithmetic(void)
{
    uint64_t Milliseconds = UINT64_MAX - (UINT64_MAX % TICK_MS);
    uint64_t Ticks = Stress4MillisecondsToTicks(Milliseconds);
    uint64_t Now = InterlockedLoadAcquire(&MeSystemTickCount);
    uint64_t WakeupTime = Ticks > UINT64_MAX - Now
        ? UINT64_MAX
        : Now + Ticks;

    bool Saturated = Ticks > UINT64_MAX - Now;
    bool Valid = Ticks != 0 && WakeupTime >= Now;
    if (Saturated) {
        Valid = Valid && WakeupTime == UINT64_MAX;
    }
    else {
        Valid = Valid && WakeupTime - Now == Ticks;
    }

    if (!Valid) {
        Stress4BugCheck(
            Stress4LargeIntervalArithmetic,
            (void*)(uintptr_t)Milliseconds,
            (void*)(uintptr_t)Ticks,
            (void*)(uintptr_t)WakeupTime
        );
    }

    gop_printf(
        COLOR_GREEN,
        "STRESS 4A large interval arithmetic PASS (%llu ticks)\n",
        (unsigned long long)Ticks
    );
}

static void
Stress4Watchdog(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);

    while (InterlockedLoadAcquire(&Stress4WatchdogActive)) {
        uint64_t Heartbeat = InterlockedLoadAcquire(
            &Stress4HeartbeatTsc
        );
        uint64_t Now = __rdtsc();

        if (Now >= Heartbeat &&
            Now - Heartbeat >
                STRESS4_WATCHDOG_SECONDS * Stress2CTscTicksPerSecond) {
            Stress4BugCheck(
                Stress4WatchdogTimeout,
                (void*)(uintptr_t)InterlockedLoadAcquire(
                    &Stress4CompletionCount
                ),
                (void*)(uintptr_t)Heartbeat,
                (void*)(uintptr_t)Now
            );
        }

        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress4Controller(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);

    static const uint64_t BoundaryMilliseconds[] = {
        0,
        1,
        TICK_MS - 1,
        TICK_MS,
        TICK_MS + 1,
        1000
    };

    gop_printf(
        COLOR_GREEN,
        "STRESS 4A START (MsDelayExecution boundaries, %u CPUs)\n",
        MeGetActiveProcessorCount()
    );

    Stress4ValidateLargeIntervalArithmetic();

    uint64_t Iteration = 0;
    for (uint32_t Index = 0;
         Index < sizeof(BoundaryMilliseconds) / sizeof(BoundaryMilliseconds[0]);
         Index++) {
        uint64_t Milliseconds = BoundaryMilliseconds[Index];
        uint64_t ElapsedTicks = Stress4RunSleep(
            Milliseconds,
            ++Iteration
        );
        gop_printf(
            COLOR_GREEN,
            "STRESS 4A sleep %llu ms PASS (%llu ticks, CPU %u)\n",
            (unsigned long long)Milliseconds,
            (unsigned long long)ElapsedTicks,
            MeGetCurrentProcessor()->ID
        );
    }

    for (uint32_t Index = 0;
         Index < STRESS4_REPEAT_ITERATIONS;
         Index++) {
        Stress4RunSleep(STRESS4_REPEAT_MS, ++Iteration);
        if (((Index + 1U) % 100U) == 0) {
            gop_printf(
                COLOR_GREEN,
                "STRESS 4A repeated sleep progress %u/%u\n",
                Index + 1U,
                STRESS4_REPEAT_ITERATIONS
            );
        }
    }

    gop_printf(
        COLOR_GREEN,
        "STRESS 4A PASS (%llu sleeps, %llu late, max +%llu ticks)\n",
        (unsigned long long)Stress4CompletionCount,
        (unsigned long long)Stress4LateWakeCount,
        (unsigned long long)Stress4MaximumOvershoot
    );
}

#define STRESS4B_WAITER_COUNT     4U
#define STRESS4B_WATCHDOG_SECONDS 5ULL

typedef enum _STRESS4B_FAILURE_STAGE {
    Stress4BProtocolFailure = 1,
    Stress4BRegistrationTimeout,
    Stress4BUnexpectedEarlyCompletion,
    Stress4BCompletionTimeout,
    Stress4BCompletionCountMismatch,
    Stress4BSetEventFailure,
    Stress4BQueueCountMismatch,
    Stress4BQueueTopologyFailure,
    Stress4BSignalStateMismatch,
    Stress4BUnexpectedStatus,
    Stress4BCleanupFailure,
    Stress4BWatchdogTimeout,
    Stress4BUnexpectedProcessorCount
} STRESS4B_FAILURE_STAGE;

typedef struct _STRESS4B_EVENT_SNAPSHOT {
    uint32_t WaiterCount;
    bool Signaled;
    bool TopologyValid;
    PETHREAD Head;
    PETHREAD Tail;
    void* FaultingEntry;
} STRESS4B_EVENT_SNAPSHOT;

static EVENT Stress4BSynchronizationEvent;
static EVENT Stress4BNotificationEvent;
static PETHREAD Stress4BWaiterThreads[STRESS4B_WAITER_COUNT];
static PEVENT volatile Stress4BRequestedEvent;
static volatile uint64_t Stress4BRequestEpoch;
static volatile uint64_t Stress4BStartedEpoch[STRESS4B_WAITER_COUNT];
static volatile uint64_t Stress4BCompletedEpoch[STRESS4B_WAITER_COUNT];
static volatile MTSTATUS Stress4BCompletionStatus[STRESS4B_WAITER_COUNT];
static volatile uint64_t Stress4BHeartbeatTsc;
static volatile uint32_t Stress4BPhase;
static volatile uint32_t Stress4BProgress;
static volatile bool Stress4BActive;

NORETURN
static void
Stress4BBugCheck(
    STRESS4B_FAILURE_STAGE Stage,
    void* Detail1,
    void* Detail2,
    void* Detail3
)
{
    // WAIT_STATE_FAILURE: P1 is the stage; P2-P4 are stage details.
    MeBugCheckEx(
        WAIT_STATE_FAILURE,
        (void*)(uintptr_t)Stage,
        Detail1,
        Detail2,
        Detail3
    );
}

static void
Stress4BPublishProgress(
    uint32_t Phase,
    uint32_t Progress
)
{
    InterlockedStoreRelease(&Stress4BPhase, Phase);
    InterlockedStoreRelease(&Stress4BProgress, Progress);
    InterlockedStoreRelease(&Stress4BHeartbeatTsc, __rdtsc());
}

static bool
Stress4BWatchdogExpired(
    uint64_t StartTsc
)
{
    uint64_t Now = __rdtsc();
    return Now >= StartTsc &&
        Now - StartTsc >
            STRESS4B_WATCHDOG_SECONDS * Stress2CTscTicksPerSecond;
}

static bool
Stress4BIsKnownWaiter(
    PETHREAD Thread
)
{
    for (uint32_t Index = 0; Index < STRESS4B_WAITER_COUNT; Index++) {
        if (Stress4BWaiterThreads[Index] == Thread) {
            return true;
        }
    }
    return false;
}

static STRESS4B_EVENT_SNAPSHOT
Stress4BSnapshotEvent(
    PEVENT Event
)
{
    STRESS4B_EVENT_SNAPSHOT Snapshot = { 0 };
    Snapshot.TopologyValid = true;

    IRQL OldIrql;
    MsAcquireSpinlock(&Event->Header.Lock, &OldIrql);
    Snapshot.Signaled = Event->Header.SignalState != 0;

    PDOUBLY_LINKED_LIST ListHead = &Event->Header.WaitListHead;
    PDOUBLY_LINKED_LIST PreviousEntry = ListHead;
    PDOUBLY_LINKED_LIST CurrentEntry = ListHead->Flink;
    if (!CurrentEntry || !ListHead->Blink) {
        Snapshot.TopologyValid = false;
        Snapshot.FaultingEntry = CurrentEntry;
    }

    while (Snapshot.TopologyValid && CurrentEntry != ListHead) {
        if (!CurrentEntry || !CurrentEntry->Flink ||
            !CurrentEntry->Blink ||
            CurrentEntry->Blink != PreviousEntry ||
            CurrentEntry->Flink->Blink != CurrentEntry) {
            Snapshot.TopologyValid = false;
            Snapshot.FaultingEntry = CurrentEntry;
            break;
        }

        PWAIT_BLOCK WaitBlock = CONTAINING_RECORD(
            CurrentEntry,
            WAIT_BLOCK,
            ObjectListEntry
        );
        PITHREAD IThread = CONTAINING_RECORD(
            WaitBlock,
            ITHREAD,
            WaitBlock
        );
        PETHREAD CurrentThread = PsGetEThreadFromIThread(IThread);
        if (Snapshot.WaiterCount >= STRESS4B_WAITER_COUNT ||
            !Stress4BIsKnownWaiter(CurrentThread)) {
            Snapshot.TopologyValid = false;
            Snapshot.FaultingEntry = CurrentThread;
            break;
        }

        if (Snapshot.WaiterCount == 0) {
            Snapshot.Head = CurrentThread;
        }
        Snapshot.Tail = CurrentThread;
        Snapshot.WaiterCount++;
        PreviousEntry = CurrentEntry;
        CurrentEntry = CurrentEntry->Flink;
    }

    if (Snapshot.TopologyValid) {
        bool EmptyConsistent = Snapshot.WaiterCount != 0 ||
            (Snapshot.Head == NULL && Snapshot.Tail == NULL &&
             ListHead->Blink == ListHead);
        if (!EmptyConsistent || PreviousEntry != ListHead->Blink ||
            ListHead->Blink->Flink != ListHead) {
            Snapshot.TopologyValid = false;
            Snapshot.FaultingEntry = PreviousEntry;
        }
    }

    MsReleaseSpinlock(&Event->Header.Lock, OldIrql);
    return Snapshot;
}

static void
Stress4BRequireEventState(
    PEVENT Event,
    uint64_t Epoch,
    uint32_t ExpectedWaiters,
    bool ExpectedSignaled
)
{
    STRESS4B_EVENT_SNAPSHOT Snapshot = Stress4BSnapshotEvent(Event);
    if (!Snapshot.TopologyValid) {
        Stress4BBugCheck(
            Stress4BQueueTopologyFailure,
            (void*)(uintptr_t)Epoch,
            Snapshot.FaultingEntry,
            Snapshot.Tail
        );
    }
    if (Snapshot.WaiterCount != ExpectedWaiters) {
        Stress4BBugCheck(
            Stress4BQueueCountMismatch,
            (void*)(uintptr_t)Epoch,
            (void*)(uintptr_t)ExpectedWaiters,
            (void*)(uintptr_t)Snapshot.WaiterCount
        );
    }
    if (Snapshot.Signaled != ExpectedSignaled) {
        Stress4BBugCheck(
            Stress4BSignalStateMismatch,
            (void*)(uintptr_t)Epoch,
            (void*)(uintptr_t)ExpectedSignaled,
            (void*)(uintptr_t)Snapshot.Signaled
        );
    }
}

static uint32_t
Stress4BCountCompletions(
    uint64_t Epoch
)
{
    uint32_t Count = 0;
    for (uint32_t Index = 0; Index < STRESS4B_WAITER_COUNT; Index++) {
        uint64_t Completed = InterlockedLoadAcquire(
            &Stress4BCompletedEpoch[Index]
        );
        if (Completed > Epoch) {
            Stress4BBugCheck(
                Stress4BProtocolFailure,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Completed,
                (void*)(uintptr_t)Index
            );
        }
        if (Completed == Epoch) {
            Count++;
        }
    }
    return Count;
}

static void
Stress4BWaitForRegistrations(
    PEVENT Event,
    uint64_t Epoch
)
{
    uint64_t StartTsc = __rdtsc();

    for (;;) {
        STRESS4B_EVENT_SNAPSHOT Snapshot = Stress4BSnapshotEvent(Event);
        if (!Snapshot.TopologyValid) {
            Stress4BBugCheck(
                Stress4BQueueTopologyFailure,
                (void*)(uintptr_t)Epoch,
                Snapshot.FaultingEntry,
                Snapshot.Tail
            );
        }
        if (Snapshot.Signaled) {
            Stress4BBugCheck(
                Stress4BSignalStateMismatch,
                (void*)(uintptr_t)Epoch,
                NULL,
                (void*)(uintptr_t)Snapshot.Signaled
            );
        }

        bool AllRegistered = Snapshot.WaiterCount == STRESS4B_WAITER_COUNT;
        for (uint32_t Index = 0;
             AllRegistered && Index < STRESS4B_WAITER_COUNT;
             Index++) {
            PETHREAD Thread = Stress4BWaiterThreads[Index];
            PITHREAD IThread = &Thread->InternalThread;
            uint64_t Started = InterlockedLoadAcquire(
                &Stress4BStartedEpoch[Index]
            );
            THREAD_STATE State = InterlockedLoadAcquire(
                &IThread->ThreadState
            );
            AllRegistered = Started == Epoch &&
                InterlockedLoadAcquire(
                    &IThread->WaitBlock.Object
                ) == &Event->Header &&
                InterlockedLoadAcquire(
                    &IThread->WaitBlock.WaitReason
                ) == WaitReasonDispatcherObject &&
                InterlockedLoadAcquire(
                    &IThread->WaitStatus
                ) == MT_PENDING &&
                (State == THREAD_BLOCKING || State == THREAD_BLOCKED);
        }

        if (AllRegistered) {
            return;
        }
        uint32_t Completed = Stress4BCountCompletions(Epoch);
        if (Completed != 0) {
            Stress4BBugCheck(
                Stress4BUnexpectedEarlyCompletion,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Snapshot.WaiterCount,
                (void*)(uintptr_t)Completed
            );
        }
        if (Stress4BWatchdogExpired(StartTsc)) {
            Stress4BBugCheck(
                Stress4BRegistrationTimeout,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Snapshot.WaiterCount,
                (void*)(uintptr_t)Completed
            );
        }

        Stress4BPublishProgress((uint32_t)Epoch, Snapshot.WaiterCount);
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress4BWaitForCompletionCount(
    uint64_t Epoch,
    uint32_t ExpectedCompletions
)
{
    uint64_t StartTsc = __rdtsc();

    for (;;) {
        uint32_t Completed = Stress4BCountCompletions(Epoch);
        if (Completed == ExpectedCompletions) {
            return;
        }
        if (Completed > ExpectedCompletions) {
            Stress4BBugCheck(
                Stress4BCompletionCountMismatch,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)ExpectedCompletions,
                (void*)(uintptr_t)Completed
            );
        }
        if (Stress4BWatchdogExpired(StartTsc)) {
            Stress4BBugCheck(
                Stress4BCompletionTimeout,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)ExpectedCompletions,
                (void*)(uintptr_t)Completed
            );
        }

        Stress4BPublishProgress((uint32_t)Epoch, Completed);
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress4BValidateCompletions(
    uint64_t Epoch
)
{
    for (uint32_t Index = 0; Index < STRESS4B_WAITER_COUNT; Index++) {
        PETHREAD Thread = Stress4BWaiterThreads[Index];
        MTSTATUS Status = InterlockedLoadAcquire(
            &Stress4BCompletionStatus[Index]
        );
        if (InterlockedLoadAcquire(
            &Stress4BCompletedEpoch[Index]
        ) != Epoch || Status != MT_SUCCESS) {
            Stress4BBugCheck(
                Stress4BUnexpectedStatus,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Index,
                (void*)(uintptr_t)Status
            );
        }
        PITHREAD IThread = &Thread->InternalThread;
        void* WaitObject = InterlockedLoadAcquire(
            &IThread->WaitBlock.Object
        );
        WAIT_REASON WaitReason = InterlockedLoadAcquire(
            &IThread->WaitBlock.WaitReason
        );
        uint64_t WakeupTime = InterlockedLoadAcquire(
            &IThread->WaitBlock.WakeupTime
        );
        if (WaitObject != NULL ||
            WaitReason != WaitReasonNone ||
            WakeupTime != 0 ||
            !Stress4TimerEntryIsIsolated(IThread)) {
            Stress4BBugCheck(
                Stress4BCleanupFailure,
                WaitObject,
                (void*)(uintptr_t)WaitReason,
                (void*)(uintptr_t)WakeupTime
            );
        }
    }
}

static void
Stress4BDispatchWait(
    PEVENT Event,
    uint64_t Epoch
)
{
    InterlockedStoreRelease(&Stress4BRequestedEvent, Event);
    InterlockedStoreRelease(&Stress4BRequestEpoch, Epoch);
    Stress4BPublishProgress((uint32_t)Epoch, 0);
}

static void
Stress4BWaiter(
    THREAD_PARAMETER Parameter
)
{
    uint32_t Index = (uint32_t)(uintptr_t)Parameter;
    if (Index >= STRESS4B_WAITER_COUNT) {
        Stress4BBugCheck(
            Stress4BProtocolFailure,
            (void*)(uintptr_t)Index,
            (void*)(uintptr_t)STRESS4B_WAITER_COUNT,
            NULL
        );
    }

    uint64_t LocalEpoch = 0;
    while (InterlockedLoadAcquire(&Stress4BActive)) {
        uint64_t Epoch = InterlockedLoadAcquire(
            &Stress4BRequestEpoch
        );
        if (Epoch == LocalEpoch) {
            MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
            continue;
        }
        if (Epoch != LocalEpoch + 1) {
            Stress4BBugCheck(
                Stress4BProtocolFailure,
                (void*)(uintptr_t)Index,
                (void*)(uintptr_t)LocalEpoch,
                (void*)(uintptr_t)Epoch
            );
        }

        PEVENT Event = InterlockedLoadAcquire(
            &Stress4BRequestedEvent
        );
        if (!Event) {
            Stress4BBugCheck(
                Stress4BProtocolFailure,
                (void*)(uintptr_t)Index,
                (void*)(uintptr_t)Epoch,
                NULL
            );
        }

        InterlockedStoreRelease(
            &Stress4BStartedEpoch[Index],
            Epoch
        );
        MTSTATUS Status = MsWaitForSingleObject(
            Event,
            KernelMode,
            false,
            MT_INFINITE
        );
        InterlockedStoreRelease(
            &Stress4BCompletionStatus[Index],
            Status
        );
        InterlockedStoreRelease(
            &Stress4BCompletedEpoch[Index],
            Epoch
        );
        LocalEpoch = Epoch;
    }
}

static void
Stress4BWatchdog(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);

    for (;;) {
        uint64_t Heartbeat = InterlockedLoadAcquire(
            &Stress4BHeartbeatTsc
        );
        uint64_t Now = __rdtsc();
        if (Now >= Heartbeat &&
            Now - Heartbeat >
                STRESS4B_WATCHDOG_SECONDS * Stress2CTscTicksPerSecond) {
            Stress4BBugCheck(
                Stress4BWatchdogTimeout,
                (void*)(uintptr_t)InterlockedLoadAcquire(
                    &Stress4BPhase
                ),
                (void*)(uintptr_t)InterlockedLoadAcquire(
                    &Stress4BProgress
                ),
                (void*)(uintptr_t)InterlockedLoadAcquire(
                    &Stress4BRequestEpoch
                )
            );
        }

        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress4BController(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);

    gop_printf(
        COLOR_GREEN,
        "STRESS 4B START (event semantics, %u waiters, %u CPUs)\n",
        STRESS4B_WAITER_COUNT,
        MeGetActiveProcessorCount()
    );

    uint64_t Epoch = 1;
    Stress4BDispatchWait(&Stress4BSynchronizationEvent, Epoch);
    Stress4BWaitForRegistrations(&Stress4BSynchronizationEvent, Epoch);

    for (uint32_t Signal = 1; Signal <= STRESS4B_WAITER_COUNT; Signal++) {
        Stress4BPublishProgress((uint32_t)Epoch, Signal);
        MTSTATUS Status = MsSetEvent(&Stress4BSynchronizationEvent);
        if (Status != MT_SUCCESS) {
            Stress4BBugCheck(
                Stress4BSetEventFailure,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Signal,
                (void*)(uintptr_t)Status
            );
        }

        Stress4BRequireEventState(
            &Stress4BSynchronizationEvent,
            Epoch,
            STRESS4B_WAITER_COUNT - Signal,
            false
        );
        Stress4BWaitForCompletionCount(Epoch, Signal);
        gop_printf(
            COLOR_GREEN,
            "STRESS 4B synchronization signal %u/%u PASS\n",
            Signal,
            STRESS4B_WAITER_COUNT
        );
    }
    Stress4BValidateCompletions(Epoch);
    gop_printf(
        COLOR_GREEN,
        "STRESS 4B synchronization PASS (one waiter per signal)\n"
    );

    Epoch++;
    Stress4BDispatchWait(&Stress4BNotificationEvent, Epoch);
    Stress4BWaitForRegistrations(&Stress4BNotificationEvent, Epoch);
    Stress4BPublishProgress((uint32_t)Epoch, 1);
    MTSTATUS Status = MsSetEvent(&Stress4BNotificationEvent);
    if (Status != MT_SUCCESS) {
        Stress4BBugCheck(
            Stress4BSetEventFailure,
            (void*)(uintptr_t)Epoch,
            NULL,
            (void*)(uintptr_t)Status
        );
    }
    Stress4BRequireEventState(
        &Stress4BNotificationEvent,
        Epoch,
        0,
        true
    );
    Stress4BWaitForCompletionCount(Epoch, STRESS4B_WAITER_COUNT);
    Stress4BValidateCompletions(Epoch);
    gop_printf(
        COLOR_GREEN,
        "STRESS 4B notification broadcast PASS (%u waiters)\n",
        STRESS4B_WAITER_COUNT
    );

    Epoch++;
    Stress4BDispatchWait(&Stress4BNotificationEvent, Epoch);
    Stress4BWaitForCompletionCount(Epoch, STRESS4B_WAITER_COUNT);
    Stress4BRequireEventState(
        &Stress4BNotificationEvent,
        Epoch,
        0,
        true
    );
    Stress4BValidateCompletions(Epoch);
    gop_printf(
        COLOR_GREEN,
        "STRESS 4B persistent notification PASS (%u immediate waiters)\n",
        STRESS4B_WAITER_COUNT
    );

    gop_printf(COLOR_GREEN, "STRESS 4B PASS\n");
}

#define STRESS4C_ITERATIONS       100U
#define STRESS4C_TIMEOUT_TICKS    4ULL
#define STRESS4C_WATCHDOG_SECONDS 5ULL
#define STRESS4C_MAX_REGISTRATION_RETRIES 100U

typedef enum _STRESS4C_TIMING {
    Stress4COneTickBefore = 1,
    Stress4CSameTick,
    Stress4COneTickAfter
} STRESS4C_TIMING;

typedef enum _STRESS4C_FAILURE_STAGE {
    Stress4CProtocolFailure = 1,
    Stress4CRegistrationTimeout,
    Stress4CTimingWindowMissed,
    Stress4CClockProgressTimeout,
    Stress4CSetEventFailure,
    Stress4CCompletionTimeout,
    Stress4CUnexpectedResult,
    Stress4CCompletionCountMismatch,
    Stress4CEventCleanupFailure,
    Stress4CWaitBlockCleanupFailure,
    Stress4CTimerCleanupFailure,
    Stress4CSignalStateMismatch,
    Stress4CWatchdogTimeout,
    Stress4CUnexpectedProcessorCount
} STRESS4C_FAILURE_STAGE;

static EVENT Stress4CEvent;
static PETHREAD Stress4CWaiterThread;
static volatile uint64_t Stress4CRequestEpoch;
static volatile uint64_t Stress4CStartedEpoch;
static volatile uint64_t Stress4CCompletedEpoch;
static volatile uint64_t Stress4CCompletionCount;
static volatile MTSTATUS Stress4CCompletionStatus;
static volatile uint32_t Stress4CRequestedTiming;
static volatile uint64_t Stress4CHeartbeatTsc;
static volatile uint32_t Stress4CProgress;
static volatile bool Stress4CActive;

NORETURN
static void
Stress4CBugCheck(
    STRESS4C_FAILURE_STAGE Stage,
    void* Detail1,
    void* Detail2,
    void* Detail3
)
{
    // WAIT_STATE_FAILURE: P1 is the stage; P2-P4 are stage details.
    MeBugCheckEx(
        WAIT_STATE_FAILURE,
        (void*)(uintptr_t)Stage,
        Detail1,
        Detail2,
        Detail3
    );
}

static void
Stress4CPublishProgress(
    uint32_t Progress
)
{
    InterlockedStoreRelease(&Stress4CProgress, Progress);
    InterlockedStoreRelease(&Stress4CHeartbeatTsc, __rdtsc());
}

static bool
Stress4CWatchdogExpired(
    uint64_t StartTsc
)
{
    uint64_t Now = __rdtsc();
    return Now >= StartTsc &&
        Now - StartTsc >
            STRESS4C_WATCHDOG_SECONDS * Stress2CTscTicksPerSecond;
}

static bool
Stress4CWaitForRegistration(
    uint64_t Epoch
)
{
    PITHREAD Thread = &Stress4CWaiterThread->InternalThread;
    uint64_t StartTsc = __rdtsc();

    for (;;) {
        IRQL OldIrql;
        MsAcquireSpinlock(&Stress4CEvent.Header.Lock, &OldIrql);
        PDOUBLY_LINKED_LIST WaitListHead =
            &Stress4CEvent.Header.WaitListHead;
        PDOUBLY_LINKED_LIST WaiterEntry =
            &Thread->WaitBlock.ObjectListEntry;
        bool QueueContainsWaiter =
            WaitListHead->Flink == WaiterEntry &&
            WaitListHead->Blink == WaiterEntry &&
            WaiterEntry->Flink == WaitListHead &&
            WaiterEntry->Blink == WaitListHead;
        bool Signaled = Stress4CEvent.Header.SignalState != 0;
        MsReleaseSpinlock(&Stress4CEvent.Header.Lock, OldIrql);

        THREAD_STATE State = InterlockedLoadAcquire(
            &Thread->ThreadState
        );
        bool Registered = QueueContainsWaiter && !Signaled &&
            InterlockedLoadAcquire(
                &Stress4CStartedEpoch
            ) == Epoch &&
            InterlockedLoadAcquire(
                &Thread->WaitBlock.Object
            ) == &Stress4CEvent.Header &&
            InterlockedLoadAcquire(
                &Thread->WaitBlock.WaitReason
            ) == WaitReasonDispatcherObject &&
            InterlockedLoadAcquire(
                &Thread->WaitStatus
            ) == MT_PENDING &&
            (State == THREAD_BLOCKING || State == THREAD_BLOCKED);
        if (Registered) {
            return true;
        }

        uint64_t Completed = InterlockedLoadAcquire(
            &Stress4CCompletedEpoch
        );
        if (Completed == Epoch) {
            return false;
        }
        if (Completed > Epoch) {
            Stress4CBugCheck(
                Stress4CProtocolFailure,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Completed,
                (void*)(uintptr_t)State
            );
        }
        if (Stress4CWatchdogExpired(StartTsc)) {
            uintptr_t StateAndStatus = (uintptr_t)State |
                ((uintptr_t)InterlockedLoadAcquire(
                    &Thread->WaitStatus
                ) << 8);
            Stress4CBugCheck(
                Stress4CRegistrationTimeout,
                (void*)(uintptr_t)Epoch,
                (void*)StateAndStatus,
                (void*)(uintptr_t)QueueContainsWaiter
            );
        }

        Stress4CPublishProgress((uint32_t)Epoch);
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static bool
Stress4CWaitForClockTick(
    uint64_t TargetTick,
    uint64_t Epoch
)
{
    uint64_t StartTsc = __rdtsc();
    uint64_t CurrentTick = InterlockedLoadAcquire(
        &MeSystemTickCount
    );
    if (CurrentTick > TargetTick) {
        return false;
    }

    while (CurrentTick < TargetTick) {
        if (Stress4CWatchdogExpired(StartTsc)) {
            Stress4CBugCheck(
                Stress4CClockProgressTimeout,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)TargetTick,
                (void*)(uintptr_t)CurrentTick
            );
        }
        __pause();
        CurrentTick = InterlockedLoadAcquire(
            &MeSystemTickCount
        );
    }

    return CurrentTick == TargetTick;
}

static void
Stress4CWaitForCompletion(
    uint64_t Epoch
)
{
    uint64_t StartTsc = __rdtsc();

    for (;;) {
        uint64_t Completed = InterlockedLoadAcquire(
            &Stress4CCompletedEpoch
        );
        if (Completed == Epoch) {
            return;
        }
        if (Completed > Epoch) {
            Stress4CBugCheck(
                Stress4CCompletionCountMismatch,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Completed,
                (void*)(uintptr_t)InterlockedLoadAcquire(
                    &Stress4CCompletionCount
                )
            );
        }
        if (Stress4CWatchdogExpired(StartTsc)) {
            Stress4CBugCheck(
                Stress4CCompletionTimeout,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Completed,
                (void*)(uintptr_t)InterlockedLoadAcquire(
                    &Stress4CCompletionCount
                )
            );
        }

        Stress4CPublishProgress((uint32_t)Epoch);
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress4CValidateCleanup(
    uint64_t Epoch,
    MTSTATUS CompletionStatus
)
{
    IRQL OldIrql;
    MsAcquireSpinlock(&Stress4CEvent.Header.Lock, &OldIrql);
    PDOUBLY_LINKED_LIST WaitListHead =
        &Stress4CEvent.Header.WaitListHead;
    bool QueueEmpty = IsListEmpty(WaitListHead);
    PDOUBLY_LINKED_LIST FirstEntry = WaitListHead->Flink;
    PDOUBLY_LINKED_LIST LastEntry = WaitListHead->Blink;
    bool Signaled = Stress4CEvent.Header.SignalState != 0;
    Stress4CEvent.Header.SignalState = 0;
    MsReleaseSpinlock(&Stress4CEvent.Header.Lock, OldIrql);

    if (!QueueEmpty) {
        Stress4CBugCheck(
            Stress4CEventCleanupFailure,
            (void*)(uintptr_t)Epoch,
            FirstEntry,
            LastEntry
        );
    }

    PITHREAD Thread = &Stress4CWaiterThread->InternalThread;
    void* WaitObject = InterlockedLoadAcquire(
        &Thread->WaitBlock.Object
    );
    WAIT_REASON WaitReason = InterlockedLoadAcquire(
        &Thread->WaitBlock.WaitReason
    );
    uint64_t WakeupTime = InterlockedLoadAcquire(
        &Thread->WaitBlock.WakeupTime
    );
    if (WaitObject != NULL ||
        WaitReason != WaitReasonNone ||
        WakeupTime != 0) {
        Stress4CBugCheck(
            Stress4CWaitBlockCleanupFailure,
            WaitObject,
            (void*)(uintptr_t)WaitReason,
            (void*)(uintptr_t)WakeupTime
        );
    }

    bool ExpectedSignaled = CompletionStatus == MT_TIMEOUT;
    if (Signaled != ExpectedSignaled) {
        Stress4CBugCheck(
            Stress4CSignalStateMismatch,
            (void*)(uintptr_t)Epoch,
            (void*)(uintptr_t)ExpectedSignaled,
            (void*)(uintptr_t)Signaled
        );
    }

    if (!Stress4TimerEntryIsIsolated(
            &Stress4CWaiterThread->InternalThread
        )) {
        PDOUBLY_LINKED_LIST Entry =
            &Stress4CWaiterThread->InternalThread.WaitBlock.TimerListEntry;
        Stress4CBugCheck(
            Stress4CTimerCleanupFailure,
            (void*)(uintptr_t)Epoch,
            Entry->Flink,
            Entry->Blink
        );
    }
}

static bool
Stress4CFinishUntimedSample(
    STRESS4C_TIMING Timing,
    uint64_t Epoch,
    MTSTATUS* CompletionStatusOut
)
{
    Stress4CWaitForCompletion(Epoch);
    MTSTATUS CompletionStatus = InterlockedLoadAcquire(
        &Stress4CCompletionStatus
    );
    uint64_t CompletionCount = InterlockedLoadAcquire(
        &Stress4CCompletionCount
    );
    if (CompletionCount != Epoch) {
        Stress4CBugCheck(
            Stress4CCompletionCountMismatch,
            (void*)(uintptr_t)Epoch,
            (void*)(uintptr_t)CompletionCount,
            (void*)(uintptr_t)Timing
        );
    }
    if (CompletionStatus != MT_TIMEOUT) {
        Stress4CBugCheck(
            Stress4CUnexpectedResult,
            (void*)(uintptr_t)Epoch,
            (void*)(uintptr_t)Timing,
            (void*)(uintptr_t)CompletionStatus
        );
    }

    // The controller missed the requested tick. Signal after timeout so the
    // private event reaches the same cleanup state as a counted late sample.
    MTSTATUS SetStatus = MsSetEvent(&Stress4CEvent);
    if (SetStatus != MT_SUCCESS) {
        Stress4CBugCheck(
            Stress4CSetEventFailure,
            (void*)(uintptr_t)Epoch,
            (void*)(uintptr_t)Timing,
            (void*)(uintptr_t)SetStatus
        );
    }
    Stress4CValidateCleanup(Epoch, CompletionStatus);
    *CompletionStatusOut = CompletionStatus;
    return false;
}

static bool
Stress4CRunRace(
    STRESS4C_TIMING Timing,
    uint64_t Epoch,
    MTSTATUS* CompletionStatusOut
)
{
    InterlockedStoreRelease(
        &Stress4CRequestedTiming,
        (uint32_t)Timing
    );
    InterlockedStoreRelease(&Stress4CRequestEpoch, Epoch);
    Stress4CPublishProgress((uint32_t)Epoch);

    if (!Stress4CWaitForRegistration(Epoch)) {
        return Stress4CFinishUntimedSample(
            Timing,
            Epoch,
            CompletionStatusOut
        );
    }

    uint64_t WakeupTime = InterlockedLoadAcquire(
        &Stress4CWaiterThread->InternalThread.WaitBlock.WakeupTime
    );
    uint64_t TargetTick;
    if (Timing == Stress4COneTickBefore) {
        if (WakeupTime == 0) {
            Stress4CBugCheck(
                Stress4CProtocolFailure,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)WakeupTime,
                (void*)(uintptr_t)Timing
            );
        }
        TargetTick = WakeupTime - 1;
    }
    else if (Timing == Stress4CSameTick) {
        TargetTick = WakeupTime;
    }
    else {
        if (WakeupTime == UINT64_MAX) {
            Stress4CBugCheck(
                Stress4CProtocolFailure,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)WakeupTime,
                (void*)(uintptr_t)Timing
            );
        }
        TargetTick = WakeupTime + 1;
    }

    if (!Stress4CWaitForClockTick(TargetTick, Epoch)) {
        return Stress4CFinishUntimedSample(
            Timing,
            Epoch,
            CompletionStatusOut
        );
    }
    Stress4CPublishProgress((uint32_t)Epoch);
    MTSTATUS SetStatus = MsSetEvent(&Stress4CEvent);
    if (SetStatus != MT_SUCCESS) {
        Stress4CBugCheck(
            Stress4CSetEventFailure,
            (void*)(uintptr_t)Epoch,
            (void*)(uintptr_t)Timing,
            (void*)(uintptr_t)SetStatus
        );
    }

    Stress4CWaitForCompletion(Epoch);
    MTSTATUS CompletionStatus = InterlockedLoadAcquire(
        &Stress4CCompletionStatus
    );
    uint64_t CompletionCount = InterlockedLoadAcquire(
        &Stress4CCompletionCount
    );
    if (CompletionCount != Epoch) {
        Stress4CBugCheck(
            Stress4CCompletionCountMismatch,
            (void*)(uintptr_t)Epoch,
            (void*)(uintptr_t)CompletionCount,
            (void*)(uintptr_t)Timing
        );
    }

    bool ResultValid = Timing == Stress4COneTickBefore
        ? CompletionStatus == MT_SUCCESS
        : Timing == Stress4COneTickAfter
            ? CompletionStatus == MT_TIMEOUT
            : CompletionStatus == MT_SUCCESS ||
                CompletionStatus == MT_TIMEOUT;
    if (!ResultValid) {
        Stress4CBugCheck(
            Stress4CUnexpectedResult,
            (void*)(uintptr_t)Epoch,
            (void*)(uintptr_t)Timing,
            (void*)(uintptr_t)CompletionStatus
        );
    }

    Stress4CValidateCleanup(Epoch, CompletionStatus);
    *CompletionStatusOut = CompletionStatus;
    return true;
}

static void
Stress4CWaiter(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);
    uint64_t LocalEpoch = 0;

    while (InterlockedLoadAcquire(&Stress4CActive)) {
        uint64_t Epoch = InterlockedLoadAcquire(
            &Stress4CRequestEpoch
        );
        if (Epoch == LocalEpoch) {
            MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
            continue;
        }
        if (Epoch != LocalEpoch + 1) {
            Stress4CBugCheck(
                Stress4CProtocolFailure,
                (void*)(uintptr_t)LocalEpoch,
                (void*)(uintptr_t)Epoch,
                NULL
            );
        }

        InterlockedStoreRelease(&Stress4CStartedEpoch, Epoch);
        MTSTATUS Status = MsWaitForSingleObject(
            &Stress4CEvent,
            KernelMode,
            false,
            STRESS4C_TIMEOUT_TICKS * TICK_MS
        );
        InterlockedStoreRelease(
            &Stress4CCompletionStatus,
            Status
        );
        InterlockedIncrementU64(&Stress4CCompletionCount);
        InterlockedStoreRelease(
            &Stress4CCompletedEpoch,
            Epoch
        );
        LocalEpoch = Epoch;
    }
}

static void
Stress4CWatchdog(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);

    for (;;) {
        uint64_t Heartbeat = InterlockedLoadAcquire(
            &Stress4CHeartbeatTsc
        );
        uint64_t Now = __rdtsc();
        if (Now >= Heartbeat &&
            Now - Heartbeat >
                STRESS4C_WATCHDOG_SECONDS * Stress2CTscTicksPerSecond) {
            Stress4CBugCheck(
                Stress4CWatchdogTimeout,
                (void*)(uintptr_t)InterlockedLoadAcquire(
                    &Stress4CRequestEpoch
                ),
                (void*)(uintptr_t)InterlockedLoadAcquire(
                    &Stress4CRequestedTiming
                ),
                (void*)(uintptr_t)InterlockedLoadAcquire(
                    &Stress4CProgress
                )
            );
        }

        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress4CController(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);

    gop_printf(
        COLOR_GREEN,
        "STRESS 4C START (signal/timeout timing, %u CPUs)\n",
        MeGetActiveProcessorCount()
    );

    uint64_t Epoch = 0;
    uint32_t BeforeSignalWins = 0;
    uint32_t SameSignalWins = 0;
    uint32_t SameTimeoutWins = 0;
    uint32_t AfterTimeoutWins = 0;
    uint32_t RegistrationRetries = 0;

    for (uint32_t Index = 0; Index < STRESS4C_ITERATIONS;) {
        MTSTATUS Status;
        bool TimedSample = Stress4CRunRace(
            Stress4COneTickBefore,
            ++Epoch,
            &Status
        );
        if (!TimedSample) {
            RegistrationRetries++;
            if (RegistrationRetries > STRESS4C_MAX_REGISTRATION_RETRIES) {
                Stress4CBugCheck(
                    Stress4CRegistrationTimeout,
                    (void*)(uintptr_t)RegistrationRetries,
                    (void*)(uintptr_t)Stress4COneTickBefore,
                    (void*)(uintptr_t)Epoch
                );
            }
            continue;
        }
        if (Status == MT_SUCCESS) {
            BeforeSignalWins++;
        }
        Index++;
    }
    gop_printf(
        COLOR_GREEN,
        "STRESS 4C one tick before PASS (%u/%u signal)\n",
        BeforeSignalWins,
        STRESS4C_ITERATIONS
    );

    for (uint32_t Index = 0; Index < STRESS4C_ITERATIONS;) {
        MTSTATUS Status;
        bool TimedSample = Stress4CRunRace(
            Stress4CSameTick,
            ++Epoch,
            &Status
        );
        if (!TimedSample) {
            RegistrationRetries++;
            if (RegistrationRetries > STRESS4C_MAX_REGISTRATION_RETRIES) {
                Stress4CBugCheck(
                    Stress4CRegistrationTimeout,
                    (void*)(uintptr_t)RegistrationRetries,
                    (void*)(uintptr_t)Stress4CSameTick,
                    (void*)(uintptr_t)Epoch
                );
            }
            continue;
        }
        if (Status == MT_SUCCESS) {
            SameSignalWins++;
        }
        else {
            SameTimeoutWins++;
        }
        Index++;
    }
    gop_printf(
        COLOR_GREEN,
        "STRESS 4C same tick PASS (%u signal, %u timeout)\n",
        SameSignalWins,
        SameTimeoutWins
    );

    for (uint32_t Index = 0; Index < STRESS4C_ITERATIONS;) {
        MTSTATUS Status;
        bool TimedSample = Stress4CRunRace(
            Stress4COneTickAfter,
            ++Epoch,
            &Status
        );
        if (!TimedSample) {
            RegistrationRetries++;
            if (RegistrationRetries > STRESS4C_MAX_REGISTRATION_RETRIES) {
                Stress4CBugCheck(
                    Stress4CRegistrationTimeout,
                    (void*)(uintptr_t)RegistrationRetries,
                    (void*)(uintptr_t)Stress4COneTickAfter,
                    (void*)(uintptr_t)Epoch
                );
            }
            continue;
        }
        if (Status == MT_TIMEOUT) {
            AfterTimeoutWins++;
        }
        Index++;
    }
    gop_printf(
        COLOR_GREEN,
        "STRESS 4C one tick after PASS (%u/%u timeout)\n",
        AfterTimeoutWins,
        STRESS4C_ITERATIONS
    );
    gop_printf(
        COLOR_GREEN,
        "STRESS 4C PASS (%u timed races, %u registration retries)\n",
        STRESS4C_ITERATIONS * 3U,
        RegistrationRetries
    );
}

#define STRESS5A_WAITER_COUNT     4U
#define STRESS5A_ROUNDS           100U
#define STRESS5A_RACE_ROUNDS      100U
#define STRESS5A_RACE_TIMEOUT_MS  (2ULL * TICK_MS)
#define STRESS5A_WATCHDOG_SECONDS 5ULL

typedef enum _STRESS5A_FAILURE_STAGE {
    Stress5ABankedPermitFailure = 1,
    Stress5AProtocolFailure,
    Stress5ARegistrationTimeout,
    Stress5ACompletionTimeout,
    Stress5ACompletionCountMismatch,
    Stress5AQueueTopologyFailure,
    Stress5AQueueCountMismatch,
    Stress5ASignalStateMismatch,
    Stress5AReleaseCountMismatch,
    Stress5AFifoFailure,
    Stress5AUnexpectedStatus,
    Stress5ARaceRegistrationTimeout,
    Stress5ARaceCompletionTimeout
} STRESS5A_FAILURE_STAGE;

typedef struct _STRESS5A_SNAPSHOT {
    uint32_t WaiterCount;
    int32_t SignalState;
    bool TopologyValid;
    PITHREAD Waiters[STRESS5A_WAITER_COUNT];
    void* FaultingEntry;
} STRESS5A_SNAPSHOT;

static SEMAPHORE Stress5ASemaphore;
static PETHREAD Stress5AWaiterThreads[STRESS5A_WAITER_COUNT];
static volatile uint64_t Stress5ARequestEpoch;
static volatile uint64_t Stress5AStartedEpoch[STRESS5A_WAITER_COUNT];
static volatile uint64_t Stress5ACompletedEpoch[STRESS5A_WAITER_COUNT];
static volatile MTSTATUS Stress5ACompletionStatus[STRESS5A_WAITER_COUNT];
static PETHREAD Stress5ARaceThread;
static volatile uint64_t Stress5ARaceRequestEpoch;
static volatile uint64_t Stress5ARaceStartedEpoch;
static volatile uint64_t Stress5ARaceCompletedEpoch;
static volatile MTSTATUS Stress5ARaceStatus;
static volatile bool Stress5AActive;

NORETURN
static void
Stress5ABugCheck(
    STRESS5A_FAILURE_STAGE Stage,
    void* Detail1,
    void* Detail2,
    void* Detail3
)
{
    MeBugCheckEx(
        WAIT_STATE_FAILURE,
        (void*)(uintptr_t)Stage,
        Detail1,
        Detail2,
        Detail3
    );
}

static bool
Stress5AWatchdogExpired(
    uint64_t StartTsc
)
{
    uint64_t Now = __rdtsc();
    return Now >= StartTsc &&
        Now - StartTsc >
            STRESS5A_WATCHDOG_SECONDS * Stress2CTscTicksPerSecond;
}

static STRESS5A_SNAPSHOT
Stress5ASnapshotSemaphore(void)
{
    STRESS5A_SNAPSHOT Snapshot = { 0 };
    Snapshot.TopologyValid = true;

    IRQL OldIrql;
    MsAcquireSpinlock(&Stress5ASemaphore.Header.Lock, &OldIrql);
    Snapshot.SignalState = Stress5ASemaphore.Header.SignalState;

    PDOUBLY_LINKED_LIST ListHead = &Stress5ASemaphore.Header.WaitListHead;
    PDOUBLY_LINKED_LIST PreviousEntry = ListHead;
    PDOUBLY_LINKED_LIST CurrentEntry = ListHead->Flink;
    if (!CurrentEntry || !ListHead->Blink) {
        Snapshot.TopologyValid = false;
        Snapshot.FaultingEntry = CurrentEntry;
    }

    while (Snapshot.TopologyValid && CurrentEntry != ListHead) {
        if (!CurrentEntry || !CurrentEntry->Flink ||
            !CurrentEntry->Blink ||
            CurrentEntry->Blink != PreviousEntry ||
            CurrentEntry->Flink->Blink != CurrentEntry ||
            Snapshot.WaiterCount >= STRESS5A_WAITER_COUNT) {
            Snapshot.TopologyValid = false;
            Snapshot.FaultingEntry = CurrentEntry;
            break;
        }

        PWAIT_BLOCK WaitBlock = CONTAINING_RECORD(
            CurrentEntry,
            WAIT_BLOCK,
            ObjectListEntry
        );
        Snapshot.Waiters[Snapshot.WaiterCount++] = CONTAINING_RECORD(
            WaitBlock,
            ITHREAD,
            WaitBlock
        );
        PreviousEntry = CurrentEntry;
        CurrentEntry = CurrentEntry->Flink;
    }

    if (Snapshot.TopologyValid &&
        (PreviousEntry != ListHead->Blink ||
         ListHead->Blink->Flink != ListHead)) {
        Snapshot.TopologyValid = false;
        Snapshot.FaultingEntry = PreviousEntry;
    }

    MsReleaseSpinlock(&Stress5ASemaphore.Header.Lock, OldIrql);
    return Snapshot;
}

static uint32_t
Stress5ACountCompletions(
    uint64_t Epoch
)
{
    uint32_t Count = 0;
    for (uint32_t Index = 0; Index < STRESS5A_WAITER_COUNT; Index++) {
        uint64_t Completed = InterlockedLoadAcquire(
            &Stress5ACompletedEpoch[Index]
        );
        if (Completed > Epoch) {
            Stress5ABugCheck(
                Stress5AProtocolFailure,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Completed,
                (void*)(uintptr_t)Index
            );
        }
        if (Completed == Epoch) {
            Count++;
        }
    }
    return Count;
}

static STRESS5A_SNAPSHOT
Stress5AWaitForRegistrations(
    uint64_t Epoch
)
{
    uint64_t StartTsc = __rdtsc();

    for (;;) {
        STRESS5A_SNAPSHOT Snapshot = Stress5ASnapshotSemaphore();
        if (!Snapshot.TopologyValid) {
            Stress5ABugCheck(
                Stress5AQueueTopologyFailure,
                (void*)(uintptr_t)Epoch,
                Snapshot.FaultingEntry,
                NULL
            );
        }

        bool AllRegistered = Snapshot.WaiterCount == STRESS5A_WAITER_COUNT &&
            Snapshot.SignalState == 0;
        for (uint32_t Index = 0;
             AllRegistered && Index < STRESS5A_WAITER_COUNT;
             Index++) {
            PITHREAD Thread = &Stress5AWaiterThreads[Index]->InternalThread;
            THREAD_STATE State = InterlockedLoadAcquire(&Thread->ThreadState);
            AllRegistered = InterlockedLoadAcquire(
                    &Stress5AStartedEpoch[Index]
                ) == Epoch &&
                InterlockedLoadAcquire(&Thread->WaitBlock.Object) ==
                    &Stress5ASemaphore.Header &&
                InterlockedLoadAcquire(&Thread->WaitStatus) == MT_PENDING &&
                (State == THREAD_BLOCKING || State == THREAD_BLOCKED);
        }

        if (AllRegistered) {
            return Snapshot;
        }
        if (Stress5ACountCompletions(Epoch) != 0) {
            Stress5ABugCheck(
                Stress5ACompletionCountMismatch,
                (void*)(uintptr_t)Epoch,
                NULL,
                NULL
            );
        }
        if (Stress5AWatchdogExpired(StartTsc)) {
            Stress5ABugCheck(
                Stress5ARegistrationTimeout,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Snapshot.WaiterCount,
                (void*)(uintptr_t)Snapshot.SignalState
            );
        }

        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress5AWaitForCompletions(
    uint64_t Epoch,
    uint32_t ExpectedCount
)
{
    uint64_t StartTsc = __rdtsc();

    for (;;) {
        uint32_t Count = Stress5ACountCompletions(Epoch);
        if (Count == ExpectedCount) {
            return;
        }
        if (Count > ExpectedCount) {
            Stress5ABugCheck(
                Stress5ACompletionCountMismatch,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)ExpectedCount,
                (void*)(uintptr_t)Count
            );
        }
        if (Stress5AWatchdogExpired(StartTsc)) {
            Stress5ABugCheck(
                Stress5ACompletionTimeout,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)ExpectedCount,
                (void*)(uintptr_t)Count
            );
        }

        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress5ARequireState(
    uint64_t Epoch,
    uint32_t ExpectedWaiters,
    int32_t ExpectedSignalState
)
{
    STRESS5A_SNAPSHOT Snapshot = Stress5ASnapshotSemaphore();
    if (!Snapshot.TopologyValid) {
        Stress5ABugCheck(
            Stress5AQueueTopologyFailure,
            (void*)(uintptr_t)Epoch,
            Snapshot.FaultingEntry,
            NULL
        );
    }
    if (Snapshot.WaiterCount != ExpectedWaiters) {
        Stress5ABugCheck(
            Stress5AQueueCountMismatch,
            (void*)(uintptr_t)Epoch,
            (void*)(uintptr_t)ExpectedWaiters,
            (void*)(uintptr_t)Snapshot.WaiterCount
        );
    }
    if (Snapshot.SignalState != ExpectedSignalState) {
        Stress5ABugCheck(
            Stress5ASignalStateMismatch,
            (void*)(uintptr_t)Epoch,
            (void*)(uintptr_t)ExpectedSignalState,
            (void*)(uintptr_t)Snapshot.SignalState
        );
    }
}

static void
Stress5AWaiter(
    THREAD_PARAMETER Parameter
)
{
    uint32_t Index = (uint32_t)(uintptr_t)Parameter;
    if (Index >= STRESS5A_WAITER_COUNT) {
        Stress5ABugCheck(
            Stress5AProtocolFailure,
            (void*)(uintptr_t)Index,
            NULL,
            NULL
        );
    }

    uint64_t LocalEpoch = 0;
    while (InterlockedLoadAcquire(&Stress5AActive)) {
        uint64_t Epoch = InterlockedLoadAcquire(&Stress5ARequestEpoch);
        if (Epoch == LocalEpoch) {
            MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
            continue;
        }
        if (Epoch != LocalEpoch + 1) {
            Stress5ABugCheck(
                Stress5AProtocolFailure,
                (void*)(uintptr_t)Index,
                (void*)(uintptr_t)LocalEpoch,
                (void*)(uintptr_t)Epoch
            );
        }

        InterlockedStoreRelease(&Stress5AStartedEpoch[Index], Epoch);
        MTSTATUS Status = MsWaitForSingleObject(
            &Stress5ASemaphore,
            KernelMode,
            false,
            MT_INFINITE
        );
        InterlockedStoreRelease(&Stress5ACompletionStatus[Index], Status);
        InterlockedStoreRelease(&Stress5ACompletedEpoch[Index], Epoch);
        LocalEpoch = Epoch;
    }
}

static void
Stress5ARaceWaiter(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);

    uint64_t LocalEpoch = 0;
    while (InterlockedLoadAcquire(&Stress5AActive)) {
        uint64_t Epoch = InterlockedLoadAcquire(&Stress5ARaceRequestEpoch);
        if (Epoch == LocalEpoch) {
            MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
            continue;
        }
        if (Epoch != LocalEpoch + 1) {
            Stress5ABugCheck(
                Stress5AProtocolFailure,
                (void*)(uintptr_t)LocalEpoch,
                (void*)(uintptr_t)Epoch,
                Stress5ARaceThread
            );
        }

        InterlockedStoreRelease(&Stress5ARaceStartedEpoch, Epoch);
        MTSTATUS Status = MsWaitForSingleObject(
            &Stress5ASemaphore,
            KernelMode,
            false,
            STRESS5A_RACE_TIMEOUT_MS
        );
        InterlockedStoreRelease(&Stress5ARaceStatus, Status);
        InterlockedStoreRelease(&Stress5ARaceCompletedEpoch, Epoch);
        LocalEpoch = Epoch;
    }
}

static uint64_t
Stress5AWaitForRaceRegistration(
    uint64_t Epoch
)
{
    uint64_t StartTsc = __rdtsc();
    PITHREAD RaceThread = &Stress5ARaceThread->InternalThread;

    for (;;) {
        STRESS5A_SNAPSHOT Snapshot = Stress5ASnapshotSemaphore();
        if (!Snapshot.TopologyValid) {
            Stress5ABugCheck(
                Stress5AQueueTopologyFailure,
                (void*)(uintptr_t)Epoch,
                Snapshot.FaultingEntry,
                Stress5ARaceThread
            );
        }

        uint64_t WakeupTime = InterlockedLoadAcquire(
            &RaceThread->WaitBlock.WakeupTime
        );
        THREAD_STATE State = InterlockedLoadAcquire(&RaceThread->ThreadState);
        bool Registered = Snapshot.WaiterCount == 1 &&
            Snapshot.Waiters[0] == RaceThread &&
            Snapshot.SignalState == 0 &&
            InterlockedLoadAcquire(&Stress5ARaceStartedEpoch) == Epoch &&
            InterlockedLoadAcquire(&RaceThread->WaitBlock.Object) ==
                &Stress5ASemaphore.Header &&
            InterlockedLoadAcquire(&RaceThread->WaitStatus) == MT_PENDING &&
            WakeupTime != 0 &&
            (State == THREAD_BLOCKING || State == THREAD_BLOCKED);
        if (Registered) {
            return WakeupTime;
        }

        if (InterlockedLoadAcquire(&Stress5ARaceCompletedEpoch) == Epoch) {
            Stress5ABugCheck(
                Stress5AProtocolFailure,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Snapshot.WaiterCount,
                (void*)(uintptr_t)WakeupTime
            );
        }
        if (Stress5AWatchdogExpired(StartTsc)) {
            Stress5ABugCheck(
                Stress5ARaceRegistrationTimeout,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Snapshot.WaiterCount,
                (void*)(uintptr_t)WakeupTime
            );
        }

        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static MTSTATUS
Stress5AWaitForRaceCompletion(
    uint64_t Epoch
)
{
    uint64_t StartTsc = __rdtsc();

    while (InterlockedLoadAcquire(&Stress5ARaceCompletedEpoch) != Epoch) {
        if (Stress5AWatchdogExpired(StartTsc)) {
            Stress5ABugCheck(
                Stress5ARaceCompletionTimeout,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)InterlockedLoadAcquire(
                    &Stress5ARaceCompletedEpoch
                ),
                Stress5ARaceThread
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }

    return InterlockedLoadAcquire(&Stress5ARaceStatus);
}

static void
Stress5ARunTimeoutRaces(void)
{
    uint32_t SignalWins = 0;
    uint32_t TimeoutWins = 0;

    for (uint64_t Epoch = 1; Epoch <= STRESS5A_RACE_ROUNDS; Epoch++) {
        InterlockedStoreRelease(&Stress5ARaceRequestEpoch, Epoch);
        uint64_t WakeupTime = Stress5AWaitForRaceRegistration(Epoch);

        if ((Epoch & 1ULL) == 0) {
            while (InterlockedLoadAcquire(&MeSystemTickCount) < WakeupTime) {
                MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
            }
        }

        int32_t PreviousCount = MsReleaseSemaphore(&Stress5ASemaphore, 1);
        if (PreviousCount != 0) {
            Stress5ABugCheck(
                Stress5AReleaseCountMismatch,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)PreviousCount,
                (void*)2
            );
        }

        MTSTATUS Status = Stress5AWaitForRaceCompletion(Epoch);
        Stress5ARequireState(
            Epoch,
            0,
            Status == MT_TIMEOUT ? 1 : 0
        );

        if (Status == MT_SUCCESS) {
            SignalWins++;
        }
        else if (Status == MT_TIMEOUT) {
            TimeoutWins++;
            Status = MsWaitForSingleObject(
                &Stress5ASemaphore,
                KernelMode,
                false,
                0
            );
            if (Status != MT_SUCCESS) {
                Stress5ABugCheck(
                    Stress5ABankedPermitFailure,
                    (void*)(uintptr_t)Epoch,
                    (void*)(uintptr_t)Status,
                    (void*)2
                );
            }
            Stress5ARequireState(Epoch, 0, 0);
        }
        else {
            Stress5ABugCheck(
                Stress5AUnexpectedStatus,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Status,
                Stress5ARaceThread
            );
        }
    }

    gop_printf(
        COLOR_GREEN,
        "STRESS 5A timeout race PASS (%u signal, %u timeout)\n",
        SignalWins,
        TimeoutWins
    );
}

static void
Stress5ATestBankedPermits(void)
{
    MsInitializeSemaphore(&Stress5ASemaphore, 2, 4);

    for (uint32_t Index = 0; Index < 2; Index++) {
        MTSTATUS Status = MsWaitForSingleObject(
            &Stress5ASemaphore,
            KernelMode,
            false,
            0
        );
        if (Status != MT_SUCCESS) {
            Stress5ABugCheck(
                Stress5ABankedPermitFailure,
                (void*)(uintptr_t)Index,
                (void*)(uintptr_t)Status,
                NULL
            );
        }
    }

    MTSTATUS Status = MsWaitForSingleObject(
        &Stress5ASemaphore,
        KernelMode,
        false,
        0
    );
    if (Status != MT_TIMEOUT || Stress5ASemaphore.Header.SignalState != 0) {
        Stress5ABugCheck(
            Stress5ABankedPermitFailure,
            (void*)(uintptr_t)Status,
            (void*)(uintptr_t)Stress5ASemaphore.Header.SignalState,
            NULL
        );
    }

    int32_t PreviousCount = MsReleaseSemaphore(&Stress5ASemaphore, 4);
    if (PreviousCount != 0) {
        Stress5ABugCheck(
            Stress5AReleaseCountMismatch,
            NULL,
            (void*)(uintptr_t)PreviousCount,
            NULL
        );
    }

    for (uint32_t Index = 0; Index < 4; Index++) {
        Status = MsWaitForSingleObject(
            &Stress5ASemaphore,
            KernelMode,
            false,
            0
        );
        if (Status != MT_SUCCESS) {
            Stress5ABugCheck(
                Stress5ABankedPermitFailure,
                (void*)(uintptr_t)(Index + 2U),
                (void*)(uintptr_t)Status,
                NULL
            );
        }
    }

    Status = MsWaitForSingleObject(
        &Stress5ASemaphore,
        KernelMode,
        false,
        0
    );
    if (Status != MT_TIMEOUT || Stress5ASemaphore.Header.SignalState != 0) {
        Stress5ABugCheck(
            Stress5ABankedPermitFailure,
            (void*)(uintptr_t)Status,
            (void*)(uintptr_t)Stress5ASemaphore.Header.SignalState,
            (void*)1
        );
    }

    gop_printf(COLOR_GREEN, "STRESS 5A banked permits PASS\n");
}

static void
Stress5AController(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);

    gop_printf(
        COLOR_GREEN,
        "STRESS 5A START (semaphore permits, %u waiters, %u CPUs)\n",
        STRESS5A_WAITER_COUNT,
        MeGetActiveProcessorCount()
    );

    for (uint64_t Epoch = 1; Epoch <= STRESS5A_ROUNDS; Epoch++) {
        InterlockedStoreRelease(&Stress5ARequestEpoch, Epoch);
        STRESS5A_SNAPSHOT Before = Stress5AWaitForRegistrations(Epoch);

        int32_t PreviousCount = MsReleaseSemaphore(&Stress5ASemaphore, 2);
        if (PreviousCount != 0) {
            Stress5ABugCheck(
                Stress5AReleaseCountMismatch,
                (void*)(uintptr_t)Epoch,
                NULL,
                (void*)(uintptr_t)PreviousCount
            );
        }

        Stress5AWaitForCompletions(Epoch, 2);
        STRESS5A_SNAPSHOT Half = Stress5ASnapshotSemaphore();
        if (!Half.TopologyValid) {
            Stress5ABugCheck(
                Stress5AQueueTopologyFailure,
                (void*)(uintptr_t)Epoch,
                Half.FaultingEntry,
                NULL
            );
        }
        if (Half.WaiterCount != 2 || Half.SignalState != 0) {
            Stress5ABugCheck(
                Stress5AQueueCountMismatch,
                (void*)(uintptr_t)Epoch,
                (void*)(uintptr_t)Half.WaiterCount,
                (void*)(uintptr_t)Half.SignalState
            );
        }
        if (Half.Waiters[0] != Before.Waiters[2] ||
            Half.Waiters[1] != Before.Waiters[3]) {
            Stress5ABugCheck(
                Stress5AFifoFailure,
                (void*)(uintptr_t)Epoch,
                Half.Waiters[0],
                Before.Waiters[2]
            );
        }

        PreviousCount = MsReleaseSemaphore(&Stress5ASemaphore, 2);
        if (PreviousCount != 0) {
            Stress5ABugCheck(
                Stress5AReleaseCountMismatch,
                (void*)(uintptr_t)Epoch,
                (void*)1,
                (void*)(uintptr_t)PreviousCount
            );
        }

        Stress5AWaitForCompletions(Epoch, STRESS5A_WAITER_COUNT);
        Stress5ARequireState(Epoch, 0, 0);
        for (uint32_t Index = 0; Index < STRESS5A_WAITER_COUNT; Index++) {
            MTSTATUS Status = InterlockedLoadAcquire(
                &Stress5ACompletionStatus[Index]
            );
            if (Status != MT_SUCCESS) {
                Stress5ABugCheck(
                    Stress5AUnexpectedStatus,
                    (void*)(uintptr_t)Epoch,
                    (void*)(uintptr_t)Index,
                    (void*)(uintptr_t)Status
                );
            }
        }

        if ((Epoch % 10U) == 0) {
            gop_printf(
                COLOR_GREEN,
                "STRESS 5A progress %llu/%u rounds\n",
                (unsigned long long)Epoch,
                STRESS5A_ROUNDS
            );
        }
    }

    gop_printf(
        COLOR_GREEN,
        "STRESS 5A PASS (%u blocked waits, FIFO and permit accounting)\n",
        STRESS5A_ROUNDS * STRESS5A_WAITER_COUNT
    );

    Stress5ARunTimeoutRaces();
}

#define STRESS_SUITE_START_TIMEOUT_SECONDS 30ULL

static void
StressSuiteInitializeEvent(
    PEVENT Event,
    DISPATCHER_TYPE Type
)
{
    MsInitializeEvent(Event, Type, false);
}

static PETHREAD
StressSuiteCreateThread(
    ThreadEntry Entry,
    THREAD_PARAMETER Parameter
)
{
    PETHREAD Thread = NULL;
    MTSTATUS Status = PsCreateSystemThread(
        Entry,
        Parameter,
        DEFAULT_TIMESLICE_TICKS,
        &Thread
    );
    if (MT_FAILURE(Status) || !Thread) {
        MeBugCheckEx(
            PSWORKER_INIT_FAILED,
            (void*)(uintptr_t)Status,
            (void*)Entry,
            Parameter,
            Thread
        );
    }

    // PsCreateSystemThread returns one caller-owned reference through
    // OutThread. This generic helper preserves its original borrowed-pointer
    // contract; tests that retain the pointer take their own guarded reference.
    ObDereferenceObject(Thread);
    return Thread;
}

static void
StressSuiteRunStress2(void)
{
    Stress2Counter1 = 0;
    Stress2Counter2 = 0;
    Stress2ControllerClaimed = 0;
    Stress2SuiteComplete = false;
    Stress2DpcExecutions = 0;
    Stress2DpcExecutionTarget = 0;
    Stress2CEnduranceActive = false;
    Stress2BusyActive = true;
    Stress2StartTick = InterlockedLoadAcquire(
        &MeSystemTickCount
    );
    MeInitializeDpc(
        &Stress2Dpc,
        Stress2DpcRoutine,
        NULL,
        LOW_PRIORITY
    );

    StressSuiteCreateThread(Stress2BusyThread1, NULL);
    StressSuiteCreateThread(Stress2BusyThread2, NULL);

    uint64_t StartTsc = __rdtsc();
    while (!InterlockedLoadAcquire(&Stress2SuiteComplete)) {
        if (__rdtsc() - StartTsc >
            STRESS_SUITE_START_TIMEOUT_SECONDS *
                Stress2CTscTicksPerSecond) {
            MeBugCheckEx(
                SCHEDULER_FAILURE,
                (void*)(uintptr_t)Stress2Counter1,
                (void*)(uintptr_t)Stress2Counter2,
                (void*)(uintptr_t)Stress2ControllerClaimed,
                (void*)StressSuiteRunStress2
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

#define STRESS5B_CONTENTION_THREADS     4U
#define STRESS5B_CONTENTION_ITERATIONS  250U
#define STRESS5B_ABANDON_WAITERS        4U
#define STRESS5B_MANY_MUTEXES           3U
#define STRESS5B_RACE_ROUNDS            64U
#define STRESS5B_TIMEOUT_MS              (2ULL * TICK_MS)
#define STRESS5B_JOIN_TIMEOUT_MS         30000ULL
#define STRESS5B_WATCHDOG_SECONDS        5ULL
#define STRESS5B_MAX_OWNED_MUTEXES       128U

typedef enum _STRESS5B_FAILURE_STAGE {
    Stress5BBasicStatusFailure = 0x5B01,
    Stress5BMutexStateFailure,
    Stress5BOwnerListFailure,
    Stress5BWrongOwnerFailure,
    Stress5BTimeoutFailure,
    Stress5BContentionStatusFailure,
    Stress5BContentionExclusionFailure,
    Stress5BContentionCountFailure,
    Stress5BThreadReferenceFailure,
    Stress5BThreadJoinFailure,
    Stress5BProtocolTimeout,
    Stress5BWaitRegistrationFailure,
    Stress5BAbandonStatusFailure,
    Stress5BAbandonCountFailure,
    Stress5BTerminationFailure,
    Stress5BRaceStateFailure
} STRESS5B_FAILURE_STAGE;

typedef struct _STRESS5B_SIMPLE_CONTEXT {
    PMUTEX Mutex;
    volatile bool Start;
    volatile MTSTATUS Status;
    uint32_t Index;
} STRESS5B_SIMPLE_CONTEXT;

typedef struct _STRESS5B_OWNER_CONTEXT {
    PMUTEX Mutexes;
    uint32_t MutexCount;
    volatile bool Start;
    volatile bool Ready;
    volatile bool Exit;
} STRESS5B_OWNER_CONTEXT;

typedef struct _STRESS5B_RACE_CONTEXT {
    PMUTEX Mutex;
    volatile bool Start;
    volatile bool Ready;
    volatile bool Race;
    volatile bool ExitWithoutRelease;
} STRESS5B_RACE_CONTEXT;

static MUTEX Stress5BContentionMutex;
static volatile uint64_t Stress5BProtectedCounter;
static volatile uint32_t Stress5BInCriticalSection;

NORETURN
static void
Stress5BBugCheck(
    STRESS5B_FAILURE_STAGE Stage,
    void* Detail1,
    void* Detail2,
    void* Detail3
)
{
    // Stage values use a 0x5Bxx prefix so a framebuffer-only failure still
    // identifies this exact test and subphase.
    MeBugCheckEx(
        WAIT_STATE_FAILURE,
        (void*)(uintptr_t)Stage,
        Detail1,
        Detail2,
        Detail3
    );
}

static bool
Stress5BWatchdogExpired(
    uint64_t StartTsc
)
{
    uint64_t Now = __rdtsc();
    return Now >= StartTsc &&
        Now - StartTsc >
            STRESS5B_WATCHDOG_SECONDS * Stress2CTscTicksPerSecond;
}

static void
Stress5BWaitForFlag(
    volatile bool* Flag,
    void* Detail
)
{
    uint64_t StartTsc = __rdtsc();
    while (!InterlockedLoadAcquire(Flag)) {
        if (Stress5BWatchdogExpired(StartTsc)) {
            Stress5BBugCheck(
                Stress5BProtocolTimeout,
                Detail,
                (void*)Flag,
                NULL
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static PETHREAD
Stress5BCreateRetainedThread(
    ThreadEntry Entry,
    THREAD_PARAMETER Parameter
)
{
    // Every 5B worker waits on its Start field, so it cannot exit between
    // PsCreateSystemThread publishing the pointer and this extra reference.
    PETHREAD Thread = StressSuiteCreateThread(Entry, Parameter);
    if (!ObReferenceObject(Thread)) {
        Stress5BBugCheck(
            Stress5BThreadReferenceFailure,
            Thread,
            (void*)Entry,
            Parameter
        );
    }
    return Thread;
}

static void
Stress5BJoinThread(
    PETHREAD Thread,
    void* Detail
)
{
    MTSTATUS Status = MsWaitForSingleObject(
        &Thread->InternalThread.Header,
        KernelMode,
        false,
        STRESS5B_JOIN_TIMEOUT_MS
    );
    if (Status != MT_SUCCESS) {
        Stress5BBugCheck(
            Stress5BThreadJoinFailure,
            Detail,
            Thread,
            (void*)(uintptr_t)Status
        );
    }
    ObDereferenceObject(Thread);
}

static bool
Stress5BValidateOwnerListLocked(
    PMUTEX Mutex,
    PETHREAD Owner
)
{
    bool Valid = true;
    uint32_t Matches = 0;
    uint32_t Traversed = 0;
    PDOUBLY_LINKED_LIST ListHead = &Owner->InternalThread.OwnedMutexListHead;
    PDOUBLY_LINKED_LIST Previous = ListHead;
    PDOUBLY_LINKED_LIST Entry = ListHead->Flink;

    if (!Entry || !ListHead->Blink) {
        return false;
    }

    while (Entry != ListHead) {
        if (!Entry || !Entry->Flink || !Entry->Blink ||
            Entry->Blink != Previous || Entry->Flink->Blink != Entry ||
            Traversed >= STRESS5B_MAX_OWNED_MUTEXES) {
            Valid = false;
            break;
        }
        if (Entry == &Mutex->OwnerListEntry) {
            Matches++;
        }
        Previous = Entry;
        Entry = Entry->Flink;
        Traversed++;
    }

    if (Valid && (Previous != ListHead->Blink ||
                  ListHead->Blink->Flink != ListHead)) {
        Valid = false;
    }
    return Valid && Matches == 1;
}

static bool
Stress5BReadAndValidateMutex(
    PMUTEX Mutex,
    PETHREAD ExpectedOwner,
    int32_t ExpectedSignalState,
    bool ExpectedAbandoned
)
{
    bool Valid = true;
    bool Abandoned;
    PETHREAD Owner;
    int32_t SignalState;
    IRQL OldIrql;

    MsAcquireSpinlock(&Mutex->Header.Lock, &OldIrql);
    Owner = Mutex->OwnerThread;
    SignalState = Mutex->Header.SignalState;
    Abandoned = Mutex->Abandoned;

    if (Owner != ExpectedOwner || SignalState != ExpectedSignalState ||
        Abandoned != ExpectedAbandoned) {
        Valid = false;
    }

    if (Valid && Owner) {
        MsAcquireSpinlockAtDpcLevel(
            &Owner->InternalThread.OwnedMutexesListLock
        );
        Valid = Stress5BValidateOwnerListLocked(Mutex, Owner);
        MsReleaseSpinlockFromDpcLevel(
            &Owner->InternalThread.OwnedMutexesListLock
        );
    }
    else if (Valid && !IsListEmpty(&Mutex->OwnerListEntry)) {
        Valid = false;
    }

    MsReleaseSpinlock(&Mutex->Header.Lock, OldIrql);

    if (!Valid) {
        Stress5BBugCheck(
            Owner == ExpectedOwner
                ? Stress5BOwnerListFailure
                : Stress5BMutexStateFailure,
            Mutex,
            Owner,
            (void*)(uintptr_t)(uint32_t)SignalState
        );
    }
    return Abandoned;
}

static void
Stress5BWaitForRegistrations(
    PMUTEX Mutex,
    PETHREAD* Threads,
    uint32_t ThreadCount
)
{
    uint64_t StartTsc = __rdtsc();

    for (;;) {
        uint32_t WaiterCount = 0;
        bool TopologyValid = true;
        IRQL OldIrql;
        MsAcquireSpinlock(&Mutex->Header.Lock, &OldIrql);

        PDOUBLY_LINKED_LIST ListHead = &Mutex->Header.WaitListHead;
        PDOUBLY_LINKED_LIST Previous = ListHead;
        PDOUBLY_LINKED_LIST Entry = ListHead->Flink;
        if (!Entry || !ListHead->Blink) {
            TopologyValid = false;
        }

        while (TopologyValid && Entry != ListHead) {
            if (!Entry || !Entry->Flink || !Entry->Blink ||
                Entry->Blink != Previous || Entry->Flink->Blink != Entry ||
                WaiterCount >= ThreadCount) {
                TopologyValid = false;
                break;
            }
            Previous = Entry;
            Entry = Entry->Flink;
            WaiterCount++;
        }

        if (TopologyValid && (Previous != ListHead->Blink ||
                              ListHead->Blink->Flink != ListHead)) {
            TopologyValid = false;
        }

        bool AllRegistered = TopologyValid && WaiterCount == ThreadCount;
        for (uint32_t Index = 0;
             AllRegistered && Index < ThreadCount;
             Index++) {
            PITHREAD Thread = &Threads[Index]->InternalThread;
            THREAD_STATE State = InterlockedLoadAcquire(&Thread->ThreadState);
            AllRegistered =
                InterlockedLoadAcquire(&Thread->WaitStatus) == MT_PENDING &&
                InterlockedLoadAcquire(&Thread->WaitBlock.Object) ==
                    &Mutex->Header &&
                (State == THREAD_BLOCKING || State == THREAD_BLOCKED);
        }

        MsReleaseSpinlock(&Mutex->Header.Lock, OldIrql);

        if (!TopologyValid) {
            Stress5BBugCheck(
                Stress5BWaitRegistrationFailure,
                Mutex,
                (void*)(uintptr_t)WaiterCount,
                (void*)(uintptr_t)ThreadCount
            );
        }
        if (AllRegistered) {
            return;
        }
        if (Stress5BWatchdogExpired(StartTsc)) {
            Stress5BBugCheck(
                Stress5BProtocolTimeout,
                Mutex,
                (void*)(uintptr_t)WaiterCount,
                (void*)(uintptr_t)ThreadCount
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress5BWrongOwnerWorker(
    THREAD_PARAMETER Parameter
)
{
    STRESS5B_SIMPLE_CONTEXT* Context = Parameter;
    Stress5BWaitForFlag(&Context->Start, Context);
    MTSTATUS Status = MsReleaseMutexObject(Context->Mutex);
    InterlockedStoreRelease(&Context->Status, Status);
}

static void
Stress5BTimeoutWorker(
    THREAD_PARAMETER Parameter
)
{
    STRESS5B_SIMPLE_CONTEXT* Context = Parameter;
    Stress5BWaitForFlag(&Context->Start, Context);
    MTSTATUS Status = MsWaitForSingleObject(
        Context->Mutex,
        KernelMode,
        false,
        STRESS5B_TIMEOUT_MS
    );
    InterlockedStoreRelease(&Context->Status, Status);
}

static void
Stress5BContentionWorker(
    THREAD_PARAMETER Parameter
)
{
    STRESS5B_SIMPLE_CONTEXT* Context = Parameter;
    Stress5BWaitForFlag(&Context->Start, Context);

    for (uint32_t Iteration = 0;
         Iteration < STRESS5B_CONTENTION_ITERATIONS;
         Iteration++) {
        MTSTATUS Status = MsWaitForSingleObject(
            &Stress5BContentionMutex,
            KernelMode,
            false,
            MT_INFINITE
        );
        if (Status != MT_SUCCESS) {
            Stress5BBugCheck(
                Stress5BContentionStatusFailure,
                (void*)(uintptr_t)Context->Index,
                (void*)(uintptr_t)Iteration,
                (void*)(uintptr_t)Status
            );
        }

        uint32_t Inside = InterlockedIncrementU32(
            &Stress5BInCriticalSection
        );
        if (Inside != 1) {
            Stress5BBugCheck(
                Stress5BContentionExclusionFailure,
                (void*)(uintptr_t)Context->Index,
                (void*)(uintptr_t)Iteration,
                (void*)(uintptr_t)Inside
            );
        }

        Stress5BProtectedCounter++;
        for (uint32_t Spin = 0; Spin < 16; Spin++) {
            __pause();
        }

        Inside = InterlockedDecrementU32(&Stress5BInCriticalSection);
        if (Inside != 0) {
            Stress5BBugCheck(
                Stress5BContentionExclusionFailure,
                (void*)(uintptr_t)Context->Index,
                (void*)(uintptr_t)Iteration,
                (void*)(uintptr_t)Inside
            );
        }

        Status = MsReleaseMutexObject(&Stress5BContentionMutex);
        if (Status != MT_SUCCESS) {
            Stress5BBugCheck(
                Stress5BContentionStatusFailure,
                (void*)(uintptr_t)Context->Index,
                (void*)(uintptr_t)Iteration,
                (void*)(uintptr_t)Status
            );
        }
    }
}

static void
Stress5BOwnerWorker(
    THREAD_PARAMETER Parameter
)
{
    STRESS5B_OWNER_CONTEXT* Context = Parameter;
    Stress5BWaitForFlag(&Context->Start, Context);

    for (uint32_t Index = 0; Index < Context->MutexCount; Index++) {
        MTSTATUS Status = MsWaitForSingleObject(
            &Context->Mutexes[Index],
            KernelMode,
            false,
            MT_INFINITE
        );
        if (Status != MT_SUCCESS) {
            Stress5BBugCheck(
                Stress5BBasicStatusFailure,
                Context,
                (void*)(uintptr_t)Index,
                (void*)(uintptr_t)Status
            );
        }
    }

    InterlockedStoreRelease(&Context->Ready, true);
    Stress5BWaitForFlag(&Context->Exit, Context);
}

static void
Stress5BAbandonWaiter(
    THREAD_PARAMETER Parameter
)
{
    STRESS5B_SIMPLE_CONTEXT* Context = Parameter;
    Stress5BWaitForFlag(&Context->Start, Context);

    MTSTATUS Status = MsWaitForSingleObject(
        Context->Mutex,
        KernelMode,
        false,
        MT_INFINITE
    );
    if (Status != MT_SUCCESS && Status != MT_MUTEX_ABANDONED) {
        Stress5BBugCheck(
            Stress5BAbandonStatusFailure,
            Context,
            (void*)(uintptr_t)Context->Index,
            (void*)(uintptr_t)Status
        );
    }
    InterlockedStoreRelease(&Context->Status, Status);

    uint32_t Inside = InterlockedIncrementU32(
        &Stress5BInCriticalSection
    );
    if (Inside != 1) {
        Stress5BBugCheck(
            Stress5BContentionExclusionFailure,
            Context,
            (void*)(uintptr_t)Context->Index,
            (void*)(uintptr_t)Inside
        );
    }
    Inside = InterlockedDecrementU32(&Stress5BInCriticalSection);
    if (Inside != 0) {
        Stress5BBugCheck(
            Stress5BContentionExclusionFailure,
            Context,
            (void*)(uintptr_t)Context->Index,
            (void*)(uintptr_t)Inside
        );
    }

    Status = MsReleaseMutexObject(Context->Mutex);
    if (Status != MT_SUCCESS) {
        Stress5BBugCheck(
            Stress5BAbandonStatusFailure,
            Context,
            (void*)(uintptr_t)Context->Index,
            (void*)(uintptr_t)Status
        );
    }
}

static void
Stress5BRaceOwner(
    THREAD_PARAMETER Parameter
)
{
    STRESS5B_RACE_CONTEXT* Context = Parameter;
    Stress5BWaitForFlag(&Context->Start, Context);

    MTSTATUS Status = MsWaitForSingleObject(
        Context->Mutex,
        KernelMode,
        false,
        MT_INFINITE
    );
    if (Status != MT_SUCCESS) {
        Stress5BBugCheck(
            Stress5BRaceStateFailure,
            Context,
            Context->Mutex,
            (void*)(uintptr_t)Status
        );
    }

    InterlockedStoreRelease(&Context->Ready, true);
    Stress5BWaitForFlag(&Context->Race, Context);

    // Race a normal final release against the owner choosing to exit while it
    // still owns the mutex. System workers are not remotely terminable.
    if (InterlockedLoadAcquire(&Context->ExitWithoutRelease)) {
        return;
    }

    Status = MsReleaseMutexObject(Context->Mutex);
    if (Status != MT_SUCCESS) {
        Stress5BBugCheck(
            Stress5BRaceStateFailure,
            Context,
            Context->Mutex,
            (void*)(uintptr_t)Status
        );
    }
}

static void
Stress5BRunBasicSemantics(void)
{
    MUTEX Mutex;
    PETHREAD CurrentThread = PsGetCurrentThread();
    MsInitializeMutexObject(&Mutex);

    MTSTATUS Status = MsWaitForSingleObject(
        &Mutex,
        KernelMode,
        false,
        MT_INFINITE
    );
    if (Status != MT_SUCCESS) {
        Stress5BBugCheck(
            Stress5BBasicStatusFailure,
            &Mutex,
            NULL,
            (void*)(uintptr_t)Status
        );
    }
    Stress5BReadAndValidateMutex(&Mutex, CurrentThread, 0, false);

    Status = MsWaitForSingleObject(&Mutex, KernelMode, false, MT_INFINITE);
    if (Status != MT_SUCCESS) {
        Stress5BBugCheck(
            Stress5BBasicStatusFailure,
            &Mutex,
            (void*)1,
            (void*)(uintptr_t)Status
        );
    }
    Stress5BReadAndValidateMutex(&Mutex, CurrentThread, -1, false);

    Status = MsReleaseMutexObject(&Mutex);
    if (Status != MT_SUCCESS) {
        Stress5BBugCheck(
            Stress5BBasicStatusFailure,
            &Mutex,
            (void*)2,
            (void*)(uintptr_t)Status
        );
    }
    Stress5BReadAndValidateMutex(&Mutex, CurrentThread, 0, false);

    STRESS5B_SIMPLE_CONTEXT WrongOwner = {
        .Mutex = &Mutex,
        .Status = MT_PENDING
    };
    PETHREAD WrongOwnerThread = Stress5BCreateRetainedThread(
        Stress5BWrongOwnerWorker,
        &WrongOwner
    );
    InterlockedStoreRelease(&WrongOwner.Start, true);
    Stress5BJoinThread(WrongOwnerThread, (void*)0x5B11);
    if (InterlockedLoadAcquire(&WrongOwner.Status) != MT_MUTEX_NOT_OWNED) {
        Stress5BBugCheck(
            Stress5BWrongOwnerFailure,
            &Mutex,
            WrongOwnerThread,
            (void*)(uintptr_t)WrongOwner.Status
        );
    }
    Stress5BReadAndValidateMutex(&Mutex, CurrentThread, 0, false);

    STRESS5B_SIMPLE_CONTEXT Timeout = {
        .Mutex = &Mutex,
        .Status = MT_PENDING
    };
    PETHREAD TimeoutThread = Stress5BCreateRetainedThread(
        Stress5BTimeoutWorker,
        &Timeout
    );
    InterlockedStoreRelease(&Timeout.Start, true);
    Stress5BJoinThread(TimeoutThread, (void*)0x5B12);
    if (InterlockedLoadAcquire(&Timeout.Status) != MT_TIMEOUT) {
        Stress5BBugCheck(
            Stress5BTimeoutFailure,
            &Mutex,
            TimeoutThread,
            (void*)(uintptr_t)Timeout.Status
        );
    }
    Stress5BReadAndValidateMutex(&Mutex, CurrentThread, 0, false);

    Status = MsReleaseMutexObject(&Mutex);
    if (Status != MT_SUCCESS) {
        Stress5BBugCheck(
            Stress5BBasicStatusFailure,
            &Mutex,
            (void*)3,
            (void*)(uintptr_t)Status
        );
    }
    Stress5BReadAndValidateMutex(&Mutex, NULL, 1, false);

    gop_printf(
        COLOR_GREEN,
        "STRESS 5B basic recursion/wrong-owner/timeout PASS\n"
    );
}

static void
Stress5BRunContention(void)
{
    STRESS5B_SIMPLE_CONTEXT Contexts[STRESS5B_CONTENTION_THREADS] = { 0 };
    PETHREAD Threads[STRESS5B_CONTENTION_THREADS] = { 0 };

    MsInitializeMutexObject(&Stress5BContentionMutex);
    Stress5BProtectedCounter = 0;
    Stress5BInCriticalSection = 0;

    for (uint32_t Index = 0; Index < STRESS5B_CONTENTION_THREADS; Index++) {
        Contexts[Index].Index = Index;
        Contexts[Index].Status = MT_PENDING;
        Threads[Index] = Stress5BCreateRetainedThread(
            Stress5BContentionWorker,
            &Contexts[Index]
        );
    }
    for (uint32_t Index = 0; Index < STRESS5B_CONTENTION_THREADS; Index++) {
        InterlockedStoreRelease(&Contexts[Index].Start, true);
    }
    for (uint32_t Index = 0; Index < STRESS5B_CONTENTION_THREADS; Index++) {
        Stress5BJoinThread(
            Threads[Index],
            (void*)(uintptr_t)(0x5B20U + Index)
        );
    }

    uint64_t Expected =
        STRESS5B_CONTENTION_THREADS * STRESS5B_CONTENTION_ITERATIONS;
    if (InterlockedLoadAcquire(&Stress5BProtectedCounter) != Expected ||
        InterlockedLoadAcquire(&Stress5BInCriticalSection) != 0) {
        Stress5BBugCheck(
            Stress5BContentionCountFailure,
            (void*)(uintptr_t)Expected,
            (void*)(uintptr_t)Stress5BProtectedCounter,
            (void*)(uintptr_t)Stress5BInCriticalSection
        );
    }
    Stress5BReadAndValidateMutex(
        &Stress5BContentionMutex,
        NULL,
        1,
        false
    );

    gop_printf(
        COLOR_GREEN,
        "STRESS 5B contention PASS (%llu protected entries)\n",
        (unsigned long long)Expected
    );
}

static void
Stress5BRunEmptyOwnerExit(void)
{
    STRESS5B_OWNER_CONTEXT Context = { 0 };
    PETHREAD Thread = Stress5BCreateRetainedThread(
        Stress5BOwnerWorker,
        &Context
    );
    InterlockedStoreRelease(&Context.Start, true);
    Stress5BWaitForFlag(&Context.Ready, &Context);
    InterlockedStoreRelease(&Context.Exit, true);
    Stress5BJoinThread(Thread, (void*)0x5B30);

    gop_printf(COLOR_GREEN, "STRESS 5B zero-owned exit PASS\n");
}

static void
Stress5BRunManyOwnedExit(void)
{
    MUTEX Mutexes[STRESS5B_MANY_MUTEXES];
    for (uint32_t Index = 0; Index < STRESS5B_MANY_MUTEXES; Index++) {
        MsInitializeMutexObject(&Mutexes[Index]);
    }

    STRESS5B_OWNER_CONTEXT Context = {
        .Mutexes = Mutexes,
        .MutexCount = STRESS5B_MANY_MUTEXES
    };
    PETHREAD Thread = Stress5BCreateRetainedThread(
        Stress5BOwnerWorker,
        &Context
    );
    InterlockedStoreRelease(&Context.Start, true);
    Stress5BWaitForFlag(&Context.Ready, &Context);

    for (uint32_t Index = 0; Index < STRESS5B_MANY_MUTEXES; Index++) {
        Stress5BReadAndValidateMutex(&Mutexes[Index], Thread, 0, false);
    }

    InterlockedStoreRelease(&Context.Exit, true);
    Stress5BJoinThread(Thread, (void*)0x5B31);

    for (uint32_t Index = 0; Index < STRESS5B_MANY_MUTEXES; Index++) {
        Stress5BReadAndValidateMutex(&Mutexes[Index], NULL, 1, true);
        MTSTATUS Status = MsWaitForSingleObject(
            &Mutexes[Index],
            KernelMode,
            false,
            MT_INFINITE
        );
        if (Status != MT_MUTEX_ABANDONED) {
            Stress5BBugCheck(
                Stress5BAbandonStatusFailure,
                &Mutexes[Index],
                (void*)(uintptr_t)Index,
                (void*)(uintptr_t)Status
            );
        }
        Stress5BReadAndValidateMutex(
            &Mutexes[Index],
            PsGetCurrentThread(),
            0,
            false
        );
        Status = MsReleaseMutexObject(&Mutexes[Index]);
        if (Status != MT_SUCCESS) {
            Stress5BBugCheck(
                Stress5BAbandonStatusFailure,
                &Mutexes[Index],
                (void*)(uintptr_t)Index,
                (void*)(uintptr_t)Status
            );
        }
        Stress5BReadAndValidateMutex(&Mutexes[Index], NULL, 1, false);
    }

    gop_printf(
        COLOR_GREEN,
        "STRESS 5B many-owned exit PASS (%u mutexes)\n",
        STRESS5B_MANY_MUTEXES
    );
}

static void
Stress5BRunAbandonedWaiters(void)
{
    MUTEX Mutex;
    MsInitializeMutexObject(&Mutex);

    STRESS5B_OWNER_CONTEXT OwnerContext = {
        .Mutexes = &Mutex,
        .MutexCount = 1
    };
    PETHREAD OwnerThread = Stress5BCreateRetainedThread(
        Stress5BOwnerWorker,
        &OwnerContext
    );
    InterlockedStoreRelease(&OwnerContext.Start, true);
    Stress5BWaitForFlag(&OwnerContext.Ready, &OwnerContext);
    Stress5BReadAndValidateMutex(&Mutex, OwnerThread, 0, false);

    STRESS5B_SIMPLE_CONTEXT Contexts[STRESS5B_ABANDON_WAITERS] = { 0 };
    PETHREAD Threads[STRESS5B_ABANDON_WAITERS] = { 0 };
    Stress5BInCriticalSection = 0;

    for (uint32_t Index = 0; Index < STRESS5B_ABANDON_WAITERS; Index++) {
        Contexts[Index].Mutex = &Mutex;
        Contexts[Index].Index = Index;
        Contexts[Index].Status = MT_PENDING;
        Threads[Index] = Stress5BCreateRetainedThread(
            Stress5BAbandonWaiter,
            &Contexts[Index]
        );
    }
    for (uint32_t Index = 0; Index < STRESS5B_ABANDON_WAITERS; Index++) {
        InterlockedStoreRelease(&Contexts[Index].Start, true);
    }
    Stress5BWaitForRegistrations(
        &Mutex,
        Threads,
        STRESS5B_ABANDON_WAITERS
    );

    InterlockedStoreRelease(&OwnerContext.Exit, true);
    Stress5BJoinThread(OwnerThread, (void*)0x5B40);

    uint32_t AbandonedCount = 0;
    for (uint32_t Index = 0; Index < STRESS5B_ABANDON_WAITERS; Index++) {
        Stress5BJoinThread(
            Threads[Index],
            (void*)(uintptr_t)(0x5B41U + Index)
        );
        MTSTATUS Status = InterlockedLoadAcquire(&Contexts[Index].Status);
        if (Status == MT_MUTEX_ABANDONED) {
            AbandonedCount++;
        }
        else if (Status != MT_SUCCESS) {
            Stress5BBugCheck(
                Stress5BAbandonStatusFailure,
                &Mutex,
                (void*)(uintptr_t)Index,
                (void*)(uintptr_t)Status
            );
        }
    }

    if (AbandonedCount != 1) {
        Stress5BBugCheck(
            Stress5BAbandonCountFailure,
            &Mutex,
            (void*)(uintptr_t)AbandonedCount,
            (void*)(uintptr_t)STRESS5B_ABANDON_WAITERS
        );
    }
    Stress5BReadAndValidateMutex(&Mutex, NULL, 1, false);

    gop_printf(
        COLOR_GREEN,
        "STRESS 5B abandoned handoff PASS (%u waiters, one abandoned)\n",
        STRESS5B_ABANDON_WAITERS
    );
}

static void
Stress5BRunReleaseTerminationRaces(void)
{
    uint32_t AbandonedWins = 0;
    uint32_t ReleaseWins = 0;

    for (uint32_t Round = 0; Round < STRESS5B_RACE_ROUNDS; Round++) {
        MUTEX Mutex;
        MsInitializeMutexObject(&Mutex);

        STRESS5B_RACE_CONTEXT Context = { .Mutex = &Mutex };
        PETHREAD Thread = Stress5BCreateRetainedThread(
            Stress5BRaceOwner,
            &Context
        );
        InterlockedStoreRelease(&Context.Start, true);
        Stress5BWaitForFlag(&Context.Ready, &Context);
        Stress5BReadAndValidateMutex(&Mutex, Thread, 0, false);

        InterlockedStoreRelease(&Context.Race, true);
        if (Round & 1U) {
            MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
        }
        InterlockedStoreRelease(&Context.ExitWithoutRelease, true);

        Stress5BJoinThread(
            Thread,
            (void*)(uintptr_t)(0x5B80U + Round)
        );

        bool Abandoned;
        IRQL OldIrql;
        MsAcquireSpinlock(&Mutex.Header.Lock, &OldIrql);
        Abandoned = Mutex.Abandoned;
        bool FreeState = Mutex.OwnerThread == NULL &&
            Mutex.Header.SignalState == 1 &&
            IsListEmpty(&Mutex.OwnerListEntry);
        MsReleaseSpinlock(&Mutex.Header.Lock, OldIrql);
        if (!FreeState) {
            Stress5BBugCheck(
                Stress5BRaceStateFailure,
                (void*)(uintptr_t)Round,
                Mutex.OwnerThread,
                (void*)(uintptr_t)(uint32_t)Mutex.Header.SignalState
            );
        }

        MTSTATUS Status = MsWaitForSingleObject(
            &Mutex,
            KernelMode,
            false,
            MT_INFINITE
        );
        MTSTATUS ExpectedStatus = Abandoned
            ? MT_MUTEX_ABANDONED
            : MT_SUCCESS;
        if (Status != ExpectedStatus) {
            Stress5BBugCheck(
                Stress5BRaceStateFailure,
                (void*)(uintptr_t)Round,
                (void*)(uintptr_t)ExpectedStatus,
                (void*)(uintptr_t)Status
            );
        }
        if (Abandoned) {
            AbandonedWins++;
        }
        else {
            ReleaseWins++;
        }

        Status = MsReleaseMutexObject(&Mutex);
        if (Status != MT_SUCCESS) {
            Stress5BBugCheck(
                Stress5BRaceStateFailure,
                (void*)(uintptr_t)Round,
                &Mutex,
                (void*)(uintptr_t)Status
            );
        }
        Stress5BReadAndValidateMutex(&Mutex, NULL, 1, false);
    }

    gop_printf(
        COLOR_GREEN,
        "STRESS 5B release/termination race PASS (%u abandoned, %u released)\n",
        AbandonedWins,
        ReleaseWins
    );
}

static void
Stress5BController(void)
{
    gop_printf(
        COLOR_GREEN,
        "STRESS 5B START (mutex ownership/abandonment, %u CPUs)\n",
        MeGetActiveProcessorCount()
    );

    Stress5BRunBasicSemantics();
    Stress5BRunContention();
    Stress5BRunEmptyOwnerExit();
    Stress5BRunManyOwnedExit();
    Stress5BRunAbandonedWaiters();
    Stress5BRunReleaseTerminationRaces();

    gop_printf(COLOR_GREEN, "STRESS 5B PASS\n");
}

typedef enum _STRESS5C_FAILURE {
    Stress5CUnexpectedStatus = 1,
    Stress5CUnexpectedState,
    Stress5CAccessCheckFailure,
    Stress5CClosedHandleFailure,
    Stress5CWaitRegistrationFailure,
    Stress5CObjectCountLeak,
    Stress5CHandleCountLeak,
    Stress5CWorkerFailure
} STRESS5C_FAILURE;

typedef struct _STRESS5C_WAIT_CONTEXT {
    volatile bool Start;
    HANDLE Handle;
    volatile MTSTATUS Status;
} STRESS5C_WAIT_CONTEXT;

typedef struct _STRESS5C_OWNER_CONTEXT {
    volatile bool Start;
    volatile MTSTATUS Status;
} STRESS5C_OWNER_CONTEXT;

NORETURN
static void
Stress5CBugCheck(
    STRESS5C_FAILURE Failure,
    void* Detail1,
    void* Detail2
)
{
    MeBugCheckEx(
        WAIT_STATE_FAILURE,
        (void*)(uintptr_t)0x5C,
        (void*)(uintptr_t)Failure,
        Detail1,
        Detail2
    );
}

static void
Stress5CRequireStatus(
    MTSTATUS Actual,
    MTSTATUS Expected,
    void* Detail
)
{
    if (Actual != Expected) {
        Stress5CBugCheck(
            Stress5CUnexpectedStatus,
            Detail,
            (void*)(uintptr_t)Actual
        );
    }
}

static void
Stress5CWaitForTypeCounts(
    POBJECT_TYPE Type,
    uintptr_t TypeTag,
    uint32_t ExpectedObjects,
    uint32_t ExpectedHandles
)
{
    uint64_t StartTsc = __rdtsc();
    for (;;) {
        uint32_t Objects = InterlockedLoadAcquire(
            (volatile uint32_t*)&Type->TotalNumberOfObjects
        );
        uint32_t Handles = InterlockedLoadAcquire(
            (volatile uint32_t*)&Type->TotalNumberOfHandles
        );
        if (Objects == ExpectedObjects && Handles == ExpectedHandles) {
            return;
        }

        if (Stress5BWatchdogExpired(StartTsc)) {
            Stress5CBugCheck(
                Handles == ExpectedHandles
                    ? Stress5CObjectCountLeak
                    : Stress5CHandleCountLeak,
                (void*)TypeTag,
                (void*)(uintptr_t)(
                    ((uint64_t)(ExpectedObjects & 0xFF) << 56) |
                    ((uint64_t)(ExpectedHandles & 0xFF) << 48) |
                    ((uint64_t)Objects << 32) |
                    Handles
                )
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress5CWaitForDispatcherRegistration(
    PDISPATCHER_HEADER Header
)
{
    uint64_t StartTsc = __rdtsc();
    for (;;) {
        IRQL OldIrql;
        MsAcquireSpinlock(&Header->Lock, &OldIrql);
        bool Registered = !IsListEmpty(&Header->WaitListHead);
        MsReleaseSpinlock(&Header->Lock, OldIrql);
        if (Registered) return;

        if (Stress5BWatchdogExpired(StartTsc)) {
            Stress5CBugCheck(
                Stress5CWaitRegistrationFailure,
                Header,
                NULL
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress5CHandleWaiter(
    THREAD_PARAMETER Parameter
)
{
    STRESS5C_WAIT_CONTEXT* Context = Parameter;
    Stress5BWaitForFlag(&Context->Start, Context);
    MTSTATUS Status = MtWaitForSingleObject(
        Context->Handle,
        MT_INFINITE,
        false
    );
    InterlockedStoreRelease(&Context->Status, Status);
}

static void
Stress5CCloseOwnedMutexWorker(
    THREAD_PARAMETER Parameter
)
{
    STRESS5C_OWNER_CONTEXT* Context = Parameter;
    Stress5BWaitForFlag(&Context->Start, Context);

    HANDLE Handle = MT_INVALID_HANDLE;
    MTSTATUS Status = MtCreateMutex(
        &Handle,
        MT_MUTEX_ALL_ACCESS,
        true,
        NULL
    );
    if (Status == MT_SUCCESS) {
        Status = MtClose(Handle);
    }
    InterlockedStoreRelease(&Context->Status, Status);
}

static void
Stress5CTestEvents(void)
{
    HANDLE Handle = MT_INVALID_HANDLE;
    MTSTATUS Status = MtCreateEvent(
        &Handle,
        MT_EVENT_ALL_ACCESS,
        SynchronizationEvent,
        false,
        NULL
    );
    Stress5CRequireStatus(Status, MT_SUCCESS, (void*)0x5C10);

    bool State = true;
    Stress5CRequireStatus(MtQueryEvent(Handle, &State), MT_SUCCESS, (void*)0x5C11);
    if (State) Stress5CBugCheck(Stress5CUnexpectedState, (void*)0x5C11, NULL);
    Stress5CRequireStatus(MtWaitForSingleObject(Handle, 0, false), MT_TIMEOUT, (void*)0x5C12);

    bool PreviousState = true;
    Stress5CRequireStatus(MtSetEvent(Handle, &PreviousState), MT_SUCCESS, (void*)0x5C13);
    if (PreviousState) Stress5CBugCheck(Stress5CUnexpectedState, (void*)0x5C13, NULL);
    Stress5CRequireStatus(MtSetEvent(Handle, &PreviousState), MT_SUCCESS, (void*)0x5C14);
    if (!PreviousState) Stress5CBugCheck(Stress5CUnexpectedState, (void*)0x5C14, NULL);
    Stress5CRequireStatus(MtWaitForSingleObject(Handle, 0, false), MT_SUCCESS, (void*)0x5C15);
    Stress5CRequireStatus(MtQueryEvent(Handle, &State), MT_SUCCESS, (void*)0x5C16);
    if (State) Stress5CBugCheck(Stress5CUnexpectedState, (void*)0x5C16, NULL);
    Stress5CRequireStatus(MtResetEvent(Handle, &PreviousState), MT_SUCCESS, (void*)0x5C17);
    if (PreviousState) Stress5CBugCheck(Stress5CUnexpectedState, (void*)0x5C17, NULL);
    Stress5CRequireStatus(MtClose(Handle), MT_SUCCESS, (void*)0x5C18);
    Stress5CRequireStatus(MtClose(Handle), MT_INVALID_HANDLE, (void*)0x5C19);

    Handle = MT_INVALID_HANDLE;
    Stress5CRequireStatus(
        MtCreateEvent(&Handle, MT_SYNCHRONIZE, NotificationEvent, false, NULL),
        MT_SUCCESS,
        (void*)0x5C1A
    );
    Stress5CRequireStatus(MtSetEvent(Handle, NULL), MT_ACCESS_DENIED, (void*)0x5C1B);
    Stress5CRequireStatus(MtQueryEvent(Handle, &State), MT_ACCESS_DENIED, (void*)0x5C1C);
    Stress5CRequireStatus(MtClose(Handle), MT_SUCCESS, (void*)0x5C1D);
}

static void
Stress5CTestSemaphores(void)
{
    HANDLE Handle = MT_INVALID_HANDLE;
    Stress5CRequireStatus(
        MtCreateSemaphore(&Handle, MT_SEMAPHORE_ALL_ACCESS, -1, 1, NULL),
        MT_INVALID_PARAM,
        (void*)0x5C20
    );
    Stress5CRequireStatus(
        MtCreateSemaphore(&Handle, MT_SEMAPHORE_ALL_ACCESS, 0, 0, NULL),
        MT_INVALID_PARAM,
        (void*)0x5C21
    );
    Stress5CRequireStatus(
        MtCreateSemaphore(&Handle, MT_SEMAPHORE_ALL_ACCESS, 1, 2, NULL),
        MT_SUCCESS,
        (void*)0x5C22
    );

    SEMAPHORE_BASIC_INFORMATION Information;
    Stress5CRequireStatus(MtQuerySemaphore(Handle, &Information), MT_SUCCESS, (void*)0x5C23);
    if (Information.CurrentCount != 1 || Information.MaximumCount != 2) {
        Stress5CBugCheck(Stress5CUnexpectedState, (void*)0x5C23, (void*)(uintptr_t)Information.CurrentCount);
    }

    Stress5CRequireStatus(MtWaitForSingleObject(Handle, 0, false), MT_SUCCESS, (void*)0x5C24);
    int32_t PreviousCount = -1;
    Stress5CRequireStatus(MtReleaseSemaphore(Handle, 2, &PreviousCount), MT_SUCCESS, (void*)0x5C25);
    if (PreviousCount != 0) Stress5CBugCheck(Stress5CUnexpectedState, (void*)0x5C25, (void*)(uintptr_t)PreviousCount);
    Stress5CRequireStatus(
        MtReleaseSemaphore(Handle, 1, NULL),
        MT_SEMAPHORE_LIMIT_EXCEEDED,
        (void*)0x5C26
    );
    Stress5CRequireStatus(MtWaitForSingleObject(Handle, 0, false), MT_SUCCESS, (void*)0x5C27);
    Stress5CRequireStatus(MtWaitForSingleObject(Handle, 0, false), MT_SUCCESS, (void*)0x5C28);
    Stress5CRequireStatus(MtWaitForSingleObject(Handle, 0, false), MT_TIMEOUT, (void*)0x5C29);
    Stress5CRequireStatus(MtClose(Handle), MT_SUCCESS, (void*)0x5C2A);

    Stress5CRequireStatus(
        MtCreateSemaphore(&Handle, MT_SYNCHRONIZE, 0, 1, NULL),
        MT_SUCCESS,
        (void*)0x5C2B
    );
    Stress5CRequireStatus(MtReleaseSemaphore(Handle, 1, NULL), MT_ACCESS_DENIED, (void*)0x5C2C);
    Stress5CRequireStatus(MtClose(Handle), MT_SUCCESS, (void*)0x5C2D);
}

static void
Stress5CTestMutexes(void)
{
    HANDLE Handle = MT_INVALID_HANDLE;
    Stress5CRequireStatus(
        MtCreateMutex(&Handle, MT_MUTEX_ALL_ACCESS, false, NULL),
        MT_SUCCESS,
        (void*)0x5C30
    );

    MUTEX_BASIC_INFORMATION Information;
    Stress5CRequireStatus(MtQueryMutex(Handle, &Information), MT_SUCCESS, (void*)0x5C31);
    if (Information.SignalState != 1 || Information.OwnedByCaller) {
        Stress5CBugCheck(Stress5CUnexpectedState, (void*)0x5C31, (void*)(uintptr_t)Information.SignalState);
    }

    Stress5CRequireStatus(MtWaitForSingleObject(Handle, 0, false), MT_SUCCESS, (void*)0x5C32);
    Stress5CRequireStatus(MtWaitForSingleObject(Handle, 0, false), MT_SUCCESS, (void*)0x5C33);
    Stress5CRequireStatus(MtQueryMutex(Handle, &Information), MT_SUCCESS, (void*)0x5C34);
    if (Information.SignalState != -1 || !Information.OwnedByCaller) {
        Stress5CBugCheck(Stress5CUnexpectedState, (void*)0x5C34, (void*)(uintptr_t)(uint32_t)Information.SignalState);
    }

    int32_t PreviousCount = 0;
    Stress5CRequireStatus(MtReleaseMutex(Handle, &PreviousCount), MT_SUCCESS, (void*)0x5C35);
    if (PreviousCount != -1) Stress5CBugCheck(Stress5CUnexpectedState, (void*)0x5C35, (void*)(uintptr_t)(uint32_t)PreviousCount);
    Stress5CRequireStatus(MtReleaseMutex(Handle, &PreviousCount), MT_SUCCESS, (void*)0x5C36);
    if (PreviousCount != 0) Stress5CBugCheck(Stress5CUnexpectedState, (void*)0x5C36, (void*)(uintptr_t)PreviousCount);
    Stress5CRequireStatus(MtReleaseMutex(Handle, NULL), MT_MUTEX_NOT_OWNED, (void*)0x5C37);
    Stress5CRequireStatus(MtClose(Handle), MT_SUCCESS, (void*)0x5C38);

    Stress5CRequireStatus(
        MtCreateMutex(&Handle, MT_MUTEX_QUERY_STATE, false, NULL),
        MT_SUCCESS,
        (void*)0x5C39
    );
    Stress5CRequireStatus(MtWaitForSingleObject(Handle, 0, false), MT_ACCESS_DENIED, (void*)0x5C3A);
    Stress5CRequireStatus(MtReleaseMutex(Handle, NULL), MT_ACCESS_DENIED, (void*)0x5C3B);
    Stress5CRequireStatus(MtClose(Handle), MT_SUCCESS, (void*)0x5C3C);
}

static void
Stress5CTestCloseWhileWaiting(void)
{
    HANDLE Handle = MT_INVALID_HANDLE;
    Stress5CRequireStatus(
        MtCreateEvent(&Handle, MT_EVENT_ALL_ACCESS, SynchronizationEvent, false, NULL),
        MT_SUCCESS,
        (void*)0x5C40
    );

    void* Object = NULL;
    Stress5CRequireStatus(
        ObReferenceObjectByHandle(Handle, MT_SYNCHRONIZE, MsEventType, &Object, NULL),
        MT_SUCCESS,
        (void*)0x5C41
    );

    STRESS5C_WAIT_CONTEXT Context = {
        .Handle = Handle,
        .Status = MT_PENDING
    };
    PETHREAD Thread = Stress5BCreateRetainedThread(Stress5CHandleWaiter, &Context);
    InterlockedStoreRelease(&Context.Start, true);
    Stress5CWaitForDispatcherRegistration((PDISPATCHER_HEADER)Object);

    Stress5CRequireStatus(MtClose(Handle), MT_SUCCESS, (void*)0x5C42);
    bool ClosedState = false;
    Stress5CRequireStatus(MtQueryEvent(Handle, &ClosedState), MT_INVALID_HANDLE, (void*)0x5C43);
    Stress5CRequireStatus(MsSetEvent((PEVENT)Object), MT_SUCCESS, (void*)0x5C44);
    Stress5BJoinThread(Thread, (void*)0x5C45);
    Stress5CRequireStatus(InterlockedLoadAcquire(&Context.Status), MT_SUCCESS, (void*)0x5C46);
    ObDereferenceObject(Object);
}

static void
Stress5CTestCloseWhileOwning(void)
{
    STRESS5C_OWNER_CONTEXT Context = { .Status = MT_PENDING };
    PETHREAD Thread = Stress5BCreateRetainedThread(
        Stress5CCloseOwnedMutexWorker,
        &Context
    );
    InterlockedStoreRelease(&Context.Start, true);
    Stress5BJoinThread(Thread, (void*)0x5C50);
    Stress5CRequireStatus(
        InterlockedLoadAcquire(&Context.Status),
        MT_SUCCESS,
        (void*)0x5C51
    );
}

static void
Stress5CController(void)
{
    uint32_t EventObjects = InterlockedLoadAcquire(
        (volatile uint32_t*)&MsEventType->TotalNumberOfObjects
    );
    uint32_t EventHandles = InterlockedLoadAcquire(
        (volatile uint32_t*)&MsEventType->TotalNumberOfHandles
    );
    uint32_t MutexObjects = InterlockedLoadAcquire(
        (volatile uint32_t*)&MsMutexType->TotalNumberOfObjects
    );
    uint32_t MutexHandles = InterlockedLoadAcquire(
        (volatile uint32_t*)&MsMutexType->TotalNumberOfHandles
    );
    uint32_t SemaphoreObjects = InterlockedLoadAcquire(
        (volatile uint32_t*)&MsSemaphoreType->TotalNumberOfObjects
    );
    uint32_t SemaphoreHandles = InterlockedLoadAcquire(
        (volatile uint32_t*)&MsSemaphoreType->TotalNumberOfHandles
    );

    gop_printf(
        COLOR_GREEN,
        "STRESS 5C START (user handles/lifetime, %u CPUs)\n",
        MeGetActiveProcessorCount()
    );

    Stress5CTestEvents();
    Stress5CTestSemaphores();
    Stress5CTestMutexes();
    Stress5CTestCloseWhileWaiting();
    Stress5CTestCloseWhileOwning();

    Stress5CWaitForTypeCounts(MsEventType, 1, EventObjects, EventHandles);
    Stress5CWaitForTypeCounts(MsMutexType, 2, MutexObjects, MutexHandles);
    Stress5CWaitForTypeCounts(
        MsSemaphoreType,
        3,
        SemaphoreObjects,
        SemaphoreHandles
    );

    gop_printf(COLOR_GREEN, "STRESS 5C PASS\n");
}

#define STRESS5D_WAITER_COUNT       4U
#define STRESS5D_RACE_ROUNDS        32U
#define STRESS5D_WAIT_TIMEOUT_MS    30000ULL
#define STRESS5D_WATCHDOG_SECONDS   30ULL
#define STRESS5D_EXIT_STATUS        MT_GENERAL_FAILURE

typedef enum _STRESS5D_FAILURE {
    Stress5DUnexpectedStatus = 1,
    Stress5DSelfWaitFailure,
    Stress5DThreadStateFailure,
    Stress5DProcessStateFailure,
    Stress5DWaitRegistrationFailure,
    Stress5DProtocolTimeout,
    Stress5DThreadReferenceFailure,
    Stress5DObjectCountLeak,
    Stress5DHandleCountLeak
} STRESS5D_FAILURE;

typedef struct _STRESS5D_TARGET_CONTEXT {
    volatile bool Start;
    volatile bool Ready;
    volatile bool Stop;
    MTSTATUS ExitStatus;
} STRESS5D_TARGET_CONTEXT;

typedef struct _STRESS5D_WAIT_CONTEXT {
    volatile bool Start;
    HANDLE Handle;
    volatile MTSTATUS Status;
} STRESS5D_WAIT_CONTEXT;

NORETURN
static void
Stress5DBugCheck(
    STRESS5D_FAILURE Failure,
    void* Detail1,
    void* Detail2
)
{
    // P1 identifies Stress 5D, P2 is the exact failed invariant, and P3/P4
    // contain the phase-specific status, object, count, or round number.
    MeBugCheckEx(
        WAIT_STATE_FAILURE,
        (void*)(uintptr_t)0x5D,
        (void*)(uintptr_t)Failure,
        Detail1,
        Detail2
    );
}

static bool
Stress5DWatchdogExpired(
    uint64_t StartTsc
)
{
    uint64_t Now = __rdtsc();
    return Now >= StartTsc &&
        Now - StartTsc >
            STRESS5D_WATCHDOG_SECONDS * Stress2CTscTicksPerSecond;
}

static void
Stress5DRequireStatus(
    MTSTATUS Actual,
    MTSTATUS Expected,
    void* Detail
)
{
    if (Actual != Expected) {
        Stress5DBugCheck(
            Stress5DUnexpectedStatus,
            Detail,
            (void*)(uintptr_t)Actual
        );
    }
}

static void
Stress5DWaitForFlag(
    volatile bool* Flag,
    void* Detail
)
{
    uint64_t StartTsc = __rdtsc();
    while (!InterlockedLoadAcquire(Flag)) {
        if (Stress5DWatchdogExpired(StartTsc)) {
            Stress5DBugCheck(
                Stress5DProtocolTimeout,
                Detail,
                (void*)Flag
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static PETHREAD
Stress5DCreateRetainedThread(
    ThreadEntry Entry,
    THREAD_PARAMETER Parameter
)
{
    // Stress 5D workers begin behind their Start flag, so this reference is
    // acquired before the worker can return and enter its exit path.
    PETHREAD Thread = StressSuiteCreateThread(Entry, Parameter);
    if (!ObReferenceObject(Thread)) {
        Stress5DBugCheck(
            Stress5DThreadReferenceFailure,
            Thread,
            (void*)Entry
        );
    }
    return Thread;
}

static void
Stress5DJoinThread(
    PETHREAD Thread,
    void* Detail
)
{
    MTSTATUS Status = MsWaitForSingleObject(
        &Thread->InternalThread.Header,
        KernelMode,
        false,
        STRESS5D_WAIT_TIMEOUT_MS
    );
    if (Status != MT_SUCCESS) {
        Stress5DBugCheck(
            Stress5DUnexpectedStatus,
            Detail,
            (void*)(uintptr_t)Status
        );
    }
    ObDereferenceObject(Thread);
}

static void
Stress5DWaitForRegistrations(
    PDISPATCHER_HEADER Header,
    uint32_t ExpectedCount,
    void* Detail
)
{
    uint64_t StartTsc = __rdtsc();
    for (;;) {
        uint32_t Count = 0;
        IRQL OldIrql;
        MsAcquireSpinlock(&Header->Lock, &OldIrql);
        PDOUBLY_LINKED_LIST Head = &Header->WaitListHead;
        for (PDOUBLY_LINKED_LIST Entry = Head->Flink;
             Entry != Head && Count <= ExpectedCount;
             Entry = Entry->Flink) {
            Count++;
        }
        MsReleaseSpinlock(&Header->Lock, OldIrql);

        if (Count == ExpectedCount) return;
        if (Count > ExpectedCount || Stress5DWatchdogExpired(StartTsc)) {
            Stress5DBugCheck(
                Stress5DWaitRegistrationFailure,
                Detail,
                (void*)(uintptr_t)Count
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress5DWaitForTypeCounts(
    POBJECT_TYPE Type,
    uint32_t ExpectedObjects,
    uint32_t ExpectedHandles
)
{
    uint64_t StartTsc = __rdtsc();
    for (;;) {
        uint32_t Objects = InterlockedLoadAcquire(
            (volatile uint32_t*)&Type->TotalNumberOfObjects
        );
        uint32_t Handles = InterlockedLoadAcquire(
            (volatile uint32_t*)&Type->TotalNumberOfHandles
        );
        if (Objects == ExpectedObjects && Handles == ExpectedHandles) return;

        if (Stress5DWatchdogExpired(StartTsc)) {
            bool ObjectMismatch = Objects != ExpectedObjects;
            Stress5DBugCheck(
                ObjectMismatch
                    ? Stress5DObjectCountLeak
                    : Stress5DHandleCountLeak,
                Type,
                (void*)(uintptr_t)(
                    ((uint64_t)(ObjectMismatch ? ExpectedObjects : ExpectedHandles) << 32) |
                    (ObjectMismatch ? Objects : Handles)
                )
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress5DSettleObjectCounts(void)
{
    uint32_t LastThreadObjects = UINT32_MAX;
    uint32_t LastThreadHandles = UINT32_MAX;
    uint32_t LastProcessObjects = UINT32_MAX;
    uint32_t LastProcessHandles = UINT32_MAX;
    uint64_t StartTsc = __rdtsc();
    uint64_t StableTsc = StartTsc;

    for (;;) {
        uint32_t ThreadObjects = InterlockedLoadAcquire(
            (volatile uint32_t*)&PsThreadType->TotalNumberOfObjects
        );
        uint32_t ThreadHandles = InterlockedLoadAcquire(
            (volatile uint32_t*)&PsThreadType->TotalNumberOfHandles
        );
        uint32_t ProcessObjects = InterlockedLoadAcquire(
            (volatile uint32_t*)&PsProcessType->TotalNumberOfObjects
        );
        uint32_t ProcessHandles = InterlockedLoadAcquire(
            (volatile uint32_t*)&PsProcessType->TotalNumberOfHandles
        );

        if (ThreadObjects != LastThreadObjects ||
            ThreadHandles != LastThreadHandles ||
            ProcessObjects != LastProcessObjects ||
            ProcessHandles != LastProcessHandles) {
            LastThreadObjects = ThreadObjects;
            LastThreadHandles = ThreadHandles;
            LastProcessObjects = ProcessObjects;
            LastProcessHandles = ProcessHandles;
            StableTsc = __rdtsc();
        }
        else if (__rdtsc() - StableTsc > Stress2CTscTicksPerSecond / 10) {
            return;
        }

        if (Stress5DWatchdogExpired(StartTsc)) {
            Stress5DBugCheck(
                Stress5DProtocolTimeout,
                PsThreadType,
                PsProcessType
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress5DTargetWorker(
    THREAD_PARAMETER Parameter
)
{
    STRESS5D_TARGET_CONTEXT* Context = Parameter;
    Stress5DWaitForFlag(&Context->Start, Context);
    InterlockedStoreRelease(&Context->Ready, true);

    while (!InterlockedLoadAcquire(&Context->Stop)) {
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }

    PspExitThread(Context->ExitStatus);
}

static void
Stress5DHandleWaiter(
    THREAD_PARAMETER Parameter
)
{
    STRESS5D_WAIT_CONTEXT* Context = Parameter;
    Stress5DWaitForFlag(&Context->Start, Context);
    MTSTATUS Status = MtWaitForSingleObject(
        Context->Handle,
        STRESS5D_WAIT_TIMEOUT_MS,
        false
    );
    InterlockedStoreRelease(&Context->Status, Status);
}

static void
Stress5DTestSelfWaits(void)
{
    Stress5DRequireStatus(
        MtWaitForSingleObject(MtCurrentThread(), 0, false),
        MT_INVALID_PARAM,
        (void*)0x5D10
    );

    // An ordinary handle to the current thread must follow the same policy as
    // its pseudo-handle; this proves the check happens after handle resolution.
    HANDLE Handle = MT_INVALID_HANDLE;
    Stress5DRequireStatus(
        ObCreateHandleForObject(PsGetCurrentThread(), MT_SYNCHRONIZE, &Handle),
        MT_SUCCESS,
        (void*)0x5D12
    );
    Stress5DRequireStatus(
        MtWaitForSingleObject(Handle, 0, false),
        MT_INVALID_PARAM,
        (void*)0x5D13
    );
    Stress5DRequireStatus(MtClose(Handle), MT_SUCCESS, (void*)0x5D14);

    gop_printf(COLOR_GREEN, "STRESS 5D self-wait policy PASS\n");
}

static void
Stress5DTestAttachedProcessSelfWaits(
    PEPROCESS Process
)
{
    // PsInitialSystemProcess is static rather than object-manager allocated, so
    // its pseudo-handle cannot be referenced. Attach to the real test process
    // to exercise current-process pseudo and ordinary handles legitimately.
    APC_STATE ApcState;
    MeAttachProcess(&Process->InternalProcess, &ApcState);

    Stress5DRequireStatus(
        MtWaitForSingleObject(MtCurrentProcess(), 0, false),
        MT_INVALID_PARAM,
        (void*)0x5D11
    );

    HANDLE Handle = MT_INVALID_HANDLE;
    Stress5DRequireStatus(
        ObCreateHandleForObject(PsGetCurrentProcess(), MT_SYNCHRONIZE, &Handle),
        MT_SUCCESS,
        (void*)0x5D15
    );
    Stress5DRequireStatus(
        MtWaitForSingleObject(Handle, 0, false),
        MT_INVALID_PARAM,
        (void*)0x5D16
    );
    Stress5DRequireStatus(MtClose(Handle), MT_SUCCESS, (void*)0x5D17);

    MeDetachProcess(&ApcState);
}

static void
Stress5DTestThreadLifetime(void)
{
    STRESS5D_TARGET_CONTEXT TargetContext = {
        .ExitStatus = STRESS5D_EXIT_STATUS
    };
    PETHREAD Target = Stress5DCreateRetainedThread(
        Stress5DTargetWorker,
        &TargetContext
    );

    HANDLE PrimaryHandle = MT_INVALID_HANDLE;
    HANDLE ObserverHandle = MT_INVALID_HANDLE;
    Stress5DRequireStatus(
        ObCreateHandleForObject(Target, MT_SYNCHRONIZE, &PrimaryHandle),
        MT_SUCCESS,
        (void*)0x5D20
    );
    Stress5DRequireStatus(
        ObCreateHandleForObject(Target, MT_SYNCHRONIZE, &ObserverHandle),
        MT_SUCCESS,
        (void*)0x5D21
    );

    InterlockedStoreRelease(&TargetContext.Start, true);
    Stress5DWaitForFlag(&TargetContext.Ready, &TargetContext);
    Stress5DRequireStatus(
        MtWaitForSingleObject(PrimaryHandle, 0, false),
        MT_TIMEOUT,
        (void*)0x5D22
    );

    STRESS5D_WAIT_CONTEXT Contexts[STRESS5D_WAITER_COUNT] = { 0 };
    PETHREAD Waiters[STRESS5D_WAITER_COUNT] = { 0 };
    for (uint32_t Index = 0; Index < STRESS5D_WAITER_COUNT; Index++) {
        Contexts[Index].Handle = PrimaryHandle;
        Contexts[Index].Status = MT_PENDING;
        Waiters[Index] = Stress5DCreateRetainedThread(
            Stress5DHandleWaiter,
            &Contexts[Index]
        );
        InterlockedStoreRelease(&Contexts[Index].Start, true);
    }
    Stress5DWaitForRegistrations(
        &Target->InternalThread.Header,
        STRESS5D_WAITER_COUNT,
        (void*)0x5D23
    );

    // Every waiter now owns an object reference inside MtWaitForSingleObject,
    // so closing the creator's handle must not invalidate their wait blocks.
    Stress5DRequireStatus(MtClose(PrimaryHandle), MT_SUCCESS, (void*)0x5D24);
    Stress5DRequireStatus(
        MtWaitForSingleObject(PrimaryHandle, 0, false),
        MT_INVALID_HANDLE,
        (void*)0x5D25
    );

    InterlockedStoreRelease(&TargetContext.Stop, true);
    Stress5DRequireStatus(
        MtWaitForSingleObject(
            ObserverHandle,
            STRESS5D_WAIT_TIMEOUT_MS,
            false
        ),
        MT_SUCCESS,
        (void*)0x5D27
    );

    for (uint32_t Index = 0; Index < STRESS5D_WAITER_COUNT; Index++) {
        Stress5DJoinThread(
            Waiters[Index],
            (void*)(uintptr_t)(0x5D28U + Index)
        );
        Stress5DRequireStatus(
            InterlockedLoadAcquire(&Contexts[Index].Status),
            MT_SUCCESS,
            (void*)(uintptr_t)(0x5D2CU + Index)
        );
    }

    IRQL OldIrql;
    MsAcquireSpinlock(&Target->InternalThread.Header.Lock, &OldIrql);
    bool ValidTerminalState =
        Target->InternalThread.Header.SignalState == 1 &&
        Target->ExitStatus == STRESS5D_EXIT_STATUS;
    MsReleaseSpinlock(&Target->InternalThread.Header.Lock, OldIrql);
    if (!ValidTerminalState) {
        Stress5DBugCheck(
            Stress5DThreadStateFailure,
            Target,
            (void*)(uintptr_t)Target->ExitStatus
        );
    }

    // Thread termination is a persistent notification: every later wait must
    // succeed without consuming the signal.
    Stress5DRequireStatus(
        MtWaitForSingleObject(ObserverHandle, 0, false),
        MT_SUCCESS,
        (void*)0x5D30
    );
    Stress5DRequireStatus(
        MtWaitForSingleObject(ObserverHandle, 0, false),
        MT_SUCCESS,
        (void*)0x5D31
    );
    Stress5DRequireStatus(MtClose(ObserverHandle), MT_SUCCESS, (void*)0x5D32);
    Stress5DJoinThread(Target, (void*)0x5D33);

    gop_printf(
        COLOR_GREEN,
        "STRESS 5D thread lifetime PASS (%u waiters)\n",
        STRESS5D_WAITER_COUNT
    );
}

static void
Stress5DTestThreadExitRaces(void)
{
    for (uint32_t Round = 0; Round < STRESS5D_RACE_ROUNDS; Round++) {
        STRESS5D_TARGET_CONTEXT TargetContext = {
            .ExitStatus = STRESS5D_EXIT_STATUS
        };
        PETHREAD Target = Stress5DCreateRetainedThread(
            Stress5DTargetWorker,
            &TargetContext
        );
        HANDLE Handle = MT_INVALID_HANDLE;
        Stress5DRequireStatus(
            ObCreateHandleForObject(Target, MT_SYNCHRONIZE, &Handle),
            MT_SUCCESS,
            (void*)(uintptr_t)Round
        );

        InterlockedStoreRelease(&TargetContext.Start, true);
        Stress5DWaitForFlag(&TargetContext.Ready, &TargetContext);

        STRESS5D_WAIT_CONTEXT WaitContext = {
            .Handle = Handle,
            .Status = MT_PENDING
        };
        PETHREAD Waiter = Stress5DCreateRetainedThread(
            Stress5DHandleWaiter,
            &WaitContext
        );
        InterlockedStoreRelease(&WaitContext.Start, true);

        // Odd rounds let the waiter register first. Even rounds request exit
        // immediately. SMP runs additionally exercise genuine concurrent races.
        if (Round & 1U) {
            MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
        }
        InterlockedStoreRelease(&TargetContext.Stop, true);

        Stress5DJoinThread(
            Waiter,
            (void*)(uintptr_t)(0x5DA0U + Round)
        );
        Stress5DRequireStatus(
            InterlockedLoadAcquire(&WaitContext.Status),
            MT_SUCCESS,
            (void*)(uintptr_t)(0x5DC0U + Round)
        );
        Stress5DRequireStatus(
            MtWaitForSingleObject(Handle, 0, false),
            MT_SUCCESS,
            (void*)(uintptr_t)(0x5DE0U + Round)
        );
        Stress5DRequireStatus(MtClose(Handle), MT_SUCCESS, (void*)Target);
        Stress5DJoinThread(Target, (void*)(uintptr_t)(0x5E00U + Round));
    }

    gop_printf(
        COLOR_GREEN,
        "STRESS 5D thread exit races PASS (%u rounds)\n",
        STRESS5D_RACE_ROUNDS
    );
}

static PETHREAD
Stress5DWaitForProcessWorker(
    PEPROCESS Process
)
{
    uint64_t StartTsc = __rdtsc();
    for (;;) {
        PETHREAD MainThread = NULL;
        MsAcquirePushLockShared(&Process->ThreadListLock);
        if (Process->NumThreads >= 2 && Process->MainThread != NULL &&
            ObReferenceObject(Process->MainThread)) {
            MainThread = Process->MainThread;
        }
        MsReleasePushLockShared(&Process->ThreadListLock);
        if (MainThread) return MainThread;

        if (Stress5DWatchdogExpired(StartTsc)) {
            Stress5DBugCheck(
                Stress5DProtocolTimeout,
                Process,
                (void*)(uintptr_t)Process->NumThreads
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress5DTestProcessLifetime(void)
{
    HANDLE ProcessHandle = MT_INVALID_HANDLE;
    Stress5DRequireStatus(
        PsCreateProcess(
            "terminateMyself.mtexe",
            &ProcessHandle,
            MT_PROCESS_ALL_ACCESS,
            0
        ),
        MT_SUCCESS,
        (void*)0x5D40
    );

    PEPROCESS Process = NULL;
    Stress5DRequireStatus(
        ObReferenceObjectByHandle(
            ProcessHandle,
            MT_PROCESS_ALL_ACCESS,
            PsProcessType,
            (void**)&Process,
            NULL
        ),
        MT_SUCCESS,
        (void*)0x5D41
    );

    HANDLE ObserverHandle = MT_INVALID_HANDLE;
    Stress5DRequireStatus(
        ObCreateHandleForObject(Process, MT_SYNCHRONIZE, &ObserverHandle),
        MT_SUCCESS,
        (void*)0x5D42
    );
    Stress5DTestAttachedProcessSelfWaits(Process);
    Stress5DRequireStatus(
        MtWaitForSingleObject(ProcessHandle, 0, false),
        MT_TIMEOUT,
        (void*)0x5D43
    );

    // terminateMyself creates a sleeping worker before its main thread exits.
    // Capturing MainThread while NumThreads >= 2 gives us a stable reference to
    // the original main thread, even after the process promotes its worker.
    PETHREAD OriginalMainThread = Stress5DWaitForProcessWorker(Process);

    STRESS5D_WAIT_CONTEXT Contexts[STRESS5D_WAITER_COUNT] = { 0 };
    PETHREAD Waiters[STRESS5D_WAITER_COUNT] = { 0 };
    for (uint32_t Index = 0; Index < STRESS5D_WAITER_COUNT; Index++) {
        Contexts[Index].Handle = ProcessHandle;
        Contexts[Index].Status = MT_PENDING;
        Waiters[Index] = Stress5DCreateRetainedThread(
            Stress5DHandleWaiter,
            &Contexts[Index]
        );
        InterlockedStoreRelease(&Contexts[Index].Start, true);
    }
    Stress5DWaitForRegistrations(
        &Process->InternalProcess.Header,
        STRESS5D_WAITER_COUNT,
        (void*)0x5D44
    );

    Stress5DRequireStatus(MtClose(ProcessHandle), MT_SUCCESS, (void*)0x5D45);
    Stress5DRequireStatus(
        MtWaitForSingleObject(ProcessHandle, 0, false),
        MT_INVALID_HANDLE,
        (void*)0x5D46
    );

    // The main thread may terminate, but the persistent process signal must
    // remain clear while its sleeping worker is still alive.
    MTSTATUS MainWaitStatus = MsWaitForSingleObject(
        &OriginalMainThread->InternalThread.Header,
        KernelMode,
        false,
        STRESS5D_WAIT_TIMEOUT_MS
    );
    if (MainWaitStatus != MT_SUCCESS) {
        Stress5DBugCheck(
            Stress5DUnexpectedStatus,
            (void*)0x5D47,
            (void*)(uintptr_t)MainWaitStatus
        );
    }
    ObDereferenceObject(OriginalMainThread);

    IRQL OldIrql;
    MsAcquireSpinlock(&Process->InternalProcess.Header.Lock, &OldIrql);
    bool ProcessSignaled = Process->InternalProcess.Header.SignalState != 0;
    PROCESS_STATE ProcessState = Process->InternalProcess.ProcessState;
    MsReleaseSpinlock(&Process->InternalProcess.Header.Lock, OldIrql);

    MsAcquirePushLockShared(&Process->ThreadListLock);
    uint32_t RemainingThreads = Process->NumThreads;
    MsReleasePushLockShared(&Process->ThreadListLock);
    if (ProcessSignaled || ProcessState == PROCESS_TERMINATED ||
        RemainingThreads == 0) {
        Stress5DBugCheck(
            Stress5DProcessStateFailure,
            Process,
            (void*)(uintptr_t)RemainingThreads
        );
    }
    Stress5DRequireStatus(
        MtWaitForSingleObject(ObserverHandle, 0, false),
        MT_TIMEOUT,
        (void*)0x5D48
    );

    Stress5DRequireStatus(
        PsTerminateProcess(Process, STRESS5D_EXIT_STATUS),
        MT_SUCCESS,
        (void*)0x5D49
    );
    Stress5DRequireStatus(
        MtWaitForSingleObject(
            ObserverHandle,
            STRESS5D_WAIT_TIMEOUT_MS,
            false
        ),
        MT_SUCCESS,
        (void*)0x5D4A
    );

    for (uint32_t Index = 0; Index < STRESS5D_WAITER_COUNT; Index++) {
        Stress5DJoinThread(
            Waiters[Index],
            (void*)(uintptr_t)(0x5D4BU + Index)
        );
        Stress5DRequireStatus(
            InterlockedLoadAcquire(&Contexts[Index].Status),
            MT_SUCCESS,
            (void*)(uintptr_t)(0x5D50U + Index)
        );
    }

    MsAcquireSpinlock(&Process->InternalProcess.Header.Lock, &OldIrql);
    bool ValidTerminalState =
        Process->InternalProcess.Header.SignalState == 1 &&
        Process->InternalProcess.ProcessState == PROCESS_TERMINATED &&
        Process->ExitStatus == STRESS5D_EXIT_STATUS;
    MsReleaseSpinlock(&Process->InternalProcess.Header.Lock, OldIrql);
    if (!ValidTerminalState) {
        Stress5DBugCheck(
            Stress5DProcessStateFailure,
            Process,
            (void*)(uintptr_t)Process->ExitStatus
        );
    }

    Stress5DRequireStatus(
        MtWaitForSingleObject(ObserverHandle, 0, false),
        MT_SUCCESS,
        (void*)0x5D54
    );
    Stress5DRequireStatus(
        MtWaitForSingleObject(ObserverHandle, 0, false),
        MT_SUCCESS,
        (void*)0x5D55
    );
    Stress5DRequireStatus(MtClose(ObserverHandle), MT_SUCCESS, (void*)0x5D56);
    ObDereferenceObject(Process);

    gop_printf(
        COLOR_GREEN,
        "STRESS 5D process lifetime PASS (main exit + %u waiters)\n",
        STRESS5D_WAITER_COUNT
    );
}

static void
Stress5DController(void)
{
    Stress5DSettleObjectCounts();
    uint32_t ThreadObjects = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsThreadType->TotalNumberOfObjects
    );
    uint32_t ThreadHandles = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsThreadType->TotalNumberOfHandles
    );
    uint32_t ProcessObjects = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsProcessType->TotalNumberOfObjects
    );
    uint32_t ProcessHandles = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsProcessType->TotalNumberOfHandles
    );

    gop_printf(
        COLOR_GREEN,
        "STRESS 5D START (thread/process handles, %u CPUs)\n",
        MeGetActiveProcessorCount()
    );

    Stress5DTestSelfWaits();
    Stress5DTestThreadLifetime();
    Stress5DTestThreadExitRaces();
    Stress5DTestProcessLifetime();

    Stress5DWaitForTypeCounts(PsThreadType, ThreadObjects, ThreadHandles);
    Stress5DWaitForTypeCounts(PsProcessType, ProcessObjects, ProcessHandles);
    gop_printf(COLOR_GREEN, "STRESS 5D PASS\n");
}

#define STRESS5E_WAIT_TIMEOUT_MS  30000ULL
#define STRESS5E_WATCHDOG_SECONDS 30ULL
#define STRESS5E_EXIT_STATUS      MT_IO_ERROR

typedef enum _STRESS5E_FAILURE {
    Stress5EUnexpectedStatus = 1,
    Stress5EUnexpectedExitStatus,
    Stress5EUnexpectedReturnLength,
    Stress5EProtocolTimeout,
    Stress5EThreadReferenceFailure,
    Stress5EObjectCountLeak,
    Stress5EHandleCountLeak
} STRESS5E_FAILURE;

typedef enum _STRESS5E_OBJECT_KIND {
    Stress5EThreadObject,
    Stress5EProcessObject
} STRESS5E_OBJECT_KIND;

typedef struct _STRESS5E_QUERY_CONTEXT {
    volatile bool Start;
    volatile bool Ready;
    volatile bool Stop;
    STRESS5E_OBJECT_KIND Kind;
    HANDLE Handle;
    MTSTATUS ExpectedExitStatus;
    volatile MTSTATUS FailureStatus;
    volatile uint32_t PendingObservations;
    volatile uint32_t TerminalObservations;
} STRESS5E_QUERY_CONTEXT;

NORETURN
static void
Stress5EBugCheck(
    STRESS5E_FAILURE Failure,
    void* Detail1,
    void* Detail2
)
{
    // P1 identifies Stress 5E, P2 is the exact failed invariant, and P3/P4
    // contain the phase-specific status, object, count, or handle.
    MeBugCheckEx(
        WAIT_STATE_FAILURE,
        (void*)(uintptr_t)0x5E,
        (void*)(uintptr_t)Failure,
        Detail1,
        Detail2
    );
}

static bool
Stress5EWatchdogExpired(
    uint64_t StartTsc
)
{
    uint64_t Now = __rdtsc();
    return Now >= StartTsc &&
        Now - StartTsc >
            STRESS5E_WATCHDOG_SECONDS * Stress2CTscTicksPerSecond;
}

static void
Stress5ERequireStatus(
    MTSTATUS Actual,
    MTSTATUS Expected,
    void* Detail
)
{
    if (Actual != Expected) {
        Stress5EBugCheck(
            Stress5EUnexpectedStatus,
            Detail,
            (void*)(uintptr_t)Actual
        );
    }
}

static void
Stress5EWaitForValue(
    volatile uint32_t* Value,
    uint32_t Minimum,
    void* Detail
)
{
    uint64_t StartTsc = __rdtsc();
    while (InterlockedLoadAcquire(Value) < Minimum) {
        if (Stress5EWatchdogExpired(StartTsc)) {
            Stress5EBugCheck(
                Stress5EProtocolTimeout,
                Detail,
                (void*)(uintptr_t)InterlockedLoadAcquire(Value)
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static PETHREAD
Stress5ECreateRetainedThread(
    ThreadEntry Entry,
    THREAD_PARAMETER Parameter
)
{
    PETHREAD Thread = StressSuiteCreateThread(Entry, Parameter);
    if (!ObReferenceObject(Thread)) {
        Stress5EBugCheck(
            Stress5EThreadReferenceFailure,
            Thread,
            (void*)Entry
        );
    }
    return Thread;
}

static void
Stress5EJoinThread(
    PETHREAD Thread,
    void* Detail
)
{
    Stress5ERequireStatus(
        MsWaitForSingleObject(
            &Thread->InternalThread.Header,
            KernelMode,
            false,
            STRESS5E_WAIT_TIMEOUT_MS
        ),
        MT_SUCCESS,
        Detail
    );
    ObDereferenceObject(Thread);
}

static MTSTATUS
Stress5EQueryExitStatus(
    STRESS5E_OBJECT_KIND Kind,
    HANDLE Handle,
    MTSTATUS* ExitStatus,
    uint32_t* ReturnLength
)
{
    if (Kind == Stress5EThreadObject) {
        THREAD_BASIC_INFORMATION Information = { 0 };
        MTSTATUS Status = MtQueryInformationThread(
            Handle,
            ThreadBasicInformation,
            &Information,
            sizeof(Information),
            ReturnLength
        );
        if (MT_SUCCEEDED(Status)) {
            *ExitStatus = Information.ExitStatus;
        }
        return Status;
    }

    PROCESS_BASIC_INFORMATION Information = { 0 };
    MTSTATUS Status = MtQueryInformationProcess(
        Handle,
        ProcessBasicInformation,
        &Information,
        sizeof(Information),
        ReturnLength
    );
    if (MT_SUCCEEDED(Status)) {
        *ExitStatus = Information.ExitStatus;
    }
    return Status;
}

static void
Stress5EQueryWorker(
    THREAD_PARAMETER Parameter
)
{
    STRESS5E_QUERY_CONTEXT* Context = Parameter;
    while (!InterlockedLoadAcquire(&Context->Start)) {
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }

    for (;;) {
        MTSTATUS ExitStatus = MT_SUCCESS;
        uint32_t ReturnLength = 0;
        MTSTATUS Status = Stress5EQueryExitStatus(
            Context->Kind,
            Context->Handle,
            &ExitStatus,
            &ReturnLength
        );

        if (Status != MT_SUCCESS) {
            InterlockedCompareExchange32(
                &Context->FailureStatus,
                Status,
                MT_SUCCESS
            );
            return;
        }

        uint32_t ExpectedLength =
            Context->Kind == Stress5EThreadObject
                ? sizeof(THREAD_BASIC_INFORMATION)
                : sizeof(PROCESS_BASIC_INFORMATION);
        if (ReturnLength != ExpectedLength) {
            InterlockedCompareExchange32(
                &Context->FailureStatus,
                MT_INFO_LENGTH_MISMATCH,
                MT_SUCCESS
            );
            return;
        }

        if (ExitStatus == MT_PENDING) {
            InterlockedIncrementU32(&Context->PendingObservations);
            InterlockedStoreRelease(&Context->Ready, true);
        }
        else if (ExitStatus == Context->ExpectedExitStatus) {
            InterlockedIncrementU32(&Context->TerminalObservations);
        }
        else {
            InterlockedCompareExchange32(
                &Context->FailureStatus,
                ExitStatus,
                MT_SUCCESS
            );
            return;
        }

        if (InterlockedLoadAcquire(&Context->Stop) &&
            InterlockedLoadAcquire(&Context->TerminalObservations) != 0) {
            return;
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress5EValidateQueryResult(
    STRESS5E_OBJECT_KIND Kind,
    HANDLE Handle,
    MTSTATUS ExpectedExitStatus,
    void* Detail
)
{
    MTSTATUS ExitStatus = MT_SUCCESS;
    uint32_t ReturnLength = 0;
    Stress5ERequireStatus(
        Stress5EQueryExitStatus(
            Kind,
            Handle,
            &ExitStatus,
            &ReturnLength
        ),
        MT_SUCCESS,
        Detail
    );

    uint32_t ExpectedLength =
        Kind == Stress5EThreadObject
            ? sizeof(THREAD_BASIC_INFORMATION)
            : sizeof(PROCESS_BASIC_INFORMATION);
    if (ReturnLength != ExpectedLength) {
        Stress5EBugCheck(
            Stress5EUnexpectedReturnLength,
            Detail,
            (void*)(uintptr_t)ReturnLength
        );
    }
    if (ExitStatus != ExpectedExitStatus) {
        Stress5EBugCheck(
            Stress5EUnexpectedExitStatus,
            Detail,
            (void*)(uintptr_t)ExitStatus
        );
    }
}

static void
Stress5EWaitForTypeCounts(
    POBJECT_TYPE Type,
    uint32_t ExpectedObjects,
    uint32_t ExpectedHandles
)
{
    uint64_t StartTsc = __rdtsc();
    for (;;) {
        uint32_t Objects = InterlockedLoadAcquire(
            (volatile uint32_t*)&Type->TotalNumberOfObjects
        );
        uint32_t Handles = InterlockedLoadAcquire(
            (volatile uint32_t*)&Type->TotalNumberOfHandles
        );
        if (Objects == ExpectedObjects && Handles == ExpectedHandles) {
            return;
        }

        if (Stress5EWatchdogExpired(StartTsc)) {
            bool ObjectMismatch = Objects != ExpectedObjects;
            Stress5EBugCheck(
                ObjectMismatch
                    ? Stress5EObjectCountLeak
                    : Stress5EHandleCountLeak,
                Type,
                (void*)(uintptr_t)(
                    ((uint64_t)(
                        ObjectMismatch ? ExpectedObjects : ExpectedHandles
                    ) << 32) |
                    (ObjectMismatch ? Objects : Handles)
                )
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress5ETestThreadQueries(void)
{
    STRESS5D_TARGET_CONTEXT TargetContext = {
        .ExitStatus = STRESS5E_EXIT_STATUS
    };
    PETHREAD Target = Stress5ECreateRetainedThread(
        Stress5DTargetWorker,
        &TargetContext
    );

    HANDLE QueryHandle = MT_INVALID_HANDLE;
    HANDLE DeniedHandle = MT_INVALID_HANDLE;
    HANDLE ClosedHandle = MT_INVALID_HANDLE;
    Stress5ERequireStatus(
        ObCreateHandleForObject(
            Target,
            MT_SYNCHRONIZE | MT_THREAD_QUERY_INFO,
            &QueryHandle
        ),
        MT_SUCCESS,
        (void*)0x5E10
    );
    Stress5ERequireStatus(
        ObCreateHandleForObject(Target, MT_SYNCHRONIZE, &DeniedHandle),
        MT_SUCCESS,
        (void*)0x5E11
    );
    Stress5ERequireStatus(
        ObCreateHandleForObject(Target, MT_THREAD_QUERY_INFO, &ClosedHandle),
        MT_SUCCESS,
        (void*)0x5E12
    );

    InterlockedStoreRelease(&TargetContext.Start, true);
    Stress5DWaitForFlag(&TargetContext.Ready, &TargetContext);
    Stress5EValidateQueryResult(
        Stress5EThreadObject,
        QueryHandle,
        STILL_ACTIVE,
        (void*)0x5E13
    );

    THREAD_BASIC_INFORMATION Information = { 0 };
    uint32_t ReturnLength = 0;
    Stress5ERequireStatus(
        MtQueryInformationThread(
            QueryHandle,
            ThreadBasicInformation,
            &Information,
            sizeof(Information) - 1,
            &ReturnLength
        ),
        MT_INFO_LENGTH_MISMATCH,
        (void*)0x5E14
    );
    if (ReturnLength != sizeof(Information)) {
        Stress5EBugCheck(
            Stress5EUnexpectedReturnLength,
            (void*)0x5E14,
            (void*)(uintptr_t)ReturnLength
        );
    }
    Stress5ERequireStatus(
        MtQueryInformationThread(
            QueryHandle,
            (THREADINFOCLASS)UINT32_MAX,
            &Information,
            sizeof(Information),
            NULL
        ),
        MT_INVALID_INFO_CLASS,
        (void*)0x5E15
    );
    Stress5ERequireStatus(
        MtQueryInformationThread(
            DeniedHandle,
            ThreadBasicInformation,
            &Information,
            sizeof(Information),
            NULL
        ),
        MT_ACCESS_DENIED,
        (void*)0x5E16
    );
    Stress5ERequireStatus(MtClose(DeniedHandle), MT_SUCCESS, (void*)0x5E17);
    Stress5ERequireStatus(MtClose(ClosedHandle), MT_SUCCESS, (void*)0x5E18);
    Stress5ERequireStatus(
        MtQueryInformationThread(
            ClosedHandle,
            ThreadBasicInformation,
            &Information,
            sizeof(Information),
            NULL
        ),
        MT_INVALID_HANDLE,
        (void*)0x5E19
    );

    STRESS5E_QUERY_CONTEXT QueryContext = {
        .Kind = Stress5EThreadObject,
        .Handle = QueryHandle,
        .ExpectedExitStatus = STRESS5E_EXIT_STATUS,
        .FailureStatus = MT_SUCCESS
    };
    PETHREAD QueryThread = Stress5ECreateRetainedThread(
        Stress5EQueryWorker,
        &QueryContext
    );
    InterlockedStoreRelease(&QueryContext.Start, true);
    Stress5EWaitForValue(
        &QueryContext.PendingObservations,
        1,
        (void*)0x5E1A
    );

    InterlockedStoreRelease(&TargetContext.Stop, true);
    Stress5ERequireStatus(
        MtWaitForSingleObject(
            QueryHandle,
            STRESS5E_WAIT_TIMEOUT_MS,
            false
        ),
        MT_SUCCESS,
        (void*)0x5E1C
    );
    Stress5EWaitForValue(
        &QueryContext.TerminalObservations,
        1,
        (void*)0x5E1D
    );
    InterlockedStoreRelease(&QueryContext.Stop, true);
    Stress5EJoinThread(QueryThread, (void*)0x5E1E);
    Stress5ERequireStatus(
        InterlockedLoadAcquire(&QueryContext.FailureStatus),
        MT_SUCCESS,
        (void*)0x5E1F
    );

    Stress5EValidateQueryResult(
        Stress5EThreadObject,
        QueryHandle,
        STRESS5E_EXIT_STATUS,
        (void*)0x5E20
    );
    Stress5ERequireStatus(MtClose(QueryHandle), MT_SUCCESS, (void*)0x5E21);
    Stress5EJoinThread(Target, (void*)0x5E22);

    gop_printf(
        COLOR_GREEN,
        "STRESS 5E thread exit-code query PASS (%u pending, %u terminal)\n",
        InterlockedLoadAcquire(&QueryContext.PendingObservations),
        InterlockedLoadAcquire(&QueryContext.TerminalObservations)
    );
}

static void
Stress5ETestProcessQueries(void)
{
    HANDLE ProcessHandle = MT_INVALID_HANDLE;
    Stress5ERequireStatus(
        PsCreateProcess(
            "terminateMyself.mtexe",
            &ProcessHandle,
            MT_PROCESS_ALL_ACCESS,
            0
        ),
        MT_SUCCESS,
        (void*)0x5E30
    );

    PEPROCESS Process = NULL;
    Stress5ERequireStatus(
        ObReferenceObjectByHandle(
            ProcessHandle,
            MT_PROCESS_ALL_ACCESS,
            PsProcessType,
            (void**)&Process,
            NULL
        ),
        MT_SUCCESS,
        (void*)0x5E31
    );

    HANDLE DeniedHandle = MT_INVALID_HANDLE;
    HANDLE ClosedHandle = MT_INVALID_HANDLE;
    Stress5ERequireStatus(
        ObCreateHandleForObject(Process, MT_SYNCHRONIZE, &DeniedHandle),
        MT_SUCCESS,
        (void*)0x5E32
    );
    Stress5ERequireStatus(
        ObCreateHandleForObject(Process, MT_PROCESS_QUERY_INFO, &ClosedHandle),
        MT_SUCCESS,
        (void*)0x5E33
    );

    Stress5EValidateQueryResult(
        Stress5EProcessObject,
        ProcessHandle,
        STILL_ACTIVE,
        (void*)0x5E34
    );

    PROCESS_BASIC_INFORMATION Information = { 0 };
    uint32_t ReturnLength = 0;
    Stress5ERequireStatus(
        MtQueryInformationProcess(
            ProcessHandle,
            ProcessBasicInformation,
            &Information,
            sizeof(Information) - 1,
            &ReturnLength
        ),
        MT_INFO_LENGTH_MISMATCH,
        (void*)0x5E35
    );
    if (ReturnLength != sizeof(Information)) {
        Stress5EBugCheck(
            Stress5EUnexpectedReturnLength,
            (void*)0x5E35,
            (void*)(uintptr_t)ReturnLength
        );
    }
    Stress5ERequireStatus(
        MtQueryInformationProcess(
            ProcessHandle,
            (PROCESSINFOCLASS)UINT32_MAX,
            &Information,
            sizeof(Information),
            NULL
        ),
        MT_INVALID_INFO_CLASS,
        (void*)0x5E36
    );
    Stress5ERequireStatus(
        MtQueryInformationProcess(
            DeniedHandle,
            ProcessBasicInformation,
            &Information,
            sizeof(Information),
            NULL
        ),
        MT_ACCESS_DENIED,
        (void*)0x5E37
    );
    Stress5ERequireStatus(MtClose(DeniedHandle), MT_SUCCESS, (void*)0x5E38);
    Stress5ERequireStatus(MtClose(ClosedHandle), MT_SUCCESS, (void*)0x5E39);
    Stress5ERequireStatus(
        MtQueryInformationProcess(
            ClosedHandle,
            ProcessBasicInformation,
            &Information,
            sizeof(Information),
            NULL
        ),
        MT_INVALID_HANDLE,
        (void*)0x5E3A
    );

    STRESS5E_QUERY_CONTEXT QueryContext = {
        .Kind = Stress5EProcessObject,
        .Handle = ProcessHandle,
        .ExpectedExitStatus = STRESS5E_EXIT_STATUS,
        .FailureStatus = MT_SUCCESS
    };
    PETHREAD QueryThread = Stress5ECreateRetainedThread(
        Stress5EQueryWorker,
        &QueryContext
    );
    InterlockedStoreRelease(&QueryContext.Start, true);
    Stress5EWaitForValue(
        &QueryContext.PendingObservations,
        1,
        (void*)0x5E3B
    );

    Stress5ERequireStatus(
        PsTerminateProcess(Process, STRESS5E_EXIT_STATUS),
        MT_SUCCESS,
        (void*)0x5E3C
    );
    Stress5ERequireStatus(
        MtWaitForSingleObject(
            ProcessHandle,
            STRESS5E_WAIT_TIMEOUT_MS,
            false
        ),
        MT_SUCCESS,
        (void*)0x5E3D
    );
    Stress5EWaitForValue(
        &QueryContext.TerminalObservations,
        1,
        (void*)0x5E3E
    );
    InterlockedStoreRelease(&QueryContext.Stop, true);
    Stress5EJoinThread(QueryThread, (void*)0x5E3F);
    Stress5ERequireStatus(
        InterlockedLoadAcquire(&QueryContext.FailureStatus),
        MT_SUCCESS,
        (void*)0x5E40
    );

    Stress5EValidateQueryResult(
        Stress5EProcessObject,
        ProcessHandle,
        STRESS5E_EXIT_STATUS,
        (void*)0x5E41
    );
    Stress5ERequireStatus(MtClose(ProcessHandle), MT_SUCCESS, (void*)0x5E42);
    ObDereferenceObject(Process);

    gop_printf(
        COLOR_GREEN,
        "STRESS 5E process exit-code query PASS (%u pending, %u terminal)\n",
        InterlockedLoadAcquire(&QueryContext.PendingObservations),
        InterlockedLoadAcquire(&QueryContext.TerminalObservations)
    );
}

static void
Stress5EController(void)
{
    Stress5DSettleObjectCounts();
    uint32_t ThreadObjects = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsThreadType->TotalNumberOfObjects
    );
    uint32_t ThreadHandles = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsThreadType->TotalNumberOfHandles
    );
    uint32_t ProcessObjects = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsProcessType->TotalNumberOfObjects
    );
    uint32_t ProcessHandles = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsProcessType->TotalNumberOfHandles
    );

    gop_printf(
        COLOR_GREEN,
        "STRESS 5E START (exit-code queries, %u CPUs)\n",
        MeGetActiveProcessorCount()
    );

    Stress5ETestThreadQueries();
    Stress5ETestProcessQueries();

    Stress5EWaitForTypeCounts(PsThreadType, ThreadObjects, ThreadHandles);
    Stress5EWaitForTypeCounts(PsProcessType, ProcessObjects, ProcessHandles);
    gop_printf(COLOR_GREEN, "STRESS 5E PASS\n");
}

#define STRESS6_WAIT_TIMEOUT_MS    30000ULL
#define STRESS6_WATCHDOG_SECONDS   30ULL
#define STRESS6_SLEEP_TICKS        20ULL
#define STRESS6_STABLE_TICKS       5ULL
#define STRESS6_READY_ATTEMPTS     64U
#define STRESS6_RACE_ROUNDS        32U
#define STRESS6_APC_REUSE_ROUNDS   32U
#define STRESS6_TERMINATION_STATUS MT_GENERAL_FAILURE
#define STRESS6_USER_APC_SLEEP_MS  250ULL
#define STRESS_GATE4_ONLY          0

typedef enum _STRESS6_FAILURE {
    Stress6UnexpectedStatus = 1,
    Stress6ProtocolTimeout,
    Stress6ThreadReferenceFailure,
    Stress6ReadyStateUnavailable,
    Stress6WaitRegistrationFailure,
    Stress6SuspendStateFailure,
    Stress6SuspendCountFailure,
    Stress6ProgressWhileSuspended,
    Stress6ProgressDidNotResume,
    Stress6WorkerStateFailure,
    Stress6TerminationFailure,
    Stress6RaceOutcomeFailure,
    Stress6ObjectCountLeak,
    Stress6HandleCountLeak,
    Stress6UserApcAllocationFailure,
    Stress6UserApcInsertionFailure,
    Stress6UserApcStateFailure,
    Stress6ExceptionPublicationFailure,
    Stress6ReadyMigrationUnavailable,
    Stress6ReadyMigrationFailure
} STRESS6_FAILURE;

typedef struct _STRESS6_READY_CONTEXT {
    volatile bool Start;
    volatile bool Entered;
    volatile bool Stop;
    volatile uint64_t Progress;
} STRESS6_READY_CONTEXT;

typedef struct _STRESS6_MIGRATION_CONTEXT {
    volatile bool Entered;
    volatile bool Stop;
    volatile uint32_t ProcessorId;
} STRESS6_MIGRATION_CONTEXT;

typedef struct _STRESS6_RUNNING_CONTEXT {
    volatile bool Start;
    volatile bool Ready;
    volatile bool RequestSelfSuspend;
    volatile bool ReturnedFromSuspend;
    volatile bool Stop;
    volatile uint64_t Progress;
    volatile MTSTATUS SuspendStatus;
    volatile uint32_t PreviousCount;
} STRESS6_RUNNING_CONTEXT;

typedef struct _STRESS6_SLEEP_CONTEXT {
    volatile bool Start;
    volatile bool Ready;
    volatile bool Returned;
    volatile MTSTATUS Status;
} STRESS6_SLEEP_CONTEXT;

typedef struct _STRESS6_EVENT_CONTEXT {
    PEVENT Event;
    volatile bool Start;
    volatile bool Ready;
    volatile bool Returned;
    volatile MTSTATUS Status;
} STRESS6_EVENT_CONTEXT;

typedef struct _STRESS6_RACE_CONTEXT {
    HANDLE Handle;
    volatile bool Ready;
    volatile bool Start;
    volatile MTSTATUS SuspendStatus;
    volatile MTSTATUS ResumeStatus;
} STRESS6_RACE_CONTEXT;

typedef struct _STRESS6_USER_TARGET {
    HANDLE ProcessHandle;
    PEPROCESS Process;
    PETHREAD Thread;
} STRESS6_USER_TARGET;

typedef enum _STRESS6_EXCEPTION_PUBLISH_MODE {
    Stress6ExceptionPublishSuccess,
    Stress6ExceptionPublishWhileActive,
    Stress6ExceptionPublishWhilePending
} STRESS6_EXCEPTION_PUBLISH_MODE;

typedef struct _STRESS6_EXCEPTION_PUBLISH_CONTEXT {
    STRESS6_EXCEPTION_PUBLISH_MODE Mode;
    volatile bool Ready;
    volatile bool Start;
    volatile bool Release;
    volatile MTSTATUS Status;
    PITHREAD Thread;
    EXCEPTION_RECORD Record;
} STRESS6_EXCEPTION_PUBLISH_CONTEXT;

NORETURN
static void
Stress6BugCheck(
    STRESS6_FAILURE Failure,
    void* Detail1,
    void* Detail2
)
{
    // P1 identifies Gate 4 suspend/resume stress. P2 is the failed invariant,
    // while P3/P4 carry a status, thread, count, phase, or race round.
    MeBugCheckEx(
        WAIT_STATE_FAILURE,
        (void*)(uintptr_t)0x60,
        (void*)(uintptr_t)Failure,
        Detail1,
        Detail2
    );
}

static bool
Stress6WatchdogExpired(
    uint64_t StartTsc
)
{
    uint64_t Now = __rdtsc();
    return Now >= StartTsc &&
        Now - StartTsc >
            STRESS6_WATCHDOG_SECONDS * Stress2CTscTicksPerSecond;
}

static void
Stress6RequireStatus(
    MTSTATUS Actual,
    MTSTATUS Expected,
    void* Detail
)
{
    if (Actual != Expected) {
        Stress6BugCheck(
            Stress6UnexpectedStatus,
            Detail,
            (void*)(uintptr_t)Actual
        );
    }
}

static void
Stress6WaitForFlag(
    volatile bool* Flag,
    void* Detail
)
{
    uint64_t StartTsc = __rdtsc();
    while (!InterlockedLoadAcquire(Flag)) {
        if (Stress6WatchdogExpired(StartTsc)) {
            Stress6BugCheck(
                Stress6ProtocolTimeout,
                Detail,
                (void*)Flag
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress6WaitForProgress(
    volatile uint64_t* Progress,
    uint64_t Previous,
    void* Detail
)
{
    uint64_t StartTsc = __rdtsc();
    while (InterlockedLoadAcquire(Progress) == Previous) {
        if (Stress6WatchdogExpired(StartTsc)) {
            Stress6BugCheck(
                Stress6ProgressDidNotResume,
                Detail,
                (void*)(uintptr_t)Previous
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress6WaitTicks(
    uint64_t TickCount,
    void* Detail
)
{
    uint64_t StartTick = InterlockedLoadAcquire(&MeSystemTickCount);
    uint64_t StartTsc = __rdtsc();

    while (InterlockedLoadAcquire(&MeSystemTickCount) - StartTick < TickCount) {
        if (Stress6WatchdogExpired(StartTsc)) {
            Stress6BugCheck(
                Stress6ProtocolTimeout,
                Detail,
                (void*)(uintptr_t)StartTick
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static bool
Stress6ListEntryIsLinked(
    PDOUBLY_LINKED_LIST Entry
)
{
    return Entry->Flink != NULL &&
        Entry->Blink != NULL &&
        Entry->Flink != Entry &&
        Entry->Blink != Entry;
}

static bool
Stress6IsDispatcherWaiting(
    PETHREAD Thread,
    PDISPATCHER_HEADER Header
)
{
    PITHREAD IThread = &Thread->InternalThread;
    IRQL OldIrql;
    MsAcquireSpinlock(&Header->Lock, &OldIrql);
    bool Registered =
        IThread->WaitStatus == MT_PENDING &&
        IThread->WaitBlock.WaitReason == WaitReasonDispatcherObject &&
        IThread->WaitBlock.Object == Header &&
        Stress6ListEntryIsLinked(&IThread->WaitBlock.ObjectListEntry);
    MsReleaseSpinlock(&Header->Lock, OldIrql);

    return Registered &&
        InterlockedLoadAcquire(&IThread->ThreadState) == THREAD_BLOCKED;
}

static bool
Stress6IsSleeping(
    PETHREAD Thread
)
{
    PITHREAD IThread = &Thread->InternalThread;
    IRQL OldIrql;
    MeRaiseIrql(CLOCK_LEVEL, &OldIrql);
    MsAcquireSpinlockAtDpcLevel(&MsTimerQueueLock);
    bool Registered =
        IThread->WaitStatus == MT_PENDING &&
        IThread->WaitBlock.WaitReason == WaitReasonSleep &&
        IThread->WaitBlock.Object == NULL &&
        Stress6ListEntryIsLinked(&IThread->WaitBlock.TimerListEntry);
    MsReleaseSpinlockFromDpcLevel(&MsTimerQueueLock);
    MeLowerIrql(OldIrql);

    return Registered &&
        InterlockedLoadAcquire(&IThread->ThreadState) == THREAD_BLOCKED;
}

static void
Stress6WaitForDispatcherWait(
    PETHREAD Thread,
    PDISPATCHER_HEADER Header,
    void* Detail
)
{
    uint64_t StartTsc = __rdtsc();
    while (!Stress6IsDispatcherWaiting(Thread, Header)) {
        if (Stress6WatchdogExpired(StartTsc)) {
            Stress6BugCheck(
                Stress6WaitRegistrationFailure,
                Detail,
                Thread
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress6WaitForSleep(
    PETHREAD Thread,
    void* Detail
)
{
    uint64_t StartTsc = __rdtsc();
    while (!Stress6IsSleeping(Thread)) {
        if (Stress6WatchdogExpired(StartTsc)) {
            Stress6BugCheck(
                Stress6WaitRegistrationFailure,
                Detail,
                Thread
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress6WaitForSuspended(
    PETHREAD Thread,
    void* Detail
)
{
    Stress6WaitForDispatcherWait(
        Thread,
        &Thread->InternalThread.SuspendSemaphore.Header,
        Detail
    );

    IRQL OldIrql;
    MsAcquireSpinlock(&Thread->InternalThread.ApcQueueLock, &OldIrql);
    bool Valid =
        Thread->InternalThread.SuspendCount != 0 &&
        Thread->InternalThread.SuspendApcActive &&
        Thread->InternalThread.SuspendAPC.Inserted == 0 &&
        Thread->InternalThread.ApcState.KernelApcInProgress;
    MsReleaseSpinlock(&Thread->InternalThread.ApcQueueLock, OldIrql);

    if (!Valid) {
        Stress6BugCheck(
            Stress6SuspendStateFailure,
            Detail,
            Thread
        );
    }
}

static uint32_t
Stress6ReadSuspendCount(
    PETHREAD Thread
)
{
    IRQL OldIrql;
    MsAcquireSpinlock(&Thread->InternalThread.ApcQueueLock, &OldIrql);
    uint32_t Count = Thread->InternalThread.SuspendCount;
    MsReleaseSpinlock(&Thread->InternalThread.ApcQueueLock, OldIrql);
    return Count;
}

static PETHREAD
Stress6CreateRetainedThread(
    ThreadEntry Entry,
    THREAD_PARAMETER Parameter
)
{
    PETHREAD Thread = StressSuiteCreateThread(Entry, Parameter);
    if (!ObReferenceObject(Thread)) {
        Stress6BugCheck(
            Stress6ThreadReferenceFailure,
            Thread,
            (void*)Entry
        );
    }
    return Thread;
}

static void
Stress6JoinThread(
    PETHREAD Thread,
    void* Detail
)
{
    Stress6RequireStatus(
        MsWaitForSingleObject(
            &Thread->InternalThread.Header,
            KernelMode,
            false,
            STRESS6_WAIT_TIMEOUT_MS
        ),
        MT_SUCCESS,
        Detail
    );
    ObDereferenceObject(Thread);
}

static HANDLE
Stress6CreateThreadHandle(
    PETHREAD Thread,
    ACCESS_MASK Access,
    void* Detail
)
{
    HANDLE Handle = MT_INVALID_HANDLE;
    Stress6RequireStatus(
        ObCreateHandleForObject(Thread, Access, &Handle),
        MT_SUCCESS,
        Detail
    );
    return Handle;
}

static STRESS6_USER_TARGET
Stress6CreateUserTarget(
    void* Detail
)
{
    STRESS6_USER_TARGET Target = {
        .ProcessHandle = MT_INVALID_HANDLE
    };

    Stress6RequireStatus(
        PsCreateProcess(
            "terminateMyself.mtexe",
            &Target.ProcessHandle,
            MT_PROCESS_ALL_ACCESS,
            0
        ),
        MT_SUCCESS,
        Detail
    );
    Stress6RequireStatus(
        ObReferenceObjectByHandle(
            Target.ProcessHandle,
            MT_PROCESS_ALL_ACCESS,
            PsProcessType,
            (void**)&Target.Process,
            NULL
        ),
        MT_SUCCESS,
        Detail
    );

    // This program creates one worker and then its original main thread exits.
    // Waiting for that exit leaves a single, stable user worker to exercise.
    PETHREAD OriginalMain = Stress5DWaitForProcessWorker(Target.Process);
    Stress6RequireStatus(
        MsWaitForSingleObject(
            &OriginalMain->InternalThread.Header,
            KernelMode,
            false,
            STRESS6_WAIT_TIMEOUT_MS
        ),
        MT_SUCCESS,
        Detail
    );
    ObDereferenceObject(OriginalMain);

    uint64_t StartTsc = __rdtsc();
    while (Target.Thread == NULL) {
        MsAcquirePushLockShared(&Target.Process->ThreadListLock);
        if (Target.Process->NumThreads == 1 &&
            Target.Process->MainThread != NULL &&
            !Target.Process->MainThread->SystemThread &&
            ObReferenceObject(Target.Process->MainThread)) {
            Target.Thread = Target.Process->MainThread;
        }
        MsReleasePushLockShared(&Target.Process->ThreadListLock);

        if (Target.Thread != NULL) {
            break;
        }
        if (Stress6WatchdogExpired(StartTsc)) {
            Stress6BugCheck(
                Stress6ProtocolTimeout,
                Detail,
                Target.Process
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }

    return Target;
}

static void
Stress6JoinUserTarget(
    STRESS6_USER_TARGET* Target,
    void* Detail
)
{
    Stress6JoinThread(Target->Thread, Detail);
    Target->Thread = NULL;

    Stress6RequireStatus(
        MtWaitForSingleObject(
            Target->ProcessHandle,
            STRESS6_WAIT_TIMEOUT_MS,
            false
        ),
        MT_SUCCESS,
        Detail
    );
    Stress6RequireStatus(
        MtClose(Target->ProcessHandle),
        MT_SUCCESS,
        Detail
    );
    Target->ProcessHandle = MT_INVALID_HANDLE;

    ObDereferenceObject(Target->Process);
    Target->Process = NULL;
}

static void
Stress6UserApcRundown(
    PAPC Apc
)
{
    MmFreePool(Apc);
}

static void
Stress6WaitForUserApcState(
    PETHREAD Thread,
    bool ExpectedActive,
    void* Detail
)
{
    uint64_t StartTsc = __rdtsc();
    while (InterlockedLoadAcquire(
        &Thread->InternalThread.UserApcActive
    ) != ExpectedActive) {
        if (Stress6WatchdogExpired(StartTsc)) {
            Stress6BugCheck(
                Stress6UserApcStateFailure,
                Detail,
                Thread
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress6TestUserApcContinue(void)
{
    STRESS6_USER_TARGET Target =
        Stress6CreateUserTarget((void*)0x6070);
    Stress6WaitForSleep(Target.Thread, (void*)0x6071);

#ifdef DEBUG
    // Prepare and inspect an exception frame on this real mapped user stack
    // without modifying the target's scheduler-owned return frame.
    ExpTestUserExceptionDispatchFrame(Target.Thread);
#endif

    APC_STATE ApcState;
    MeAttachProcess(&Target.Process->InternalProcess, &ApcState);
    uintptr_t DelayRoutine = PspFindMtdllEntryAddress(
        "MtDelayExecution",
        Target.Thread
    );
    MeDetachProcess(&ApcState);
    if (!DelayRoutine || DelayRoutine > MmHighestUserAddress) {
        Stress6BugCheck(
            Stress6UserApcStateFailure,
            (void*)0x6072,
            (void*)DelayRoutine
        );
    }

    PAPC Apc = MmAllocatePoolWithTag(
        NonPagedPool,
        sizeof(*Apc),
        '6cpA'
    );
    if (!Apc) {
        Stress6BugCheck(
            Stress6UserApcAllocationFailure,
            (void*)0x6073,
            Target.Thread
        );
    }

    /*
     * MtDelayExecution is used as the ordinary MTDLL normal routine. The APC
     * dispatcher supplies Alertable=false through NormalContext and the delay
     * through SystemArgument1. It remains inside the APC long enough for the
     * controller to observe UserApcActive, then returns to
     * MeUserApcDispatcher, which must invoke MtContinue.
     */
    MeInitializeApc(
        Apc,
        &Target.Thread->InternalThread,
        UserMode,
        NULL,
        Stress6UserApcRundown,
        (PNORMAL_ROUTINE)DelayRoutine,
        NULL
    );
    if (!MeInsertQueueApc(
        Apc,
        (void*)(uintptr_t)STRESS6_USER_APC_SLEEP_MS,
        NULL
    )) {
        MmFreePool(Apc);
        Stress6BugCheck(
            Stress6UserApcInsertionFailure,
            (void*)0x6074,
            Target.Thread
        );
    }

    Stress6WaitForUserApcState(Target.Thread, true, (void*)0x6075);
    Stress6WaitForUserApcState(Target.Thread, false, (void*)0x6076);

    if (InterlockedLoadAcquire(&Target.Thread->TerminationState) !=
            ThreadTerminationNone ||
        Target.Thread->InternalThread.Header.SignalState != 0) {
        Stress6BugCheck(
            Stress6UserApcStateFailure,
            (void*)0x6077,
            Target.Thread
        );
    }

    Stress6RequireStatus(
        PsTerminateThread(
            Target.Thread,
            STRESS6_TERMINATION_STATUS
        ),
        MT_SUCCESS,
        (void*)0x6078
    );
    Stress6JoinUserTarget(&Target, (void*)0x6079);

    gop_printf(
        COLOR_GREEN,
        "STRESS 6 user APC/MtContinue PASS\n"
    );
}

static void
Stress6WaitForThreadTypeCounts(
    uint32_t ExpectedObjects,
    uint32_t ExpectedHandles,
    uintptr_t Phase
)
{
    uint64_t StartTsc = __rdtsc();
    for (;;) {
        uint32_t Objects = InterlockedLoadAcquire(
            (volatile uint32_t*)&PsThreadType->TotalNumberOfObjects
        );
        uint32_t Handles = InterlockedLoadAcquire(
            (volatile uint32_t*)&PsThreadType->TotalNumberOfHandles
        );
        if (Objects == ExpectedObjects && Handles == ExpectedHandles) {
            return;
        }

        if (Stress6WatchdogExpired(StartTsc)) {
            bool ObjectMismatch = Objects != ExpectedObjects;
            Stress6BugCheck(
                ObjectMismatch
                    ? Stress6ObjectCountLeak
                    : Stress6HandleCountLeak,
                (void*)(uintptr_t)(
                    Phase
                ),
                (void*)(uintptr_t)(
                    ((uint64_t)(
                        ObjectMismatch ? ExpectedObjects : ExpectedHandles
                    ) << 32) |
                    (ObjectMismatch ? Objects : Handles)
                )
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress6ReadyWorker(
    THREAD_PARAMETER Parameter
)
{
    STRESS6_READY_CONTEXT* Context = Parameter;
    while (!InterlockedLoadAcquire(&Context->Start)) {
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }

    InterlockedStoreRelease(&Context->Entered, true);
    while (!InterlockedLoadAcquire(&Context->Stop)) {
        InterlockedIncrementU64(&Context->Progress);
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress6MigrationWorker(
    THREAD_PARAMETER Parameter
)
{
    STRESS6_MIGRATION_CONTEXT* Context = Parameter;

    // Publish the CPU that first dispatched this migrated worker.
    InterlockedStoreRelease(
        &Context->ProcessorId,
        MeGetCurrentProcessor()->ID
    );
    InterlockedStoreRelease(&Context->Entered, true);

    // Keep the thread alive until the controller has inspected the result.
    while (!InterlockedLoadAcquire(&Context->Stop)) {
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress6ExceptionPublishWorker(
    THREAD_PARAMETER Parameter
)
{
    STRESS6_EXCEPTION_PUBLISH_CONTEXT* Context = Parameter;
    PITHREAD Thread = MeGetCurrentThread();

    // Publish the target pointer before the controller starts this worker.
    Context->Thread = Thread;
    InterlockedStoreRelease(&Context->Ready, true);

    // Wait until the controller is ready to observe publication concurrently.
    while (!InterlockedLoadAcquire(&Context->Start)) {
        MsYieldExecution(&Thread->TrapRegisters);
    }

    // Construct the preexisting state required by each fatal policy test.
    if (Context->Mode == Stress6ExceptionPublishWhileActive) {
        InterlockedStoreRelease(&Thread->UserExceptionActive, true);
    }
    else if (Context->Mode == Stress6ExceptionPublishWhilePending) {
        InterlockedStoreRelease(&Thread->UserExceptionPending, true);
    }

    // Publish the record or enter the expected no-return policy path.
    MTSTATUS Status = ExpPublishUserException(&Context->Record);

    // Fatal modes must terminate inside the publisher and never reach here.
    if (Context->Mode != Stress6ExceptionPublishSuccess) {
        Stress6BugCheck(
            Stress6ExceptionPublicationFailure,
            (void*)(uintptr_t)Context->Mode,
            (void*)(uintptr_t)Status
        );
    }

    // Expose the successful return status after pending state is visible.
    InterlockedStoreRelease(&Context->Status, Status);

    // Keep the record stable until the controller completes its observation.
    while (!InterlockedLoadAcquire(&Context->Release)) {
        MsYieldExecution(&Thread->TrapRegisters);
    }

    // Consume the synthetic pending record before this system thread exits.
    InterlockedStoreRelease(&Thread->UserExceptionPending, false);
}

static void
Stress6WaitForExceptionPending(
    PITHREAD Thread,
    void* Detail
)
{
    uint64_t StartTsc = __rdtsc();
    while (!InterlockedLoadAcquire(&Thread->UserExceptionPending)) {
        if (Stress6WatchdogExpired(StartTsc)) {
            Stress6BugCheck(
                Stress6ExceptionPublicationFailure,
                Detail,
                Thread
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static MTSTATUS
Stress6ReadThreadExitStatus(
    PETHREAD Thread
)
{
    IRQL OldIrql;
    MsAcquireSpinlock(&Thread->InternalThread.Header.Lock, &OldIrql);
    MTSTATUS Status = Thread->ExitStatus;
    MsReleaseSpinlock(&Thread->InternalThread.Header.Lock, OldIrql);
    return Status;
}

static void
Stress6TestExceptionPublication(void)
{
    STRESS6_EXCEPTION_PUBLISH_CONTEXT Context = {
        .Mode = Stress6ExceptionPublishSuccess,
        .Status = MT_PENDING
    };
    Context.Record.ExceptionCode = (uint32_t)MT_ACCESS_VIOLATION;
    Context.Record.ExceptionAddress = (void*)(uintptr_t)0x115000;
    Context.Record.NumberParameters = 2;
    Context.Record.ExceptionInformation[0] = 1;
    Context.Record.ExceptionInformation[1] = 0x153000;

    // Start a publisher whose pending state can be observed from this thread.
    PETHREAD Target = Stress6CreateRetainedThread(
        Stress6ExceptionPublishWorker,
        &Context
    );
    Stress6WaitForFlag(&Context.Ready, (void*)0x6090);
    InterlockedStoreRelease(&Context.Start, true);

    // Acquire pending state, then verify every preceding record write is visible.
    Stress6WaitForExceptionPending(Context.Thread, (void*)0x6091);
    if (kmemcmp(
            &Context.Thread->PendingExceptionRecord,
            &Context.Record,
            sizeof(Context.Record)
        ) != 0) {
        Stress6BugCheck(
            Stress6ExceptionPublicationFailure,
            (void*)0x6092,
            Context.Thread
        );
    }

    // Allow the worker to clear its synthetic state and terminate normally.
    InterlockedStoreRelease(&Context.Release, true);
    Stress6JoinThread(Target, (void*)0x6093);
    Stress6RequireStatus(
        InterlockedLoadAcquire(&Context.Status),
        MT_SUCCESS,
        (void*)0x6094
    );

    // Verify both fatal no-nesting policies with disposable system threads.
    for (STRESS6_EXCEPTION_PUBLISH_MODE Mode =
            Stress6ExceptionPublishWhileActive;
         Mode <= Stress6ExceptionPublishWhilePending;
         Mode++) {
        STRESS6_EXCEPTION_PUBLISH_CONTEXT FatalContext = {
            .Mode = Mode,
            .Status = MT_PENDING
        };
        FatalContext.Record.ExceptionCode = (uint32_t)MT_INVALID_STATE;
        FatalContext.Record.ExceptionAddress = (void*)(uintptr_t)0x115000;

        Target = Stress6CreateRetainedThread(
            Stress6ExceptionPublishWorker,
            &FatalContext
        );
        Stress6WaitForFlag(&FatalContext.Ready, (void*)0x6095);
        InterlockedStoreRelease(&FatalContext.Start, true);

        Stress6RequireStatus(
            MsWaitForSingleObject(
                &Target->InternalThread.Header,
                KernelMode,
                false,
                STRESS6_WAIT_TIMEOUT_MS
            ),
            MT_SUCCESS,
            (void*)0x6096
        );
        Stress6RequireStatus(
            Stress6ReadThreadExitStatus(Target),
            MT_INVALID_STATE,
            (void*)(uintptr_t)Mode
        );
        ObDereferenceObject(Target);
    }

    gop_printf(
        COLOR_GREEN,
        "STRESS 6 exception publication PASS\n"
    );
}

static void
Stress6RunningWorker(
    THREAD_PARAMETER Parameter
)
{
    STRESS6_RUNNING_CONTEXT* Context = Parameter;
    while (!InterlockedLoadAcquire(&Context->Start)) {
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }

    InterlockedStoreRelease(&Context->Ready, true);
    while (!InterlockedLoadAcquire(&Context->RequestSelfSuspend)) {
        InterlockedIncrementU64(&Context->Progress);
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }

    uint32_t PreviousCount = UINT32_MAX;
    MTSTATUS Status = MtSuspendThread(
        MtCurrentThread(),
        &PreviousCount
    );
    InterlockedStoreRelease(&Context->PreviousCount, PreviousCount);
    InterlockedStoreRelease(&Context->SuspendStatus, Status);
    InterlockedStoreRelease(&Context->ReturnedFromSuspend, true);

    while (!InterlockedLoadAcquire(&Context->Stop)) {
        InterlockedIncrementU64(&Context->Progress);
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }
}

static void
Stress6SleepWorker(
    THREAD_PARAMETER Parameter
)
{
    STRESS6_SLEEP_CONTEXT* Context = Parameter;
    while (!InterlockedLoadAcquire(&Context->Start)) {
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }

    InterlockedStoreRelease(&Context->Ready, true);
    MTSTATUS Status = MsDelayExecution(
        KernelMode,
        false,
        STRESS6_SLEEP_TICKS * TICK_MS
    );
    InterlockedStoreRelease(&Context->Status, Status);
    InterlockedStoreRelease(&Context->Returned, true);
}

static void
Stress6EventWorker(
    THREAD_PARAMETER Parameter
)
{
    STRESS6_EVENT_CONTEXT* Context = Parameter;
    while (!InterlockedLoadAcquire(&Context->Start)) {
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }

    InterlockedStoreRelease(&Context->Ready, true);
    MTSTATUS Status = MsWaitForSingleObject(
        Context->Event,
        KernelMode,
        false,
        MT_INFINITE
    );
    InterlockedStoreRelease(&Context->Status, Status);
    InterlockedStoreRelease(&Context->Returned, true);
}

static void
Stress6RaceWorker(
    THREAD_PARAMETER Parameter
)
{
    STRESS6_RACE_CONTEXT* Context = Parameter;
    InterlockedStoreRelease(&Context->Ready, true);
    while (!InterlockedLoadAcquire(&Context->Start)) {
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }

    uint32_t PreviousCount = 0;
    MTSTATUS SuspendStatus = MtSuspendThread(
        Context->Handle,
        &PreviousCount
    );
    InterlockedStoreRelease(&Context->SuspendStatus, SuspendStatus);

    MTSTATUS ResumeStatus = MT_PENDING;
    if (SuspendStatus == MT_SUCCESS) {
        ResumeStatus = MtResumeThread(Context->Handle, &PreviousCount);
    }
    InterlockedStoreRelease(&Context->ResumeStatus, ResumeStatus);
}

static void
Stress6TestReadyTarget(void)
{
    PETHREAD Target = NULL;
    STRESS6_READY_CONTEXT Context = { 0 };

    for (uint32_t Attempt = 0;
         Attempt < STRESS6_READY_ATTEMPTS;
         Attempt++) {
        Context = (STRESS6_READY_CONTEXT){ 0 };
        Target = Stress6CreateRetainedThread(
            Stress6ReadyWorker,
            &Context
        );

        if (InterlockedLoadAcquire(
                &Target->InternalThread.ThreadState
            ) == THREAD_READY &&
            InterlockedLoadAcquire(
                &Target->InternalThread.ActiveProcessor
            ) == NULL) {
            break;
        }

        InterlockedStoreRelease(&Context.Stop, true);
        InterlockedStoreRelease(&Context.Start, true);
        Stress6JoinThread(Target, (void*)(uintptr_t)Attempt);
        Target = NULL;
    }

    if (!Target) {
        Stress6BugCheck(
            Stress6ReadyStateUnavailable,
            (void*)(uintptr_t)STRESS6_READY_ATTEMPTS,
            NULL
        );
    }

    // PsTerminateThread is an internal remote-termination path for user
    // threads. Kernel workers must cooperate with their own shutdown.
    Stress6RequireStatus(
        PsTerminateThread(Target, STRESS6_TERMINATION_STATUS),
        MT_ACCESS_DENIED,
        (void*)0x600F
    );

    uint32_t PreviousCount = UINT32_MAX;
    Stress6RequireStatus(
        MeSuspendThread(&Target->InternalThread, &PreviousCount),
        MT_SUCCESS,
        (void*)0x6010
    );
    if (PreviousCount != 0) {
        Stress6BugCheck(
            Stress6SuspendCountFailure,
            (void*)0x6010,
            (void*)(uintptr_t)PreviousCount
        );
    }

    Stress6WaitForSuspended(Target, (void*)0x6011);
    if (InterlockedLoadAcquire(&Context.Entered)) {
        Stress6BugCheck(
            Stress6WorkerStateFailure,
            (void*)0x6011,
            Target
        );
    }

    Stress6RequireStatus(
        MeResumeThread(&Target->InternalThread, &PreviousCount),
        MT_SUCCESS,
        (void*)0x6012
    );
    if (PreviousCount != 1) {
        Stress6BugCheck(
            Stress6SuspendCountFailure,
            (void*)0x6012,
            (void*)(uintptr_t)PreviousCount
        );
    }

    InterlockedStoreRelease(&Context.Start, true);
    Stress6WaitForFlag(&Context.Entered, (void*)0x6013);
    InterlockedStoreRelease(&Context.Stop, true);
    Stress6JoinThread(Target, (void*)0x6014);
    gop_printf(COLOR_GREEN, "STRESS 6 READY target PASS\n");
}

static void
Stress6TestReadyMigration(void)
{
    uint32_t ProcessorCount = MeGetActiveProcessorCount();
    if (ProcessorCount < 2) {
        gop_printf(
            COLOR_GREEN,
            "STRESS 6 READY migration SKIP (requires SMP)\n"
        );
        return;
    }

    PETHREAD Target = NULL;
    PPROCESSOR Source = NULL;
    PPROCESSOR Destination = NULL;
    STRESS6_MIGRATION_CONTEXT Context = { 0 };

    for (uint32_t Attempt = 0;
         Attempt < STRESS6_READY_ATTEMPTS;
         Attempt++) {
        Context = (STRESS6_MIGRATION_CONTEXT){
            .ProcessorId = UINT32_MAX
        };
        Target = Stress6CreateRetainedThread(
            Stress6MigrationWorker,
            &Context
        );

        // Pin this controller to its current CPU while the migrated worker is
        // waiting to be selected. This makes its first-dispatch CPU observable.
        IRQL OldIrql;
        MeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        Source = MeGetCurrentProcessor();
        Destination = MeGetProcessorBlock(
            (uint8_t)((Source->ID + 1U) % ProcessorCount)
        );

        if (MepMigrateReadyThread(Target, Source, Source)) {
            MeLowerIrql(OldIrql);
            InterlockedStoreRelease(&Context.Stop, true);
            Stress6JoinThread(Target, (void*)0x60A0);
            Stress6BugCheck(
                Stress6ReadyMigrationFailure,
                (void*)0x60A0,
                Target
            );
        }

        if (!MepMigrateReadyThread(Target, Source, Destination)) {
            MeLowerIrql(OldIrql);
            InterlockedStoreRelease(&Context.Stop, true);
            Stress6JoinThread(Target, (void*)(uintptr_t)Attempt);
            Target = NULL;
            continue;
        }

        // Source cannot schedule while held at DISPATCH_LEVEL. A successful
        // migration therefore has to execute first on another processor.
        uint64_t StartTsc = __rdtsc();
        while (!InterlockedLoadAcquire(&Context.Entered) &&
               !Stress6WatchdogExpired(StartTsc)) {
            __pause();
        }
        MeLowerIrql(OldIrql);

        if (!InterlockedLoadAcquire(&Context.Entered)) {
            InterlockedStoreRelease(&Context.Stop, true);
            Stress6JoinThread(Target, (void*)0x60A1);
            Stress6BugCheck(
                Stress6ReadyMigrationFailure,
                (void*)0x60A1,
                Destination
            );
        }

        break;
    }

    if (!Target) {
        Stress6BugCheck(
            Stress6ReadyMigrationUnavailable,
            (void*)(uintptr_t)STRESS6_READY_ATTEMPTS,
            Source
        );
    }

    uint32_t ObservedProcessor = InterlockedLoadAcquire(
        &Context.ProcessorId
    );
    bool WrongProcessor = ObservedProcessor == Source->ID;
    if (ProcessorCount == 2) {
        WrongProcessor = ObservedProcessor != Destination->ID;
    }

    if (WrongProcessor) {
        InterlockedStoreRelease(&Context.Stop, true);
        Stress6JoinThread(Target, (void*)0x60A2);
        Stress6BugCheck(
            Stress6ReadyMigrationFailure,
            (void*)(uintptr_t)(
                ((uint64_t)Source->ID << 32) |
                Destination->ID
            ),
            (void*)(uintptr_t)ObservedProcessor
        );
    }

    InterlockedStoreRelease(&Context.Stop, true);
    Stress6JoinThread(Target, (void*)0x60A3);
    gop_printf(
        COLOR_GREEN,
        "STRESS 6 READY migration PASS (CPU %u -> CPU %u, first CPU %u)\n",
        Source->ID,
        Destination->ID,
        ObservedProcessor
    );
}

static void
Stress6TestRunningAndNested(void)
{
    STRESS6_RUNNING_CONTEXT Context = {
        .SuspendStatus = MT_PENDING,
        .PreviousCount = UINT32_MAX
    };
    PETHREAD Target = Stress6CreateRetainedThread(
        Stress6RunningWorker,
        &Context
    );
    HANDLE Handle = Stress6CreateThreadHandle(
        Target,
        MT_SYNCHRONIZE | MT_THREAD_SUSPEND_RESUME,
        (void*)0x6020
    );
    HANDLE DeniedHandle = Stress6CreateThreadHandle(
        Target,
        MT_SYNCHRONIZE,
        (void*)0x6021
    );

    Stress6RequireStatus(
        MtSuspendThread(DeniedHandle, NULL),
        MT_ACCESS_DENIED,
        (void*)0x6022
    );
    Stress6RequireStatus(
        MtResumeThread(DeniedHandle, NULL),
        MT_ACCESS_DENIED,
        (void*)0x6023
    );
    Stress6RequireStatus(
        MtClose(DeniedHandle),
        MT_SUCCESS,
        (void*)0x6024
    );
    Stress6RequireStatus(
        MtSuspendThread(DeniedHandle, NULL),
        MT_INVALID_HANDLE,
        (void*)0x6025
    );

    InterlockedStoreRelease(&Context.Start, true);
    Stress6WaitForFlag(&Context.Ready, (void*)0x6026);
    Stress6WaitForProgress(&Context.Progress, 0, (void*)0x6027);
    InterlockedStoreRelease(&Context.RequestSelfSuspend, true);
    Stress6WaitForSuspended(Target, (void*)0x6028);

    uint32_t PreviousCount = UINT32_MAX;
    Stress6RequireStatus(
        MtSuspendThread(Handle, &PreviousCount),
        MT_SUCCESS,
        (void*)0x6029
    );
    if (PreviousCount != 1 || Stress6ReadSuspendCount(Target) != 2) {
        Stress6BugCheck(
            Stress6SuspendCountFailure,
            (void*)(uintptr_t)PreviousCount,
            (void*)(uintptr_t)Stress6ReadSuspendCount(Target)
        );
    }

    uint64_t FrozenProgress = InterlockedLoadAcquire(&Context.Progress);
    Stress6WaitTicks(STRESS6_STABLE_TICKS, (void*)0x602A);
    if (InterlockedLoadAcquire(&Context.Progress) != FrozenProgress) {
        Stress6BugCheck(
            Stress6ProgressWhileSuspended,
            (void*)(uintptr_t)FrozenProgress,
            (void*)(uintptr_t)InterlockedLoadAcquire(&Context.Progress)
        );
    }

    Stress6RequireStatus(
        MtResumeThread(Handle, &PreviousCount),
        MT_SUCCESS,
        (void*)0x602B
    );
    if (PreviousCount != 2 || Stress6ReadSuspendCount(Target) != 1) {
        Stress6BugCheck(
            Stress6SuspendCountFailure,
            (void*)(uintptr_t)PreviousCount,
            (void*)(uintptr_t)Stress6ReadSuspendCount(Target)
        );
    }

    Stress6WaitTicks(STRESS6_STABLE_TICKS, (void*)0x602C);
    if (InterlockedLoadAcquire(&Context.Progress) != FrozenProgress ||
        InterlockedLoadAcquire(&Context.ReturnedFromSuspend)) {
        Stress6BugCheck(
            Stress6ProgressWhileSuspended,
            (void*)(uintptr_t)FrozenProgress,
            (void*)(uintptr_t)InterlockedLoadAcquire(&Context.Progress)
        );
    }

    Stress6RequireStatus(
        MtResumeThread(Handle, &PreviousCount),
        MT_SUCCESS,
        (void*)0x602D
    );
    if (PreviousCount != 1) {
        Stress6BugCheck(
            Stress6SuspendCountFailure,
            (void*)0x602D,
            (void*)(uintptr_t)PreviousCount
        );
    }

    Stress6WaitForFlag(&Context.ReturnedFromSuspend, (void*)0x602E);
    Stress6RequireStatus(
        InterlockedLoadAcquire(&Context.SuspendStatus),
        MT_SUCCESS,
        (void*)0x602F
    );
    if (InterlockedLoadAcquire(&Context.PreviousCount) != 0) {
        Stress6BugCheck(
            Stress6SuspendCountFailure,
            (void*)0x602F,
            (void*)(uintptr_t)InterlockedLoadAcquire(
                &Context.PreviousCount
            )
        );
    }
    Stress6WaitForProgress(
        &Context.Progress,
        FrozenProgress,
        (void*)0x6030
    );

    Stress6RequireStatus(
        MtResumeThread(Handle, &PreviousCount),
        MT_SUCCESS,
        (void*)0x6031
    );
    if (PreviousCount != 0) {
        Stress6BugCheck(
            Stress6SuspendCountFailure,
            (void*)0x6031,
            (void*)(uintptr_t)PreviousCount
        );
    }

    InterlockedStoreRelease(&Context.Stop, true);
    Stress6JoinThread(Target, (void*)0x6032);
    Stress6RequireStatus(MtClose(Handle), MT_SUCCESS, (void*)0x6033);
    gop_printf(
        COLOR_GREEN,
        "STRESS 6 RUNNING/nested/zero-CPU PASS\n"
    );
}

static void
Stress6TestSleepingTarget(void)
{
    STRESS6_SLEEP_CONTEXT Context = { .Status = MT_PENDING };
    PETHREAD Target = Stress6CreateRetainedThread(
        Stress6SleepWorker,
        &Context
    );
    HANDLE Handle = Stress6CreateThreadHandle(
        Target,
        MT_THREAD_SUSPEND_RESUME,
        (void*)0x6040
    );

    InterlockedStoreRelease(&Context.Start, true);
    Stress6WaitForFlag(&Context.Ready, (void*)0x6041);
    Stress6WaitForSleep(Target, (void*)0x6042);

    uint32_t PreviousCount = UINT32_MAX;
    Stress6RequireStatus(
        MtSuspendThread(Handle, &PreviousCount),
        MT_SUCCESS,
        (void*)0x6043
    );
    if (PreviousCount != 0) {
        Stress6BugCheck(
            Stress6SuspendCountFailure,
            (void*)0x6043,
            (void*)(uintptr_t)PreviousCount
        );
    }

    // The timer first completes the sleep. Before the worker can return from
    // MsDelayExecution, its pending suspend APC must run and park it on the private gate.
    Stress6WaitForSuspended(Target, (void*)0x6044);
    if (InterlockedLoadAcquire(&Context.Returned)) {
        Stress6BugCheck(
            Stress6WorkerStateFailure,
            (void*)0x6044,
            Target
        );
    }

    Stress6RequireStatus(
        MtResumeThread(Handle, &PreviousCount),
        MT_SUCCESS,
        (void*)0x6045
    );
    Stress6WaitForFlag(&Context.Returned, (void*)0x6046);
    Stress6RequireStatus(
        InterlockedLoadAcquire(&Context.Status),
        MT_SUCCESS,
        (void*)0x6047
    );
    Stress6JoinThread(Target, (void*)0x6048);
    Stress6RequireStatus(MtClose(Handle), MT_SUCCESS, (void*)0x6049);
    gop_printf(COLOR_GREEN, "STRESS 6 sleeping target PASS\n");
}

static void
Stress6TestSuspendApcReuse(void)
{
    STRESS6_READY_CONTEXT Context = { 0 };
    PETHREAD Target = Stress6CreateRetainedThread(
        Stress6ReadyWorker,
        &Context
    );
    HANDLE Handle = Stress6CreateThreadHandle(
        Target,
        MT_THREAD_SUSPEND_RESUME,
        (void*)0x6034
    );

    InterlockedStoreRelease(&Context.Start, true);
    Stress6WaitForFlag(&Context.Entered, (void*)0x6035);
    Stress6WaitForProgress(&Context.Progress, 0, (void*)0x6036);

    for (uint32_t Round = 0;
         Round < STRESS6_APC_REUSE_ROUNDS;
         Round++) {
        uint32_t PreviousCount = UINT32_MAX;

        /*
         * Resume immediately after the first suspend, then suspend again
         * before waiting for the target. Depending on scheduling, the same APC
         * may still be queued, waking from its semaphore, or finishing. Every
         * state must result in exactly one active suspension invocation.
         */
        Stress6RequireStatus(
            MtSuspendThread(Handle, &PreviousCount),
            MT_SUCCESS,
            (void*)(uintptr_t)(0x6200U + Round)
        );
        if (PreviousCount != 0) {
            Stress6BugCheck(
                Stress6SuspendCountFailure,
                (void*)(uintptr_t)(0x6200U + Round),
                (void*)(uintptr_t)PreviousCount
            );
        }

        Stress6RequireStatus(
            MtResumeThread(Handle, &PreviousCount),
            MT_SUCCESS,
            (void*)(uintptr_t)(0x6240U + Round)
        );
        if (PreviousCount != 1) {
            Stress6BugCheck(
                Stress6SuspendCountFailure,
                (void*)(uintptr_t)(0x6240U + Round),
                (void*)(uintptr_t)PreviousCount
            );
        }

        Stress6RequireStatus(
            MtSuspendThread(Handle, &PreviousCount),
            MT_SUCCESS,
            (void*)(uintptr_t)(0x6280U + Round)
        );
        if (PreviousCount != 0) {
            Stress6BugCheck(
                Stress6SuspendCountFailure,
                (void*)(uintptr_t)(0x6280U + Round),
                (void*)(uintptr_t)PreviousCount
            );
        }

        Stress6WaitForSuspended(
            Target,
            (void*)(uintptr_t)(0x62C0U + Round)
        );
        uint64_t FrozenProgress = InterlockedLoadAcquire(&Context.Progress);
        Stress6WaitTicks(STRESS6_STABLE_TICKS, (void*)(uintptr_t)Round);
        if (InterlockedLoadAcquire(&Context.Progress) != FrozenProgress) {
            Stress6BugCheck(
                Stress6ProgressWhileSuspended,
                (void*)(uintptr_t)FrozenProgress,
                (void*)(uintptr_t)InterlockedLoadAcquire(&Context.Progress)
            );
        }

        Stress6RequireStatus(
            MtResumeThread(Handle, &PreviousCount),
            MT_SUCCESS,
            (void*)(uintptr_t)(0x6300U + Round)
        );
        if (PreviousCount != 1) {
            Stress6BugCheck(
                Stress6SuspendCountFailure,
                (void*)(uintptr_t)(0x6300U + Round),
                (void*)(uintptr_t)PreviousCount
            );
        }

        Stress6WaitForProgress(
            &Context.Progress,
            FrozenProgress,
            (void*)(uintptr_t)(0x6340U + Round)
        );
    }

    InterlockedStoreRelease(&Context.Stop, true);
    Stress6JoinThread(Target, (void*)0x6037);
    Stress6RequireStatus(MtClose(Handle), MT_SUCCESS, (void*)0x6038);
    gop_printf(
        COLOR_GREEN,
        "STRESS 6 suspend APC reuse PASS (%u rounds)\n",
        STRESS6_APC_REUSE_ROUNDS
    );
}

static void
Stress6TestDispatcherWaitingTarget(void)
{
    EVENT Event;
    MsInitializeEvent(&Event, DispatcherSynchronizationEvent, false);
    STRESS6_EVENT_CONTEXT Context = {
        .Event = &Event,
        .Status = MT_PENDING
    };
    PETHREAD Target = Stress6CreateRetainedThread(
        Stress6EventWorker,
        &Context
    );
    HANDLE Handle = Stress6CreateThreadHandle(
        Target,
        MT_THREAD_SUSPEND_RESUME,
        (void*)0x6050
    );

    InterlockedStoreRelease(&Context.Start, true);
    Stress6WaitForFlag(&Context.Ready, (void*)0x6051);
    Stress6WaitForDispatcherWait(
        Target,
        &Event.Header,
        (void*)0x6052
    );

    uint32_t PreviousCount = UINT32_MAX;
    Stress6RequireStatus(
        MtSuspendThread(Handle, &PreviousCount),
        MT_SUCCESS,
        (void*)0x6053
    );
    Stress6RequireStatus(MsSetEvent(&Event), MT_SUCCESS, (void*)0x6054);
    Stress6WaitForSuspended(Target, (void*)0x6055);

    if (InterlockedLoadAcquire(&Context.Returned)) {
        Stress6BugCheck(
            Stress6WorkerStateFailure,
            (void*)0x6055,
            Target
        );
    }

    Stress6RequireStatus(
        MtResumeThread(Handle, &PreviousCount),
        MT_SUCCESS,
        (void*)0x6056
    );
    Stress6WaitForFlag(&Context.Returned, (void*)0x6057);
    Stress6RequireStatus(
        InterlockedLoadAcquire(&Context.Status),
        MT_SUCCESS,
        (void*)0x6058
    );
    Stress6JoinThread(Target, (void*)0x6059);
    Stress6RequireStatus(MtClose(Handle), MT_SUCCESS, (void*)0x605A);
    gop_printf(COLOR_GREEN, "STRESS 6 dispatcher-wait target PASS\n");
}

static void
Stress6TestTerminationRundown(void)
{
    uint32_t ThreadObjects = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsThreadType->TotalNumberOfObjects
    );
    uint32_t ThreadHandles = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsThreadType->TotalNumberOfHandles
    );

    // First terminate a sleeping user thread while its suspend APC is queued.
    // Termination rundown must remove that ordinary kernel APC before the
    // pending user termination APC is delivered on the syscall return path.
    STRESS6_USER_TARGET QueuedTarget = Stress6CreateUserTarget((void*)0x6060);
    Stress6WaitForSleep(QueuedTarget.Thread, (void*)0x6061);
    uint32_t PreviousCount = UINT32_MAX;
    Stress6RequireStatus(
        MeSuspendThread(
            &QueuedTarget.Thread->InternalThread,
            &PreviousCount
        ),
        MT_SUCCESS,
        (void*)0x6062
    );

    IRQL OldIrql;
    MsAcquireSpinlock(
        &QueuedTarget.Thread->InternalThread.ApcQueueLock,
        &OldIrql
    );
    bool QueuedSuspend =
        QueuedTarget.Thread->InternalThread.SuspendCount == 1 &&
        QueuedTarget.Thread->InternalThread.SuspendAPC.Inserted != 0;
    MsReleaseSpinlock(
        &QueuedTarget.Thread->InternalThread.ApcQueueLock,
        OldIrql
    );
    if (!QueuedSuspend) {
        Stress6BugCheck(
            Stress6SuspendStateFailure,
            (void*)0x6062,
            QueuedTarget.Thread
        );
    }

    Stress6RequireStatus(
        PsTerminateThread(
            QueuedTarget.Thread,
            STRESS6_TERMINATION_STATUS
        ),
        MT_SUCCESS,
        (void*)0x6063
    );
    Stress6JoinUserTarget(&QueuedTarget, (void*)0x6064);
    Stress6WaitForThreadTypeCounts(
        ThreadObjects,
        ThreadHandles,
        0x6051
    );

    // Then let the suspend APC run and block the user thread on its private
    // semaphore. Termination must force that wait to finish, return through the
    // interrupted syscall, and retire the user termination APC before CPL3.
    STRESS6_USER_TARGET SuspendedTarget =
        Stress6CreateUserTarget((void*)0x6065);
    Stress6WaitForSleep(SuspendedTarget.Thread, (void*)0x6066);
    Stress6RequireStatus(
        MeSuspendThread(
            &SuspendedTarget.Thread->InternalThread,
            &PreviousCount
        ),
        MT_SUCCESS,
        (void*)0x6067
    );
    Stress6WaitForSuspended(SuspendedTarget.Thread, (void*)0x6068);
    Stress6RequireStatus(
        PsTerminateThread(
            SuspendedTarget.Thread,
            STRESS6_TERMINATION_STATUS
        ),
        MT_SUCCESS,
        (void*)0x6069
    );
    Stress6JoinUserTarget(&SuspendedTarget, (void*)0x606A);
    Stress6WaitForThreadTypeCounts(
        ThreadObjects,
        ThreadHandles,
        0x6052
    );

    gop_printf(
        COLOR_GREEN,
        "STRESS 6 termination rundown PASS (queued and delivered APC)\n"
    );
}

static void
Stress6TestTerminationRaces(void)
{
    for (uint32_t Round = 0; Round < STRESS6_RACE_ROUNDS; Round++) {
        STRESS6_USER_TARGET Target = Stress6CreateUserTarget(
            (void*)(uintptr_t)(0x6100U + Round)
        );
        Stress6WaitForSleep(
            Target.Thread,
            (void*)(uintptr_t)(0x6120U + Round)
        );
        HANDLE Handle = Stress6CreateThreadHandle(
            Target.Thread,
            MT_THREAD_SUSPEND_RESUME,
            (void*)(uintptr_t)Round
        );

        STRESS6_RACE_CONTEXT Context = {
            .Handle = Handle,
            .SuspendStatus = MT_PENDING,
            .ResumeStatus = MT_PENDING
        };
        PETHREAD Racer = Stress6CreateRetainedThread(
            Stress6RaceWorker,
            &Context
        );
        Stress6WaitForFlag(&Context.Ready, (void*)(uintptr_t)Round);
        InterlockedStoreRelease(&Context.Start, true);

        // Odd rounds allow the suspend/resume caller to run first. Even rounds
        // favor termination. SMP adds true overlap between both operations.
        if (Round & 1U) {
            MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
        }

        Stress6RequireStatus(
            PsTerminateThread(
                Target.Thread,
                STRESS6_TERMINATION_STATUS
            ),
            MT_SUCCESS,
            (void*)(uintptr_t)(0x6140U + Round)
        );
        Stress6JoinThread(Racer, (void*)(uintptr_t)(0x6160U + Round));

        MTSTATUS SuspendStatus = InterlockedLoadAcquire(
            &Context.SuspendStatus
        );
        MTSTATUS ResumeStatus = InterlockedLoadAcquire(
            &Context.ResumeStatus
        );
        bool Valid =
            (SuspendStatus == MT_THREAD_IS_TERMINATING &&
                ResumeStatus == MT_PENDING) ||
            (SuspendStatus == MT_SUCCESS &&
                (ResumeStatus == MT_SUCCESS ||
                 ResumeStatus == MT_THREAD_IS_TERMINATING));
        if (!Valid) {
            Stress6BugCheck(
                Stress6RaceOutcomeFailure,
                (void*)(uintptr_t)SuspendStatus,
                (void*)(uintptr_t)ResumeStatus
            );
        }

        Stress6RequireStatus(
            MtClose(Handle),
            MT_SUCCESS,
            (void*)(uintptr_t)(0x6180U + Round)
        );
        Stress6JoinUserTarget(
            &Target,
            (void*)(uintptr_t)(0x61A0U + Round)
        );
    }

    gop_printf(
        COLOR_GREEN,
        "STRESS 6 suspend/resume/termination races PASS (%u rounds)\n",
        STRESS6_RACE_ROUNDS
    );
}

static void
Stress6Controller(void)
{
    Stress5DSettleObjectCounts();
    uint32_t ThreadObjects = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsThreadType->TotalNumberOfObjects
    );
    uint32_t ThreadHandles = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsThreadType->TotalNumberOfHandles
    );
    uint32_t ProcessObjects = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsProcessType->TotalNumberOfObjects
    );
    uint32_t ProcessHandles = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsProcessType->TotalNumberOfHandles
    );

    gop_printf(
        COLOR_GREEN,
        "STRESS 6 START (suspend/resume, %u CPUs)\n",
        MeGetActiveProcessorCount()
    );

    Stress6TestExceptionPublication();
    Stress6WaitForThreadTypeCounts(ThreadObjects, ThreadHandles, 0x609);
    Stress6TestReadyMigration();
    Stress6WaitForThreadTypeCounts(ThreadObjects, ThreadHandles, 0x60A);
    Stress6TestReadyTarget();
    Stress6WaitForThreadTypeCounts(ThreadObjects, ThreadHandles, 0x601);
    Stress6TestRunningAndNested();
    Stress6WaitForThreadTypeCounts(ThreadObjects, ThreadHandles, 0x602);
    Stress6TestSuspendApcReuse();
    Stress6WaitForThreadTypeCounts(ThreadObjects, ThreadHandles, 0x608);
    Stress6TestSleepingTarget();
    Stress6WaitForThreadTypeCounts(ThreadObjects, ThreadHandles, 0x603);
    Stress6TestDispatcherWaitingTarget();
    Stress6WaitForThreadTypeCounts(ThreadObjects, ThreadHandles, 0x604);
    Stress6TestTerminationRundown();
    Stress6WaitForThreadTypeCounts(ThreadObjects, ThreadHandles, 0x605);
    Stress5DWaitForTypeCounts(
        PsProcessType,
        ProcessObjects,
        ProcessHandles
    );
    Stress6TestTerminationRaces();

    Stress6WaitForThreadTypeCounts(ThreadObjects, ThreadHandles, 0x606);
    Stress5DWaitForTypeCounts(
        PsProcessType,
        ProcessObjects,
        ProcessHandles
    );
    Stress6TestUserApcContinue();
    Stress6WaitForThreadTypeCounts(ThreadObjects, ThreadHandles, 0x607);
    Stress5DWaitForTypeCounts(
        PsProcessType,
        ProcessObjects,
        ProcessHandles
    );
    gop_printf(COLOR_GREEN, "STRESS 6 PASS\n");
}

#define STRESS7_WORKER_COUNT       8U
#define STRESS7_SEMAPHORE_LIMIT    4
#define STRESS7_WATCHDOG_SECONDS   10ULL
#define STRESS7_JOIN_TIMEOUT_MS    30000ULL
#define STRESS7_PROGRESS_SECONDS   60ULL

typedef enum _STRESS7_OPERATION {
    Stress7OperationMutex = 0,
    Stress7OperationSemaphoreWait,
    Stress7OperationSynchronizationEventWait,
    Stress7OperationNotificationEventWait,
    Stress7OperationSleep,
    Stress7OperationYield,
    Stress7OperationQueueDpc,
    Stress7OperationSignal
} STRESS7_OPERATION;

typedef enum _STRESS7_FAILURE {
    Stress7UnexpectedStatus = 1,
    Stress7WorkerStall,
    Stress7DpcStall,
    Stress7DpcContextFailure,
    Stress7MutexExclusionFailure,
    Stress7MutexStateFailure,
    Stress7DispatcherStateFailure,
    Stress7ThreadReferenceFailure,
    Stress7ThreadJoinFailure,
    Stress7ObjectCountLeak,
    Stress7ClockFailure
} STRESS7_FAILURE;

typedef struct _STRESS7_WORKER_CONTEXT {
    uint32_t Index;
    volatile bool Start;
    volatile uint64_t Progress;
    volatile uint32_t LastOperation;
    volatile MTSTATUS LastStatus;
    uint64_t RandomState;
    PETHREAD Thread;
} STRESS7_WORKER_CONTEXT;

typedef struct _STRESS7_DPC_CONTEXT {
    DPC Dpc;
    uint32_t TargetCpu;
    volatile uint64_t Accepted;
    volatile uint64_t Executed;
} STRESS7_DPC_CONTEXT;

static EVENT Stress7SynchronizationEvent;
static EVENT Stress7NotificationEvent;
static SEMAPHORE Stress7Semaphore;
static MUTEX Stress7Mutex;
static STRESS7_WORKER_CONTEXT Stress7Workers[STRESS7_WORKER_COUNT];
static STRESS7_DPC_CONTEXT Stress7Dpcs[MAX_CPUS];
static volatile bool Stress7Active;
static volatile uint32_t Stress7InsideMutex;
static volatile uint64_t Stress7ProtectedCounter;

static bool
Stress7ListEntryIsLinked(
    PDOUBLY_LINKED_LIST Entry
)
{
    return Entry->Flink != NULL &&
        Entry->Blink != NULL &&
        Entry->Flink != Entry &&
        Entry->Blink != Entry;
}

NORETURN
static void
Stress7BugCheck(
    STRESS7_FAILURE Failure,
    STRESS7_WORKER_CONTEXT* Context,
    PDISPATCHER_HEADER Header,
    MTSTATUS Status
)
{
    PITHREAD Thread = Context && Context->Thread
        ? &Context->Thread->InternalThread
        : MeGetCurrentThread();
    uint32_t Worker = Context ? Context->Index : UINT8_MAX;
    uint32_t Operation = Context
        ? InterlockedLoadAcquire(&Context->LastOperation)
        : UINT8_MAX;
    uint64_t Location =
        ((uint64_t)0x70U) |
        ((uint64_t)Failure << 8) |
        ((uint64_t)(Worker & UINT8_MAX) << 24) |
        ((uint64_t)(Operation & UINT8_MAX) << 32) |
        ((uint64_t)(MeGetCurrentProcessor()->ID & UINT8_MAX) << 40) |
        ((uint64_t)(MeGetCurrentIrql() & UINT8_MAX) << 48);

    uint64_t WaitState = (uint32_t)Status;
    uint64_t ObjectState = 0;
    if (Thread) {
        WaitState |=
            ((uint64_t)(Thread->ThreadState & UINT8_MAX) << 32) |
            ((uint64_t)(Thread->WaitBlock.WaitReason & UINT8_MAX) << 40) |
            ((uint64_t)Stress7ListEntryIsLinked(
                &Thread->WaitBlock.ObjectListEntry
            ) << 48) |
            ((uint64_t)Stress7ListEntryIsLinked(
                &Thread->WaitBlock.TimerListEntry
            ) << 49);
    }
    if (Header) {
        ObjectState =
            (uint32_t)Header->SignalState |
            ((uint64_t)(Header->Type & UINT8_MAX) << 32) |
            ((uint64_t)!IsListEmpty(&Header->WaitListHead) << 40);
    }

    // P1 packs test, invariant, worker, operation, CPU, and IRQL. P2 is the
    // affected thread. P3 packs status, thread/wait state, and both queue
    // memberships. P4 packs dispatcher signal/type/wait-list state.
    MeBugCheckEx(
        WAIT_STATE_FAILURE,
        (void*)(uintptr_t)Location,
        Thread,
        (void*)(uintptr_t)WaitState,
        (void*)(uintptr_t)ObjectState
    );
}

static uint64_t
Stress7NextRandom(
    uint64_t* State
)
{
    uint64_t Value = *State;
    if (Value == 0) {
        Value = 0x9E3779B97F4A7C15ULL;
    }
    Value ^= Value >> 12;
    Value ^= Value << 25;
    Value ^= Value >> 27;
    *State = Value;
    return Value * 0x2545F4914F6CDD1DULL;
}

static uint64_t
Stress7RandomTimeout(
    uint64_t Random
)
{
    return ((Random >> 8) & 3ULL) * TICK_MS;
}

static void
Stress7RequireWaitResult(
    STRESS7_WORKER_CONTEXT* Context,
    PDISPATCHER_HEADER Header,
    MTSTATUS Status
)
{
    InterlockedStoreRelease(&Context->LastStatus, Status);
    if (Status != MT_SUCCESS && Status != MT_TIMEOUT) {
        Stress7BugCheck(
            Stress7UnexpectedStatus,
            Context,
            Header,
            Status
        );
    }
}

static void
Stress7DpcRoutine(
    DPC* Dpc,
    void* DeferredContext,
    void* SystemArgument1,
    void* SystemArgument2
)
{
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    STRESS7_DPC_CONTEXT* Context = DeferredContext;
    if (!Context ||
        Dpc != &Context->Dpc ||
        MeGetCurrentProcessor()->ID != Context->TargetCpu ||
        MeGetCurrentIrql() != DISPATCH_LEVEL ||
        !MeAreInterruptsEnabled()) {
        Stress7BugCheck(
            Stress7DpcContextFailure,
            NULL,
            NULL,
            (MTSTATUS)(Context ? Context->TargetCpu : UINT32_MAX)
        );
    }

    InterlockedIncrementU64(&Context->Executed);
}

static void
Stress7QueueRandomDpc(
    STRESS7_WORKER_CONTEXT* Worker,
    uint64_t Random
)
{
    uint32_t ProcessorCount = MeGetActiveProcessorCount();
    uint32_t Target = (uint32_t)(Random % ProcessorCount);
    STRESS7_DPC_CONTEXT* Context = &Stress7Dpcs[Target];

    if (MeInsertQueueDpc(&Context->Dpc, Worker, (void*)(uintptr_t)Random)) {
        // The callback can execute before this increment. Only the final
        // accepted/executed equality is significant.
        InterlockedIncrementU64(&Context->Accepted);
    }
}

static void
Stress7Worker(
    THREAD_PARAMETER Parameter
)
{
    STRESS7_WORKER_CONTEXT* Context = Parameter;
    while (!InterlockedLoadAcquire(&Context->Start)) {
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }

    while (InterlockedLoadAcquire(&Stress7Active)) {
        uint64_t Random = Stress7NextRandom(&Context->RandomState);
        STRESS7_OPERATION Operation =
            (STRESS7_OPERATION)(Random % 8ULL);
        uint64_t Timeout = Stress7RandomTimeout(Random);
        MTSTATUS Status = MT_SUCCESS;

        InterlockedStoreRelease(
            &Context->LastOperation,
            (uint32_t)Operation
        );

        switch (Operation) {
        case Stress7OperationMutex: {
            Status = MsWaitForSingleObject(
                &Stress7Mutex,
                KernelMode,
                false,
                Timeout
            );
            Stress7RequireWaitResult(
                Context,
                &Stress7Mutex.Header,
                Status
            );
            if (Status == MT_SUCCESS) {
                uint32_t Inside = InterlockedIncrementU32(
                    &Stress7InsideMutex
                );
                if (Inside != 1) {
                    Stress7BugCheck(
                        Stress7MutexExclusionFailure,
                        Context,
                        &Stress7Mutex.Header,
                        (MTSTATUS)Inside
                    );
                }

                Stress7ProtectedCounter++;
                if ((Random & 0x100ULL) != 0) {
                    MsYieldExecution(
                        &MeGetCurrentThread()->TrapRegisters
                    );
                }

                Inside = InterlockedDecrementU32(
                    &Stress7InsideMutex
                );
                if (Inside != 0) {
                    Stress7BugCheck(
                        Stress7MutexExclusionFailure,
                        Context,
                        &Stress7Mutex.Header,
                        (MTSTATUS)Inside
                    );
                }

                Status = MsReleaseMutexObject(&Stress7Mutex);
                if (Status != MT_SUCCESS) {
                    Stress7BugCheck(
                        Stress7UnexpectedStatus,
                        Context,
                        &Stress7Mutex.Header,
                        Status
                    );
                }
            }
            break;
        }

        case Stress7OperationSemaphoreWait:
            Status = MsWaitForSingleObject(
                &Stress7Semaphore,
                KernelMode,
                false,
                Timeout
            );
            Stress7RequireWaitResult(
                Context,
                &Stress7Semaphore.Header,
                Status
            );
            break;

        case Stress7OperationSynchronizationEventWait:
            Status = MsWaitForSingleObject(
                &Stress7SynchronizationEvent,
                KernelMode,
                false,
                Timeout
            );
            Stress7RequireWaitResult(
                Context,
                &Stress7SynchronizationEvent.Header,
                Status
            );
            break;

        case Stress7OperationNotificationEventWait:
            Status = MsWaitForSingleObject(
                &Stress7NotificationEvent,
                KernelMode,
                false,
                Timeout
            );
            Stress7RequireWaitResult(
                Context,
                &Stress7NotificationEvent.Header,
                Status
            );
            break;

        case Stress7OperationSleep:
            Status = MsDelayExecution(KernelMode, false, Timeout);
            if (Status != MT_SUCCESS) {
                Stress7BugCheck(
                    Stress7UnexpectedStatus,
                    Context,
                    NULL,
                    Status
                );
            }
            break;

        case Stress7OperationYield:
            MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
            break;

        case Stress7OperationQueueDpc:
            Stress7QueueRandomDpc(Context, Random);
            break;

        case Stress7OperationSignal:
            switch ((Random >> 12) & 3ULL) {
            case 0:
                Status = MsSetEvent(&Stress7SynchronizationEvent);
                break;
            case 1:
                Status = MsSetEvent(&Stress7NotificationEvent);
                break;
            case 2:
                MsResetEvent(&Stress7NotificationEvent);
                Status = MT_SUCCESS;
                break;
            default:
                Status = MsReleaseSemaphoreChecked(
                    &Stress7Semaphore,
                    1,
                    NULL
                );
                if (Status == MT_SEMAPHORE_LIMIT_EXCEEDED) {
                    Status = MT_SUCCESS;
                }
                break;
            }
            if (Status != MT_SUCCESS) {
                Stress7BugCheck(
                    Stress7UnexpectedStatus,
                    Context,
                    NULL,
                    Status
                );
            }
            break;
        }

        InterlockedIncrementU64(&Context->Progress);
    }
}

static PETHREAD
Stress7CreateRetainedThread(
    STRESS7_WORKER_CONTEXT* Context
)
{
    PETHREAD Thread = StressSuiteCreateThread(
        Stress7Worker,
        Context
    );
    if (!ObReferenceObject(Thread)) {
        Stress7BugCheck(
            Stress7ThreadReferenceFailure,
            Context,
            NULL,
            MT_GENERAL_FAILURE
        );
    }
    return Thread;
}

static void
Stress7JoinThread(
    STRESS7_WORKER_CONTEXT* Context
)
{
    MTSTATUS Status = MsWaitForSingleObject(
        &Context->Thread->InternalThread.Header,
        KernelMode,
        false,
        STRESS7_JOIN_TIMEOUT_MS
    );
    if (Status != MT_SUCCESS) {
        Stress7BugCheck(
            Stress7ThreadJoinFailure,
            Context,
            &Context->Thread->InternalThread.Header,
            Status
        );
    }

    ObDereferenceObject(Context->Thread);
    Context->Thread = NULL;
}

static void
Stress7ValidateDispatcher(
    PDISPATCHER_HEADER Header,
    int32_t MinimumSignal,
    int32_t MaximumSignal
)
{
    IRQL OldIrql;
    MsAcquireSpinlock(&Header->Lock, &OldIrql);
    bool Valid = Header->SignalState >= MinimumSignal &&
        Header->SignalState <= MaximumSignal &&
        IsListEmpty(&Header->WaitListHead);
    MsReleaseSpinlock(&Header->Lock, OldIrql);

    if (!Valid) {
        Stress7BugCheck(
            Stress7DispatcherStateFailure,
            NULL,
            Header,
            MT_INVALID_STATE
        );
    }
}

static void
Stress7Controller(void)
{
    uint32_t ProcessorCount = MeGetActiveProcessorCount();
    uint32_t ThreadObjects = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsThreadType->TotalNumberOfObjects
    );
    uint32_t ThreadHandles = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsThreadType->TotalNumberOfHandles
    );
    uint64_t LastProgress[STRESS7_WORKER_COUNT] = { 0 };
    uint64_t LastProgressTsc[STRESS7_WORKER_COUNT] = { 0 };
    uint64_t LastDpcAccepted[MAX_CPUS] = { 0 };
    uint64_t LastDpcExecuted[MAX_CPUS] = { 0 };
    uint64_t LastDpcProgressTsc[MAX_CPUS] = { 0 };
    uint64_t RandomState = __rdtsc() ^
        ((uint64_t)ProcessorCount << 48) ^
        0xD1B54A32D192ED03ULL;

    gop_printf(
        COLOR_GREEN,
        "STRESS 7 START (randomized synchronization, %u CPUs, %u seconds)\n",
        ProcessorCount,
        (unsigned int)MT_STRESS_DURATION_SECONDS
    );

    MsInitializeEvent(
        &Stress7SynchronizationEvent,
        DispatcherSynchronizationEvent,
        false
    );
    MsInitializeEvent(
        &Stress7NotificationEvent,
        DispatcherNotificationEvent,
        false
    );
    MsInitializeSemaphore(
        &Stress7Semaphore,
        0,
        STRESS7_SEMAPHORE_LIMIT
    );
    if (MsInitializeMutexObject(&Stress7Mutex) != MT_SUCCESS) {
        Stress7BugCheck(
            Stress7MutexStateFailure,
            NULL,
            &Stress7Mutex.Header,
            MT_INVALID_STATE
        );
    }

    Stress7InsideMutex = 0;
    Stress7ProtectedCounter = 0;
    Stress7Active = false;

    for (uint32_t Cpu = 0; Cpu < ProcessorCount; Cpu++) {
        STRESS7_DPC_CONTEXT* Context = &Stress7Dpcs[Cpu];
        Context->TargetCpu = Cpu;
        Context->Accepted = 0;
        Context->Executed = 0;
        MeInitializeDpc(
            &Context->Dpc,
            Stress7DpcRoutine,
            Context,
            MEDIUM_PRIORITY
        );
        MeSetTargetProcessorDpc(&Context->Dpc, Cpu);
    }

    for (uint32_t Index = 0; Index < STRESS7_WORKER_COUNT; Index++) {
        STRESS7_WORKER_CONTEXT* Context = &Stress7Workers[Index];
        kmemset(Context, 0, sizeof(*Context));
        Context->Index = Index;
        Context->LastOperation = Stress7OperationYield;
        Context->LastStatus = MT_PENDING;
        Context->RandomState = RandomState ^
            ((uint64_t)(Index + 1U) * 0x9E3779B97F4A7C15ULL);
        Context->Thread = Stress7CreateRetainedThread(Context);
    }

    uint64_t StartTsc = __rdtsc();
    uint64_t DurationCycles =
        Stress2CTscTicksPerSecond * MT_STRESS_DURATION_SECONDS;
    uint64_t WatchdogCycles =
        Stress2CTscTicksPerSecond * STRESS7_WATCHDOG_SECONDS;
    uint64_t NextProgressTsc =
        StartTsc + Stress2CTscTicksPerSecond * STRESS7_PROGRESS_SECONDS;

    for (uint32_t Index = 0; Index < STRESS7_WORKER_COUNT; Index++) {
        LastProgressTsc[Index] = StartTsc;
    }
    for (uint32_t Cpu = 0; Cpu < ProcessorCount; Cpu++) {
        LastDpcProgressTsc[Cpu] = StartTsc;
    }

    InterlockedStoreRelease(&Stress7Active, true);
    for (uint32_t Index = 0; Index < STRESS7_WORKER_COUNT; Index++) {
        InterlockedStoreRelease(&Stress7Workers[Index].Start, true);
    }

    for (;;) {
        uint64_t Now = __rdtsc();
        if (Now < StartTsc) {
            Stress7BugCheck(
                Stress7ClockFailure,
                NULL,
                NULL,
                MT_INVALID_STATE
            );
        }
        if (Now - StartTsc >= DurationCycles) {
            break;
        }

        uint64_t Random = Stress7NextRandom(&RandomState);
        MTSTATUS Status;
        switch (Random & 3ULL) {
        case 0:
            Status = MsSetEvent(&Stress7SynchronizationEvent);
            break;
        case 1:
            Status = MsSetEvent(&Stress7NotificationEvent);
            break;
        case 2:
            MsResetEvent(&Stress7NotificationEvent);
            Status = MT_SUCCESS;
            break;
        default:
            Status = MsReleaseSemaphoreChecked(
                &Stress7Semaphore,
                (int32_t)(((Random >> 8) & 1ULL) + 1ULL),
                NULL
            );
            if (Status == MT_SEMAPHORE_LIMIT_EXCEEDED) {
                Status = MT_SUCCESS;
            }
            break;
        }
        if (Status != MT_SUCCESS) {
            Stress7BugCheck(
                Stress7UnexpectedStatus,
                NULL,
                NULL,
                Status
            );
        }

        if ((Random & 0x10ULL) != 0) {
            Stress7QueueRandomDpc(NULL, Random >> 16);
        }

        for (uint32_t Index = 0;
             Index < STRESS7_WORKER_COUNT;
             Index++) {
            STRESS7_WORKER_CONTEXT* Context = &Stress7Workers[Index];
            uint64_t Progress = InterlockedLoadAcquire(
                &Context->Progress
            );
            if (Progress != LastProgress[Index]) {
                LastProgress[Index] = Progress;
                LastProgressTsc[Index] = Now;
            }
            else if (Now - LastProgressTsc[Index] > WatchdogCycles) {
                Stress7BugCheck(
                    Stress7WorkerStall,
                    Context,
                    (PDISPATCHER_HEADER)
                        Context->Thread->InternalThread.WaitBlock.Object,
                    Context->Thread->InternalThread.WaitStatus
                );
            }
        }

        for (uint32_t Cpu = 0; Cpu < ProcessorCount; Cpu++) {
            STRESS7_DPC_CONTEXT* Context = &Stress7Dpcs[Cpu];
            uint64_t Accepted = InterlockedLoadAcquire(
                &Context->Accepted
            );
            uint64_t Executed = InterlockedLoadAcquire(
                &Context->Executed
            );

            if (Accepted == Executed ||
                Accepted != LastDpcAccepted[Cpu] ||
                Executed != LastDpcExecuted[Cpu]) {
                LastDpcAccepted[Cpu] = Accepted;
                LastDpcExecuted[Cpu] = Executed;
                LastDpcProgressTsc[Cpu] = Now;
            }
            else if (Now - LastDpcProgressTsc[Cpu] > WatchdogCycles) {
                Stress7BugCheck(
                    Stress7DpcStall,
                    NULL,
                    NULL,
                    (MTSTATUS)Cpu
                );
            }
        }

        if (Now >= NextProgressTsc) {
            uint64_t TotalProgress = 0;
            for (uint32_t Index = 0;
                 Index < STRESS7_WORKER_COUNT;
                 Index++) {
                TotalProgress += InterlockedLoadAcquire(
                    &Stress7Workers[Index].Progress
                );
            }
            gop_printf(
                COLOR_GREEN,
                "STRESS 7 progress %llu/%u seconds (%llu operations)\n",
                (unsigned long long)(
                    (Now - StartTsc) / Stress2CTscTicksPerSecond
                ),
                (unsigned int)MT_STRESS_DURATION_SECONDS,
                (unsigned long long)TotalProgress
            );
            NextProgressTsc +=
                Stress2CTscTicksPerSecond * STRESS7_PROGRESS_SECONDS;
        }

        Status = MsDelayExecution(KernelMode, false, TICK_MS);
        if (Status != MT_SUCCESS) {
            Stress7BugCheck(
                Stress7UnexpectedStatus,
                NULL,
                NULL,
                Status
            );
        }
    }

    InterlockedStoreRelease(&Stress7Active, false);
    MsSetEvent(&Stress7NotificationEvent);
    MsSetEvent(&Stress7SynchronizationEvent);

    for (uint32_t Index = 0; Index < STRESS7_WORKER_COUNT; Index++) {
        Stress7JoinThread(&Stress7Workers[Index]);
    }

    uint64_t DrainStart = __rdtsc();
    for (;;) {
        bool Drained = true;
        for (uint32_t Cpu = 0; Cpu < ProcessorCount; Cpu++) {
            STRESS7_DPC_CONTEXT* Context = &Stress7Dpcs[Cpu];
            uint64_t Accepted = InterlockedLoadAcquire(
                &Context->Accepted
            );
            uint64_t Executed = InterlockedLoadAcquire(
                &Context->Executed
            );
            if (Accepted != Executed ||
                InterlockedLoadAcquire(&Context->Dpc.DpcData) != NULL) {
                Drained = false;
            }
        }
        if (Drained) {
            break;
        }
        if (__rdtsc() - DrainStart > WatchdogCycles) {
            Stress7BugCheck(
                Stress7DpcStall,
                NULL,
                NULL,
                MT_TIMEOUT
            );
        }
        MsYieldExecution(&MeGetCurrentThread()->TrapRegisters);
    }

    Stress7ValidateDispatcher(
        &Stress7SynchronizationEvent.Header,
        0,
        1
    );
    Stress7ValidateDispatcher(
        &Stress7NotificationEvent.Header,
        0,
        1
    );
    Stress7ValidateDispatcher(
        &Stress7Semaphore.Header,
        0,
        STRESS7_SEMAPHORE_LIMIT
    );
    Stress7ValidateDispatcher(&Stress7Mutex.Header, 1, 1);

    if (Stress7Mutex.OwnerThread != NULL ||
        Stress7InsideMutex != 0 ||
        !IsListEmpty(&Stress7Mutex.OwnerListEntry)) {
        Stress7BugCheck(
            Stress7MutexStateFailure,
            NULL,
            &Stress7Mutex.Header,
            MT_INVALID_STATE
        );
    }

    Stress5DSettleObjectCounts();
    Stress6WaitForThreadTypeCounts(
        ThreadObjects,
        ThreadHandles,
        0x700
    );

    uint64_t TotalProgress = 0;
    uint64_t TotalDpcs = 0;
    for (uint32_t Index = 0; Index < STRESS7_WORKER_COUNT; Index++) {
        TotalProgress += InterlockedLoadAcquire(
            &Stress7Workers[Index].Progress
        );
    }
    for (uint32_t Cpu = 0; Cpu < ProcessorCount; Cpu++) {
        TotalDpcs += InterlockedLoadAcquire(
            &Stress7Dpcs[Cpu].Executed
        );
    }

    gop_printf(
        COLOR_GREEN,
        "STRESS 7 PASS (%llu operations, %llu DPCs, %llu mutex entries)\n",
        (unsigned long long)TotalProgress,
        (unsigned long long)TotalDpcs,
        (unsigned long long)Stress7ProtectedCounter
    );
}

typedef enum _STRESS_EXCEPTION_CHAIN_FAILURE {
    StressExceptionChainCreateProcess = 1,
    StressExceptionChainReferenceProcess,
    StressExceptionChainResolveApcRoutine,
    StressExceptionChainAllocateApc,
    StressExceptionChainWaitProcess,
    StressExceptionChainQueryProcess,
    StressExceptionChainUnexpectedExit,
    StressExceptionChainCloseHandle,
    StressExceptionChainProcessStall,
    StressExceptionChainReferenceWatchdog
} STRESS_EXCEPTION_CHAIN_FAILURE;

NORETURN
static void
StressExceptionChainBugCheck(
    STRESS_EXCEPTION_CHAIN_FAILURE Failure,
    MTSTATUS Status,
    void* Detail
)
{
    // P1 identifies the exception-chain suite, P2 is the failed stage, and
    // P3/P4 carry the returned status and stage-specific detail.
    MeBugCheckEx(
        WAIT_STATE_FAILURE,
        (void*)(uintptr_t)0x64,
        (void*)(uintptr_t)Failure,
        (void*)(uintptr_t)Status,
        Detail
    );
}

static bool
StressExceptionChainProcessSignaled(
    PEPROCESS Process
)
{
    IRQL OldIrql;
    MsAcquireSpinlock(&Process->InternalProcess.Header.Lock, &OldIrql);
    bool Signaled = Process->InternalProcess.Header.SignalState != 0;
    MsReleaseSpinlock(&Process->InternalProcess.Header.Lock, OldIrql);
    return Signaled;
}

static uintptr_t
StressExceptionChainResolveUserApcAddress(
    PEPROCESS Process
)
{
    PETHREAD Thread = PsGetNextProcessThread(Process, NULL);
    if (!Thread) {
        return 0;
    }

    APC_STATE ApcState;
    MeAttachProcess(&Process->InternalProcess, &ApcState);
    uintptr_t ApcRoutine = PspFindMtdllEntryAddress(
        "MtpExceptionStressApc",
        Thread
    );
    MeDetachProcess(&ApcState);
    ObDereferenceObject(Thread);
    return ApcRoutine;
}

static uint32_t
StressExceptionChainQueueUserApcs(
    PEPROCESS Process,
    uintptr_t ApcRoutine
)
{
    uint32_t InsertedCount = 0;
    PETHREAD Thread = PsGetNextProcessThread(Process, NULL);

    while (Thread) {
        PITHREAD IThread = &Thread->InternalThread;
        bool CanQueue = !Thread->SystemThread &&
            InterlockedLoadAcquire(&Thread->TerminationState) ==
                ThreadTerminationNone &&
            !InterlockedLoadAcquire(&IThread->ApcState.UserApcPending) &&
            !IThread->UserApcActive;

        if (CanQueue) {
            PAPC Apc = MmAllocatePoolWithTag(
                NonPagedPool,
                sizeof(*Apc),
                'ecpA'
            );
            if (!Apc) {
                StressExceptionChainBugCheck(
                    StressExceptionChainAllocateApc,
                    MT_NO_MEMORY,
                    Thread
                );
            }

            // The test-only no-op isolates the APC dispatcher and MtContinue
            // round trip from blocking syscall and timer-queue behavior.
            MeInitializeApc(
                Apc,
                IThread,
                UserMode,
                NULL,
                Stress6UserApcRundown,
                (PNORMAL_ROUTINE)ApcRoutine,
                NULL
            );

            if (MeInsertQueueApc(Apc, NULL, NULL)) {
                InsertedCount++;
            }
            else {
                MmFreePool(Apc);
            }
        }

        // This transfers the safe process-list reference to the next entry.
        Thread = PsGetNextProcessThread(Process, Thread);
    }

    return InsertedCount;
}

#if MT_STRESS_AUTOMATION
static void
StressAutomationWriteText(
    const char* Text
);

typedef enum _STRESS_EXCEPTION_CHAIN_STAGE {
    StressExceptionStageStarting = 0,
    StressExceptionStageCreateProcess,
    StressExceptionStageResolveApc,
    StressExceptionStagePollProcess,
    StressExceptionStageQueueApc,
    StressExceptionStageYield,
    StressExceptionStageWaitProcess,
    StressExceptionStageQueryProcess,
    StressExceptionStageCloseProcess,
    StressExceptionStageReclaimThread,
    StressExceptionStageReclaimProcess,
    StressExceptionStageComplete
} STRESS_EXCEPTION_CHAIN_STAGE;

static volatile uint32_t StressExceptionChainStage;
static volatile bool StressExceptionChainWatchdogActive;

static void
StressExceptionChainWatchdog(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);

    while (InterlockedLoadAcquire(&StressExceptionChainWatchdogActive)) {
        switch ((STRESS_EXCEPTION_CHAIN_STAGE)InterlockedLoadAcquire(
            &StressExceptionChainStage
        )) {
        case StressExceptionStageCreateProcess:
            StressAutomationWriteText("MT-EXCEPTION STAGE CREATE\n");
            break;
        case StressExceptionStageResolveApc:
            StressAutomationWriteText("MT-EXCEPTION STAGE RESOLVE\n");
            break;
        case StressExceptionStagePollProcess:
            StressAutomationWriteText("MT-EXCEPTION STAGE POLL\n");
            break;
        case StressExceptionStageQueueApc:
            StressAutomationWriteText("MT-EXCEPTION STAGE QUEUE-APC\n");
            break;
        case StressExceptionStageYield:
            StressAutomationWriteText("MT-EXCEPTION STAGE YIELD\n");
            break;
        case StressExceptionStageWaitProcess:
            StressAutomationWriteText("MT-EXCEPTION STAGE WAIT-PROCESS\n");
            break;
        case StressExceptionStageQueryProcess:
            StressAutomationWriteText("MT-EXCEPTION STAGE QUERY\n");
            break;
        case StressExceptionStageCloseProcess:
            StressAutomationWriteText("MT-EXCEPTION STAGE CLOSE\n");
            break;
        case StressExceptionStageReclaimThread:
            StressAutomationWriteText("MT-EXCEPTION STAGE RECLAIM-THREAD\n");
            break;
        case StressExceptionStageReclaimProcess:
            StressAutomationWriteText("MT-EXCEPTION STAGE RECLAIM-PROCESS\n");
            break;
        case StressExceptionStageComplete:
            StressAutomationWriteText("MT-EXCEPTION STAGE COMPLETE\n");
            break;
        default:
            StressAutomationWriteText("MT-EXCEPTION STAGE STARTING\n");
            break;
        }

        MsDelayExecution(KernelMode, false, 1000);
    }
}
#endif

static void
StressExceptionChainController(
    void
)
{
#if MT_STRESS_AUTOMATION
    StressAutomationWriteText("MT-EXCEPTION CAMPAIGN START\n");
    InterlockedStoreRelease(
        &StressExceptionChainStage,
        StressExceptionStageStarting
    );
    InterlockedStoreRelease(&StressExceptionChainWatchdogActive, true);
    PETHREAD Watchdog = StressSuiteCreateThread(
        StressExceptionChainWatchdog,
        NULL
    );
    if (!ObReferenceObject(Watchdog)) {
        StressExceptionChainBugCheck(
            StressExceptionChainReferenceWatchdog,
            MT_OBJECT_DELETED,
            Watchdog
        );
    }
#endif

    Stress5DSettleObjectCounts();
    uint32_t ThreadObjects = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsThreadType->TotalNumberOfObjects
    );
    uint32_t ThreadHandles = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsThreadType->TotalNumberOfHandles
    );
    uint32_t ProcessObjects = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsProcessType->TotalNumberOfObjects
    );
    uint32_t ProcessHandles = InterlockedLoadAcquire(
        (volatile uint32_t*)&PsProcessType->TotalNumberOfHandles
    );

    Stress2CalibrateTsc();
    uint64_t CampaignStart = __rdtsc();
    uint64_t CampaignCycles = Stress2CTscTicksPerSecond *
        MT_STRESS_DURATION_SECONDS;
    uint64_t ProcessWatchdogCycles = Stress2CTscTicksPerSecond * 30ULL;
    uint64_t NextProgress = CampaignStart + Stress2CTscTicksPerSecond * 60ULL;
    uint64_t RandomState = CampaignStart ^
        ((uint64_t)MeGetActiveProcessorCount() << 48) ^
        0xA0761D6478BD642FULL;
    uint64_t TotalApcs = 0;
    uint32_t Iteration = 0;

    do {
#if MT_STRESS_AUTOMATION
        InterlockedStoreRelease(
            &StressExceptionChainStage,
            StressExceptionStageCreateProcess
        );
#endif
        HANDLE ProcessHandle = MT_INVALID_HANDLE;
        MTSTATUS Status = PsCreateProcess(
            "terminateMyself.mtexe",
            &ProcessHandle,
            MT_PROCESS_ALL_ACCESS,
            0
        );
        if (Status != MT_SUCCESS) {
            StressExceptionChainBugCheck(
                StressExceptionChainCreateProcess,
                Status,
                (void*)(uintptr_t)Iteration
            );
        }

        PEPROCESS Process = NULL;
        Status = ObReferenceObjectByHandle(
            ProcessHandle,
            MT_PROCESS_ALL_ACCESS,
            PsProcessType,
            (void**)&Process,
            NULL
        );
        if (Status != MT_SUCCESS) {
            StressExceptionChainBugCheck(
                StressExceptionChainReferenceProcess,
                Status,
                (void*)(uintptr_t)ProcessHandle
            );
        }

#if MT_STRESS_AUTOMATION
        InterlockedStoreRelease(
            &StressExceptionChainStage,
            StressExceptionStageResolveApc
        );
#endif
        uintptr_t ApcRoutine = StressExceptionChainResolveUserApcAddress(
            Process
        );
        if (!ApcRoutine || ApcRoutine > MmHighestUserAddress) {
            StressExceptionChainBugCheck(
                StressExceptionChainResolveApcRoutine,
                MT_INVALID_ADDRESS,
                (void*)ApcRoutine
            );
        }

        uint64_t ProcessStart = __rdtsc();
        while (!StressExceptionChainProcessSignaled(Process)) {
#if MT_STRESS_AUTOMATION
            InterlockedStoreRelease(
                &StressExceptionChainStage,
                StressExceptionStagePollProcess
            );
#endif
            uint64_t Random = Stress7NextRandom(&RandomState);
            if ((Random & 3ULL) != 0) {
#if MT_STRESS_AUTOMATION
                InterlockedStoreRelease(
                    &StressExceptionChainStage,
                    StressExceptionStageQueueApc
                );
#endif
                TotalApcs += StressExceptionChainQueueUserApcs(
                    Process,
                    ApcRoutine
                );
            }

            if (__rdtsc() - ProcessStart >= ProcessWatchdogCycles) {
                StressExceptionChainBugCheck(
                    StressExceptionChainProcessStall,
                    MT_TIMEOUT,
                    (void*)(uintptr_t)Iteration
                );
            }

#if MT_STRESS_AUTOMATION
            InterlockedStoreRelease(
                &StressExceptionChainStage,
                StressExceptionStageYield
            );
#endif
            MsDelayExecution(
                KernelMode,
                false,
                (Random >> 8) & 1ULL
            );
        }

#if MT_STRESS_AUTOMATION
        InterlockedStoreRelease(
            &StressExceptionChainStage,
            StressExceptionStageWaitProcess
        );
#endif
        Status = MtWaitForSingleObject(
            ProcessHandle,
            STRESS6_WAIT_TIMEOUT_MS,
            false
        );
        if (Status != MT_SUCCESS) {
            StressExceptionChainBugCheck(
                StressExceptionChainWaitProcess,
                Status,
                (void*)(uintptr_t)ProcessHandle
            );
        }

#if MT_STRESS_AUTOMATION
        InterlockedStoreRelease(
            &StressExceptionChainStage,
            StressExceptionStageQueryProcess
        );
#endif
        PROCESS_BASIC_INFORMATION Information = { 0 };
        uint32_t ReturnLength = 0;
        Status = MtQueryInformationProcess(
            ProcessHandle,
            ProcessBasicInformation,
            &Information,
            sizeof(Information),
            &ReturnLength
        );
        if (Status != MT_SUCCESS || ReturnLength != sizeof(Information)) {
            StressExceptionChainBugCheck(
                StressExceptionChainQueryProcess,
                Status,
                (void*)(uintptr_t)ReturnLength
            );
        }
        if (Information.ExitStatus != MT_SUCCESS) {
            StressExceptionChainBugCheck(
                StressExceptionChainUnexpectedExit,
                Information.ExitStatus,
                (void*)(uintptr_t)ProcessHandle
            );
        }

#if MT_STRESS_AUTOMATION
        InterlockedStoreRelease(
            &StressExceptionChainStage,
            StressExceptionStageCloseProcess
        );
#endif
        ObDereferenceObject(Process);

        Status = MtClose(ProcessHandle);
        if (Status != MT_SUCCESS) {
            StressExceptionChainBugCheck(
                StressExceptionChainCloseHandle,
                Status,
                (void*)(uintptr_t)ProcessHandle
            );
        }

        // Every handled and unhandled exception worker has exited by this
        // point. APC rundown and object/handle reclamation must also finish.
#if MT_STRESS_AUTOMATION
        InterlockedStoreRelease(
            &StressExceptionChainStage,
            StressExceptionStageReclaimThread
        );
#endif
        Stress5DWaitForTypeCounts(PsThreadType, ThreadObjects, ThreadHandles);
#if MT_STRESS_AUTOMATION
        InterlockedStoreRelease(
            &StressExceptionChainStage,
            StressExceptionStageReclaimProcess
        );
#endif
        Stress5DWaitForTypeCounts(PsProcessType, ProcessObjects, ProcessHandles);

        Iteration++;
        uint64_t Now = __rdtsc();
        if (MT_STRESS_DURATION_SECONDS > 1 && Now >= NextProgress) {
            gop_printf(
                COLOR_GREEN,
                "EXCEPTION CAMPAIGN %llu/%u seconds "
                "(%u processes, %llu APCs)\n",
                (unsigned long long)(
                    (Now - CampaignStart) / Stress2CTscTicksPerSecond
                ),
                (unsigned int)MT_STRESS_DURATION_SECONDS,
                Iteration,
                (unsigned long long)TotalApcs
            );
#if MT_STRESS_AUTOMATION
            StressAutomationWriteText("MT-EXCEPTION CAMPAIGN HEARTBEAT\n");
#endif
            NextProgress = Now + Stress2CTscTicksPerSecond * 60ULL;
        }
    } while (MT_STRESS_DURATION_SECONDS > 1 &&
        __rdtsc() - CampaignStart < CampaignCycles);

#if MT_STRESS_AUTOMATION
    InterlockedStoreRelease(
        &StressExceptionChainStage,
        StressExceptionStageComplete
    );
    InterlockedStoreRelease(&StressExceptionChainWatchdogActive, false);
    Stress6JoinThread(Watchdog, (void*)0x64F0);
#endif

    gop_printf(
        COLOR_GREEN,
        "EXCEPTION CHAIN TEST PASS (%u processes, %llu APCs)\n",
        Iteration,
        (unsigned long long)TotalApcs
    );
}

#if MT_STRESS_AUTOMATION
#define MT_AUTOMATION_DEBUG_PORT 0x402
#define MT_AUTOMATION_EXIT_PORT  0xF4
#define MT_AUTOMATION_EXIT_PASS  0x10

static void
StressAutomationWriteText(
    const char* Text
)
{
    while (*Text != '\0') {
        __outbyte(MT_AUTOMATION_DEBUG_PORT, (uint8_t)*Text++);
    }
}

NORETURN
static void
StressAutomationPass(
    const char* Mode
)
{
    StressAutomationWriteText("MT-STRESS PASS ");
    StressAutomationWriteText(Mode);
    StressAutomationWriteText("\n");

    /*
     * isa-debug-exit terminates the complete VM, so no AP shutdown protocol is
     * needed here. A synchronous STOP broadcast after the pass marker can race
     * with an AP that still owns another CPU's mailbox and report a false test
     * failure even though every stress phase already completed successfully.
     */
    __cli();
    __outdword(MT_AUTOMATION_EXIT_PORT, MT_AUTOMATION_EXIT_PASS);
    for (;;) {
        __hlt();
    }
}
#endif

NORETURN
static void
StressSuiteController(
    THREAD_PARAMETER Parameter
)
{
    UNREFERENCED_PARAMETER(Parameter);

#ifdef DEBUG
    ExpTestContextConversion();
    ExpTestExceptionRecordConstruction();
    ExpTestUserExceptionPublication();
#endif

    gop_printf(
        COLOR_GREEN,
        "STRESS SUITE START (%u CPUs)\n",
        MeGetActiveProcessorCount()
    );

#if MT_STRESS_MODE == MT_STRESS_MODE_COLD_BOOT
    gop_printf(COLOR_GREEN, "STRESS COLD BOOT PASS\n");
    StressAutomationPass("COLD-BOOT");
#elif MT_STRESS_MODE == MT_STRESS_MODE_RANDOMIZED
    Stress7Controller();
    StressAutomationPass("RANDOMIZED");
#elif MT_STRESS_MODE == MT_STRESS_MODE_EXCEPTION_CHAIN
    StressExceptionChainController();
    StressAutomationPass("EXCEPTION-CHAIN");
#else
#if STRESS_GATE4_ONLY
    Stress6Controller();
#else
#if MT_STRESS_AUTOMATION
    StressAutomationWriteText("MT-STRESS PHASE STRESS2\n");
#endif
    StressSuiteRunStress2();

#if MT_STRESS_AUTOMATION
    StressAutomationWriteText("MT-STRESS PHASE STRESS3\n");
#endif
    StressSuiteInitializeEvent(
        &Stress3Event,
        DispatcherSynchronizationEvent
    );
    Stress3Active = true;
    Stress3WaiterThread = StressSuiteCreateThread(Stress3Waiter, NULL);
    Stress3Controller(NULL);
    InterlockedStoreRelease(&Stress3Active, false);

#if MT_STRESS_AUTOMATION
    StressAutomationWriteText("MT-STRESS PHASE STRESS4\n");
#endif
    Stress4CompletionCount = 0;
    Stress4LateWakeCount = 0;
    Stress4MaximumOvershoot = 0;
    Stress4PublishHeartbeat();
    Stress4WatchdogActive = true;
    StressSuiteCreateThread(Stress4Watchdog, NULL);
    Stress4Controller(NULL);
    InterlockedStoreRelease(&Stress4WatchdogActive, false);

    StressSuiteInitializeEvent(
        &Stress4BSynchronizationEvent,
        DispatcherSynchronizationEvent
    );
    StressSuiteInitializeEvent(
        &Stress4BNotificationEvent,
        DispatcherNotificationEvent
    );
    Stress4BActive = true;
    for (uint32_t Index = 0; Index < STRESS4B_WAITER_COUNT; Index++) {
        Stress4BWaiterThreads[Index] = StressSuiteCreateThread(
            Stress4BWaiter,
            (void*)(uintptr_t)Index
        );
    }
    Stress4BController(NULL);
    InterlockedStoreRelease(&Stress4BActive, false);

    StressSuiteInitializeEvent(
        &Stress4CEvent,
        DispatcherSynchronizationEvent
    );
    Stress4CActive = true;
    Stress4CWaiterThread = StressSuiteCreateThread(Stress4CWaiter, NULL);
    Stress4CController(NULL);
    InterlockedStoreRelease(&Stress4CActive, false);

#if MT_STRESS_AUTOMATION
    StressAutomationWriteText("MT-STRESS PHASE STRESS5A\n");
#endif
    Stress5ATestBankedPermits();
    MsInitializeSemaphore(&Stress5ASemaphore, 0, STRESS5A_WAITER_COUNT);
    Stress5AActive = true;
    for (uint32_t Index = 0; Index < STRESS5A_WAITER_COUNT; Index++) {
        Stress5AWaiterThreads[Index] = StressSuiteCreateThread(
            Stress5AWaiter,
            (void*)(uintptr_t)Index
        );
    }
    Stress5ARaceThread = StressSuiteCreateThread(Stress5ARaceWaiter, NULL);
    Stress5AController(NULL);
    InterlockedStoreRelease(&Stress5AActive, false);

#if MT_STRESS_AUTOMATION
    StressAutomationWriteText("MT-STRESS PHASE STRESS5B\n");
#endif
    Stress5BController();
#if MT_STRESS_AUTOMATION
    StressAutomationWriteText("MT-STRESS PHASE STRESS5C\n");
#endif
    Stress5CController();
#if MT_STRESS_AUTOMATION
    StressAutomationWriteText("MT-STRESS PHASE STRESS5D\n");
#endif
    Stress5DController();
#if MT_STRESS_AUTOMATION
    StressAutomationWriteText("MT-STRESS PHASE STRESS5E\n");
#endif
    Stress5EController();
#if MT_STRESS_AUTOMATION
    StressAutomationWriteText("MT-STRESS PHASE STRESS6\n");
#endif
    Stress6Controller();
#endif

    gop_printf(COLOR_GREEN, "STRESS SUITE ALL PASS\n");

#if MT_STRESS_AUTOMATION
    StressAutomationPass("FULL-SUITE");
#else
    IPI_PARAMS StopParams = { 0 };
    MhSendActionToCpusAndWait(CPU_ACTION_STOP, StopParams);
    __cli();
    for (;;) {
        __hlt();
    }
#endif
#endif
}

/** Remember that paging is on when this is called, as UEFI turned it on. */
__attribute__((noreturn))
void kernel_main(BOOT_INFO* boot_info) {
    // 1. CORE SYSTEM INITIALIZATION
    __writemsr(IA32_GS_BASE, (uint64_t)&cpu0);
    __cli();
    // Zero the BSS.
    size_t len = &bss_end - &bss_start;
    RtlZeroMemory(&bss_start, len);
#ifdef DEBUG
    // No protected C frame may span a change to the global stack guard.
    MiInitializeStackCookie();
#endif
    // Create the local boot struct.
    init_boot_info(boot_info);
    gop_clear_screen(&gop_local, 0); // 0 is just black. (0x0000000)
    // Initialize the global CPU struct.
    MeInitializeProcessor(&cpu0, false, false);
    // Initialize interrupts & exceptions.
    init_interrupts();
    // Initialize the memory manager
    MmInitSystem(SYSTEM_PHASE_INITIALIZE_ALL, boot_info);

    // Initialize the TSS & GDT & New IDT with TSS
    MeInitializeProcessor(&cpu0, true, false);
    // Initialize ACPI after initializing Mm (since page faults will happen on pfn db if not).
    MTSTATUS st = MhInitializeACPI();
    if (MT_FAILURE(st)) {
        gop_printf(COLOR_RED, "InitializeACPI Failure: %x\n", st);
        __hlt();
    }

    // Move all UEFI Pointers to kernel higher half (after physical memory offset)
    // To allow copying PML4 of kernel to processes.
    MiMoveUefiDataToHigherHalf(boot_info);

    // Initialize the object manager subsystem.
    ObInitialize();

    // Initialize the handle table subsystem.
    HtInitializeSystem();

    // Initialize Ps subsystem.
    st = PsInitializeSystem(PS_PHASE_INITIALIZE_SYSTEM);
    if (MT_FAILURE(st)) {
        MeBugCheckEx(PSMGR_INIT_FAILED, (void*)(uintptr_t)st, NULL, NULL, NULL);
    }

    // Initiate section type initializer (must be called after Ob)
    st = MmInitSections();
    if (MT_FAILURE(st)) {
        MeBugCheckEx(
            MANUALLY_INITIATED_CRASH2,
            (void*)(uintptr_t)st,
            NULL,
            NULL,
            NULL
        );
    }

    // Initialize the synchronization manager.
    st = MsInitializeSynchronization();
    if (MT_FAILURE(st)) {
        MeBugCheckEx(MSMGR_INIT_FAILED, (void*)(uintptr_t)st, NULL, NULL, NULL);
    }

    // And, initialize our system process.
    InitSystemProcess();
    _MeSetIrql(PASSIVE_LEVEL);

    /* Initiate Scheduler */
    InitScheduler();

    // The stress harness needs the local timer and all target CPUs online.
    lapic_init_cpu();
    lapic_enable();
    lapic_timer_calibrate();
    MhInitializeTscTimebase();
    Stress2CalibrateTsc();
    int TimerStatus = init_lapic_timer(TICK_HZ);
    if (TimerStatus != 0) {
        MeBugCheckEx(
            INVALID_INITIALIZATION_PHASE,
            (void*)(intptr_t)TimerStatus,
            (void*)(uintptr_t)TICK_HZ,
            NULL,
            NULL
        );
    }

#ifndef MT_UP
    st = MhParseLAPICs(
        (uint8_t*)apic_list,
        MAX_CPUS,
        &cpu_count,
        &lapicAddress
    );
    if (MT_FAILURE(st)) {
        MeBugCheckEx(
            INVALID_INITIALIZATION_PHASE,
            (void*)(uintptr_t)st,
            (void*)apic_list,
            (void*)(uintptr_t)cpu_count,
            (void*)(uintptr_t)lapicAddress
        );
    }

    MhInitializeSMP(apic_list, cpu_count, lapicAddress);
    IPI_PARAMS PrintParams = { 0 };
    MhSendActionToCpusAndWait(CPU_ACTION_PRINT_ID, PrintParams);
    allApsInitialized = true;
#else
    allApsInitialized = true;
#endif

    // Deferred object deletion and kernel-stack reclamation must be online
    // before tests begin creating and closing object-manager handles. The
    // stress controller never returns, so initializing these workers below
    // its Schedule() call leaves zero-reference objects queued forever.
    st = PsInitializeSystem(PS_PHASE_INITIALIZE_WORKER_THREADS);
    if (MT_FAILURE(st)) {
        MeBugCheckEx(PSWORKER_INIT_FAILED, (void*)(uintptr_t)st,
            NULL, NULL, NULL);
    }

    // Stress 5D creates a real user process, so the executable and MTDLL must
    // be reachable through the mounted filesystem before the suite starts.
    st = FsInitialize();
    if (MT_FAILURE(st)) {
        MeBugCheck(FILESYSTEM_PANIC);
    }

    uint32_t StressProcessorCount = MeGetActiveProcessorCount();
    if (StressProcessorCount == 0 || StressProcessorCount > MAX_CPUS) {
        Stress4BugCheck(
            Stress4UnexpectedProcessorCount,
            (void*)(uintptr_t)StressProcessorCount,
            (void*)(uintptr_t)MAX_CPUS,
            NULL
        );
    }

    st = PsCreateSystemThread(
        StressSuiteController,
        NULL,
        DEFAULT_TIMESLICE_TICKS,
        NULL
    );
    if (MT_FAILURE(st)) {
        MeBugCheckEx(PSWORKER_INIT_FAILED, (void*)(uintptr_t)st,
            (void*)StressSuiteController, NULL, NULL);
    }
    Schedule();
    UNREACHABLE_CODE();

    uint64_t rip;
    __asm__ volatile (
        "lea 1f(%%rip), %0\n\t"  // Calculate the address of label 1 relative to RIP
        "1:"                     // The label whose address we want
        : "=r"(rip)              // Output to the 'rip' variable
        );

    gop_printf_forced(0xFFFFFF00, "Current RIP: %p\n", (void*)(uintptr_t)rip);

    if (rip >= KernelVaStart) {
        gop_printf_forced(0x00FF00FF, "**[+] Running in higher-half**\n");
    }
    else {
        gop_printf_forced(0xFF0000FF, "[-] Still identity-mapped\n");
    }

    /* SYSTEM IS FULLY INITIALIZED. (except SMP and APIC) */

    void* buf = MmAllocatePoolWithTag(NonPagedPool, 64, 'buf1');
    gop_printf_forced(0xFFFFFF00, "buf addr: %p\n", buf);
    void* buf2 = MmAllocatePoolWithTag(NonPagedPool, 128, 'buf2');
    gop_printf_forced(0xFFFFFF00, "buf2 addr: %p\n", buf2);
    MmFreePool(buf2);
    void* buf3 = MmAllocatePoolWithTag(NonPagedPool, 128, 'buf3');
    gop_printf_forced(0xFFFFFF00, "buf3 addr (should be same as buf2): %p\n", buf3);
    void* buf4 = MmAllocatePoolWithTag(NonPagedPool, 2048, 'buf4');
    gop_printf_forced(0xFF964B00, "buf4 addr (should reside after buf3, allocated 2048 bytes): %p\n", buf4);
    void* buf5 = MmAllocatePoolWithTag(NonPagedPool, 64, 'buf5');
    gop_printf_forced(0xFF964B00, "buf5 addr (should be a larger addr): %p\n", buf5);
    void* buf6 = MmAllocatePoolWithTag(NonPagedPool, 5000, 'buf6');
    gop_printf_forced(0xFFFFFF00, "buf6 addr (should use dynamic memory): %p\n", buf6);
    void* buf7 = MmAllocatePoolWithTag(NonPagedPool, 10000, 'buf7');
    gop_printf_forced(0xFFFFFF00, "buf7 addr (should use dynamic memory, extremely larger): %p\n", buf7);

    if (checkcpuid()) {
        char str[256];
        getCpuName(str);
        gop_printf(COLOR_GREEN, "CPU Identified: %s\n", str);
    }

    TIME_ENTRY currTime = get_time();
#define ISRAEL_UTC_OFFSET 3
    gop_printf(COLOR_GREEN, "Current Time: (YY:MM:DD:hh:mm:ss) %d/%d/%d | %d:%d:%d\n", currTime.year, currTime.month, currTime.day, currTime.hour + ISRAEL_UTC_OFFSET, currTime.minute, currTime.second);
    /*
    status = PsCreateSystemThread((ThreadEntry)MeCreateInitialUserModeProcess,
        NULL, DEFAULT_TIMESLICE_TICKS, NULL);
    if (MT_FAILURE(status)) {
        MeBugCheckEx(PSWORKER_INIT_FAILED, (void*)(uintptr_t)status,
            (void*)MeCreateInitialUserModeProcess, NULL, NULL);
    }
    */
#ifdef DEBUG
    // Set hardware write breakpoint on the stack chk guard so anybody writing to it would be caught.
    MTSTATUS z = MdSetHardwareBreakpoint(DbgCallback, (void*)&__stack_chk_guard, DEBUG_ACCESS_WRITE, DEBUG_LEN_QWORD);
    assert(MT_SUCCEEDED(z));
#endif

    /*
    DEBUGGING, REMOVE AFTER
    */

    // __sti(); STI Call commented out, this is what caused the scheduler assertion to fail, and guess how much time it took to debug? 2 days
    // Thread creations (including idle threads) must come with the IF flag set.
    Schedule();
    UNREACHABLE_CODE();
}
