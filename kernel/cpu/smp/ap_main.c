#include "smp.h"
#include "../../cpu/apic/apic.h"
#include "../../intrin/intrin.h"
#include "../../intrin/atomic.h"
#include "../../interrupts/idt.h"
#include "../../cpu/cpu_types.h"
#include "../../interrupts/idt.h"

extern SMP_BOOTINFO bootInfo;
extern CPU cpus[];

extern IDT_PTR PIDT_AP;

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
    // gdt is uint64_t gdt[5];
    gdt[0] = 0;
    gdt[1] = build_seg(0, 0, 0x9A, 0x20); // code, the granulity of 0x20 sets L=1
    gdt[2] = build_seg(0, 0, 0x92, 0x00); // data

    // tss descriptor is 2 quadwords
    uint64_t tss_base = (uint64_t)tss;
    uint32_t limit = sizeof(TSS) - 1;

    // tss entry
    kmemset(tss, 0, sizeof(TSS));
    tss->rsp0 = (uint64_t)cur->VirtStackTop;
    tss->ist[0] = (uint64_t)cur->IstPFStackTop;
    tss->ist[1] = (uint64_t)cur->IstDFStackTop;
    tss->io_map_base = sizeof(TSS);

    // gdt tss descriptor
    GDTEntry64 tss_desc;
    kmemset(&tss_desc, 0, sizeof(tss_desc));
    tss_desc.limit_low = (uint16_t)(limit & 0xFFFFu);
    tss_desc.base_low = (uint16_t)(tss_base & 0xFFFFu);
    tss_desc.base_middle = (uint8_t)((tss_base >> 16) & 0xFFu);
    tss_desc.access = 0x89; /* present + type = 9 (available 64-bit TSS) */
    tss_desc.granularity = (uint8_t)(((limit >> 16) & 0x0Fu) /* | (flags<<4) */);
    tss_desc.base_high = (uint8_t)((tss_base >> 24) & 0xFFu);
    tss_desc.base_upper = (uint32_t)(tss_base >> 32);
    tss_desc.reserved = 0;

    /* copy two qwords into GDT */
    kmemcpy((void*)&gdt[3], &tss_desc, sizeof(GDTEntry64));
    const int GDT_ENTRIES = 5;

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
    unsigned short sel = 0x18; // index 3 * 8
    __asm__ volatile("ltr %w0" :: "r"(sel));
}

void ap_main(void) {
	// First, setup the GDT&TSS, then IDT.
	int idx = -1;
	// early map lapic mmio (lapic_init_cpu maps it).
	lapic_init_cpu();
	uint32_t lapic_raw = lapic_mmio_read(LAPIC_ID);
	uint8_t id = (lapic_raw >> 24) & 0xFF;

	for (int i = 0; i < (int)bootInfo.cpu_count && i < MAX_CPUS; i++) {
		if (cpus[i].lapic_ID == id) { idx = i; break; }
	}

	if (idx < 0) {
		BUGCHECK_ADDITIONALS addt = { 0 };
		ksnprintf(addt.str, sizeof(addt.str), "Could not find CPU entry.");
		MtBugcheckEx(NULL, NULL, SEVERE_MACHINE_CHECK, &addt, true);
	}
	uint64_t gs = (uint64_t)&cpus[idx];
	__writemsr(IA32_KERNEL_GS_BASE, gs);
    __swapgs();
	setup_gdt_tss();

    // set RSP to per CPU stack.
    void* stack_top = cpus[idx].VirtStackTop;
    __asm__ volatile("mov %0, %%rsp" :: "r"(stack_top));

    // Now setup the IDT for the CPU. (load the one setupped by the smp func)
    __lidt(&PIDT_AP);
	// init per cpu timer
	init_lapic_timer(100);

	// mark as online
	InterlockedOrU64(&cpus[idx].flags, CPU_ONLINE); 

	// enable interupts and join scheduler queue
	__sti();
	gop_printf(COLOR_ORANGE, "**Hello From AP CPU! - I'm ID: %d | StackTop: %p | CPU Ptr: %p**\n", id, stack_top, gs);
    //Schedule();

	for (;;) __hlt();
}