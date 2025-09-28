#ifndef X86_APIC_H
#define X86_APIC_H

#include <stdint.h>
#include <stdbool.h>
#include "../../intrinsics/intrin.h"
#include "pit.h"

void lapic_init_cpu(void);                    // call once on BSP early
void lapic_enable(void);
uint32_t lapic_mmio_read(uint32_t off);
void lapic_mmio_write(uint32_t off, uint32_t val);
void lapic_write(uint32_t reg, uint32_t value);
void lapic_eoi(void);
// lapic spurious interrupt vector, protects against faulty interrupts.
void lapic_init_siv(void);
// send IPI to APIC id 
// apic_id - APICId of the CPU.
// vector - IDT Vector number
// flags - specified cpu flags, 0 for none.
void lapic_send_ipi(uint8_t apic_id, uint8_t vector, uint32_t flags);
int init_lapic_timer(uint32_t hz);           // calibrate + start periodic timer at `hz` (returns 0 on success)
void lapic_timer_calibrate(void);
#endif
