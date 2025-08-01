/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Intrinsics for easy assembly use.
 */
#ifndef X86_INTRINSICS_H
#define X86_INTRINSICS_H

 // PIC Ports
#define PIC1_COMMAND_MASTER 0x20
#define PIC1_DATA           0x21
#define PIC2_COMMAND_SLAVE  0xA0
#define PIC2_DATA           0xA1

// End of Interrupt command code
#define PIC_EOI 0x20

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(x) (void)(x)
#endif

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

// Read port (inw)
static inline unsigned short __inword(unsigned short port) {
    unsigned short ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Write port (outw)
static inline void __outword(unsigned short port, unsigned short val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
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

static inline uint64_t __readmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void __writemsr(uint32_t msr, uint64_t value) {
    uint32_t lo = value & 0xFFFFFFFF;
    uint32_t hi = value >> 32;
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

#endif // X86_INTRINSICS_H