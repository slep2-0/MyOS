/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Core Kernel Entry Point for MatanelOS.
 */

#include "kernel.h"
#ifndef _MSC_VER
_Static_assert(sizeof(void*) == 8, "This Kernel is 64 bit only! The 32bit version is deprecated.");
#endif

#define MAX_AHCI_CONTROLLERS 32
uint64_t ahci_bases_local[MAX_AHCI_CONTROLLERS];

GOP_PARAMS gop_local;
BOOT_INFO boot_info_local;

//static MUTEX mutx;
//MUTEX* sharedMutex = &mutx;

/*
Global variables initialization
*/
bool isBugChecking = false;
LASTFUNC_HISTORY lastfunc_history = { .current_index = -1 };
CPU cpu;

/*
Ended
*/
#define MAX_MEMORY_MAP_SIZE 0x4000  // 16 KB, enough for ~256 descriptors

static EFI_MEMORY_DESCRIPTOR memory_map_copy[MAX_MEMORY_MAP_SIZE / sizeof(EFI_MEMORY_DESCRIPTOR)];

void copy_memory_map(BOOT_INFO* boot_info) {
    if (!boot_info || !boot_info->MemoryMap) return;
    if (boot_info->MapSize > MAX_MEMORY_MAP_SIZE) {
        // handle error, memory map too big
        MtBugcheck(NULL, NULL, MEMORY_MAP_SIZE_OVERRUN, 0, false);
    }

    // Copy the entire memory map into the static buffer
    kmemcpy(memory_map_copy, boot_info->MemoryMap, boot_info->MapSize);

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
    boot_info->Gop = &gop_local;
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
}

