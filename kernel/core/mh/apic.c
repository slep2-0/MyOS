#include "../../includes/me.h"
#include "../../includes/mh.h"
#include "../../includes/mm.h"
#include <stddef.h>
#include <stdint.h>

#define IA32_APIC_BASE_MSR     0x1BULL
#define APIC_BASE_RESERVED     0xFFF0000000000000ULL

#define LAPIC_PAGE_SIZE    0x1000
#define LAPIC_MAP_FLAGS (PAGE_PRESENT | PAGE_RW | PAGE_PCD)

// LAPIC register offsets (32-bit registers)
enum {
    LAPIC_VERSION = 0x030,
    LAPIC_TPR = 0x080,
    LAPIC_EOI = 0x0B0,
    LAPIC_SVR = 0x0F0,
    LAPIC_ESR = 0x280,
    LAPIC_ICR_LOW = 0x300,
    LAPIC_ICR_HIGH = 0x310,
    LAPIC_LVT_TIMER = 0x320,
    LAPIC_LVT_THERMAL = 0x330,
    LAPIC_LVT_PCC = 0x340,
    LAPIC_LVT_LINT0 = 0x350,
    LAPIC_LVT_LINT1 = 0x360,
    LAPIC_LVT_ERROR = 0x370,
    LAPIC_TIMER_INITCNT = 0x380,
    LAPIC_TIMER_CURRCNT = 0x390,
    LAPIC_TIMER_DIV = 0x3E0
};

// --- low-level mmio helpers (assumes lapic mapped to virtual memory) ---
uint32_t lapic_mmio_read(uint32_t off) {
    return MeGetCurrentProcessor()->LapicAddressVirt[off / 4];
}

void lapic_mmio_write(uint32_t off, uint32_t val) {
    MeGetCurrentProcessor()->LapicAddressVirt[off / 4] = val;
    (void)MeGetCurrentProcessor()->LapicAddressVirt[0]; // Serializing read
}

// Wait for ICR delivery to complete (ICR low: bit 12 = Delivery Status)
static void lapic_wait_icr(void) {
    while (lapic_mmio_read(LAPIC_ICR_LOW) & (1 << 12)) {
        /* spin */
        __pause();
    }
}

// Initialize the Spurious Interrupt Vector
void lapic_init_siv(void) {
    uint32_t svr = lapic_mmio_read(LAPIC_SVR);
    uint32_t vector = 0xFF; // IDT Entry
    svr = (svr & 0xFFFFFF00) | vector; // preserve enable bit, update vector.
    lapic_mmio_write(LAPIC_SVR, svr);
}

static void map_lapic(uint64_t lapicPhysicalAddr) {
    if (MeGetCurrentProcessor()->LapicAddressVirt) return;

    void* virt = (void*)(lapicPhysicalAddr + PhysicalMemoryOffset);

    // Map the single LAPIC page (phys -> virt)
    PMMPTE pte = MiGetPtePointer((uintptr_t)virt);
    if (!pte) return;
    MI_WRITE_PTE(pte, virt, lapicPhysicalAddr, PAGE_PRESENT | PAGE_RW | PAGE_PCD);

    // store the mmio base pointer
    MeGetCurrentProcessor()->LapicAddressVirt = (volatile uint32_t*)virt;
    MeGetCurrentProcessor()->LapicAddressPhys = lapicPhysicalAddr;
}

static inline uint64_t get_lapic_base_address(void) {
    uint32_t eax, edx;

    // The 'rdmsr' instruction reads a 64-bit MSR into the EDX:EAX registers.
    __asm__ volatile("rdmsr" : "=a"(eax), "=d"(edx) : "c"(IA32_APIC_BASE_MSR));

    // Combine the high (edx) and low (eax) parts into a 64-bit value.
    uint64_t msr_value = ((uint64_t)edx << 32) | eax;

    // The address is in bits 12 through the most significant bit.
    // We must mask off the lower 12 bits which contain flags.
    return msr_value & ~0xFFFULL;
}

