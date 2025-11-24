/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     GPLv3
 * PURPOSE:		IMPLEMENTATION To SETUP IDT Entries.
 */

#include "../../includes/mh.h"

IDT_ENTRY64 IDT[IDT_ENTRIES];
IDT_PTR  PIDT;

/* Set one gate. */
void set_idt_gate(int n, unsigned long int handler) {
    IDT[n].offset_low = handler & 0xFFFF;
    IDT[n].selector = 0x08;   // code segment selector
    IDT[n].ist = 0;         
    IDT[n].type_attr = 0x8E;  // interrupt gate, present, ring 0
    IDT[n].offset_mid = (handler >> 16) & 0xFFFF;
    IDT[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    IDT[n].zero = 0;
}

/* Populate IDT: exceptions, IRQ, and then finally load it. */
void install_idt() {
    /* REMAP the PIC so IRQs start at vector 0x20 */
    __outbyte(0x20, 0x11); // initialize master PIC
    __outbyte(0xA0, 0x11); // initialize slave PIC
    __outbyte(0x21, 0x20); // master PIC vector offset 0x20.
    __outbyte(0xA1, 0x28); // slave PIC vector offset 0x28
    __outbyte(0x21, 0x04);
    __outbyte(0xA1, 0x02);
    __outbyte(0x21, 0x01);
    __outbyte(0xA1, 0x01);
    __outbyte(0x21, 0x0);
    __outbyte(0xA1, 0x0);

    /* Fill IDT Entries for CPU Exceptions (0-31) */ /* For clarifications, all of the ISR and IRQ externals live in isr_stub (where it defines the functions and gets linked together, via the global keyword) and isr_common_stub (where it does the routine), where they are linked together via the linker (externs) */
    extern void isr0(void); extern void isr1(void); extern void isr2(void); extern void isr3(void); extern void isr4(void); extern void isr5(void); extern void isr6(void); extern void isr7(void); extern void isr8(void); extern void isr9(void); extern void isr10(void); extern void isr11(void); extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void); extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void); extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void); extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void); extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);
    /* I forgo t to set n in the set_idt_gate, they were all zeros and I didn't understand why I got IRQ of like 50 thousand and error code of 4 billion. (i copy pasted each line instead of typing manually) */
    set_idt_gate(0, (unsigned long)isr0);
    set_idt_gate(1, (unsigned long)isr1);
    set_idt_gate(2, (unsigned long)isr2);
    set_idt_gate(3, (unsigned long)isr3);
    set_idt_gate(4, (unsigned long)isr4);
    set_idt_gate(5, (unsigned long)isr5);
    set_idt_gate(6, (unsigned long)isr6);
    set_idt_gate(7, (unsigned long)isr7);
    set_idt_gate(8, (unsigned long)isr8);
    set_idt_gate(9, (unsigned long)isr9);
    set_idt_gate(10, (unsigned long)isr10);
    set_idt_gate(11, (unsigned long)isr11);
    set_idt_gate(12, (unsigned long)isr12);
    set_idt_gate(13, (unsigned long)isr13);
    set_idt_gate(14, (unsigned long)isr14);
    set_idt_gate(15, (unsigned long)isr15);
    set_idt_gate(16, (unsigned long)isr16);
    set_idt_gate(17, (unsigned long)isr17);
    set_idt_gate(18, (unsigned long)isr18);
    set_idt_gate(19, (unsigned long)isr19);
    set_idt_gate(20, (unsigned long)isr20);
    set_idt_gate(21, (unsigned long)isr21);
    set_idt_gate(22, (unsigned long)isr22);
    set_idt_gate(23, (unsigned long)isr23);
    set_idt_gate(24, (unsigned long)isr24);
    set_idt_gate(25, (unsigned long)isr25);
    set_idt_gate(26, (unsigned long)isr26);
    set_idt_gate(27, (unsigned long)isr27);
    set_idt_gate(28, (unsigned long)isr28);
    set_idt_gate(29, (unsigned long)isr29);
    set_idt_gate(30, (unsigned long)isr30);
    set_idt_gate(31, (unsigned long)isr31);

    /* Fill IDT Gates for IRQs (32-47) */
    extern void irq0(void); extern void irq1(void); extern void irq2(void); extern void irq3(void); extern void irq4(void); extern void irq5(void); extern void irq6(void); extern void irq7(void); extern void irq8(void); extern void irq9(void); extern void irq10(void); extern void irq11(void); extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);
    set_idt_gate(32, (unsigned long)irq0);
    set_idt_gate(33, (unsigned long)irq1);
    set_idt_gate(34, (unsigned long)irq2);
    set_idt_gate(35, (unsigned long)irq3);
    set_idt_gate(36, (unsigned long)irq4);
    set_idt_gate(37, (unsigned long)irq5);
    set_idt_gate(38, (unsigned long)irq6);
    set_idt_gate(39, (unsigned long)irq7);
    set_idt_gate(40, (unsigned long)irq8);
    set_idt_gate(41, (unsigned long)irq9);
    set_idt_gate(42, (unsigned long)irq10);
    set_idt_gate(43, (unsigned long)irq11);
    set_idt_gate(44, (unsigned long)irq12);
    set_idt_gate(45, (unsigned long)irq13);
    set_idt_gate(46, (unsigned long)irq14);
    set_idt_gate(47, (unsigned long)irq15);
#define LAPIC_TIMER_VECTOR 0xEF
    /* For LAPIC */
    extern void isr239(void); // LAPIC ISR.
    set_idt_gate(LAPIC_TIMER_VECTOR, (unsigned long)isr239);
#define LAPIC_SPURIOUS_VECTOR 254
    /* For SIV LAPIC */
    extern void isr254(void); // SIV ISR
    set_idt_gate(LAPIC_SPURIOUS_VECTOR, (unsigned long)isr254);
    /* For LAPIC CPU Action */
    extern void isr222(void);
    set_idt_gate(LAPIC_ACTION_VECTOR, (unsigned long)isr222);
    /* Enable IST for Page Fault and Double Fault */
    IDT[14].ist = 1;  // uses gTss.ist[0] (page fault)
    IDT[8].ist = 2;  // uses gTss.ist[1] (double fault)
    IDT[LAPIC_TIMER_VECTOR].ist = 3; // uses gTss.ist[2] (LAPIC Timer)
    IDT[LAPIC_ACTION_VECTOR].ist = 4; // uses gTss.ist[3] (APIC IPI)

    /* Finally, Load IDT. */
    PIDT.limit = sizeof(IDT_ENTRY64) * IDT_ENTRIES - 1; // Max limit is the amount of IDT_ENTRIES structs (0-255)
    PIDT.base = (unsigned long)&IDT;
    __lidt(&PIDT);
}
