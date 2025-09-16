/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Symmetric MultiProcessing Functions And Implementation.
 */

#include "smp.h"
#include "../../cpu/cpu.h"
#include "../../memory/paging/paging.h"
#include "../../memory/memory.h"
#include "../../cpu/apic/apic.h"
#include "../../bugcheck/bugcheck.h"
#include "../../assert.h"
#include "../../interrupts/idt.h"

extern uint8_t _binary_build_ap_trampoline_bin_start[];
extern uint8_t _binary_build_ap_trampoline_bin_end[];

CPU cpus[MAX_CPUS];
int smp_cpu_count = 0;
SMP_BOOTINFO bootInfo;

static inline void* phys_to_virt(uintptr_t phys) {
	return (void*)(phys + PHYS_MEM_OFFSET);
}

static inline uintptr_t virt_to_phys(void* virt) {
	return ((uintptr_t)virt) - PHYS_MEM_OFFSET;
}

// Copy trampoline binary to low phys and map identity for this page.
// Copy trampoline binary to low phys and map identity for this page.
static void install_trampoline(void) {
	tracelast_func("install_trampoline");
	void* virt = phys_to_virt(AP_TRAMP_PHYS);
	size_t sz = (size_t)(_binary_build_ap_trampoline_bin_end - _binary_build_ap_trampoline_bin_start);
	assert((sz <= AP_TRAMP_SIZE), "Size of copy must not be larger than the binary itself");
	/* 2) Map the physical page into our page tables (virt -> AP_TRAMP_PHYS) */
	map_page(virt, AP_TRAMP_PHYS, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	map_page((void*)AP_TRAMP_PHYS, AP_TRAMP_PHYS, PAGE_PRESENT | PAGE_RW | PAGE_PCD);

	/* 3) Copy the trampoline into that mapped page */
	kmemcpy(virt, _binary_build_ap_trampoline_bin_start, sz);

	/* 4) Make sure caches/TLB don't have stale data:
	   clflush the page (per 64-byte cacheline) and invlpg the page. */
	for (uintptr_t off = 0; off < 4096; off += 64) {
		__asm__ volatile("clflush (%0)" :: "r"((char*)virt + off) : "memory");
	}
	__asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

#define CPU_STACK_SIZE (32*1024) // 32 KiB stack, 4Kib Alignment.

// Allocate PER CPU stack and populare cpus[]
static void prepare_percpu(uint8_t* apic_list, uint32_t cpu_count) {
	tracelast_func("prepare_percpu");
	for (uint32_t i = 0; i < cpu_count && i < MAX_CPUS; i++) {
		// initialize members
		cpus[i].self = &cpus[i];
		cpus[i].currentIrql = PASSIVE_LEVEL;
		cpus[i].currentThread = NULL;
		cpus[i].readyQueue.head = cpus[i].readyQueue.tail = NULL;
		cpus[i].ID = i;
		cpus[i].lapic_ID = apic_list[i];
		void* stack = MtAllocateGuardedVirtualMemory(CPU_STACK_SIZE, 0x1000);
#ifdef DEBUG
		if (!stack) {
			BUGCHECK_ADDITIONALS addt = { 0 };
			ksnprintf(addt.str, sizeof(addt.str), "Could not allocate stack for CPUs..");
			MtBugcheckEx(NULL, NULL, SEVERE_MACHINE_CHECK, &addt, true);
		}
#endif

		uint64_t stack_top = (uint64_t)stack + CPU_STACK_SIZE;
		cpus[i].VirtStackTop = (void*)stack_top;
		cpus[i].flags = 0;
		// readyQueue / currentThread are zero for now.

		// allocate tss
		void* tss = MtAllocateVirtualMemory(sizeof(TSS), 16); // tss must be 16 byte aligned.
		cpus[i].tss = tss;

		// setup the IST stacks.
		void* istpf = MtAllocateGuardedVirtualMemory(IST_SIZE, IST_ALIGNMENT);
		void* istdf = MtAllocateGuardedVirtualMemory(IST_SIZE, IST_ALIGNMENT);
#ifdef DEBUG
		if (!istpf || !istdf) {
			BUGCHECK_ADDITIONALS addt = { 0 };
			ksnprintf(addt.str, sizeof(addt.str), "Could not allocate IST df/pf stack for CPUs..");
			MtBugcheckEx(NULL, NULL, SEVERE_MACHINE_CHECK, &addt, true);
		}
#endif
		uint64_t pftop = (uint64_t)istpf + IST_SIZE;
		uint64_t dftop = (uint64_t)istdf + IST_SIZE;
		gop_printf(COLOR_RED, "**istpf: %p | istdf: %p | top pf: %p | top df: %p**\n", istpf, istdf, pftop, dftop);
		cpus[i].IstPFStackTop = (void*)pftop;
		cpus[i].IstDFStackTop = (void*)dftop;
		uint64_t* gdt = MtAllocateVirtualMemory(sizeof(uint64_t) * 5, 16); // 16 byte aligned
		cpus[i].gdt = gdt;
	}
	smp_cpu_count = cpu_count;
}

static inline uint8_t my_lapic_id(void) {
	uint32_t x = lapic_mmio_read(LAPIC_ID);
	return (uint8_t)(x >> 24);
}

static void send_startup_ipis(uint8_t apic_id) {
	tracelast_func("send_startup_ipis");
	// init
	lapic_send_ipi(apic_id, 0, (0x5 << 8) | (1 << 14)); // init assert
	pit_sleep_ms(10);

	uint8_t vector = (uint8_t)(AP_TRAMP_PHYS >> 12);

	// SIPI x2
	lapic_send_ipi(apic_id, vector, (0x6 << 8));
	pit_sleep_ms(1);
	lapic_send_ipi(apic_id, vector, (0x6 << 8));
	pit_sleep_ms(1);
}

IDT_ENTRY64 IDT_ENTRY_AP[IDT_ENTRIES];
IDT_PTR PIDT_AP;

static void set_idt_gate_ap(int n, unsigned long int handler) {
	tracelast_func("set_idt_gate_ap");
	IDT_ENTRY_AP[n].offset_low = handler & 0xFFFF;
	IDT_ENTRY_AP[n].selector = 0x08;   // code segment selector
	IDT_ENTRY_AP[n].ist = 0;
	IDT_ENTRY_AP[n].type_attr = 0x8E;  // interrupt gate, present, ring 0
	IDT_ENTRY_AP[n].offset_mid = (handler >> 16) & 0xFFFF;
	IDT_ENTRY_AP[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
	IDT_ENTRY_AP[n].zero = 0;
}

static void setup_idt(void) {
	// all idts are the same, just use the one we defined already.
	tracelast_func("install_idt");
	/* REMAP the PIC so IRQs start at vector 0x20 */
	__outbyte(0x20, 0x11); // initialize master PIC
	__outbyte(0xA0, 0x11); // initialize slave PIC
	__outbyte(0x21, 0x20); // master PIC vector offset 0x20.
	__outbyte(0xA1, 0x28); // slave PIC vector offset 0x28
	__outbyte(0x21, 0x04);
	__outbyte(0xA1, 0x02);
	__outbyte(0x21, 0x01);
	__outbyte(0xA1, 0x01);
	__outbyte(0x21, 0x0);
	__outbyte(0xA1, 0x0);

	/* Fill IDT Entries for CPU Exceptions (0-31) */ /* For clarifications, all of the ISR and IRQ externals live in isr_stub (where it defines the functions and gets linked together, via the global keyword) and isr_common_stub (where it does the routine), where they are linked together via the linker (externs) */
	extern void isr0(void); extern void isr1(void); extern void isr2(void); extern void isr3(void); extern void isr4(void); extern void isr5(void); extern void isr6(void); extern void isr7(void); extern void isr8(void); extern void isr9(void); extern void isr10(void); extern void isr11(void); extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void); extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void); extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void); extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void); extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);
	/* I forgo t to set n in the set_idt_gate, they were all zeros and I didn't understand why I got IRQ of like 50 thousand and error code of 4 billion. (i copy pasted each line instead of typing manually) */
	set_idt_gate_ap(0, (unsigned long)isr0);
	set_idt_gate_ap(1, (unsigned long)isr1);
	set_idt_gate_ap(2, (unsigned long)isr2);
	set_idt_gate_ap(3, (unsigned long)isr3);
	set_idt_gate_ap(4, (unsigned long)isr4);
	set_idt_gate_ap(5, (unsigned long)isr5);
	set_idt_gate_ap(6, (unsigned long)isr6);
	set_idt_gate_ap(7, (unsigned long)isr7);
	set_idt_gate_ap(8, (unsigned long)isr8);
	set_idt_gate_ap(9, (unsigned long)isr9);
	set_idt_gate_ap(10, (unsigned long)isr10);
	set_idt_gate_ap(11, (unsigned long)isr11);
	set_idt_gate_ap(12, (unsigned long)isr12);
	set_idt_gate_ap(13, (unsigned long)isr13);
	set_idt_gate_ap(14, (unsigned long)isr14);
	set_idt_gate_ap(15, (unsigned long)isr15);
	set_idt_gate_ap(16, (unsigned long)isr16);
	set_idt_gate_ap(17, (unsigned long)isr17);
	set_idt_gate_ap(18, (unsigned long)isr18);
	set_idt_gate_ap(19, (unsigned long)isr19);
	set_idt_gate_ap(20, (unsigned long)isr20);
	set_idt_gate_ap(21, (unsigned long)isr21);
	set_idt_gate_ap(22, (unsigned long)isr22);
	set_idt_gate_ap(23, (unsigned long)isr23);
	set_idt_gate_ap(24, (unsigned long)isr24);
	set_idt_gate_ap(25, (unsigned long)isr25);
	set_idt_gate_ap(26, (unsigned long)isr26);
	set_idt_gate_ap(27, (unsigned long)isr27);
	set_idt_gate_ap(28, (unsigned long)isr28);
	set_idt_gate_ap(29, (unsigned long)isr29);
	set_idt_gate_ap(30, (unsigned long)isr30);
	set_idt_gate_ap(31, (unsigned long)isr31);

	/* Fill IDT Gates for IRQs (32-47) */
	extern void irq0(void); extern void irq1(void); extern void irq2(void); extern void irq3(void); extern void irq4(void); extern void irq5(void); extern void irq6(void); extern void irq7(void); extern void irq8(void); extern void irq9(void); extern void irq10(void); extern void irq11(void); extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);
	set_idt_gate_ap(32, (unsigned long)irq0);
	set_idt_gate_ap(33, (unsigned long)irq1);
	set_idt_gate_ap(34, (unsigned long)irq2);
	set_idt_gate_ap(35, (unsigned long)irq3);
	set_idt_gate_ap(36, (unsigned long)irq4);
	set_idt_gate_ap(37, (unsigned long)irq5);
	set_idt_gate_ap(38, (unsigned long)irq6);
	set_idt_gate_ap(39, (unsigned long)irq7);
	set_idt_gate_ap(40, (unsigned long)irq8);
	set_idt_gate_ap(41, (unsigned long)irq9);
	set_idt_gate_ap(42, (unsigned long)irq10);
	set_idt_gate_ap(43, (unsigned long)irq11);
	set_idt_gate_ap(44, (unsigned long)irq12);
	set_idt_gate_ap(45, (unsigned long)irq13);
	set_idt_gate_ap(46, (unsigned long)irq14);
	set_idt_gate_ap(47, (unsigned long)irq15);
