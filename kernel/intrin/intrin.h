#ifndef X86_INTRINSICS_H
#define X86_INTRINSICS_H

// Disable interrupts (cli)
static inline void __cli() {
    __asm__ volatile ("cli");
}

// Enable interrupts (sti)
static inline void __sti() {
    __asm__ volatile ("sti");
}

// Halt CPU until next interrupt (hlt)
static inline void __hlt() {
    __asm__ volatile ("hlt");
}

// Read CR0 register
static inline unsigned long int __read_cr0() {
    unsigned long int val;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(val));
    return val;
}

// Write CR0 register
static inline void __write_cr0(unsigned long int val) {
    __asm__ volatile ("mov %0, %%cr0" :: "r"(val));
}

// Load IDT: lidt
typedef struct _IDT_PTR {
    unsigned short limit;
    unsigned long int base;
} IDT_PTR;

static inline void __lidt(void* idt_ptr) {
    __asm__ volatile ("lidt (%0)" :: "r"(idt_ptr));
}

// Read EFLAGS register
static inline unsigned long int __read_eflags() {
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

#endif // X86_INTRINSICS_H
