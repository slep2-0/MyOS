#include "apic.h"
#include <stddef.h>
#include <stdint.h>
#include "../../memory/paging/paging.h"
#include "../../memory/allocator/allocator.h"

#define IA32_APIC_BASE_MSR     0x1BULL
#define APIC_BASE_RESERVED     0xFFF0000000000000ULL

#define LAPIC_PAGE_SIZE    0x1000
// page flags - adapt names to your kernel constants:
#define LAPIC_MAP_FLAGS (PAGE_PRESENT | PAGE_RW | PAGE_PCD)

// LAPIC register offsets (32-bit registers)
enum {
    LAPIC_ID = 0x020,
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


#define LAPIC_DEFAULT_PADDR    0xFEE00000ULL
static volatile uint32_t* lapic = NULL;
static uint64_t lapic_phys = LAPIC_DEFAULT_PADDR;

// --- low-level mmio helpers (assumes lapic mapped to virtual memory) ---
static inline uint32_t lapic_mmio_read(uint32_t off) {
    return lapic[off / 4];
}
static inline void lapic_mmio_write(uint32_t off, uint32_t val) {
    tracelast_func("lapic_mmio_write");
    lapic[off / 4] = val;
    (void)lapic_mmio_read(LAPIC_ID); // serializing read to ensure write completes
}

// Wait for ICR delivery to complete (ICR low: bit 12 = Delivery Status)
static void lapic_wait_icr(void) {
    while (lapic_mmio_read(LAPIC_ICR_LOW) & (1 << 12)) {
        /* spin */
    }
}

static void map_lapic(void) {
    if (lapic) return;
    tracelast_func("map_lapic");

    void* virt = (void*)(lapic_phys + PHYS_MEM_OFFSET);

    // Map the single LAPIC page (phys -> virt)
    map_page(virt, lapic_phys, PAGE_PRESENT | PAGE_RW | PAGE_PCD);

    // store the mmio base pointer
    lapic = (volatile uint32_t*)virt;
}


// Enable local APIC via IA32_APIC_BASE MSR and set SVR
void lapic_enable(void) {
    tracelast_func("lapic_enable");
    uint64_t apic_msr = __readmsr(IA32_APIC_BASE_MSR);
    if (!(apic_msr & (1ULL << 11))) {
        // set APIC global enable
        apic_msr |= (1ULL << 11);
        // optionally set a custom base in apic_msr bits [35:12] if not default
        // wrmsr(IA32_APIC_BASE_MSR, apic_msr);
        __writemsr(IA32_APIC_BASE_MSR, apic_msr);
    }
    map_lapic();

    // Set Spurious Vector Register and enable (bit 8 = APIC enable)
    // choose an interrupt vector for spurious (e.g., 0xFF). Keep vector values consistent with your IDT.
    uint32_t svr = (0xFF) | (1 << 8);
    lapic_mmio_write(LAPIC_SVR, svr);
}

// Initialize BSP's LAPIC (call early from kernel init on BSP)
void lapic_init_bsp(void) {
    tracelast_func("lapic_init_bsp");
    // If your bootloader set APIC base adjust lapic_phys by reading MSR:
    uint64_t apic_msr = __readmsr(IA32_APIC_BASE_MSR);
    uint64_t base = (apic_msr & 0xFFFFF000ULL);
    if (base) lapic_phys = base;
    map_lapic();

    lapic_enable();

    // mask LINT0/LINT1 as appropriate, clear error status, etc.
    lapic_mmio_write(LAPIC_LVT_LINT0, (1U << 16)); // mask
    lapic_mmio_write(LAPIC_LVT_LINT1, (1U << 16)); // mask
    lapic_mmio_write(LAPIC_LVT_ERROR, (1U << 16)); // mask (until handler in place)
    lapic_mmio_write(LAPIC_EOI, 0);
}

// send IPI to APIC id
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
// Replace pit_sleep_ms() with your accurate sleep.
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

int init_lapic_timer(uint32_t hz) {
    if (hz == 0) return -1;
    map_lapic();

    // calibrate using 100ms window
    uint32_t ticks_per_10ms = calibrate_lapic_ticks_per_10ms();
    if (ticks_per_10ms == 0) return -2;

    // compute target initial count
    // desired_period_ms = 1000 / hz
    uint32_t period_ms = 1000 / hz;
    // ticks_per_10ms: ticks per 10ms, so ticks_per_ms = ticks_per_10ms / 10
    // initial_count = ticks_per_ms * period_ms = ticks_per_10ms * period_ms / 10
    uint64_t initial = ((uint64_t)ticks_per_10ms * (uint64_t)period_ms) / 10ULL;
    if (initial == 0) initial = 1;

    // mask the timer while programming
    lapic_mmio_write(LAPIC_LVT_TIMER, APIC_LVT_TIMER_PERIODIC | 0xEF /* IDT vector 0xEF */);
    lapic_mmio_write(LAPIC_TIMER_INITCNT, (uint32_t)initial);
    return 0;
}
