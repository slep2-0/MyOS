/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Symmetric MultiProcessing Functions And Implementation.
 */

#include "../../assert.h"
#include "../../includes/mh.h"
#include "../../includes/mm.h"
#include "../../includes/me.h"
#include <stdint.h>

extern uint8_t _binary_build_ap_trampoline_bin_start[];
extern uint8_t _binary_build_ap_trampoline_bin_end[];

PROCESSOR cpus[MAX_CPUS];
int smp_cpu_count = 0;
SMP_BOOTINFO bootInfo;
extern bool smpInitialized;

static inline uint8_t my_lapic_id(void) {
	uint32_t x = lapic_mmio_read(LAPIC_ID);
	return (uint8_t)(x >> 24);
}

// Copy trampoline binary to low phys and map identity for this page.
// Copy trampoline binary to low phys and map identity for this page.
static void install_trampoline(void) {
	uintptr_t virt = AP_TRAMP_PHYS + PhysicalMemoryOffset;
	PMMPTE pte = MiGetPtePointer(virt);
	PMMPTE apPhysPte = MiGetPtePointer(AP_TRAMP_PHYS);
	size_t sz = (size_t)(_binary_build_ap_trampoline_bin_end - _binary_build_ap_trampoline_bin_start);
	assert((sz <= AP_TRAMP_SIZE), "Size of copy must not be larger than the binary itself");
	/* 2) Map the physical page into our page tables (virt -> AP_TRAMP_PHYS) */
	MI_WRITE_PTE(pte, virt, AP_TRAMP_PHYS, PAGE_PRESENT | PAGE_RW);
	MI_WRITE_PTE(apPhysPte, AP_TRAMP_PHYS, AP_TRAMP_PHYS, PAGE_PRESENT | PAGE_RW);

	/* 3) Copy the trampoline into that mapped page */
	kmemcpy((void*)virt, _binary_build_ap_trampoline_bin_start, sz);

	/* 4) Make sure caches/TLB don't have stale data:
	   clflush the page (per 64-byte cacheline) and invlpg the page. */
	for (uintptr_t off = 0; off < 4096; off += 64) {
		__asm__ volatile("clflush (%0)" :: "r"((char*)virt + off) : "memory");
	}
	__asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

#define CPU_STACK_SIZE (32*1024) // 64 KiB stack, 16 Byte Alignment.

extern PROCESSOR cpu0;

// Allocate PER CPU stack and populare cpus[]
static void prepare_percpu(uint8_t* apic_list, uint32_t cpu_count) {
    uint8_t my_id = my_lapic_id();

    for (uint32_t i = 0; i < cpu_count && i < MAX_CPUS; i++) {
        uint8_t aid = apic_list[i];

		if (aid == my_id) {
			// BSP slot, ensure basic mapping, aside from that, continue.
			cpus[i].self = &cpu0;
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
		// FIXME TODO MiCreateKernelStack.
		void* stack = MmAllocatePoolWithTag(NonPagedPool, CPU_STACK_SIZE, ' UPC');
		void* stackTop = (void*)((uint64_t)stack + CPU_STACK_SIZE);
		cpus[i].VirtStackTop = stackTop;
		
		// allocate tss
		void* tss = MmAllocatePoolWithTag(NonPagedPool, sizeof(TSS), ' sst'); // If fails on here, check alignment (16 byte)
		cpus[i].tss = tss;

		// setup the IST stacks.
		void* istpf = MmAllocatePoolWithTag(NonPagedPool, IST_SIZE, ' UPC');
		void* istdf = MmAllocatePoolWithTag(NonPagedPool, IST_SIZE, ' UPC');
		void* istTimer = MmAllocatePoolWithTag(NonPagedPool, IST_SIZE, ' UPC');
		void* istIpi = MmAllocatePoolWithTag(NonPagedPool, IST_SIZE, ' UPC');
#ifdef DEBUG
		if (!istpf || !istdf) {
			MeBugCheck(MEMORY_LIMIT_REACHED);
		}
#endif
		uint64_t pftop = (uint64_t)istpf + IST_SIZE;
		uint64_t dftop = (uint64_t)istdf + IST_SIZE;
		uint64_t timerTop = (uint64_t)istTimer + IST_SIZE;
		uint64_t IpiTop = (uint64_t)istIpi + IST_SIZE;
		cpus[i].IstPFStackTop = (void*)pftop;
		cpus[i].IstDFStackTop = (void*)dftop;
		cpus[i].IstTimerStackTop = (void*)timerTop;
		cpus[i].IstIpiStackTop = (void*)IpiTop;

		// CPU Flags
		cpus[i].flags |= CPU_UNAVAILABLE; // Start unavailable.
		cpus[i].schedulePending = false;

		// GDT
		uint64_t* gdt = MmAllocatePoolWithTag(NonPagedPool, sizeof(uint64_t) * 7, ' TDG');
		cpus[i].gdt = gdt;

		// DPCs & Queue
		cpus[i].DeferredRoutineQueue.dpcQueueHead = cpus[i].DeferredRoutineQueue.dpcQueueTail = NULL;
		kmemset(&cpus[i].CurrentDeferredRoutine, 0, sizeof(cpus[i].CurrentDeferredRoutine));

		// Function Trace Buffer
		LASTFUNC_HISTORY* bfr = MmAllocatePoolWithTag(NonPagedPool, sizeof(LASTFUNC_HISTORY), 'TSAL');
		cpus[i].lastfuncBuffer = bfr;
		cpus[i].lastfuncBuffer->current_index = -1; // init to -1
	}
	smp_cpu_count = cpu_count;
}

static void send_startup_ipis(uint8_t apic_id) {
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
void MhInitializeSMP(uint8_t* apic_list, uint32_t cpu_count, uint32_t lapicAddress) {
	// populate cpus and per cpu stacks.
	prepare_percpu(apic_list, cpu_count);
	// copy trampoline
	install_trampoline();

	// Fill in the globals.
	g_cpuCount = cpu_count;
	g_lapicAddress = lapicAddress;
	for (uint32_t i = 0; i < cpu_count; i++) {
		g_apic_list[i] = apic_list[i];
	}
	
	bootInfo.magic = SMP_MAGIC;
	bootInfo.kernel_pml4_phys = boot_info_local.Pml4Phys;
	bootInfo.ap_entry_virt = (uint64_t)&APMain;
	bootInfo.cpu_count = cpu_count;
	bootInfo.lapic_base = lapicAddress;

	// write address of ap main to the offset
	uintptr_t virt = PhysicalMemoryOffset + AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET;
	PMMPTE pte = MiGetPtePointer(virt);
	PMMPTE apPtePhys = MiGetPtePointer((AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET));
	MI_WRITE_PTE(pte, virt, AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	MI_WRITE_PTE(apPtePhys, AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET, AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	uint64_t ap_main_addr = (uint64_t)&APMain;
	kmemcpy((void*)virt, &ap_main_addr, sizeof(ap_main_addr));

	//// write physical address of PML4 (cr3) to CPU offset. (both virt and identity mapping it)
	virt = PhysicalMemoryOffset + AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET;
	pte = MiGetPtePointer(virt);
	apPtePhys = MiGetPtePointer((AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET));
	MI_WRITE_PTE(pte, virt, AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	MI_WRITE_PTE(apPtePhys, AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET, AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	uintptr_t cr3 = boot_info_local.Pml4Phys;
	kmemcpy((void*)virt, &cr3, sizeof(cr3));

	// send INIT/SIPI/SIPI to APs (skip BSP)
	uint8_t my_id = my_lapic_id();
	for (uint32_t i = 0; i < cpu_count; i++) {
		uint8_t aid = apic_list[i];
		if (aid == my_id) continue;
		send_startup_ipis(aid);
	}
	// over - Application Processors (the other CPUs) should execute trampoline and call ap_main();
	// now, we wait until all are online.
	for (uint32_t i = 0; i < g_cpuCount; i++) {
		while (!(cpus[i].flags & CPU_ONLINE)) {
			__pause();
		}
	}
	smpInitialized = true;
}

void MhSendActionToCpusAndWait(CPU_ACTION action, IPI_PARAMS parameter) {
	if (!g_cpuCount || !smpInitialized) return;
	uint8_t myid = my_lapic_id();

	static uint64_t g_ipiSeq = 1;
	uint64_t seq = InterlockedIncrementU64(&g_ipiSeq);

	__asm__ volatile("mfence" ::: "memory");

	for (uint32_t i = 0; i < g_cpuCount; i++) {
		if (cpus[i].lapic_ID == myid) continue;
		if (!(cpus[i].flags & CPU_ONLINE)) continue;

		cpus[i].IpiAction = action;
		cpus[i].IpiParameter = parameter;

		cpus[i].IpiSeq = seq; // assign sequence number
		lapic_send_ipi(cpus[i].lapic_ID, LAPIC_ACTION_VECTOR, 0x0);
	}

	// wait for all CPUs to handle this exact IPI
	for (uint32_t i = 0; i < g_cpuCount; i++) {
		if (cpus[i].lapic_ID == myid || !(cpus[i].flags & CPU_ONLINE))
			continue;

		while (cpus[i].IpiSeq == seq) {
			__pause(); // spin until they clear the seq
		}
	}
}