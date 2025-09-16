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

// MSRs
#define IA32_KERNEL_GS_BASE 0xC0000102
#define IA32_GS_BASE 0xC0000101 /* used both in kernel mode and user mode */
#define IA32_FS_BASE 0xC0000100

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(x) (void)(x)
#endif

#ifdef _MSC_VER
#ifndef __asm__
#define __asm__ __asm
#endif
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

// CR2 (Page fault linear address)
static inline unsigned long __read_cr2(void) {
    unsigned long val;
    __asm__ volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}

static inline void __write_cr2(unsigned long val) {
    __asm__ volatile("mov %0, %%cr2" :: "r"(val) : "memory");
}

// CR3 (Page table base address)
static inline unsigned long __read_cr3(void) {
    unsigned long val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}
static inline void __write_cr3(unsigned long val) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(val) : "memory");
}

// CR4 (Feature control)
static inline unsigned long __read_cr4(void) {
    unsigned long val;
    __asm__ volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}
static inline void __write_cr4(unsigned long val) {
    __asm__ volatile("mov %0, %%cr4" :: "r"(val) : "memory");
}

// CR8 (Task Priority Register, x86-64 only)
static inline unsigned long __read_cr8(void) {
    unsigned long val;
    __asm__ volatile("mov %%cr8, %0" : "=r"(val));
    return val;
}
static inline void __write_cr8(unsigned long val) {
    __asm__ volatile("mov %0, %%cr8" :: "r"(val) : "memory");
}


// Read DRx register (dr0-dr7) (Usage __read_dr(3) = will return dr3.
static inline uint64_t __read_dr(int reg) {
    unsigned long val = 0;
    switch (reg) {
        case 0: __asm__ volatile("mov %%dr0, %0" : "=r"(val)); break;
        case 1: __asm__ volatile("mov %%dr1, %0" : "=r"(val)); break;
        case 2: __asm__ volatile("mov %%dr2, %0" : "=r"(val)); break;
        case 3: __asm__ volatile("mov %%dr3, %0" : "=r"(val)); break;
        case 6: __asm__ volatile("mov %%dr6, %0" : "=r"(val)); break;
        case 7: __asm__ volatile("mov %%dr7, %0" : "=r"(val)); break;
        default: break;
    }
    return val;
}

// Write DRx register (dr0-dr7) (Usage __write_dr(3, 0x5000) = will write 0x5000 to dr3.
static inline void __write_dr(int reg, uint64_t val) {
    switch (reg) {
        case 0: __asm__ volatile("mov %0, %%dr0" :: "r"(val)); break;
        case 1: __asm__ volatile("mov %0, %%dr1" :: "r"(val)); break;
        case 2: __asm__ volatile("mov %0, %%dr2" :: "r"(val)); break;
        case 3: __asm__ volatile("mov %0, %%dr3" :: "r"(val)); break;
        case 6: __asm__ volatile("mov %0, %%dr6" :: "r"(val)); break;
        case 7: __asm__ volatile("mov %0, %%dr7" :: "r"(val)); break;
        default: break;
    }
}
static inline void __lidt(void* idt_ptr) {
    __asm__ volatile ("lidt (%0)" :: "r"(idt_ptr));
}

// Read RFLAGS register
static inline unsigned long int __read_rflags(void) {
    unsigned long int rflags;
    __asm__ volatile (
        "pushfl\n\t"
        "pop %0"
        : "=r"(rflags)
        );
    return rflags;
}

// Write RFLAGS register
static inline void __write_rflags(unsigned long int rflags) {
    __asm__ volatile (
        "push %0\n\t"
        "popfl"
        :: "r"(rflags)
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

static inline uint64_t __read_rbp(void) {
    uint64_t val;
    __asm__ volatile ("mov %%rbp, %0" : "=r"(val));
    return val;
}

static inline uint64_t __read_rsp(void) {
    uint64_t val;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(val));
    return val;
}

static inline uint64_t __read_rip(void) {
    uint64_t rip;
    __asm__ volatile ("leaq (%%rip), %0" : "=r"(rip));
    return rip;
}

static inline void __pause(void) {
    __asm__ volatile("pause" ::: "memory");
}

static inline uint64_t __readgsqword(uint64_t offset) {
    uint64_t value;
    __asm__ volatile (
        "movq %%gs:(%1), %0"
        : "=r"(value)
        : "r"(offset)
        : "memory"
        );
    return value;
}

static inline uint64_t __readfsqword(uint64_t offset) {
    uint64_t value;
    __asm__ volatile (
        "movq %%fs:(%1), %0"
        : "=r"(value)
        : "r"(offset)
        : "memory"
        );
    return value;
}

static inline void __swapgs(void) {
    __asm__ volatile ("swapgs" ::: "memory");
}


#endif // X86_INTRINSICS_H
