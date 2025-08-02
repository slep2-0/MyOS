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

/*
Global variables initialization
*/
bool isBugChecking = false;
LASTFUNC_HISTORY lastfunc_history = { .current_index = -1 };
CPU cpu;

//DECLARE_THREAD(workerThread, 4096)
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
}

void InitCPU(void) {
    cpu.currentIrql = PASSIVE_LEVEL;
    cpu.schedulerEnabled = NULL; // since NULL is 0, it would be false.
    cpu.currentThread = NULL;
    cpu.readyQueue.head = cpu.readyQueue.tail = NULL;
}

extern volatile DPC* dpcQueueHead;

void kernel_idle_checks(void) {
    static volatile bool first_time = true;

    if (first_time) {
        first_time = false;
        gop_printf(0xFF000FF0, "Reached the scheduler!\n");
        for (volatile uint64_t i = 0; i < 100000000ULL; ++i) {
            /* delay loop */
        }
        gop_printf(0xFF000FF0, "**Ended Testing Thread Execution**\n");
    }

    while (1) {
        __hlt();
        if (dpcQueueHead) {
            DispatchDPC();
        }
    }
}

static void test(void) {
    gop_printf(0xFF00FF00, "Hit Test!\n");
    volatile uint64_t z = 0;
    for (uint64_t i = 0; i < 0xFFFFFFFFF; i++) {
        z++;
    }
    gop_printf(0xFFA020F0, "**Ended Test.**\n");
}

static void funcWithParam(int* integer) {
    gop_printf(COLOR_OLIVE, "Hit funcWithParam, Integer: %d\n", *integer);
    volatile uint64_t z = 0;
    for (uint64_t i = 0; i < 0xFFFFFFFFF; i++) {
        z++;
    }
    gop_printf(COLOR_OLIVE, "**Ended funcWithParam.**\n");
}

void kernel_main(BOOT_INFO* boot_info) {
    //tracelast_func("kernel_main");
    // 1. CORE SYSTEM INITIALIZATION
    __cli();
    zero_bss();
    // Create the local boot struct.
    init_boot_info(boot_info);
    gop_clear_screen(&gop_local, 0);
    // Initialize the global CPU struct.
    InitCPU();
    // Initialize the frame bitmaps for dynamic frame allocation.
    frame_bitmap_init();
    // Initialize paging.
    paging_init();
    // Initialize interrupts & exceptions.
    init_interrupts();
    // Finally, initialize our heap for memory allocation (like threads, processes, structs..)
    init_heap();
    _MtSetIRQL(PASSIVE_LEVEL);
    /* Initiate Scheduler and DPCs */
    InitScheduler();
    init_dpc_system();
    init_timer(100);

    __sti(); // only now enable interrupts
    
    // Initialize AHCI now.
    if (!ahci_init()) {
        CTX_FRAME ctxfr;
        SAVE_CTX_FRAME(&ctxfr);
        MtBugcheck(&ctxfr, NULL, AHCI_INIT_FAILED, 0, false);
    }
    gop_clear_screen(&gop_local, 0); // 0 is just black. (0x0000000)
    extern uint32_t cursor_x, cursor_y;
    cursor_x = cursor_y = 0; // set to 0, since it somehow decrements them.
    gop_printf(0xFFFF0000, "Hello People! Number: %d , String: %s , HEX: %p\n", 5, "MyOS!", 0x123123);
    gop_printf(0xFF0000FF, "Testing! %d %d %d\n", 1, 2, 3);
    // test if init heap works
    void* buf = MtAllocateMemory(64, 16);
    gop_printf(0xFFFFFF00, "buf addr: %p\n", buf);
    void* buf2 = MtAllocateMemory(128, 16);
    gop_printf(0xFFFFFF00, "buf2 addr: %p\n", buf2);
    MtFreeMemory(buf2);
    void* buf3 = MtAllocateMemory(128, 16);
    gop_printf(0xFFFFFF00, "buf3 addr (should be same as buf2): %p\n", buf3);
    void* buf4 = MtAllocateMemory(2048, 16);
    gop_printf(0xFF964B00, "buf4 addr (should reside after buf3, allocated 2048 bytes): %p\n", buf4);
    void* buf5 = MtAllocateMemory(64, 16);
    gop_printf(0xFF964B00, "buf5 addr (should be a larger addr): %p\n", buf5);
    void* buf6 = MtAllocateMemory(5000, 64);
    gop_printf(0xFFFFFF00, "buf6 addr (should use dynamic memory): %p\n", buf6);
    void* buf7 = MtAllocateMemory(10000, 128);
    gop_printf(0xFFFFFF00, "buf7 addr (should use dynamic memory, extremely larger): %p\n", buf7);
#ifdef CAUSE_BUGCHECK
    CTX_FRAME regs;
    SAVE_CTX_FRAME(&regs);
    MtBugcheck(&regs, NULL, MANUALLY_INITIATED_CRASH, 0xDEADBEEF, true);
#endif
    /*
    void* writebuf = MtAllocateMemory(512, 16);
    gop_printf(COLOR_GREEN, "Attempting to read to writebuf...\n");
    if (ahci_read_sector(get_block_device(0), 0, writebuf)) {
        for (int i = 0; i < 512; i++) {
            gop_printf(0xFFFFA500, "%d", ((uint8_t*)writebuf)[i]);
        }
    }
    else {
        gop_printf(COLOR_RED, "Could not read AHCI...\n");
    }
    */
    /*
    if (!fat32_init(0)) {
        CTX_FRAME ctxfr;
        SAVE_CTX_FRAME(&ctxfr);
        bugcheck_system(&ctxfr, NULL, FILESYSTEM_PANIC, 0, false);
    }
    fat32_list_root();
    */
    MtCreateThread((ThreadEntry)test, NULL, DEFAULT_TIMESLICE_TICKS, true);
    int integer = 1234;
    MtCreateThread((ThreadEntry)funcWithParam, &integer, DEFAULT_TIMESLICE_TICKS, true);

    Schedule();
}
