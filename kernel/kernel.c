/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Core Kernel Entry Point for MatanelOS.
 */

#include "kernel.h"
#ifndef _MSC_VER
_Static_assert(sizeof(void*) == 8, "This Kernel is 64 bit only! The 32bit version is deprecated.");
#endif


#define OFFSET_NESTED(st, member, inner_st, inner_member) \
    (offsetof(st, member) + offsetof(inner_st, inner_member))

#define PRINT_OFFSETS_AND_HALT()                                      \
    do {                                                              \
        gop_printf(COLOR_ORANGE,                                      \
            "(offsets 24/9/2025) (CPU OFFSETS)\n"                     \
            "self: %x\n"                                              \
            "currentIrql: %x\n"                                       \
            "schedulerEnabled: %x\n"                                  \
            "currentThread: %x\n"                                     \
            "readyQueue: %x\n"                                        \
            "ID: %x\n"                                                \
            "lapic_ID: %x\n"                                          \
            "VirtStackTop: %x\n"                                      \
            "tss: %x\n"                                               \
            "IstPFStackTop: %x\n"                                     \
            "IstDFStackTop: %x\n"                                     \
            "flags: %x\n"                                             \
            "schedulePending: %x\n"                                   \
            "gdt: %x\n"                                               \
            "DeferredRoutineQueue.dpcQueueHead: %x\n"                 \
            "DeferredRoutineQueue.dpcQueueTail: %x\n",                \
            offsetof(CPU, self),                        \
            offsetof(CPU, currentIrql),                 \
            offsetof(CPU, schedulerEnabled),            \
            offsetof(CPU, currentThread),               \
            offsetof(CPU, readyQueue),                  \
            offsetof(CPU, ID),                          \
            offsetof(CPU, lapic_ID),                    \
            offsetof(CPU, VirtStackTop),                \
            offsetof(CPU, tss),                         \
            offsetof(CPU, IstPFStackTop),               \
            offsetof(CPU, IstDFStackTop),               \
            offsetof(CPU, flags),                       \
            offsetof(CPU, schedulePending),             \
            offsetof(CPU, gdt),                         \
            OFFSET_NESTED(CPU, DeferredRoutineQueue, struct _DPC_QUEUE, dpcQueueHead), \
            OFFSET_NESTED(CPU, DeferredRoutineQueue, struct _DPC_QUEUE, dpcQueueTail)  \
        );                                                            \
                                                                       \
        gop_printf(COLOR_CYAN,                                        \
            "(THREAD OFFSETS from - Thread)\n"                                       \
            "registers: %x\n"                                         \
            "threadState: %x\n"                                       \
            "timeSlice: %x\n"                                         \
            "origTimeSlice: %x\n"                                     \
            "nextThread: %x\n"                                        \
            "TID: %x\n"                                                \
            "startStackPtr: %x\n"                                     \
            "registers themselves (offsets from CTX_FRAME):\n"                           \
            " RAX: %x | RBX: %x | RCX: %x | RDX: %x | RSI: %x | RDI: %x | RBP: %x |\n" \
            " R8: %x | R9: %x | R10: %x | R11: %x | R12: %x | R13: %x |\n" \
            " R14: %x | R15: %x | RSP: %x | RIP: %x | ",               \
            offsetof(Thread, registers),                \
            offsetof(Thread, threadState),              \
            offsetof(Thread, timeSlice),                \
            offsetof(Thread, origTimeSlice),            \
            offsetof(Thread, nextThread),               \
            offsetof(Thread, TID),                      \
            offsetof(Thread, startStackPtr),           \
            offsetof(CTX_FRAME, rax),                   \
            offsetof(CTX_FRAME, rbx),                   \
            offsetof(CTX_FRAME, rcx),                   \
            offsetof(CTX_FRAME, rdx),                   \
            offsetof(CTX_FRAME, rsi),                   \
            offsetof(CTX_FRAME, rdi),                   \
            offsetof(CTX_FRAME, rbp),                   \
            offsetof(CTX_FRAME, r8),                    \
            offsetof(CTX_FRAME, r9),                    \
            offsetof(CTX_FRAME, r10),                   \
            offsetof(CTX_FRAME, r11),                   \
            offsetof(CTX_FRAME, r12),                   \
            offsetof(CTX_FRAME, r13),                   \
            offsetof(CTX_FRAME, r14),                   \
            offsetof(CTX_FRAME, r15),                   \
            offsetof(CTX_FRAME, rsp),                   \
            offsetof(CTX_FRAME, rip)                    \
        );                                                            \
                                                                       \
        gop_printf(COLOR_CYAN, "RFLAGS: %x |\n",                       \
            offsetof(CTX_FRAME, rflags)                 \
        );                                                            \
                                                                       \
        __hlt();                                                      \
    } while (0)

