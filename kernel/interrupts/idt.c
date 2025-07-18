/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		IMPLEMENTATION To SETUP IDT Entries.
 */
#include "idt.h"

IDT_ENTRY IDT[IDT_ENTRIES];
IDT_PTR   PIDT;

/* Set one gate. */
void set_idt_gate(int n, unsigned long int handler) {
    IDT[n].offset_low = handler & 0xFFFF;
    IDT[n].selector = 0x08; // CS is at 0x08 when it was set-upped in the GDT.
    IDT[n].zero = 0;
    IDT[n].type_attr = 0x8E; // Interrupt gate, present, ring 0.
    IDT[n].offset_high = (handler >> 16) & 0xFFFF;
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

    /* Fill IDT Entries for CPU Exceptions (0-31) */
    extern void isr0(); extern void isr1(); extern void isr2(); extern void isr3(); extern void isr4(); extern void isr5(); extern void isr6(); extern void isr7(); extern void isr8(); extern void isr9(); extern void isr10(); extern void isr11(); extern void isr12(); extern void isr13(); extern void isr14(); extern void isr15(); extern void isr16(); extern void isr17(); extern void isr18(); extern void isr19(); extern void isr20(); extern void isr21(); extern void isr22(); extern void isr23(); extern void isr24(); extern void isr25(); extern void isr26(); extern void isr27(); extern void isr28(); extern void isr29(); extern void isr30(); extern void isr31();
    /* I forgot to set n in the set_idt_gate, they were all zeros and I didn't understand why I got IRQ of like 50 thousand and error code of 4 billion. (i copy pasted each line instead of typing manually) */
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
    extern void irq0(); extern void irq1(); extern void irq2(); extern void irq3(); extern void irq4(); extern void irq5(); extern void irq6(); extern void irq7(); extern void irq8(); extern void irq9(); extern void irq10(); extern void irq11(); extern void irq12(); extern void irq13(); extern void irq14(); extern void irq15();
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
    
    /* Finally, Load IDT. */
    PIDT.limit = sizeof(IDT_ENTRY) * IDT_ENTRIES - 1; // Max limit is the amount of IDT_ENTRIES structs (0-255)
    PIDT.base = (unsigned long)&IDT;
    __lidt(&PIDT);
    __sti(); // Enable interrupts.
}