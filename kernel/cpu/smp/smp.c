/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Symmetric MultiProcessing Functions And Implementation.
 */

#include "smp.h"
#include "../../cpu/cpu.h"
#include "../../core/memory/paging/paging.h"
#include "../../core/memory/memory.h"
#include "../../cpu/apic/apic.h"
#include "../../core/bugcheck/bugcheck.h"
#include "../../assert.h"
#include "../../core/interrupts/idt.h"

extern uint8_t _binary_build_ap_trampoline_bin_start[];
extern uint8_t _binary_build_ap_trampoline_bin_end[];

CPU cpus[MAX_CPUS];
int smp_cpu_count = 0;
SMP_BOOTINFO bootInfo;

static inline uint8_t my_lapic_id(void) {
	uint32_t x = lapic_mmio_read(LAPIC_ID);
	return (uint8_t)(x >> 24);
}

// Copy trampoline binary to low phys and map identity for this page.
// Copy trampoline binary to low phys and map identity for this page.
static void install_trampoline(void) {
	tracelast_func("install_trampoline");
	void* virt = MtTranslatePhysicalMemoryToVirtualOffset(AP_TRAMP_PHYS);
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
    uint8_t my_id = my_lapic_id();

    for (uint32_t i = 0; i < cpu_count && i < MAX_CPUS; i++) {
        uint8_t aid = apic_list[i];

		if (aid == my_id) {
			// BSP slot, ensure basic mapping, aside from that, continue.
			cpus[i].ID = i;
			cpus[i].lapic_ID = aid;
			cpus[i].flags = CPU_ONLINE;
			continue;
		}
		
		// Initialize basic values.
		cpus[i].self = &cpus[i];
		cpus[i].currentIrql = PASSIVE_LEVEL;
		cpus[i].schedulerEnabled = false;
		cpus[i].currentThread = NULL;
		kmemset(&cpus[i].readyQueue, 0, sizeof(cpus[i].readyQueue));
		cpus[i].ID = i;
		cpus[i].lapic_ID = aid;

		// Allocate stack -- aligned 16.
		void* stack = MtAllocateGuardedVirtualMemory(CPU_STACK_SIZE, 16);
		void* stackTop = (void*)((uint64_t)stack + CPU_STACK_SIZE);
		cpus[i].VirtStackTop = stackTop;

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

		// CPU Flags
		cpus[i].flags |= CPU_UNAVAILABLE; // Start unavailable.
		cpus[i].schedulePending = false;

		// GDT
		uint64_t* gdt = MtAllocateVirtualMemory(sizeof(uint64_t) * 7, 16); // 16 byte aligned
		cpus[i].gdt = gdt;

		// DPCs & Queue
		cpus[i].DeferredRoutineQueue.dpcQueueHead = cpus[i].DeferredRoutineQueue.dpcQueueTail = NULL;
		kmemset(&cpus[i].CurrentDeferredRoutine, 0, sizeof(cpus[i].CurrentDeferredRoutine));
	}
	smp_cpu_count = cpu_count;
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

// Globals for use of IPI & other functions.
uint8_t g_apic_list[MAX_CPUS];
uint32_t g_cpuCount;
uint32_t g_lapicAddress;

// BSP Entry: start all APs.
void smp_start(uint8_t* apic_list, uint32_t cpu_count, uint32_t lapicAddress) {
	tracelast_func("smp_start");
	gop_printf(COLOR_GRAY, "**Hit SMP_START**\n");
	// populate cpus and per cpu stacks.
	prepare_percpu(apic_list, cpu_count);
	// copy trampoline
	install_trampoline();

	{
		// Fill in the globals.
		g_cpuCount = cpu_count;
		g_lapicAddress = lapicAddress;
		for (uint32_t i = 0; i < cpu_count; i++) {
			g_apic_list[i] = apic_list[i];
		}
	}
	
	bootInfo.magic = SMP_MAGIC;
	bootInfo.kernel_pml4_phys = boot_info_local.Pml4Phys;
	bootInfo.ap_entry_virt = (uint64_t)&ap_main;
	bootInfo.cpu_count = cpu_count;
	bootInfo.lapic_base = lapicAddress;

	// write address of ap main to the offset
	void* virt = MtTranslatePhysicalMemoryToVirtualOffset(AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET);
	map_page(virt, AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	map_page((void*)(AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET), AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	uint64_t ap_main_addr = (uint64_t)&ap_main;
	kmemcpy(virt, &ap_main_addr, sizeof(ap_main_addr));

	//// write physical address of PML4 (cr3) to CPU offset. (both virt and identity mapping it)
	virt = MtTranslatePhysicalMemoryToVirtualOffset(AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET);
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
	gop_printf(COLOR_BLUE, "**returning**\n");
	// over - Application Processors (the other CPUs) should execute trampoline and call ap_main();
	// now, we wait until all are online.
	for (uint32_t i = 0; i < g_cpuCount; i++) {
		while (!(cpus[i].flags & CPU_ONLINE)) {
			__pause();
		}
	}
	extern bool smpInitialized;
	smpInitialized = true;
}

void MtSendActionToCpus(CPU_ACTION action, uint64_t parameter) {
	if (!g_cpuCount) return;
	uint8_t myid = my_lapic_id();

	__asm__ volatile("mfence" ::: "memory");

	for (uint32_t i = 0; i < g_cpuCount; i++) {
		if (cpus[i].lapic_ID == myid) continue; // skip ourselves
		if (!(cpus[i].flags & CPU_ONLINE)) continue; // skip CPUs that aren't online yet.
		if (cpus[i].flags & CPU_UNAVAILABLE) continue; // skip unavailable cpus (cpus shouldnt be marked as unavailable mostly)
		if (cpus[i].flags & CPU_DOING_IPI) continue; // skip CPUs that are doing an IPI right now.

		// Set the pending action
		cpus[i].IpiAction = action;
		if (action == CPU_ACTION_PERFORM_TLB_SHOOTDOWN) cpus[i].IpiParameter = parameter;
		lapic_send_ipi(cpus[i].lapic_ID, LAPIC_ACTION_VECTOR, 0x0);

		while (*(volatile uint64_t*)&cpus[i].flags & CPU_DOING_IPI) {
			__pause();
		}
	}
}