#define LAPIC_TIMER_VECTOR 0xEF
	/* For LAPIC */
	extern void isr239(void); // LAPIC ISR.
	set_idt_gate_ap(LAPIC_TIMER_VECTOR, (unsigned long)isr239);
#define LAPIC_SPURIOUS_VECTOR 254
	/* For SIV LAPIC */
	extern void isr254(void); // SIV ISR
	set_idt_gate_ap(LAPIC_SPURIOUS_VECTOR, (unsigned long)isr254);

	/* Enable IST for Page Fault and Double Fault */
	IDT_ENTRY_AP[14].ist = 1;  // uses tss.ist[0] (page fault)
	IDT_ENTRY_AP[8].ist = 2;  // uses tss.ist[1] (double fault)

	/* Finally, Load IDT. */
	PIDT_AP.limit = sizeof(IDT_ENTRY64) * IDT_ENTRIES - 1; // Max limit is the amount of IDT_ENTRIES structs (0-255)
	PIDT_AP.base = (unsigned long)&IDT_ENTRY_AP;
	return;
}

// BSP Entry: start all APs.
void smp_start(uint8_t* apic_list, uint32_t cpu_count) {
	tracelast_func("smp_start");
	gop_printf(COLOR_GRAY, "**Hit SMP_START**\n");
	// populate cpus and per cpu stacks.
	prepare_percpu(apic_list, cpu_count);
	// init idt
	setup_idt();
	// copy trampoline
	install_trampoline();

	bootInfo.magic = SMP_MAGIC;
	bootInfo.kernel_pml4_phys = boot_info_local.Pml4Phys;
	bootInfo.ap_entry_virt = (uint64_t)&ap_main;
	bootInfo.cpu_count = cpu_count;
	bootInfo.lapic_base = 0xFEE00000ULL; // LAPIC Base Spec.

	// write address of ap main to the offset
	void* virt = phys_to_virt(AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET);
	map_page(virt, AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	map_page((void*)(AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET), AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	uint64_t ap_main_addr = (uint64_t)&ap_main;
	gop_printf(COLOR_ORANGE, "**AP_MAIN_ADDR: %p**", ap_main_addr);
	kmemcpy(virt, &ap_main_addr, sizeof(ap_main_addr));

	//// write physical address of PML4 (cr3) to CPU offset. (both virt and identity mapping it)
	virt = phys_to_virt(AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET);
	map_page(virt, AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	map_page((void*)(AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET), AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	uintptr_t cr3 = boot_info_local.Pml4Phys;
	kmemcpy(virt, &cr3, sizeof(cr3));

	// send INIT/SIPI/SIPI to APs (skip BSP)
	uint8_t my_id = my_lapic_id();
	for (uint32_t i = 0; i < cpu_count; i++) {
		uint8_t aid = apic_list[i];
		if (aid == my_id) continue;
		send_startup_ipis(aid);
	}
	// over - Application Processors (the other CPUs) should execute trampoline and call ap_main();
}