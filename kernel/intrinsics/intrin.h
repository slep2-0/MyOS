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
#define IA32_EFER 0xC0000080
#define IA32_STAR 0xC0000081
#define IA32_LSTAR 0xC0000082
#define IA32_CSTAR 0xC0000083
#define IA32_FMASK 0xC0000084

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(x) (void)(x)
#endif

#ifdef _MSC_VER
#ifndef __asm__
#define __asm__ __asm
#endif
#endif

#include <stdbool.h>
#include "../includes/annotations.h"

// Disable interrupts (cli)
FORCEINLINE
void __cli(void) {
    __asm__ volatile ("cli");
}

// Enable supervisor access to user memory (STAC)
FORCEINLINE void __stac(void) {
    __asm__ volatile("stac" ::: "memory");
}

// Disable supervisor access to user memory (CLAC)
FORCEINLINE void __clac(void) {
    __asm__ volatile("clac" ::: "memory");
}

// Enable interrupts (sti)
FORCEINLINE
void __sti(void) {
    __asm__ volatile ("sti");
}

// Halt CPU until next interrupt (hlt)
FORCEINLINE
void __hlt(void) {
    __asm__ volatile ("hlt");
}

// Read CR0 register
FORCEINLINE unsigned long int __read_cr0(void) {
    unsigned long int val;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(val));
    return val;
}

// Write CR0 register
FORCEINLINE void __write_cr0(unsigned long int val) {
    __asm__ volatile ("mov %0, %%cr0" :: "r"(val));
}

// CR2 (Page fault linear address)
FORCEINLINE unsigned long __read_cr2(void) {
    unsigned long val;
    __asm__ volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}

FORCEINLINE void __write_cr2(unsigned long val) {
    __asm__ volatile("mov %0, %%cr2" :: "r"(val) : "memory");
}

// CR3 (Page table base address)
FORCEINLINE uint64_t __read_cr3(void) {
    uint64_t val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}
FORCEINLINE void __write_cr3(uint64_t val) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(val) : "memory");
}

// CR4 (Feature control)
FORCEINLINE unsigned long __read_cr4(void) {
    unsigned long val;
    __asm__ volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}
FORCEINLINE void __write_cr4(unsigned long val) {
    __asm__ volatile("mov %0, %%cr4" :: "r"(val) : "memory");
}

// CR8 (Task Priority Register, x86-64 only)
FORCEINLINE unsigned long __read_cr8(void) {
    unsigned long val;
    __asm__ volatile("mov %%cr8, %0" : "=r"(val));
    return val;
}
FORCEINLINE void __write_cr8(unsigned long val) {
    __asm__ volatile("mov %0, %%cr8" :: "r"(val) : "memory");
}


// Read DRx register (dr0-dr7) (Usage __read_dr(3) = will return dr3.
FORCEINLINE uint64_t __read_dr(int reg) {
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
FORCEINLINE void __write_dr(int reg, uint64_t val) {
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
FORCEINLINE void __lidt(void* idt_ptr) {
    __asm__ volatile ("lidt (%0)" :: "r"(idt_ptr));
}

// Read RFLAGS register
FORCEINLINE unsigned long int __read_rflags(void) {
    unsigned long int rflags;
    __asm__ volatile (
        "pushfl\n\t"
        "pop %0"
        : "=r"(rflags)
        );
    return rflags;
}

// Write RFLAGS register
FORCEINLINE void __write_rflags(unsigned long int rflags) {
    __asm__ volatile (
        "push %0\n\t"
        "popfl"
        :: "r"(rflags)
        );
}

// Read port (inw)
FORCEINLINE unsigned short __inword(unsigned short port) {
    unsigned short ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Write port (outw)
FORCEINLINE void __outword(unsigned short port, unsigned short val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

// Read port (inb)
FORCEINLINE unsigned char __inbyte(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Write port (outb)
FORCEINLINE void __outbyte(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

FORCEINLINE void send_eoi(unsigned char irq) {
    if (irq >= 8) {
        __outbyte(PIC2_COMMAND_SLAVE, PIC_EOI);  // Slave PIC
    }
    __outbyte(PIC1_COMMAND_MASTER, PIC_EOI);      // Master PIC
}

FORCEINLINE void invlpg(void* m) {
    __asm__ volatile("invlpg (%0)" : : "b"(m) : "memory");
}

FORCEINLINE uint64_t __readmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

FORCEINLINE void __writemsr(uint32_t msr, uint64_t value) {
    uint32_t lo = value & 0xFFFFFFFF;
    uint32_t hi = value >> 32;
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

FORCEINLINE uint64_t __read_rbp(void) {
    uint64_t val;
    __asm__ volatile ("mov %%rbp, %0" : "=r"(val));
    return val;
}

FORCEINLINE uint64_t __read_rsp(void) {
    uint64_t val;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(val));
    return val;
}

FORCEINLINE uint64_t __read_rip(void) {
    uint64_t rip;
    __asm__ volatile ("leaq (%%rip), %0" : "=r"(rip));
    return rip;
}

FORCEINLINE void __pause(void) {
    __asm__ volatile("pause" ::: "memory");
}

FORCEINLINE uint64_t __readgsqword(uint64_t offset) {
    uint64_t value;
    __asm__ volatile (
        "movq %%gs:(%1), %0"
        : "=r"(value)
        : "r"(offset)
        : "memory"
        );
    return value;
}

FORCEINLINE uint64_t __readfsqword(uint64_t offset) {
    uint64_t value;
    __asm__ volatile (
        "movq %%fs:(%1), %0"
        : "=r"(value)
        : "r"(offset)
        : "memory"
        );
    return value;
}

FORCEINLINE void __swapgs(void) {
    __asm__ volatile ("swapgs" ::: "memory");
}

FORCEINLINE bool __rdrand64(uint64_t* out) {
    unsigned char ok;
    uint64_t val;
    __asm__ volatile("rdrand %0; setc %1"
        : "=r"(val), "=qm"(ok));
    *out = val;
    return ok; // 1=success, 0=failure
}

FORCEINLINE uint64_t __rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#ifdef DEBUG

// GDB Func to CLI and STI

static void gcli(void) {
    __cli();
}

static void gsti(void) {
    __sti();
}

#endif

#endif // X86_INTRINSICS_H