/**
Global variables initialization
**/

/*
Kernel Specific
*/
bool isBugChecking = false;
PROCESSOR cpu0;

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


#define MAX_MEMORY_MAP_SIZE 0x4000  // 16 KB, enough for ~256 descriptors

static EFI_MEMORY_DESCRIPTOR memory_map_copy[MAX_MEMORY_MAP_SIZE / sizeof(EFI_MEMORY_DESCRIPTOR)];

static void* tmpcpy(void* dest, const void* src, size_t len) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < len; i++) d[i] = s[i];
    return dest;
}

void copy_memory_map(BOOT_INFO* boot_info) {
    if (!boot_info || !boot_info->MemoryMap) return;
    if (boot_info->MapSize > MAX_MEMORY_MAP_SIZE) {
        // handle error, memory map too big
        MtBugcheck(NULL, NULL, MEMORY_MAP_SIZE_OVERRUN, 0, false);
    }

    // Copy the entire memory map into the static buffer
    tmpcpy(memory_map_copy, boot_info->MemoryMap, boot_info->MapSize);

    boot_info_local.MemoryMap = memory_map_copy;
    boot_info_local.MapSize = boot_info->MapSize;
    boot_info_local.DescriptorSize = boot_info->DescriptorSize;
    boot_info_local.DescriptorVersion = boot_info->DescriptorVersion;
}

void copy_gop(BOOT_INFO* boot_info) {
    if (!boot_info || !boot_info->Gop) return;

    // Copy the GOP data to a local global variable
    gop_local = *(boot_info->Gop);

    // Update all relevant pointers to point to the local copy
    boot_info_local.Gop = &gop_local;
}


void init_boot_info(BOOT_INFO* boot_info) {
    if (!boot_info) return;

    copy_memory_map(boot_info);
    copy_gop(boot_info);
    if (boot_info->AhciCount > MAX_AHCI_CONTROLLERS) {
        MtBugcheck(NULL, NULL, BAD_AHCI_COUNT, 0, false);
    }
    for (uint32_t i = 0; i < boot_info->AhciCount; i++) {
        ahci_bases_local[i] = boot_info->AhciBarBases[i];
    }
    boot_info_local.AhciBarBases = ahci_bases_local;
    boot_info_local.AhciCount = boot_info->AhciCount;
    boot_info_local.KernelStackTop = boot_info->KernelStackTop;
    boot_info_local.Pml4Phys = boot_info->Pml4Phys;
    boot_info_local.AcpiRsdpPhys = boot_info->AcpiRsdpPhys;
}

void InitialiseControlRegisters(void) {

    /* CR0 */
    unsigned long cr0 = __read_cr0();
    cr0 |= (1UL << 16); // Set bit 16 (WRITE PROTECT), so when the kernel touches read only memory it would #PF.
#ifdef DISABLE_CACHE
    cr0 |= (1UL << 30); // Set bit 30 (CACHE DISABLE).
#endif
    __write_cr0(cr0);

    /* CR4 */
    unsigned long cr4 = __read_cr4();
    cr4 |= (1UL << 11); // Set bit 11 - User Mode Instruction Prevention. This'll be useful against user mode attacks to locate IDT/GDT/LDT...
    __write_cr4(cr4);

    /* Debug Registers */
    for (int i = 0; i < 7; i++) {
        // reset all
        __write_dr(i, 0);
    }
}

