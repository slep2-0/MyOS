/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Interrupt Structure and funjctions declaration.
 */
#include "../kernel.h"

#ifndef X86_IDT_H
#define X86_IDT_H

#define IDT_ENTRIES 256
#define PIC_EOI 0x20
#define PIC1_COMMAND_MASTER 0x20
#define PIC2_COMMAND_SLAVE 0xA0

typedef struct __attribute__((packed)) _IDT_PTR {
    unsigned short limit;
    unsigned long int base;
} IDT_PTR;
// SIZEOF: 6 bytes.

typedef struct __attribute__((packed)) _IDT_ENTRY {
    unsigned short offset_low; // lower 16 bits of the handler function address. 0-15
    unsigned short selector; // CS (code) segment in the GDT.
    unsigned char zero; // Always 0.
    unsigned char type_attr; // type and attributes (e.g, 0x8E - present, ring 0, 32-bit interrupt gate)
    unsigned short offset_high; // higher 16 bits of handler address 16-31
} IDT_ENTRY;
//SIZEOF: 8 bytes.

typedef struct __attribute__((packed)) _REGS {
    unsigned long eflags;   // EFLAGS register
    unsigned long cs;       // Code segment (CS)
    unsigned long eip;      // Instruction pointer (EIP)
    unsigned long error_code; // Error code (optional)
} REGS;

/* FUNCTIONS */

// Function to set an IDT entry.
void set_idt_gate(int n, unsigned long int handler);

// Function to install IDT + load.
void install_idt();

// Load all interupts and ISR's.
void init_interrupts();

#endif /* X86_IDT_H */