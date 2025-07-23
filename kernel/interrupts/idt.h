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

 /*
 Exception Definitions
 */
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
/*
Ended.
*/

/*
Interrupt Definitions
*/
typedef enum _INTERRUPT_LIST {
    TIMER_INTERRUPT = 32,
    KEYBOARD_INTERRUPT = 33,
    ATA_INTERRUPT = 46,
} INTERRUPT_LIST;
/*
Ended
*/
#ifdef _MSC_VER
#pragma pack(push, 1)
typedef struct _IDT_PTR {
#else
typedef struct __attribute__((packed)) _IDT_PTR {
#endif
    uint16_t limit;
    uint64_t base;
} IDT_PTR;
#ifdef _MSC_VER
#pragma pack(pop)
#endif
// SIZEOF: 6 bytes.

#ifdef _MSC_VER
#pragma pack(push, 1)
typedef struct _IDT_ENTRY {
#else
typedef struct __attribute__((packed)) _IDT_ENTRY_64 {
#endif
    uint16_t offset_low; // lower 16 bits of the handler function address. 0-15
    uint16_t selector; // CS (code) segment in the GDT.
    uint8_t ist; // Always 0.
    uint8_t type_attr; // type and attributes (e.g, 0x8E - present, ring 0, 32-bit interrupt gate)
    uint16_t offset_mid; // middle 16 bits.
    uint32_t offset_high; // highest 32 bits. (total 64)
    uint32_t zero;
} IDT_ENTRY64;
#ifdef _MSC_VER
#pragma pack(pop)
#endif
//SIZEOF: 8 bytes.

#ifdef _MSC_VER
#pragma pack(push, 1)
typedef struct _REGS {
#else
typedef struct __attribute__((packed)) _REGS {
#endif
    uint32_t gs, fs, es, ds;             // Segment registers
    uint32_t edi, esi, ebp, esp;         // Pushed by pusha -> Special Registers -> FIXME, PUSHA pushes the OLD esp, and not the current (when the cpu switches).
    uint32_t ebx, edx, ecx, eax;         // Pushed by pusha -> General purpose registers
    uint32_t error_code;                 // Manually pushed -> Error code (if any)
    uint32_t vec_num;                     // Vector number -> Vector Number.
    uint32_t eip, cs, eflags;           // Pushed by CPU. EIP -> Current instruction pointer address. CS -> Code Segment. EFLAGS -> EFLAGS Register.
} REGS;
#ifdef _MSC_VER
#pragma pack(pop)
#endif

/* FUNCTIONS */

// Function to set an IDT entry.
void set_idt_gate(int n, unsigned long int handler);

// Function to install IDT + load.
void install_idt(void);

// Load all interupts and ISR's.
void init_interrupts(void);

#endif /* X86_IDT_H */