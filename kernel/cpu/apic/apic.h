#ifndef X86_APIC_H
#define X86_APIC_H

#include <stdint.h>
#include <stdbool.h>
#include "../../intrin/intrin.h"
#include "pit.h"

void lapic_init_bsp(void);                    // call once on BSP early
void lapic_enable(void);
uint32_t lapic_read(uint32_t reg);
void lapic_write(uint32_t reg, uint32_t value);
void lapic_eoi(void);
void lapic_send_ipi(uint8_t apic_id, uint8_t vector, uint32_t flags);
int init_lapic_timer(uint32_t hz);           // calibrate + start periodic timer at `hz` (returns 0 on success)

#endif