static void InitCPU(void) {
    cpu0.self = &cpu0;
    cpu0.currentIrql = PASSIVE_LEVEL;
    cpu0.schedulerEnabled = NULL; // since NULL is 0, it would be false.
    cpu0.currentThread = NULL;
    cpu0.readyQueue.head = cpu0.readyQueue.tail = NULL;
    cpu0.lastfuncBuffer = NULL;
    // Function Trace Buffer
    spinlock_init(&cpu0.readyQueue.lock);
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
    tracelast_func("kernel_idle_checks - Thread");
    gop_printf(0xFF000FF0, "Reached the scheduler!\n");
    while (1) {
        // Reaching the idle thread with interrupts off means something did not have the RFLAGS IF Bit set.
        if (!interrupts_enabled()) {
            gop_printf(COLOR_RED, "**Interrupts aren't enabled..\n Stack Trace:\n");
        }
        __hlt();
        //Schedule();
    }
}

static void test(MUTEX* mut) {
    tracelast_func("test - Thread");
    PETHREAD currentThread = PsGetCurrentThread();
    gop_printf_forced(0xFF00FF00, "Hit Test! test thread ptr: %p\n", currentThread);
    gop_printf(COLOR_GREEN, "(test) Acquiring Mutex Object: %p\n", mut);
    MTSTATUS status = MsAcquireMutexObject(mut);
    gop_printf(COLOR_GREEN, "(test) status returned: %p\n", status);
    volatile uint64_t z = 0;
#ifdef GDB
    for (uint64_t i = 0; i < 0xA; i++) {
#else
    for (uint64_t i = 0; i < 0xFFFFFFF; i++) {
#endif
        z++;
    }
    gop_printf(COLOR_GREEN, "(test) Releasing Mutex Object: %p\n", mut);
    MsReleaseMutexObject(mut);
    gop_printf_forced(0xFFA020F0, "**Ended Test.**\n");
}

static void funcWithParam(MUTEX* mut) {
    tracelast_func("funcWithParam - Thread");
    gop_printf(COLOR_OLIVE, "Hit funcWithParam - funcWithParam threadptr: %p | stackStart: %p\n", PsGetCurrentThread(), PsGetCurrentThread()->InternalThread.StackBase);
    char buf[256];
    ksnprintf(buf, sizeof(buf), "echo \"Hello World\"");
    gop_printf(COLOR_OLIVE, "(funcWithParam) Acquiring Mutex Object: %p\n", mut);
    //MTSTATUS status = vfs_mkdir("/testdir/");
    //if (MT_FAILURE(status)) { gop_printf(COLOR_GRAY, "**[MTSTATUS-FAILURE] Failure on vfs_mkdir: %p**\n", status); }
    //status = vfs_write("/testdir/test.sh", buf, kstrlen(buf), WRITE_MODE_CREATE_OR_REPLACE);
    //if (MT_FAILURE(status)) { gop_printf(COLOR_GRAY, "**[MTSTATUS-FAILURE] Failure on vfs_write: %p**\n", status); }
    MsAcquireMutexObject(mut);
    volatile uint64_t z = 0;
#ifdef GDB
    for (uint64_t i = 0; i < 0xA; i++) {
#else
    for (uint64_t i = 0; i < 0xFFFFFFF; i++) {
#endif
        z++;
    }
    gop_printf(COLOR_OLIVE, "(funcWithParam) Releasing Mutex Object: %p\n", mut);
    MsReleaseMutexObject(mut);
    PETHREAD currentThread = PsGetCurrentThread();
    gop_printf(COLOR_OLIVE, "Current thread in funcWithParam: %p\n", currentThread);
    gop_printf_forced(COLOR_OLIVE, "**Ended funcWithParam.**\n");
}

static void bp_exec(void* vinfo) {
    DBG_CALLBACK_INFO* info = (DBG_CALLBACK_INFO*)vinfo;
    if (!info) return;

    gop_printf(0xFFFFFF00, "(EXECUTE) HWBP: idx=%d variable addr=%p rip: %p DR6=%p\n", info->BreakIdx, info->Address, info->trap->rip, (unsigned long long)info->Dr6);
    gop_printf(COLOR_RED, "Stack Trace:\n");
    __hlt();
}

// All CPUs
uint8_t apic_list[MAX_CPUS];
uint32_t cpu_count = 0;
uint32_t lapicAddress;
bool smpInitialized;

/// The Stack Overflow check only checks for minor overflows, that don't completetly smash the stack, yet do change the canaries (since it only checks in function epilogue)
/// To check for complete stack smashing, use the MtAllocateGuardedVirtualMemory function.
#ifdef DEBUG
// Stack Canary GCC
volatile uintptr_t __stack_chk_guard;

__attribute__((noreturn))
void __stack_chk_fail(void) {
    __cli();
    BUGCHECK_ADDITIONALS addt = { 0 };
    ksnprintf(addt.str, sizeof(addt.str), "Kernel Has Encountered a buffer overflow, return address: %p (will signify the stack check guard, it only shows which function this was triggered)", __builtin_return_address(0));
    CTX_FRAME ctx;
    SAVE_CTX_FRAME(&ctx);
    MtBugcheckEx(&ctx, NULL, KERNEL_STACK_OVERFLOWN, &addt, true);
}
#endif

// Todo allocate dynamically
EPROCESS SystemProcess;

static void InitSystemProcess(void) {
    SystemProcess.PID = 4; // Initial PID, reserved.
    SystemProcess.ParentProcess = NULL; // No creator process
    kstrncpy(SystemProcess.ImageName, "mtoskrnl.mtexe", sizeof(SystemProcess.ImageName)); // Name for the process
    SystemProcess.priority = 0; // TODO
    SystemProcess.InternalProcess.PageDirectoryPhysical = __read_cr3(); // The PML4 of the system process, is our kernel PML4.
    SystemProcess.CreationTime = MtGetEpoch();
    SystemProcess.MainThread = &MeGetCurrentProcessor()->idleThread; // The main thread for the SYSTEM process is the BSP's idle thread.
    MsEnqueueThreadWithLock(&SystemProcess.AllThreads, &MeGetCurrentProcessor()->idleThread);
}

extern uint8_t bss_start;
extern uint8_t bss_end;

/** Remember that paging is on when this is called, as UEFI turned it on. */
__attribute__((noreturn))
void kernel_main(BOOT_INFO* boot_info) {
    //tracelast_func("kernel_main");
    // 1. CORE SYSTEM INITIALIZATION
    __writemsr(IA32_KERNEL_GS_BASE, (uint64_t)&cpu0);
    __swapgs();
    __cli();
    // Initialize the CR (Control Registers) registers to our settings.
    InitialiseControlRegisters();
    // Zero the BSS.
    size_t len = &bss_end - &bss_start;
    RtlZeroMemory(&bss_start, len);
    // Create the local boot struct.
    init_boot_info(boot_info);
    gop_clear_screen(&gop_local, 0); // 0 is just black. (0x0000000)
    // Initialize the global CPU struct.
    InitCPU();
    // Initialize interrupts & exceptions.
    init_interrupts();
    // Initialize ACPI.
    MTSTATUS st = MhInitializeACPI();
    if (MT_FAILURE(st)) {
        gop_printf(COLOR_RED, "InitializeACPI Failure: %x\n");
        __hlt();
    }
    // Initialize the memory manager.
    MiInitializePfnDatabase(boot_info);
    MiInitializePoolVaSpace();
    MiInitializePoolSystem();
    // And, initialize our system process.
    InitSystemProcess();
    _MeSetIrql(PASSIVE_LEVEL);
#ifdef DEBUG
    {
        uint64_t temp_canary = 0;
        bool rdrand_ok = false;
        for (int n = 0; n < 64; n++) {
            if (__rdrand64(&temp_canary)) {
                rdrand_ok = true;
                break;
            }
        }

        if (rdrand_ok) {
            __stack_chk_guard = temp_canary;
        }
        else {
            // rdrand didnt give a value, use timestamp of CPU cycles.
            __stack_chk_guard = __rdtsc();
        }

        // The canary should never be zero.
        if (__stack_chk_guard == 0) {
            __stack_chk_guard = 0xDEADC0DEDEADC0DE; // fallback
        }
    }
#endif
    /* Initiate Scheduler and DPCs */
    InitScheduler();
    MeGetCurrentProcessor()->DeferredRoutineQueue.dpcQueueHead = MeGetCurrentProcessor()->DeferredRoutineQueue.dpcQueueTail = NULL;
    /* Initiate the lastfunc buffer for the BSP, placed here since after init_heap call */
    LASTFUNC_HISTORY* bfr = MmAllocatePoolWithTag(NonPagedPool, sizeof(LASTFUNC_HISTORY), 'TSAL');
    cpu0.lastfuncBuffer = bfr;
    cpu0.lastfuncBuffer->current_index = -1; // init to -1
    ///PRINT_OFFSETS_AND_HALT();
    uint64_t rip;
    __asm__ volatile (
        "lea 1f(%%rip), %0\n\t"  // Calculate the address of label 1 relative to RIP
        "1:"                     // The label whose address we want
        : "=r"(rip)              // Output to the 'rip' variable
        );

    gop_printf_forced(0xFFFFFF00, "Current RIP: %p\n", rip);

    if (rip >= KernelVaStart) {
        gop_printf_forced(0x00FF00FF, "**[+] Running in higher-half**\n");
    }
    else {
        gop_printf_forced(0xFF0000FF, "[-] Still identity-mapped\n");
    }

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
    MTSTATUS status = MdSetHardwareBreakpoint((DebugCallback)bp_exec, (void*)0x10, DEBUG_ACCESS_EXECUTE, DEBUG_LEN_8);
    gop_printf(COLOR_RED, "[MTSTATUS] Status Returned: %p\n", status);
    status = vfs_init();
    gop_printf(COLOR_RED, "vfs_init returned: %s\n", MT_SUCCEEDED(status) ? "Success" : "Unsuccessful");
    if (MT_FAILURE(status)) {
        MeBugCheck(FILESYSTEM_PANIC);
    }
    TIME_ENTRY currTime = get_time();
#define ISRAEL_UTC_OFFSET 3
    gop_printf(COLOR_GREEN, "Current Time: %d/%d/%d | %d:%d:%d\n", currTime.year, currTime.month, currTime.day, currTime.hour + ISRAEL_UTC_OFFSET, currTime.minute, currTime.second);
    char listings[256];
    status = vfs_listdir("/", listings, sizeof(listings));
    gop_printf(COLOR_RED, "vfs_listdir returned: %p\n", status);
    gop_printf(COLOR_RED, "root directory is: %s\n", vfs_is_dir_empty("/") ? "Empty" : "Not Empty");
    gop_printf(COLOR_CYAN, "%s", listings);
    MUTEX* sharedMutex = MmAllocatePoolWithTag(NonPagedPool, sizeof(MUTEX), ' TUM');
    if (!sharedMutex) { gop_printf(COLOR_RED, "It's null\n"); __hlt(); }
    status = MsInitializeMutexObject(sharedMutex);
    gop_printf(COLOR_RED, "[MTSTATUS] MtInitializeObject Returned: %p\n", status);
    PsCreateSystemThread((ThreadEntry)test, sharedMutex, DEFAULT_TIMESLICE_TICKS);
    //int integer = 1234;
    PsCreateSystemThread((ThreadEntry)funcWithParam, sharedMutex, DEFAULT_TIMESLICE_TICKS); // I have tested 5+ threads, works perfectly as it should. ( SMP UPDATED - Tested with 4 threads, MUTEX and scheduling works perfectly :) )
    /* Enable LAPIC & SMP Now. */
    lapic_init_cpu();
    lapic_enable(); // call again.
    lapic_timer_calibrate();
    init_lapic_timer(100); // 10ms, must be called before other APs
    /* Enable SMP */
    status = MhParseLAPICs((uint8_t*)apic_list, MAX_CPUS, &cpu_count, &lapicAddress);
    if (MT_FAILURE(status)) {
        gop_printf(COLOR_RED, "**[MTSTATUS-FAILURE]** ParseLAPICs status returned: %x\n");
    }
    else {
        MhInitializeSMP(apic_list, 4, lapicAddress);
    }
    IPI_PARAMS dummy = { 0 }; // zero-initialize the struct
    MhSendActionToCpusAndWait(CPU_ACTION_PRINT_ID, dummy);
    __sti();
    Schedule();
    for (;;) __hlt();
    __builtin_unreachable();
}