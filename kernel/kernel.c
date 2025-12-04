/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Core Kernel Entry Point for MatanelOS.
 */

#include "kernel.h"
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
PROCESSOR cpu0; // In UP Mode - Will be the place the CPU struct lives permanently, however in SMP mode, the struct transfers to cpus[my_lapic_id] after initializing SMP.

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
        MeBugCheck(MEMORY_MAP_SIZE_OVERRUN);
    }

    // Copy the entire memory map into the static buffer
    tmpcpy(memory_map_copy, boot_info->MemoryMap, boot_info->MapSize);

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
    kmemcpy(boot_info_local.AhciBarBases, ahci_bases_local, sizeof(ahci_bases_local));
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

static inline bool interrupts_enabled(void) {
    unsigned long flags;
    __asm__ __volatile__("pushfq; popq %0"
        : "=r"(flags)
        :
        : "memory", "cc");
    return (flags & (1UL << 9)) != 0; // IF is bit 9
}

void kernel_idle_checks(void) {
    gop_printf(0xFF000FF0, "Reached the scheduler!\n");
    // Reaching the idle thread with interrupts off means something did not have the RFLAGS IF Bit set.
    if (!interrupts_enabled()) {
        gop_printf(COLOR_RED, "**Interrupts aren't enabled..\n Stack Trace:\n");
        FREEZE();
    }
    while (1) {
        __hlt();
        //Schedule();
    }
}

static void test(MUTEX* mut) {
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

// All CPUs
uint8_t apic_list[MAX_CPUS];
uint32_t cpu_count = 0;
uint32_t lapicAddress;
bool smpInitialized;

/// The Stack Overflow check only checks for minor overflows, that don't completely smash the stack, yet do change the canaries (since it only checks in function epilogue)
/// To check for complete stack smashing, use the MtAllocateGuardedVirtualMemory function.
#ifdef DEBUG
// Stack Canary GCC
volatile uintptr_t __stack_chk_guard;

__attribute__((noreturn))
void __stack_chk_fail(void) {
    __cli();
    MeBugCheckEx(KERNEL_STACK_OVERFLOWN, (void*)__builtin_return_address(0), NULL, NULL, NULL);
}
#endif

// TODO allocate dynamically (use PsCreateProcess)
EPROCESS PsInitialSystemProcess;

static void InitSystemProcess(void) {
    PsInitialSystemProcess.PID = 4; // Initial PID, reserved.
    PsInitialSystemProcess.ParentProcess = NULL; // No creator process
    kstrncpy(PsInitialSystemProcess.ImageName, "mtoskrnl.mtexe", sizeof(PsInitialSystemProcess.ImageName)); // Name for the process
    PsInitialSystemProcess.priority = 0; // TODO
    PsInitialSystemProcess.InternalProcess.PageDirectoryPhysical = __read_cr3(); // The PML4 of the system process, is our kernel PML4.
    PsInitialSystemProcess.CreationTime = MeGetEpoch();
    PsInitialSystemProcess.MainThread = MeGetCurrentProcessor()->idleThread; // The main thread for the SYSTEM process is the BSP's idle thread.
}

extern uint8_t bss_start;
extern uint8_t bss_end;

/** Remember that paging is on when this is called, as UEFI turned it on. */
__attribute__((noreturn))
void kernel_main(BOOT_INFO* boot_info) {
    // 1. CORE SYSTEM INITIALIZATION
    __writemsr(IA32_GS_BASE, (uint64_t)&cpu0);
    __cli();
    // Initialize the CR (Control Registers) to our settings.
    InitialiseControlRegisters();
    // Zero the BSS.
    size_t len = &bss_end - &bss_start;
    RtlZeroMemory(&bss_start, len);
    // Create the local boot struct.
    init_boot_info(boot_info);
    gop_clear_screen(&gop_local, 0); // 0 is just black. (0x0000000)
    // Initialize the global CPU struct.
    MeInitializeProcessor(&cpu0);
    // Initialize interrupts & exceptions.
    init_interrupts();

    // Initialize the memory manager
    MmInitSystem(SYSTEM_PHASE_INITIALIZE_ALL, boot_info);

    // Initialize ACPI after initializing Mm (since page faults will happen on pfn db if not).
    MTSTATUS st = MhInitializeACPI();
    if (MT_FAILURE(st)) {
        gop_printf(COLOR_RED, "InitializeACPI Failure: %x\n");
        __hlt();
    }

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
    /* Initiate Scheduler */
    InitScheduler();
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

    MTSTATUS status = vfs_init();
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
    PsCreateSystemThread((ThreadEntry)test, sharedMutex, DEFAULT_TIMESLICE_TICKS);
    PsCreateSystemThread((ThreadEntry)funcWithParam, sharedMutex, DEFAULT_TIMESLICE_TICKS); // I have tested 5+ threads, works perfectly as it should. ( SMP UPDATED - Tested with 4 threads, MUTEX and scheduling works perfectly :) )
    /* Enable LAPIC & SMP Now. */
    lapic_init_cpu();
    lapic_enable(); // call again.
    lapic_timer_calibrate();
    init_lapic_timer(100); // 10ms, must be called before other APs
#ifndef MT_UP
    /* Enable SMP */
    status = MhParseLAPICs((uint8_t*)apic_list, MAX_CPUS, &cpu_count, &lapicAddress);
    if (MT_FAILURE(status)) {
        gop_printf(COLOR_RED, "**[MTSTATUS-FAILURE]** ParseLAPICs status returned: %x, continuing in UP mode.\n");
    }
    else {
        MhInitializeSMP(apic_list, 4, lapicAddress);
    }
    IPI_PARAMS dummy = { 0 }; // zero-initialize the struct
    MhSendActionToCpusAndWait(CPU_ACTION_PRINT_ID, dummy);
#else
    gop_printf(COLOR_RED, "System configured to run in UP mode.\n");
#endif
    // __sti(); STI Call commented out, this is what caused the scheduler assertion to fail, and guess how much time it took to debug? 2 days
    // Thread creations (including idle threads) must come with the IF flag set.
    Schedule();
    for (;;) __hlt();
    __builtin_unreachable();
}
