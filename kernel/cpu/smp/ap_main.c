#include "smp.h"
#include "../../cpu/apic/apic.h"
#include "../../intrinsics/intrin.h"
#include "../../intrinsics/atomic.h"
#include "../../core/interrupts/idt.h"
#include "../../cpu/cpu_types.h"

extern SMP_BOOTINFO bootInfo;
extern CPU cpus[];

extern IDT_PTR PIDT;

static inline uint64_t build_seg(uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    uint64_t desc = 0;
    desc = (limit & 0xFFFFull);
    desc |= (uint64_t)(base & 0xFFFFull) << 16;
    desc |= (uint64_t)((base >> 16) & 0xFFull) << 32;
    desc |= (uint64_t)access << 40;
    uint8_t gran_byte = (uint8_t)(((limit >> 16) & 0x0Fu) | (gran & 0xF0u));
    desc |= (uint64_t)gran_byte << 48;
    desc |= (uint64_t)((base >> 24) & 0xFFull) << 56;
    return desc;
}

static void setup_gdt_tss(void) {
    CPU* cur = thisCPU();
    TSS* tss = cur->tss;
    uint64_t* gdt = cur->gdt;
    // gdt is uint64_t gdt[7];
    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFF;
    gdt[2] = 0x00CF92000000FFFF;
    // user code & data
    gdt[3] = 0x00AFFA000000FFFF;
    gdt[4] = 0x00CFF2000000FFFF;
    uint64_t tss_base = (uint64_t)tss;
    uint32_t limit = sizeof(TSS) - 1;

    // tss entry
    kmemset(tss, 0, sizeof(TSS));
    tss->rsp0 = (uint64_t)cur->VirtStackTop;
    tss->ist[0] = (uint64_t)cur->IstPFStackTop;
    tss->ist[1] = (uint64_t)cur->IstDFStackTop;
    tss->io_map_base = sizeof(TSS);

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

static void InitPerCPU(void) {
    thisCPU()->self = thisCPU();
    thisCPU()->currentIrql = PASSIVE_LEVEL;
    thisCPU()->schedulerEnabled = NULL; // since NULL is 0, it would be false.
    thisCPU()->currentThread = NULL;
    thisCPU()->readyQueue.head = thisCPU()->readyQueue.tail = NULL;
    spinlock_init(&thisCPU()->readyQueue.lock);
}

static inline uint8_t get_initial_apic_id(void) {
    uint32_t eax, ebx, ecx, edx;

    // When EAX=1, CPUID returns processor info.
    // The initial APIC ID is in bits 31-24 of the EBX register.
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1), "c"(0));

    return (uint8_t)(ebx >> 24);
}

void ap_main(void) {
	// First, setup the GDT&TSS, then IDT.
	int idx = -1;
	// early map lapic mmio (lapic_init_cpu maps it).
    uint8_t id = get_initial_apic_id();

	for (int i = 0; i < (int)bootInfo.cpu_count && i < MAX_CPUS; i++) {
		if (cpus[i].lapic_ID == id) { idx = i; break; }
	}

	if (idx < 0) {
        gop_printf(COLOR_RED, "Fatal error, AP Failed to initialize, index below 0.\n");
        __hlt();
	}
    __writemsr(IA32_KERNEL_GS_BASE, (uint64_t)&cpus[idx]);
    __swapgs();
	setup_gdt_tss();

    // Self invalidate all TLBs
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" : : : "rax", "memory");

    // set RSP to per CPU stack.
    void* stack_top = cpus[idx].VirtStackTop;
    __asm__ volatile("mov %0, %%rsp" :: "r"(stack_top));

    // Now setup the IDT for the CPU. (load the one setupped by the smp func)
    __lidt(&PIDT);

extern void InitialiseControlRegisters(void);

    // Initiate per cpu functions.
    InitPerCPU();
    InitScheduler();
    init_dpc_system();
    InitialiseControlRegisters();

	// mark as online and clear being unavailable
	InterlockedOrU64(&cpus[idx].flags, CPU_ONLINE); 
    InterlockedAndU64(&cpus[idx].flags, ~CPU_UNAVAILABLE);   // clear unavailable
    gop_printf(COLOR_ORANGE, "**Hello From AP CPU! - I'm ID: %d | StackTop: %p | CPU Ptr: %p**\n", id, stack_top, thisCPU());
	// enable interupts, initiate timer and join scheduler queue
    lapic_init_cpu();
    lapic_enable();
    init_lapic_timer(100);
	__sti();
    Schedule();
	for (;;) __hlt();
}