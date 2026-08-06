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

#define SMP_AP_ONLINE_TIMEOUT_MS       5000ULL
#define SMP_MAILBOX_TIMEOUT_MS         2000ULL
#define SMP_IPI_COMPLETION_TIMEOUT_MS  2000ULL

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
static void install_trampoline(void) {
	uintptr_t virt = AP_TRAMP_PHYS + PhysicalMemoryOffset;
	PMMPTE pte = MiGetPtePointer(virt);
	PMMPTE apPhysPte = MiGetPtePointer(AP_TRAMP_PHYS);
	size_t sz = (size_t)(_binary_build_ap_trampoline_bin_end - _binary_build_ap_trampoline_bin_start);
	assert((sz <= AP_TRAMP_SIZE), "Size of copy must not be larger than the binary itself");
	/* 2) Map the physical page into our page tables (virt -> AP_TRAMP_PHYS) */
	MI_WRITE_PTE_RAW(pte, virt, AP_TRAMP_PHYS, PAGE_PRESENT | PAGE_RW);
	MI_WRITE_PTE_RAW(apPhysPte, AP_TRAMP_PHYS, AP_TRAMP_PHYS, PAGE_PRESENT | PAGE_RW);

	/* 3) Copy the trampoline into that mapped page */
	kmemcpy((void*)virt, _binary_build_ap_trampoline_bin_start, sz);

	/* 4) Make sure caches/TLB don't have stale data:
	   clflush the page (per 64-byte cacheline) and invlpg the page. */
	for (uintptr_t off = 0; off < 4096; off += 64) {
		__asm__ volatile("clflush (%0)" :: "r"((char*)virt + off) : "memory");
	}
	__asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

extern PROCESSOR cpu0;
PPROCESSOR MeClockProcessor = &cpu0;

// Allocate PER CPU stack and populare cpus[]
static void prepare_percpu(uint8_t* apic_list, uint32_t cpu_count) {
    uint8_t my_id = my_lapic_id();

    for (uint32_t i = 0; i < cpu_count && i < MAX_CPUS; i++) {
        uint8_t aid = apic_list[i];

		if (aid == my_id) {
			// BSP slot, since we want synchronization for all APs, we migrate cpu0 to this global variable of CPUs, and change gs once again.
			// Debugging helped me solve this, I saw that [i].IpiSeq (i = bsp slot), was 3, but [i].self->IpiSeq is 0, which was the real one.
			// So we infinite looped.

			// Explicitly disable interrupts for synchronization.

			bool Enabled = MeDisableInterrupts();
			assert(cpu0.DpcData.DpcQueueDepth == 0);
			assert(IsListEmpty(&cpu0.DpcData.DpcListHead));
			assert(cpu0.TimerExpirationDPC.DpcData == NULL);

			// Copy all of the cpu data to here.
			kmemcpy(&cpus[i], &cpu0, sizeof(PROCESSOR));

			// Set the new self ptr and other variables.
			cpus[i].self = &cpus[i];
			cpus[i].ID = i;
			cpus[i].lapic_ID = aid;
			cpus[i].State = ProcessorStateOnline;
			cpus[i].IpiRoutineActive = false;
			cpus[i].StartupStage = ProcessorStartupOnline;
			MeClockProcessor = &cpus[i];
			InitializeListHead(&cpus[i].DpcData.DpcListHead);
			cpus[i].DpcData.DpcQueueDepth = 0;
			cpus[i].DpcData.DpcLock.locked = 0;
			InitializeListHead(&cpus[i].TimerExpirationDPC.DpcListEntry);
			cpus[i].TimerExpirationDPC.DpcData = NULL;
			if (cpus[i].currentThread) {
				cpus[i].currentThread->ActiveProcessor = &cpus[i];
			}

			// Both GS halves still point at cpu0 after the structure migration.
			__writemsr(IA32_GS_BASE, (uint64_t)&cpus[i]);
			__writemsr(IA32_KERNEL_GS_BASE, (uint64_t)&cpus[i]);

			// Re-Enable if enabled before.
			MeEnableInterrupts(Enabled);

			continue;
		}
		
		// Initialize basic values.
		cpus[i].self = &cpus[i];
		cpus[i].currentIrql = PASSIVE_LEVEL;
		cpus[i].currentThread = NULL;
		kmemset(&cpus[i].readyQueue, 0, sizeof(cpus[i].readyQueue));
		cpus[i].ID = i;
		cpus[i].lapic_ID = aid;

		// Allocate stack -- aligned 16.
		void* stack = MiCreateKernelStack(true);
		if (!stack) {
			MeBugCheckEx(
				MEMORY_LIMIT_REACHED,
				(void*)(uintptr_t)i,
				(void*)(uintptr_t)aid,
				(void*)MI_LARGE_STACK_SIZE,
				NULL
			);
		}
		cpus[i].VirtStackTop = stack;

		// IST Stack setup & GDT & TSS have been moved to MeInitProcesor function.

		// The AP cannot receive normal work until ap_main publishes Online.
		cpus[i].State = ProcessorStateUnavailable;
		cpus[i].IpiRoutineActive = false;
		cpus[i].StartupStage = ProcessorStartupAllocated;
		cpus[i].schedulePending = false;

		// DPCs & Queue
		kmemset(&cpus[i].CurrentDeferredRoutine, 0, sizeof(cpus[i].CurrentDeferredRoutine));

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
uint32_t g_cpuCount = 1; // Must be 1, to include the BSP.
uint32_t g_lapicAddress;
static volatile uint64_t MhIpiSequence = 1;

static NORETURN void
MhpSmpTimeout(
	SMP_TIMEOUT_STAGE Stage,
	PPROCESSOR TargetProcessor,
	uintptr_t Detail1,
	uintptr_t Detail2
)
{
	MeBugCheckEx(
		SMP_SYNCHRONIZATION_TIMEOUT,
		(void*)(uintptr_t)Stage,
		TargetProcessor,
		(void*)Detail1,
		(void*)Detail2
	);
}

// BSP Entry: start all APs.
void MhInitializeSMP(uint8_t* apic_list, uint32_t cpu_count, uint32_t lapicAddress) {
	if (!apic_list || cpu_count == 0 || cpu_count > MAX_CPUS) {
		MeBugCheckEx(
			INVALID_INITIALIZATION_PHASE,
			apic_list,
			(void*)(uintptr_t)cpu_count,
			(void*)(uintptr_t)MAX_CPUS,
			NULL
		);
	}

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
	MI_WRITE_PTE_RAW(pte, virt, AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	MI_WRITE_PTE_RAW(apPtePhys, AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET, AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	uint64_t ap_main_addr = (uint64_t)&APMain;
	kmemcpy((void*)virt, &ap_main_addr, sizeof(ap_main_addr));

	//// write physical address of PML4 (cr3) to CPU offset. (both virt and identity mapping it)
	virt = PhysicalMemoryOffset + AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET;
	pte = MiGetPtePointer(virt);
	apPtePhys = MiGetPtePointer((AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET));
	MI_WRITE_PTE_RAW(pte, virt, AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	MI_WRITE_PTE_RAW(apPtePhys, AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET, AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	uintptr_t cr3 = boot_info_local.Pml4Phys;
	kmemcpy((void*)virt, &cr3, sizeof(cr3));

	// write address of CPUs to the offset
	virt = PhysicalMemoryOffset + AP_TRAMP_PHYS + AP_TRAMP_CPUS_OFFSET;
	pte = MiGetPtePointer(virt);
	apPtePhys = MiGetPtePointer((AP_TRAMP_PHYS + AP_TRAMP_CPUS_OFFSET));
	MI_WRITE_PTE_RAW(pte, virt, AP_TRAMP_PHYS + AP_TRAMP_CPUS_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	MI_WRITE_PTE_RAW(apPtePhys, AP_TRAMP_PHYS + AP_TRAMP_CPUS_OFFSET, AP_TRAMP_PHYS + AP_TRAMP_CPUS_OFFSET, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	uintptr_t cpuAddress = (uintptr_t)cpus;
	kmemcpy((void*)virt, &cpuAddress, sizeof(cpuAddress));

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
		uint64_t StartTsc = MhReadTsc();

		while (InterlockedLoadAcquire(&cpus[i].State) != ProcessorStateOnline) {
			if (MhTscTimeoutExpired(StartTsc, SMP_AP_ONLINE_TIMEOUT_MS)) {
				MhpSmpTimeout(
					SmpTimeoutApOnline,
					&cpus[i],
					(uintptr_t)InterlockedLoadAcquire(
					    &cpus[i].State
					),
					(uintptr_t)InterlockedLoadAcquire(
					    &cpus[i].StartupStage
					)
				);
			}

			__pause();
		}
	}
	smpInitialized = true;
}

PPROCESSOR 
MeGetProcessorBlock(
	uint8_t ProcessorNumber // ID, not lapic_ID.
)

{
	if (!smpInitialized) return &cpu0;

	// SMP Is on, we iterate over the cpus list until we find the lapic for the processor.
	for (uint8_t i = 0; i < MeGetActiveProcessorCount(); i++) {
		if (cpus[i].ID == ProcessorNumber) return &cpus[i];
	}

	// The CPU isn't found, we return the current one.
	assert(false, "DPC Inputted wrong INDEX ID of target processor.");
	return MeGetCurrentProcessor();
}

void MhSpinAndProcessIpis(void) {
	uint64_t rflags;
	unsigned long oldCr8;
	PPROCESSOR cpu = MeGetCurrentProcessor();
	IRQL oldIrql;

	// Before SMP there is no IPI to service. Never enable a second IPI while
	// already executing on the per-CPU IPI IST stack.
	if (!smpInitialized || InterlockedLoadAcquire(&cpu->IpiRoutineActive)) {
		__pause();
		return;
	}

	// Preserve the caller's interrupt and task-priority state.
	__asm__ volatile("pushfq; pop %0" : "=rm"(rflags) :: "memory");
	oldCr8 = __read_cr8();
	oldIrql = cpu->currentIrql;

	// Permit the IPI priority class while keeping the clock, DPC, and APC
	// classes masked. A timer preemption here could otherwise schedule away
	// from a per-CPU IST and later resume on a stack that has been reused.
	__cli();
	cpu->currentIrql = CLOCK_LEVEL;
	__write_cr8(VECTOR_CLOCK >> 4);
	__asm__ volatile("sti");
	__asm__ volatile("nop");
	__asm__ volatile("cli");
	cpu->currentIrql = oldIrql;
	__write_cr8(oldCr8);

	// Restore the caller's original IF state.
	if (rflags & (1ULL << 9)) {
		__asm__ volatile("sti");
	}

	__asm__ volatile("pause");
}

static void
MhpAcquireMailbox(
	PPROCESSOR TargetProcessor
)
{
	uint64_t StartTsc = MhReadTsc();

	while (InterlockedCompareExchangeU64(
		&TargetProcessor->MailboxLock,
		1,
		0
	) != 0) {
		if (MhTscTimeoutExpired(StartTsc, SMP_MAILBOX_TIMEOUT_MS)) {
			MhpSmpTimeout(
				SmpTimeoutMailboxAcquire,
				TargetProcessor,
				(uintptr_t)InterlockedLoadAcquire(
				    &TargetProcessor->MailboxLock
				),
				(uintptr_t)InterlockedLoadAcquire(
				    &TargetProcessor->IpiSeq
				)
			);
		}

		MhSpinAndProcessIpis();
	}
}

static void
MhpWaitForIpiCompletion(
	PPROCESSOR TargetProcessor,
	uint64_t Sequence
)
{
	uint64_t StartTsc = MhReadTsc();

	while (InterlockedLoadAcquire(
	    &TargetProcessor->IpiSeq
	) == Sequence) {
		if (MhTscTimeoutExpired(StartTsc, SMP_IPI_COMPLETION_TIMEOUT_MS)) {
			MhpSmpTimeout(
				SmpTimeoutIpiCompletion,
				TargetProcessor,
				(uintptr_t)Sequence,
				(uintptr_t)InterlockedLoadAcquire(
				    &TargetProcessor->IpiSeq
				)
			);
		}

		MhSpinAndProcessIpis();
	}
}

void MhSendActionToCpusAndWait(CPU_ACTION action, IPI_PARAMS parameter) {
	if (!g_cpuCount || !smpInitialized) return;
	assert(MeGetCurrentIrql() <= DISPATCH_LEVEL);
	uint8_t myid = my_lapic_id();

	uint64_t seq = InterlockedIncrementU64(&MhIpiSequence);

	for (uint32_t i = 0; i < g_cpuCount; i++) {
		if (cpus[i].lapic_ID == myid) continue;
		if (InterlockedLoadAcquire(&cpus[i].State) != ProcessorStateOnline) continue;
		PPROCESSOR TargetProcessor = &cpus[i];

		// Complete one target transaction before acquiring another target's
		// mailbox. Holding several mailbox locks at once allows concurrent
		// broadcasts to form an SMP lock cycle.
		//
		// Keep this CPU on the same thread until the mailbox transaction is
		// complete. IPI_LEVEL remains unmasked, so crossing IPIs still run.
		IRQL OldIrql;
		MeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
		MhpAcquireMailbox(TargetProcessor);

		TargetProcessor->IpiAction = action;
		TargetProcessor->IpiParameter = parameter;
		InterlockedStoreRelease(&TargetProcessor->IpiSeq, seq);

		uint32_t LAPIC_ACTION_VECTOR = VECTOR_IPI;
		lapic_send_ipi(TargetProcessor->lapic_ID, (uint8_t)LAPIC_ACTION_VECTOR, 0x0);

		// Completion of this function still means every online CPU has
		// processed the action, but no sender owns multiple mailbox locks.
		MhpWaitForIpiCompletion(TargetProcessor, seq);

		InterlockedExchangeU64(&TargetProcessor->MailboxLock, 0);
		MeLowerIrql(OldIrql);
	}
}

void MhSendActionToSpecificCpuAndWait(PPROCESSOR TargetProcessor, CPU_ACTION action, IPI_PARAMS parameter) {
	// Ensure SMP is initialized and the target is valid.
	if (!smpInitialized || !TargetProcessor) return;

	uint8_t myid = my_lapic_id();

	// Prevent sending an IPI to ourselves and ensure the target is online.
	if (TargetProcessor->lapic_ID == myid) return;
	if (InterlockedLoadAcquire(
	    &TargetProcessor->State
	) != ProcessorStateOnline) return;
	assert(MeGetCurrentIrql() <= DISPATCH_LEVEL);

	// Generate a unique sequence number for this IPI request.
	uint64_t seq = InterlockedIncrementU64(&MhIpiSequence);

	// Acquire the mailbox lock for the target processor.
	// DISPATCH_LEVEL prevents a thread switch while the mailbox transaction is
	// live. MhSpinAndProcessIpis still admits the higher-priority IPI vector.
	IRQL OldIrql;
	MeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
	MhpAcquireMailbox(TargetProcessor);

	// Assign the action, parameters, and sequence number to the target's mailbox.
	TargetProcessor->IpiAction = action;
	TargetProcessor->IpiParameter = parameter;
	InterlockedStoreRelease(&TargetProcessor->IpiSeq, seq);

	// Send the IPI using the global IPI vector.
	lapic_send_ipi(TargetProcessor->lapic_ID, (uint8_t)VECTOR_IPI, 0x0);

	// Wait for the target processor to finish handling the IPI.
	// The target will clear or update its IpiSeq upon completion.
	MhpWaitForIpiCompletion(TargetProcessor, seq);

	// Release the mailbox lock so other processors can send requests to this CPU.
	InterlockedExchangeU64(&TargetProcessor->MailboxLock, 0);
	MeLowerIrql(OldIrql);
}
