/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Core Kernel Entry Point for MatanelOS.
 */

#include "assert.h"
#include "kernel.h"
#include "includes/exception.h"
#include "../tests/kernel/stress.h"
#ifndef _MSC_VER
_Static_assert(sizeof(void*) == 8, "This Kernel is 64 bit only! The 32bit version is deprecated.");
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

static const char EmptyProcessEnvironment[2] = { '\0', '\0' };
static const MT_CREATE_PROCESS_PARAMETERS TerminateMyselfProcessParameters = {
    .Size = sizeof(MT_CREATE_PROCESS_PARAMETERS),
    .Flags = 0,
    .ImagePath = "terminateMyself.mtexe",
    .ImagePathLength = sizeof("terminateMyself.mtexe") - 1,
    .CommandLine = "terminateMyself.mtexe",
    .CommandLineLength = sizeof("terminateMyself.mtexe") - 1,
    .CurrentDirectory = "",
    .CurrentDirectoryLength = 0,
    .Environment = EmptyProcessEnvironment,
    .EnvironmentSize = sizeof(EmptyProcessEnvironment),
    .ParentProcess = 0,
    .DesiredAccess = MT_PROCESS_ALL_ACCESS
};

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

void copy_memory_map(BOOT_INFO* boot_info)

/*++

    Routine description:

        Copies the firmware memory map into kernel-owned storage and redirects the boot information to that copy.

    Arguments:

        [IN] boot_info - Firmware boot information supplied by the loader.

    Return Values:

        None.

--*/

{
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

void copy_gop(BOOT_INFO* boot_info)

/*++

    Routine description:

        Copies the firmware framebuffer parameters into kernel-owned boot state.

    Arguments:

        [IN] boot_info - Firmware boot information supplied by the loader.

    Return Values:

        None.

--*/

{
    if (!boot_info || !boot_info->Gop.FrameBufferBase) return;

    // Copy the GOP data to a local global variable
    gop_local = (boot_info->Gop);

    // Update all relevant pointers to point to the local copy
    boot_info_local.Gop = gop_local;
}


void init_boot_info(BOOT_INFO* boot_info)

/*++

    Routine description:

        Copies the boot information and its referenced firmware data into kernel-owned storage.

    Arguments:

        [IN] boot_info - Firmware boot information supplied by the loader.

    Return Values:

        None.

--*/

{
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

static inline bool interrupts_enabled(void)

/*++

    Routine description:

        Reports whether maskable interrupts are enabled on the current processor.

    Arguments:

        None.

    Return Values:

        A nonzero value when the reported condition holds, or zero otherwise.

--*/

{
    unsigned long flags;
    __asm__ __volatile__("pushfq; popq %0"
        : "=r"(flags)
        :
        : "memory", "cc");
    return (flags & (1UL << 9)) != 0; // IF is bit 9
}

void kernel_idle_checks(void)

/*++

    Routine description:

        Runs scheduler and processor consistency checks from the idle path.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
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
    }
}

static void MeCreateInitialUserModeProcess(void)

/*++

    Routine description:

        Creates and starts the initial user-mode process.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    gop_printf(COLOR_OLIVE, "Starting initial user mode process.\n");
    MT_PROCESS_INFORMATION ProcessInformation = { 0 };
    PETHREAD InitialThread = NULL;
    MTSTATUS status = PsCreateProcess(
        &TerminateMyselfProcessParameters,
        &ProcessInformation,
        &InitialThread
    );
    if (MT_FAILURE(status)) {
        gop_printf(COLOR_RED, "Failed to create initial user process: %x\n", status);
        return;
    }

    PspStartThread(InitialThread);

    status = HtClose(ProcessInformation.ThreadHandle);
    if (MT_FAILURE(status)) {
        gop_printf(COLOR_RED, "Failed to close initial thread handle: %x\n", status);
    }

    // Always free handles, important.
    status = HtClose(ProcessInformation.ProcessHandle);
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

/*++

    Routine description:

        Initializes the kernel stack-protection cookie from boot-time entropy.

    Arguments:

        None.

    Return Values:

        None.

--*/

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
void __stack_chk_fail(void)

/*++

    Routine description:

        Stops the system after the compiler stack protector detects corruption.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
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

static void InitSystemProcess(void)

/*++

    Routine description:

        Initializes the system process and its initial kernel thread.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    // TODO Setup system process like PsCreateProcess, and modify the func.
    kmemset(&PsInitialSystemProcess, 0, sizeof(EPROCESS));
    PsInitialSystemProcess.PID = 4; // Initial PID, reserved.
    PsInitialSystemProcess.ParentProcessPid = 0; // No creator process
    kstrncpy(PsInitialSystemProcess.ImageName, "mtoskrnl.mtexe", sizeof(PsInitialSystemProcess.ImageName)); // Name for the process
    PsInitialSystemProcess.BasePriority = MT_PRIORITY_NORMAL;
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

static void DbgCallback(void* vinfo)

/*++

    Routine description:

        Receives formatted debug output from the formatting library.

    Arguments:

        [IN] vinfo - Formatted debug information supplied by the formatting callback.

    Return Values:

        None.

--*/

{
    DBG_CALLBACK_INFO* info = (DBG_CALLBACK_INFO*)vinfo;
    gop_printf(COLOR_RED, "**->>>>> RIP %p TOUCHED THE GLOBAL STACK CANARY!**\n", (void*)(uintptr_t)info->trap->rip);
    FREEZE_OTHER_CPUS();
    FREEZE();
}

/** Remember that paging is on when this is called, as UEFI turned it on. */
__attribute__((noreturn))
void kernel_main(BOOT_INFO* boot_info)

/*++

    Routine description:

        Initializes the kernel subsystems and enters the selected startup or stress-test path.

    Arguments:

        [IN] boot_info - Firmware boot information supplied by the loader.

    Return Values:

        None.

--*/

{
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
    st = ObInitialize();

    if (MT_FAILURE(st)) {
        MeBugCheckEx(
            OBJECT_INITIALIZATION_FAILED,
            (void*)(uintptr_t)st,
            NULL,
            NULL,
            NULL
        );
    }

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

#if MT_STRESS_AUTOMATION
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
#else
    st = PsCreateSystemThread(
        (ThreadEntry)MeCreateInitialUserModeProcess,
        NULL,
        DEFAULT_TIMESLICE_TICKS,
        NULL
    );
    if (MT_FAILURE(st)) {
        MeBugCheckEx(PSWORKER_INIT_FAILED, (void*)(uintptr_t)st,
            (void*)MeCreateInitialUserModeProcess, NULL, NULL);
    }
#endif

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
