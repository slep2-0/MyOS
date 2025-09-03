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
    boot_info_local.KernelStackTop = boot_info->KernelStackTop;
    boot_info_local.Pml4Phys = boot_info->Pml4Phys;
}

static void InitialiseControlRegisters(void) {

    /* CR0 */
    unsigned long cr0 = __read_cr0();
    cr0 |= (1UL << 16); // Set bit 16 (WRITE PROTECT), so when the kernel touches read only memory it would #PF.
#ifdef DEBUG
    cr0 |= (1UL << 30); // Set bit 30 (CACHE DISABLE).
#endif
    __write_cr0(cr0);

    /* CR4 */
    unsigned long cr4 = __read_cr4();
    cr4 |= (1UL << 11); // Set bit 11 - User Mode Instruction Prevention. This'll be useful against user mode attacks to locate IDT/GDT/LDT...
    __write_cr4(cr4);
}

void InitCPU(void) {
    cpu.currentIrql = PASSIVE_LEVEL;
    cpu.schedulerEnabled = NULL; // since NULL is 0, it would be false.
    cpu.currentThread = NULL;
    cpu.readyQueue.head = cpu.readyQueue.tail = NULL;
}

static inline bool interrupts_enabled(void) {
    unsigned long flags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    return (flags & (1UL << 9)) != 0; // IF is bit 9
}

void kernel_idle_checks(void) {
    tracelast_func("kernel_idle_checks - Thread");  
    static volatile bool first_time = true;

    if (first_time) {
        first_time = false;
        gop_printf_forced(0xFF000FF0, "Reached the scheduler!\n");
        for (volatile uint64_t i = 0; i < 100000000ULL; ++i) {
            /* delay loop */
        }
        
        gop_printf_forced(0xFF000FF0, "**Ended Testing Thread Execution**\n");
    }
    /*
    assert((interrupts_enabled()) == true, "Interrupts are not enabled...");
    */
    while (1) {
        __hlt();
    }
}

static void test(void) {
    tracelast_func("test - Thread");
    gop_printf_forced(0xFF00FF00, "Hit Test!\n");
    volatile uint64_t z = 0;
    for (uint64_t i = 0; i < 0xFFFFFFF; i++) {
        z++;
    }
    Thread* currentThread = MtGetCurrentThread();
    gop_printf(COLOR_OLIVE, "Current thread in test: %p\n", currentThread);
    gop_printf_forced(0xFFA020F0, "**Ended Test.**\n");
}

static void funcWithParam(int* integer) {
    tracelast_func("funcWithParam - Thread");
    gop_printf_forced(COLOR_OLIVE, "Hit funcWithParam, Integer: %d\n", *integer);
    volatile uint64_t z = 0;
    for (uint64_t i = 0; i < 0xFFFFFFF; i++) {
        z++;
    }
    Thread* currentThread = MtGetCurrentThread();
    gop_printf(COLOR_OLIVE, "Current thread in funcWithParam: %p\n", currentThread);
    gop_printf_forced(COLOR_OLIVE, "**Ended funcWithParam.**\n");
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
    init_timer(100);
    gop_clear_screen(&gop_local, 0); // 0 is just black. (0x0000000)
    //MemoryTest();
    //__cli();
    //__hlt();
    __sti(); // only now enable interrupts
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
    
    // Initialize AHCI now.
    if (!ahci_init()) {
        CTX_FRAME ctxfr;
        SAVE_CTX_FRAME(&ctxfr);
        MtBugcheck(&ctxfr, NULL, AHCI_INIT_FAILED, 0, false);
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
    CTX_FRAME regs;
    SAVE_CTX_FRAME(&regs);
    MtBugcheck(&regs, NULL, MANUALLY_INITIATED_CRASH, 0xDEADBEEF, true);
#endif
    
    if (checkcpuid()) {
        char str[256];
        getCpuName(str);
        gop_printf(COLOR_GREEN, "CPU Identified: %s\n", str);
    }

    TIME_ENTRY currTime = get_time();

#define ISRAEL_UTC_OFFSET 3
    gop_printf(COLOR_GREEN, "Current Time: %d/%d/%d | %d:%d:%d\n", currTime.year, currTime.month, currTime.day, currTime.hour + ISRAEL_UTC_OFFSET, currTime.minute, currTime.second);

    void* writebuf = MtAllocateVirtualMemory(512, 512);
    void* readbuf = MtAllocateVirtualMemory(512, 512);
    gop_printf_forced(COLOR_YELLOW, "Attempting to WRITE from writebuf... PHYS: %p\n", MtTranslateVirtualToPhysical(writebuf));
    // set writebuf to something to signify if it was re-written
    kmemset(writebuf, 12, 512);
    if (ahci_write_sector(get_block_device(0), 1, writebuf)) {
        if (ahci_read_sector(get_block_device(0), 1, readbuf)) {
            for (int i = 0; i < 512; i++) {
                gop_printf(COLOR_RED, "%d", ((uint8_t*)readbuf)[i]);
            }
        }
        gop_printf(0, "\n");
    }
    else {
        gop_printf_forced(COLOR_RED, "Could not write AHCI...\n");
    }
    
    
    if (!fat32_init(0)) {
        CTX_FRAME ctxfr;
        SAVE_CTX_FRAME(&ctxfr);
        MtBugcheck(&ctxfr, NULL, FILESYSTEM_PANIC, 0, false);
    }
    
    fat32_list_root();
    
    uint32_t fileSize;
    void* textbuf = fat32_read_file("hello.txt", &fileSize);
    gop_printf(COLOR_RED, "In buffer: %s\n", textbuf);

    char buffer[256];
    ksnprintf(buffer, sizeof(buffer), "This is test data inserted by the OS! Num: %d\n", 10);
    fat32_write_file("hello.txt", (void*)buffer, sizeof(buffer));
    textbuf = fat32_read_file("hello.txt", &fileSize);
    gop_printf(COLOR_GREEN, "In hello.txt: %s\n", textbuf);

    MtCreateThread((ThreadEntry)test, NULL, DEFAULT_TIMESLICE_TICKS, true);
    int integer = 1234;
    MtCreateThread((ThreadEntry)funcWithParam, &integer, DEFAULT_TIMESLICE_TICKS, true); // I have tested 5+ threads, works perfectly as it should.
    Schedule();
}
