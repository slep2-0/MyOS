/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Interrupt Structure and functions declaration.
 */
#ifndef X86_IDT_H
#define X86_IDT_H

// Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../cpu/cpu.h"
#include "../trace.h"
#include "handlers/handlers.h"

#define PIC1_COMMAND_MASTER 0x20
#define PIC1_DATA_MASTER    0x21
#define PIC2_COMMAND_SLAVE  0xA0
#define PIC2_DATA_SLAVE     0xA1

#define PIC_EOI            0x20
#define IDT_ENTRIES        256

/** Exception Definitions **/
typedef enum _CPU_EXCEPTIONS {
	EXCEPTION_DIVIDE_BY_ZERO,
	EXCEPTION_SINGLE_STEP,
	EXCEPTION_NON_MASKABLE_INTERRUPT,
	EXCEPTION_BREAKPOINT,
	EXCEPTION_OVERFLOW,
	EXCEPTION_BOUNDS_CHECK,
	EXCEPTION_INVALID_OPCODE,
	EXCEPTION_NO_COPROCESSOR,
	EXCEPTION_DOUBLE_FAULT,
	EXCEPTION_COPROCESSOR_SEGMENT_OVERRUN,
	EXCEPTION_INVALID_TSS,
	EXCEPTION_SEGMENT_SELECTOR_NOTPRESENT,
	EXCEPTION_STACK_SEGMENT_OVERRUN,
	EXCEPTION_GENERAL_PROTECTION_FAULT,
	EXCEPTION_PAGE_FAULT,
	EXCEPTION_RESERVED,
	EXCEPTION_FLOATING_POINT_ERROR,
	EXCEPTION_ALIGNMENT_CHECK,
	EXCEPTION_SEVERE_MACHINE_CHECK,
} CPU_EXCEPTIONS;

/** Interrupt Definitions **/
typedef enum _INTERRUPT_LIST {
	TIMER_INTERRUPT = 32,
	KEYBOARD_INTERRUPT = 33,
	ATA_INTERRUPT = 46,
} INTERRUPT_LIST;

/** PIC IRQ Lines **/
typedef enum _PIC_IRQ_LINE {
	IRQ0_TIMER = 0,
	IRQ1_KEYBOARD = 1,
	IRQ2_CASCADE = 2,
	IRQ3_SERIAL2 = 3,
	IRQ4_SERIAL1 = 4,
	IRQ5_LPT2 = 5,
	IRQ6_FLOPPY = 6,
	IRQ7_LPT1 = 7,
	IRQ8_CMOS = 8,
	IRQ9_FREE = 9,
	IRQ10_FREE = 10,
	IRQ11_FREE = 11,
	IRQ12_MOUSE = 12,
	IRQ13_FPU = 13,
	IRQ14_PRIMARY_ATA = 14,
	IRQ15_SECONDARY_ATA = 15,
} PIC_IRQ_LINE;

#pragma pack(push, 1)
typedef struct _IDT_PTR {
	uint16_t limit;
	uint64_t base;
} IDT_PTR;

typedef struct _IDT_ENTRY_64 {
	uint16_t offset_low;
	uint16_t selector;
	uint8_t  ist;
	uint8_t  type_attr;
	uint16_t offset_mid;
	uint32_t offset_high;
	uint32_t zero;
} IDT_ENTRY64;
#pragma pack(pop)
 
/** Functions */
void set_idt_gate(int n, unsigned long int handler);
void install_idt(void);
void init_interrupts(void);
void isr_handler64(int vec_num, REGS* r);

/** PIC Masking Helpers **/
static inline void mask_irq(PIC_IRQ_LINE irq_line) {
	tracelast_func("mask_irq");
	uint16_t port;
	uint8_t mask;
	if (irq_line < 8) {
		port = PIC1_DATA_MASTER;
	}
	else {
		port = PIC2_DATA_SLAVE;
		irq_line -= 8;
	}
	mask = __inbyte(port);
	mask |= (1 << irq_line);
	__outbyte(port, mask);
}

static inline void unmask_irq(PIC_IRQ_LINE irq_line) {
	tracelast_func("unmask_irq");
	uint16_t port;
	uint8_t mask;
	if (irq_line < 8) {
		port = PIC1_DATA_MASTER;
	}
	else {
		port = PIC2_DATA_SLAVE;
		irq_line -= 8;
	}
	mask = __inbyte(port);
	mask &= ~(1 << irq_line);
	__outbyte(port, mask);
}

#endif /* X86_IDT_H */
