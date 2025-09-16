/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Symmetric MultiProcessing Header
 */

#ifndef X86_SMP_H
#define X86_SMP_H

#include "../../cpu/cpu_types.h"


#define AP_TRAMP_PHYS 0x7000ULL
#define AP_TRAMP_SIZE 0x1000UL   // single page
#define AP_TRAMP_APMAIN_OFFSET 0x1000ULL
#define AP_TRAMP_PML4_OFFSET 0x2000ULL


#define MAX_CPUS 32

#define LAPIC_ID 0x020
#define SMP_MAGIC 0x4D4154414E454C00 // MATANEL\0
#define SMP_A

typedef struct _SMP_BOOTINFO{
    uint64_t magic;
    uint64_t kernel_pml4_phys;  // from boot_info_local.Pml4Phys
    uint64_t ap_entry_virt;     // kernel virtual address of ap_main()
    uint32_t cpu_count;
    uint32_t reserved;
    uint64_t lapic_base;
} SMP_BOOTINFO;

typedef struct __attribute__((packed)) _GDTEntry64 {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t  base_middle;
	uint8_t  access;
	uint8_t  granularity;
	uint8_t  base_high;
	uint32_t base_upper;
	uint32_t reserved;
} GDTEntry64;

typedef struct __attribute__((packed)) _GDTPtr {
	uint16_t limit;
	uint64_t base;
} GDTPtr;

typedef struct __attribute__((packed)) _TSS {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7]; // This is the Interrupt Stack Table
    uint32_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} TSS;

#ifndef __INTELLISENSE__
// Static Asserts to make sure at compile time we're good.
_Static_assert(sizeof(GDTEntry64) == 16, "GDTEntry64 must be 16 bytes");
_Static_assert(offsetof(GDTEntry64, limit_low) == 0, "limit_low offset wrong");
_Static_assert(offsetof(GDTEntry64, base_low) == 2, "base_low offset wrong");
_Static_assert(offsetof(GDTEntry64, base_middle) == 4, "base_middle offset wrong");
_Static_assert(offsetof(GDTEntry64, access) == 5, "access offset wrong");
_Static_assert(offsetof(GDTEntry64, granularity) == 6, "granularity offset wrong");
_Static_assert(offsetof(GDTEntry64, base_high) == 7, "base_high offset wrong");
_Static_assert(offsetof(GDTEntry64, base_upper) == 8, "base_upper offset wrong");
_Static_assert(offsetof(GDTEntry64, reserved) == 12, "reserved offset wrong");
_Static_assert(sizeof(GDTPtr) == 10, "GDTPtr must be 10 bytes (16-bit limit + 64-bit base)");
#endif

void ap_main(void);
void smp_start(uint8_t* apic_list, uint32_t cpu_count);

#define IST_SIZE (16*1024) // 16 KiB
#define IST_ALIGNMENT 16

extern CPU cpus[MAX_CPUS];
extern int smp_cpu_count;

#endif