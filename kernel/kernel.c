/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Core Kernel Entry Point for MatanelOS.
 */

#include "kernel.h"

GOP_PARAMS gop_local;
BOOT_INFO boot_info_local;

/*
Global variables initialization
*/
bool isBugChecking = false;
LASTFUNC_HISTORY lastfunc_history = { .current_index = -1 };
CPU cpu;

// thread
DECLARE_THREAD(mainThread, 4096)
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
        bugcheck_system(NULL, NULL, MEMORY_MAP_SIZE_OVERRUN, 0, false);
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
}

void InitCPU(void) {
    cpu.currentIrql = PASSIVE_LEVEL;
    cpu.schedulerEnabled = NULL; // since NULL is 0, it would be false.
    cpu.currentThread = NULL;
    cpu.readyQueue.head = cpu.readyQueue.tail = NULL;
}

extern DPC* dpcQueueHead;

void kernel_idle_checks(void) {
    // Here the main thread would HALT the CPU, and not in kernel_main
    volatile int z = 0;
    gop_printf(&gop_local, 0xFF000FF0, "Reached the scheduler!\n");
    for (volatile uint64_t i = 0; i < 100000000ULL; ++i) {
        z++;
    }
    gop_printf(&gop_local, 0xFF000FF0, "**Ended Testing Thread Exceution**\n");
    while (1) {
        __hlt();
        if (dpcQueueHead) {
            DispatchDPC();
        }
    }
}

void test(void);

void test(void) {
    gop_printf(&gop_local, 0xFF00FF00, "Hit Test!\n");
    volatile uint64_t z = 0;
    for (uint64_t i = 0; i < 0xFFF; i++) {
        z++;
    }
    gop_printf(&gop_local, 0xFFA020F0, "**Ended Test.**\n");
}

void kernel_main(BOOT_INFO* boot_info) {
    //tracelast_func("kernel_main");
    // 1. CORE SYSTEM INITIALIZATION
    __cli();
    zero_bss();
    // Create the local boot struct.
    init_boot_info(boot_info);
    // Initialize the global CPU struct.
    InitCPU();
    // Initialize the frame bitmaps for dynamic frame allocation.
    frame_bitmap_init();
    // Initialize paging.
    paging_init();
    // Initialize interrupts & exceptions.
    init_interrupts();
    // Finally, initialize our heap for memory allocation (like threads, processes, sturcts..)
    init_heap();
    // Initialize ATA, will soon be unused & replaced & deleted.
    ata_init_primary();
    _SetIRQL(PASSIVE_LEVEL);
    /* Initiate Scheduler and DPCs */
    InitScheduler();
    init_dpc_system();
    init_timer(100);

    __sti(); // only now enable interrupts
    gop_clear_screen(&gop_local, 0); // 0 is just black. (0x0000000)
    gop_printf(&gop_local, 0xFFFF0000, "Hello People! Number: %d , String: %s , HEX: %p\n", 5, "MyOS!", 0x123123);
    gop_printf(&gop_local, 0xFF0000FF, "Testing! %d %d %d\n", 1, 2, 3);
    // test if init heap works
    void* buf = kmalloc(64, 16);
    gop_printf(&gop_local, 0xFFFFFF00, "buf addr: %p\n", buf);
    void* buf2 = kmalloc(128, 16);
    gop_printf(&gop_local, 0xFFFFFF00, "buf2 addr: %p\n", buf2);
    kfree(buf2);
    void* buf3 = kmalloc(128, 16);
    gop_printf(&gop_local, 0xFFFFFF00, "buf3 addr (should be same as buf2): %p\n", buf3);
    void* buf4 = kmalloc(2048, 16);
    gop_printf(&gop_local, 0xFF964B00, "buf4 addr (should reside after buf3, allocated 2048 bytes): %p\n", buf4);
    void* buf5 = kmalloc(64, 16);
    gop_printf(&gop_local, 0xFF964B00, "buf5 addr (should be a larger addr): %p\n", buf5);
#ifdef CAUSE_BUGCHECK
    CTX_FRAME regs;
    read_context_frame(&regs);
    bugcheck_system(&regs, 0, MANUALLY_INITIATED_CRASH, 0xDEADBEEF, true);
#endif
    
    CREATE_THREAD(mainThread, test, NULL, true);
    //CREATE_THREAD(workerThread, kernel_idle_checks, NULL, true);
    while (1) {
        __hlt();
        if (dpcQueueHead) {
            DispatchDPC();
        }
    }
}
