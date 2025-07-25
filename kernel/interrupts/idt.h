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
    uint16_t offset_low;    // lower 16 bits of handler address (0-15)
    uint16_t selector;      // CS segment selector
    uint8_t ist;           // Interrupt Stack Table (0-2 bits), reserved (3-7 bits)
    uint8_t type_attr;     // type and attributes
    uint16_t offset_mid;   // middle 16 bits of handler address (16-31)
    uint32_t offset_high;  // upper 32 bits of handler address (32-63)
    uint32_t zero;         // reserved, must be zero
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
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
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

// forward declaration for prototype error in gcc.
void isr_handler64(int vec_num, REGS* r);

#endif /* X86_IDT_H */