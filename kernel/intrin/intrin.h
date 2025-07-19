/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Intrinsics for easy assembly use.
 */
#ifndef X86_INTRINSICS_H
#define X86_INTRINSICS_H
#include "../kernel.h"

// Disable interrupts (cli)
static inline void __cli(void) {
    __asm__ volatile ("cli");
}

// Enable interrupts (sti)
static inline void __sti(void) {
    __asm__ volatile ("sti");
}

// Halt CPU until next interrupt (hlt)
static inline void __hlt(void) {
    __asm__ volatile ("hlt");
}

// Read CR0 register
static inline unsigned long int __read_cr0(void) {
    unsigned long int val;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(val));
    return val;
}

// Write CR0 register
static inline void __write_cr0(unsigned long int val) {
    __asm__ volatile ("mov %0, %%cr0" :: "r"(val));
}

static inline void __lidt(void* idt_ptr) {
    __asm__ volatile ("lidt (%0)" :: "r"(idt_ptr));
}

// Read EFLAGS register
static inline unsigned long int __read_eflags(void) {
    unsigned long int eflags;
    __asm__ volatile (
        "pushfl\n\t"
        "pop %0"
        : "=r"(eflags)
        );
    return eflags;
}

// Write EFLAGS register
static inline void __write_eflags(unsigned long int eflags) {
    __asm__ volatile (
        "push %0\n\t"
        "popfl"
        :: "r"(eflags)
        );
}

// Read port (inb)
static inline unsigned char __inbyte(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Write port (outb)
static inline void __outbyte(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline void send_eoi(unsigned char irq) {
    if (irq >= 8) {
        __outbyte(PIC2_COMMAND_SLAVE, PIC_EOI);  // Slave PIC
    }
    __outbyte(PIC1_COMMAND_MASTER, PIC_EOI);      // Master PIC
}

static inline void invlpg(void* m) {
    __asm__ volatile("invlpg (%0)" : : "b"(m) : "memory");
}

#endif // X86_INTRINSICS_H