static void InitialiseControlRegisters(void) {

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

void InitCPU(void) {
    cpu.currentIrql = PASSIVE_LEVEL;
    cpu.schedulerEnabled = NULL; // since NULL is 0, it would be false.
    cpu.currentThread = NULL;
    cpu.readyQueue.head = cpu.readyQueue.tail = NULL;
    __writemsr(IA32_KERNEL_GS_BASE, (uint64_t) & cpu);
    spinlock_init(&cpu.readyQueue.lock);
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
    assert((interrupts_enabled()) == true, "Interrupts are not enabled...");
    while (1) {
        __hlt();
        //Schedule();
    }
}

static void test(MUTEX* mut) {
    tracelast_func("test - Thread");
    Thread* currentThread = MtGetCurrentThread();
    gop_printf_forced(0xFF00FF00, "Hit Test! test thread ptr: %p\n", currentThread);
    gop_printf(COLOR_GREEN, "(test) Acquiring Mutex Object: %p\n", mut);
    MTSTATUS status = MtAcquireMutexObject(mut);
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
    MtReleaseMutexObject(mut);
    gop_printf_forced(0xFFA020F0, "**Ended Test.**\n");
}

static void funcWithParam(MUTEX* mut) {
    funcWithParam(mut);
    tracelast_func("funcWithParam - Thread");
    gop_printf(COLOR_OLIVE, "Hit funcWithParam - funcWithParam threadptr: %p | stackStart: %p\n", MtGetCurrentThread(), MtGetCurrentThread()->startStackPtr);
    char buf[256];
    ksnprintf(buf, sizeof(buf), "In funcwithParam! - thread ptr: %p, mutex ptr: %p\n", MtGetCurrentThread(), mut);
    MTSTATUS status = vfs_mkdir("/testdir/");
    if (MT_FAILURE(status)) { gop_printf(COLOR_GRAY, "**[MTSTATUS-FAILURE] Failure on vfs_mkdir: %p**\n", status); __hlt(); }
    status = vfs_write("/testdir/test.txt", buf, kstrlen(buf), WRITE_MODE_CREATE_OR_REPLACE);
    if (MT_FAILURE(status)) { gop_printf(COLOR_GRAY, "**[MTSTATUS-FAILURE] Failure on vfs_write: %p**\n", status); }
    gop_printf(COLOR_OLIVE, "(funcWithParam) Acquiring Mutex Object: %p\n", mut);
    MtAcquireMutexObject(mut);
    volatile uint64_t z = 0;
#ifdef GDB
    for (uint64_t i = 0; i < 0xA; i++) {
#else
    for (uint64_t i = 0; i < 0xFFFFFFF; i++) {
#endif
        z++;
    }
    gop_printf(COLOR_OLIVE, "(funcWithParam) Releasing Mutex Object: %p\n", mut);
    MtReleaseMutexObject(mut);
    Thread* currentThread = MtGetCurrentThread();
    gop_printf(COLOR_OLIVE, "Current thread in funcWithParam: %p\n", currentThread);
    gop_printf_forced(COLOR_OLIVE, "**Ended funcWithParam.**\n");
}

static void bp_test(void* vinfo) {
    DBG_CALLBACK_INFO* info = (DBG_CALLBACK_INFO*)vinfo;
    if (!info) return;

    gop_printf_forced(0xFFFFFF00, "HWBP: idx=%d addr=%p DR6=%p\n",
        info->BreakIdx, info->Address, (unsigned long long)info->Dr6);

    MtClearHardwareBreakpointByIndex(info->BreakIdx);
}

/** Remember that paging is on when this is called, as UEFI turned it on. */
void kernel_main(BOOT_INFO* boot_info) {
    //tracelast_func("kernel_main");
    // 1. CORE SYSTEM INITIALIZATION
    __cli();
    // Initialize the CR (Control Registers) registers to our settings.
    InitialiseControlRegisters();
    // Zero the BSS.
    zero_bss();
    // Create the local boot struct.
    init_boot_info(boot_info);
    // Initialize the global CPU struct.
    InitCPU();
    // Initialize interrupts & exceptions.
    init_interrupts();
    // Initialize the frame bitmaps for dynamic frame allocation.
    frame_bitmap_init();
    // Finally, initialize our heap for memory allocation (like threads, processes, structs..)
    init_heap();
    _MtSetIRQL(PASSIVE_LEVEL);
    /* Initiate Scheduler and DPCs */
    InitScheduler();
    init_dpc_system();
    gop_clear_screen(&gop_local, 0); // 0 is just black. (0x0000000)
    
    //MemoryTestStable();
    //__cli();
    //__hlt();
    extern uint32_t cursor_x, cursor_y;
    cursor_x = cursor_y = 0; // set to 0, since it somehow decrements them.

    uint64_t rip;
    __asm__ volatile (
        "lea 1f(%%rip), %0\n\t"  // Calculate the address of label 1 relative to RIP
        "1:"                     // The label whose address we want
        : "=r"(rip)              // Output to the 'rip' variable
        );

    gop_printf_forced(0xFFFFFF00, "Current RIP: %p\n", rip);

    if (rip >= KERNEL_VA_START) {
        gop_printf_forced(0x00FF00FF, "**[+] Running in higher-half**\n");
    }
    else {
        gop_printf_forced(0xFF0000FF, "[-] Still identity-mapped\n");
    }

    void* buf = MtAllocateVirtualMemory(64, 16);
    gop_printf_forced(0xFFFFFF00, "buf addr: %p\n", buf);
    void* buf2 = MtAllocateVirtualMemory(128, 16);
    gop_printf_forced(0xFFFFFF00, "buf2 addr: %p\n", buf2);
    MtFreeVirtualMemory(buf2);
    void* buf3 = MtAllocateVirtualMemory(128, 16);
    gop_printf_forced(0xFFFFFF00, "buf3 addr (should be same as buf2): %p\n", buf3);
    void* buf4 = MtAllocateVirtualMemory(2048, 16);
    gop_printf_forced(0xFF964B00, "buf4 addr (should reside after buf3, allocated 2048 bytes): %p\n", buf4);
    void* buf5 = MtAllocateVirtualMemory(64, 16);
    gop_printf_forced(0xFF964B00, "buf5 addr (should be a larger addr): %p\n", buf5);
    void* buf6 = MtAllocateVirtualMemory(5000, 64);
    gop_printf_forced(0xFFFFFF00, "buf6 addr (should use dynamic memory): %p\n", buf6);
    void* buf7 = MtAllocateVirtualMemory(10000, 128);
    gop_printf_forced(0xFFFFFF00, "buf7 addr (should use dynamic memory, extremely larger): %p\n", buf7);
    // check
    void* addr = 0;
    gop_printf(COLOR_ORANGE, "Address: %p is %s\n", addr, MtIsAddressValid(addr) ? "Valid" : "Invalid");
    gop_printf(COLOR_ORANGE, "Address %p (buf7) is %s\n", buf7, MtIsAddressValid(buf7) ? "Valid" : "Invalid");
    gop_printf(COLOR_MAGENTA, "BUF7 (VIRT): %p | (PHYS): %p\n", buf7, MtTranslateVirtualToPhysical(buf7));
    
#ifdef CAUSE_BUGCHECK
    MtBugcheck(NULL, NULL, MANUALLY_INITIATED_CRASH, 0xDEADBEEF, true);
#endif
    
    if (checkcpuid()) {
        char str[256];
        getCpuName(str);
        gop_printf(COLOR_GREEN, "CPU Identified: %s\n", str);
    }
    volatile int x = 1;
    MTSTATUS status = MtSetHardwareBreakpoint((DebugCallback)bp_test, (void*)&x, DEBUG_ACCESS_WRITE, DEBUG_LEN_4);
    gop_printf(COLOR_RED, "[MTSTATUS] Status Returned: %p\n", status);
    x = 2;
    status = vfs_init();
    gop_printf(COLOR_RED, "vfs_init returned: %s\n", MT_SUCCEEDED(status) ? "Success" : "Unsuccessful");
    if (MT_FAILURE(status)) {
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        MtBugcheck(&ctx, NULL, FILESYSTEM_PANIC, 0, false);
    }

    TIME_ENTRY currTime = get_time();
#define ISRAEL_UTC_OFFSET 3
    gop_printf(COLOR_GREEN, "Current Time: %d/%d/%d | %d:%d:%d\n", currTime.year, currTime.month, currTime.day, currTime.hour + ISRAEL_UTC_OFFSET, currTime.minute, currTime.second);

    char listings[256];
    status = vfs_listdir("/", listings, sizeof(listings));
    gop_printf(COLOR_RED, "vfs_listdir returned: %p\n", status);
    gop_printf(COLOR_RED, "root directory is: %s\n", vfs_is_dir_empty("/") ? "Empty" : "Not Empty");
    gop_printf(COLOR_CYAN, "%s", listings);
    
    MUTEX* sharedMutex =  MtAllocateVirtualMemory(sizeof(MUTEX), _Alignof(MUTEX));
    if (!sharedMutex) { gop_printf(COLOR_RED, "It's null\n"); __hlt(); }
    status = MtInitializeMutexObject(sharedMutex);
    gop_printf(COLOR_RED, "[MTSTATUS] MtInitializeObject Returned: %p\n", status);
    MtCreateThread((ThreadEntry)test, sharedMutex, DEFAULT_TIMESLICE_TICKS, true);
    //int integer = 1234;
    MtCreateThread((ThreadEntry)funcWithParam, sharedMutex, DEFAULT_TIMESLICE_TICKS, true); // I have tested 5+ threads, works perfectly as it should.
    /* Enable LAPIC Now. */
    lapic_init_bsp();
    lapic_enable();
    init_lapic_timer(100); // 10ms
    __sti();
    Schedule();
}