// Enable local APIC via IA32_APIC_BASE MSR and set SVR
void lapic_enable(void) {
    uint64_t apic_msr = __readmsr(IA32_APIC_BASE_MSR);
    if (!(apic_msr & (1ULL << 11))) {
        // set APIC global enable
        apic_msr |= (1ULL << 11);
        __writemsr(IA32_APIC_BASE_MSR, apic_msr);
    }
    map_lapic(get_lapic_base_address());

    // Set Spurious Vector Register and enable (bit 8 = APIC enable)
    uint32_t svr = (0xFF) | (1 << 8);
    lapic_mmio_write(LAPIC_SVR, svr);
}

// Initialize CPU's LAPIC (call early from kernel init on BSP, and from each ap)
void lapic_init_cpu(void) {
    map_lapic(get_lapic_base_address());

    lapic_enable();

    // mask LINT0/LINT1 as appropriate, clear error status, etc.
    lapic_mmio_write(LAPIC_LVT_LINT0, (1U << 16)); // mask
    lapic_mmio_write(LAPIC_LVT_LINT1, (1U << 16)); // mask
    lapic_mmio_write(LAPIC_LVT_ERROR, (1U << 16)); // mask (until handler in place)
    lapic_mmio_write(LAPIC_EOI, 0);
}

// send IPI to APIC id 
// apic_id - APICId of the CPU.
// vector - IDT Vector number
// flags - specified cpu flags, 0 for none.
void lapic_send_ipi(uint8_t apic_id, uint8_t vector, uint32_t flags) {
    uint32_t high = ((uint32_t)apic_id) << 24;
    lapic_mmio_write(LAPIC_ICR_HIGH, high);
    lapic_mmio_write(LAPIC_ICR_LOW, (uint32_t)vector | flags);
    lapic_wait_icr();
}

void lapic_eoi(void) {
    lapic_mmio_write(LAPIC_EOI, 0);
}

// --- Timer calibration and init ---
// NOTE: the APIC timer is a downward counter. Strategy:
//  1. Set divide to known divisor.
//  2. Write initcount = 0xFFFFFFFF.
//  3. Wait EXACTLY 100 ms via PIT/HPET.
//  4. curr = read current count -> ticks_in_100ms = start - curr
//  5. ticks_per_period(10ms) = ticks_in_100ms / 10
//  6. Program LVT timer to periodic and initial count = ticks_per_period
//
#define APIC_LVT_TIMER_PERIODIC (1U << 17)
#define APIC_TIMER_MASKED        (1U << 16)

static uint32_t calibrate_lapic_ticks_per_10ms(void) {
    // choose divide config: here set encode 0x3 (divide by 16). Adjust if needed.
    lapic_mmio_write(LAPIC_TIMER_DIV, 0x3);

    const uint32_t start = 0xFFFFFFFFU;
    lapic_mmio_write(LAPIC_TIMER_INITCNT, start);

    pit_sleep_ms(100);

    uint32_t curr = lapic_mmio_read(LAPIC_TIMER_CURRCNT);
    uint32_t ticks = start - curr;
    if (ticks == 0) return 0;
    return ticks / 10; // ticks per 10ms -> for 100Hz (10ms period)
}

// Make the global variable static to this file
static uint32_t g_apic_ticks_per_10ms = 0;

// BSP-only calibration function
void lapic_timer_calibrate(void) {
    // Only calibrate if it hasn't been done. This is the single entry point.
    if (g_apic_ticks_per_10ms == 0) {
        g_apic_ticks_per_10ms = calibrate_lapic_ticks_per_10ms();
    }
}

// Renamed and simplified init function
int init_lapic_timer(uint32_t hz) {
    if (hz == 0) return -1;

    // This now assumes calibration is already done!
    if (g_apic_ticks_per_10ms == 0) {
        // Calibration failed or wasn't run, this is an error.
        return -2;
    }

    uint32_t period_ms = 1000 / hz;
    uint64_t initial = ((uint64_t)g_apic_ticks_per_10ms * (uint64_t)period_ms) / 10ULL;
    if (initial == 0) initial = 1;

    // Program THIS CPU's timer using the shared calibration value
    lapic_mmio_write(LAPIC_LVT_TIMER, APIC_LVT_TIMER_PERIODIC | 0xEF /* vector */);
    lapic_mmio_write(LAPIC_TIMER_INITCNT, (uint32_t)initial);
    return 0;
}

