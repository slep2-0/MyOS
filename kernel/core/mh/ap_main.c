#include "../../includes/mh.h"
#include "../../includes/mg.h"
#include "../../includes/me.h"
#include "../../assert.h"

extern SMP_BOOTINFO bootInfo;
extern PROCESSOR cpus[];

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

static inline uint8_t get_initial_apic_id(void) {
    uint32_t eax, ebx, ecx, edx;

    // When EAX=1, CPUID returns processor info.
    // The initial APIC ID is in bits 31-24 of the EBX register.
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1), "c"(0));

    return (uint8_t)(ebx >> 24);
}

void APMain(void) {
	// First, setup the GDT&TSS, then IDT.
	int idx = -1;
	// early map lapic mmio (lapic_init_cpu maps it).
    uint8_t id = get_initial_apic_id();

	for (int i = 0; i < (int)bootInfo.cpu_count && i < MAX_CPUS; i++) {
		if (cpus[i].lapic_ID == id) { idx = i; break; }
	}

	if (idx < 0) {
        assert(false, "All APs must be initialized fully and successfully.");
        gop_printf(COLOR_RED, "**Fatal error, AP Failed to initialize, index below 0.**\n");
        __hlt();
	}
    __writemsr(IA32_GS_BASE, (uint64_t)&cpus[idx]);

    // Self invalidate all TLBs
    __write_cr3(__read_cr3());

    // Now setup the IDT for the CPU. (load the one setupped by the smp func)
    __lidt(&PIDT);

    // Initiate per cpu functions.
    MeInitializeProcessor(MeGetCurrentProcessor(), true, true);
    
    // Initialize the MM For current core (init PAT)
    MmInitSystem(SYSTEM_PHASE_INITIALIZE_PAT_ONLY, NULL);

    // Initialize the idle thread.
    InitScheduler();

	// mark as online and clear being unavailable
	InterlockedOrU64(&cpus[idx].flags, CPU_ONLINE); 
    InterlockedAndU64(&cpus[idx].flags, ~CPU_UNAVAILABLE);   // clear unavailable
    gop_printf(COLOR_ORANGE, "**Hello From AP CPU! - I'm ID: %d | StackTop: %p | CPU Ptr: %p**\n", id, MeGetCurrentProcessor()->VirtStackTop, MeGetCurrentProcessor());
	// enable interupts, initiate timer and join scheduler queue
    lapic_init_cpu();
    lapic_enable();
    init_lapic_timer(100);
	__sti();
    Schedule();
	for (;;) __hlt();
}