# 1 "kernel/cpu/scheduler/scheduler.c"
# 1 "<built-in>"
# 1 "<command-line>"
# 1 "kernel/cpu/scheduler/scheduler.c"






# 1 "kernel/cpu/scheduler/scheduler.h" 1
# 9 "kernel/cpu/scheduler/scheduler.h"
# 1 "kernel/cpu/scheduler/../cpu.h" 1
# 81 "kernel/cpu/scheduler/../cpu.h"
# 1 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stdbool.h" 1 3 4
# 82 "kernel/cpu/scheduler/../cpu.h" 2
# 1 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 1 3 4
# 143 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 3 4

# 143 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 3 4
typedef long int ptrdiff_t;
# 209 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 3 4
typedef long unsigned int size_t;
# 321 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 3 4
typedef int wchar_t;
# 415 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 3 4
typedef struct {
  long long __max_align_ll __attribute__((__aligned__(__alignof__(long long))));
  long double __max_align_ld __attribute__((__aligned__(__alignof__(long double))));
# 426 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 3 4
} max_align_t;
# 83 "kernel/cpu/scheduler/../cpu.h" 2
# 1 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stdint.h" 1 3 4
# 11 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stdint.h" 3 4
# 1 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stdint-gcc.h" 1 3 4
# 34 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stdint-gcc.h" 3 4
typedef signed char int8_t;


typedef short int int16_t;


typedef int int32_t;


typedef long int int64_t;


typedef unsigned char uint8_t;


typedef short unsigned int uint16_t;


typedef unsigned int uint32_t;


typedef long unsigned int uint64_t;




typedef signed char int_least8_t;
typedef short int int_least16_t;
typedef int int_least32_t;
typedef long int int_least64_t;
typedef unsigned char uint_least8_t;
typedef short unsigned int uint_least16_t;
typedef unsigned int uint_least32_t;
typedef long unsigned int uint_least64_t;



typedef int int_fast8_t;
typedef int int_fast16_t;
typedef int int_fast32_t;
typedef long int int_fast64_t;
typedef unsigned int uint_fast8_t;
typedef unsigned int uint_fast16_t;
typedef unsigned int uint_fast32_t;
typedef long unsigned int uint_fast64_t;




typedef long int intptr_t;


typedef long unsigned int uintptr_t;




typedef long int intmax_t;
typedef long unsigned int uintmax_t;
# 12 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stdint.h" 2 3 4
# 84 "kernel/cpu/scheduler/../cpu.h" 2
# 1 "kernel/cpu/scheduler/../cpu_types.h" 1




# 1 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 1 3 4
# 6 "kernel/cpu/scheduler/../cpu_types.h" 2






# 11 "kernel/cpu/scheduler/../cpu_types.h"
typedef enum _THREAD_STATE { RUNNING, READY, BLOCKED, TERMINATING, TERMINATED } THREAD_STATE;


typedef struct _Thread Thread;
typedef struct _DPC DPC;
typedef struct _Queue Queue;
typedef struct _CPU CPU;



typedef enum _IRQL {
    PASSIVE_LEVEL = 0,
    DISPATCH_LEVEL = 2,

    DIRQL_SECONDARY_ATA = 12,
    DIRQL_PRIMARY_ATA = 13,
    DIRQL_FPU = 14,
    DIRQL_MOUSE = 15,
    DIRQL_PERIPHERAL11 = 16,
    DIRQL_PERIPHERAL10 = 17,
    DIRQL_PERIPHERAL9 = 18,
    DIRQL_RTC = 19,
    DIRQL_LPT1 = 20,
    DIRQL_FLOPPY = 21,
    DIRQL_SOUND_LPT2 = 22,
    DIRQL_COM1 = 23,
    DIRQL_COM2 = 24,
    DIRQL_CASCADE = 25,
    DIRQL_KEYBOARD = 26,
    DIRQL_TIMER = 27,
    PROFILE_LEVEL = 27,
    CLOCK_LEVEL = 28,
    SYNCH_LEVEL = 29,
    POWER_LEVEL = 30,
    HIGH_LEVEL = 31
} IRQL;



#pragma pack(push, 1)
typedef struct _INT_FRAME {
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} INT_FRAME;
#pragma pack(pop)


#pragma pack(push, 1)
typedef struct _CTX_FRAME {

    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rsp;
    uint64_t rip;

} CTX_FRAME;
#pragma pack(pop)


#pragma pack(push, 1)
typedef struct _INTERRUPT_FULL_REGS {
 uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
 uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;

 uint64_t vector;
 uint64_t error_code;
 uint64_t rip;
 uint64_t cs;
 uint64_t rflags;
} INTERRUPT_FULL_REGS;
#pragma pack(pop)

typedef struct _Queue {
 Thread* head;
 Thread* tail;
} Queue;

typedef struct _Thread {

 CTX_FRAME registers;


 THREAD_STATE threadState;


 Thread* nextThread;

} Thread;

typedef enum _DPC_PRIORITY {
    NO_PRIORITY = 0,
    LOW_PRIORITY = 25,
    MEDIUM_PRIORITY = 50,
    HIGH_PRIORITY = 75,
    SYSTEM_PRIORITY = 99
} DPC_PRIORITY;

typedef enum _DPC_KIND {
    NO_KIND = 0,
    DPC_SCHEDULE,

} DPC_KIND;

typedef struct _DPC {
    volatile DPC* Next;
    void (*callbackWithCtx)(void* ctx);
    void (*callback)(void);
    CTX_FRAME* ctx;
    DPC_KIND Kind;
    
# 125 "kernel/cpu/scheduler/../cpu_types.h" 3 4
   _Bool 
# 125 "kernel/cpu/scheduler/../cpu_types.h"
        hasCtx;
    DPC_PRIORITY priority;
} DPC;

typedef struct _CPU {
 IRQL currentIrql;
 
# 131 "kernel/cpu/scheduler/../cpu_types.h" 3 4
_Bool 
# 131 "kernel/cpu/scheduler/../cpu_types.h"
     schedulerEnabled;
 Thread* currentThread;
 Queue readyQueue;
} CPU;
# 85 "kernel/cpu/scheduler/../cpu.h" 2
# 1 "kernel/cpu/scheduler/../irql/irql.h" 1
# 10 "kernel/cpu/scheduler/../irql/irql.h"
# 1 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 1 3 4
# 11 "kernel/cpu/scheduler/../irql/irql.h" 2


# 1 "kernel/cpu/scheduler/../irql/../../trace.h" 1




# 1 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 1 3 4
# 6 "kernel/cpu/scheduler/../irql/../../trace.h" 2


# 1 "kernel/cpu/scheduler/../irql/../../intrin/intrin.h" 1
# 23 "kernel/cpu/scheduler/../irql/../../intrin/intrin.h"
static inline void __cli(void) {
    __asm__ volatile ("cli");
}


static inline void __sti(void) {
    __asm__ volatile ("sti");
}


static inline void __hlt(void) {
    __asm__ volatile ("hlt");
}


static inline unsigned long int __read_cr0(void) {
    unsigned long int val;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(val));
    return val;
}


static inline void __write_cr0(unsigned long int val) {
    __asm__ volatile ("mov %0, %%cr0" :: "r"(val));
}

static inline void __lidt(void* idt_ptr) {
    __asm__ volatile ("lidt (%0)" :: "r"(idt_ptr));
}


static inline unsigned long int __read_eflags(void) {
    unsigned long int eflags;
    __asm__ volatile (
        "pushfl\n\t"
        "pop %0"
        : "=r"(eflags)
        );
    return eflags;
}


static inline void __write_eflags(unsigned long int eflags) {
    __asm__ volatile (
        "push %0\n\t"
        "popfl"
        :: "r"(eflags)
        );
}


static inline unsigned short __inword(unsigned short port) {
    unsigned short ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}


static inline void __outword(unsigned short port, unsigned short val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}


static inline unsigned char __inbyte(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}


static inline void __outbyte(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline void send_eoi(unsigned char irq) {
    if (irq >= 8) {
        __outbyte(0xA0, 0x20);
    }
    __outbyte(0x20, 0x20);
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
# 9 "kernel/cpu/scheduler/../irql/../../trace.h" 2




typedef struct {
    uint8_t names[10][128];
    int current_index;
} LASTFUNC_HISTORY;


static inline void tracelast_func(const char* function_name) {


    extern 
# 22 "kernel/cpu/scheduler/../irql/../../trace.h" 3 4
          _Bool 
# 22 "kernel/cpu/scheduler/../irql/../../trace.h"
               isBugChecking;
    extern LASTFUNC_HISTORY lastfunc_history;

    if (!function_name || isBugChecking) return;

    lastfunc_history.current_index =
        (lastfunc_history.current_index + 1) % 10;


    for (size_t j = 0; j < 128; j++) {
        lastfunc_history.names[lastfunc_history.current_index][j] = 0;
    }


    for (size_t i = 0; i < 128 - 1 && function_name[i]; i++) {
        lastfunc_history.names[lastfunc_history.current_index][i] =
            (uint8_t)function_name[i];
    }

}
# 14 "kernel/cpu/scheduler/../irql/irql.h" 2
# 1 "kernel/cpu/scheduler/../irql/../cpu_types.h" 1
# 15 "kernel/cpu/scheduler/../irql/irql.h" 2

extern CPU cpu;


void update_pic_mask_for_current_irql(void);

void MtGetCurrentIRQL(IRQL* current_irql);

void MtRaiseIRQL(IRQL new_irql, IRQL* old_irql);

void MtLowerIRQL(IRQL new_irql);


void _MtSetIRQL(IRQL new_irql);

void enforce_max_irql(IRQL max_allowed);
# 86 "kernel/cpu/scheduler/../cpu.h" 2
# 1 "kernel/cpu/scheduler/../dpc/dpc.h" 1
# 12 "kernel/cpu/scheduler/../dpc/dpc.h"
# 1 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 1 3 4
# 13 "kernel/cpu/scheduler/../dpc/dpc.h" 2
# 1 "kernel/cpu/scheduler/../dpc/../cpu.h" 1
# 14 "kernel/cpu/scheduler/../dpc/dpc.h" 2




void init_dpc_system(void);



void MtQueueDPC(volatile DPC* dpc);



void DispatchDPC(void);
# 87 "kernel/cpu/scheduler/../cpu.h" 2
# 1 "kernel/cpu/scheduler/../dpc/dpc_list.h" 1
# 11 "kernel/cpu/scheduler/../dpc/dpc_list.h"
void TimerDPC(void);
# 88 "kernel/cpu/scheduler/../cpu.h" 2
# 1 "kernel/cpu/scheduler/../scheduler/scheduler.h" 1
# 89 "kernel/cpu/scheduler/../cpu.h" 2
# 1 "kernel/cpu/scheduler/../thread/thread.h" 1
# 10 "kernel/cpu/scheduler/../thread/thread.h"
# 1 "kernel/cpu/scheduler/../thread/../cpu.h" 1
# 11 "kernel/cpu/scheduler/../thread/thread.h" 2
# 1 "kernel/cpu/scheduler/../thread/../../memory/memory.h" 1
# 10 "kernel/cpu/scheduler/../thread/../../memory/memory.h"
# 1 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 1 3 4
# 11 "kernel/cpu/scheduler/../thread/../../memory/memory.h" 2


# 1 "kernel/cpu/scheduler/../thread/../../memory/../cpu/cpu.h" 1
# 14 "kernel/cpu/scheduler/../thread/../../memory/memory.h" 2
# 1 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h" 1
# 10 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h"
# 1 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 1 3 4
# 11 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h" 2


# 1 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/../../cpu/cpu.h" 1
# 14 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h" 2
# 1 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/../../trace.h" 1
# 15 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h" 2
# 1 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/uefi_memory.h" 1




# 1 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 1 3 4
# 6 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/uefi_memory.h" 2




typedef struct _BLOCK_DEVICE BLOCK_DEVICE;




typedef struct _EFI_MEMORY_DESCRIPTOR {
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct _GOP_PARAMS {
    uint64_t FrameBufferBase;
    uint64_t FrameBufferSize;
    uint32_t Width;
    uint32_t Height;
    uint32_t PixelsPerScanLine;
} GOP_PARAMS;

typedef struct _BOOT_INFO {
    GOP_PARAMS* Gop;
    EFI_MEMORY_DESCRIPTOR* MemoryMap;
    size_t MapSize;
    size_t DescriptorSize;
    uint32_t DescriptorVersion;
    size_t AhciCount;
    uint64_t* AhciBarBases;
} BOOT_INFO;

_Static_assert(sizeof(BOOT_INFO) == 56, "Size of BOOT_INFO doesn't equal 56 bytes. Update the struct.");
# 56 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/uefi_memory.h"
extern BOOT_INFO boot_info_local;
# 16 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h" 2
# 28 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h"
typedef enum _MEMORY_DESCRIPTOR {
 Free = 7,
 TempFree,
 Bad,
} MEMORY_DESCRIPTOR;






static inline 
# 39 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h" 3 4
             _Bool 
# 39 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h"
                  classify(int type) {
 if (type == 7) {
  return 
# 41 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h" 3 4
        1
# 41 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h"
            ;
 }
 if (type == 3 || type == 4) {
  return 
# 44 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h" 3 4
        1
# 44 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h"
            ;
 }
 return 
# 46 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h" 3 4
       0
# 46 "kernel/cpu/scheduler/../thread/../../memory/../memory/allocator/allocator.h"
            ;
}



void frame_bitmap_init(void);


void* alloc_frame(void);


void free_frame(void* p);
# 15 "kernel/cpu/scheduler/../thread/../../memory/memory.h" 2
# 1 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/paging.h" 1
# 11 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/paging.h"
# 1 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 1 3 4
# 12 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/paging.h" 2


# 1 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../cpu/cpu.h" 1
# 15 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/paging.h" 2
# 1 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../drivers/gop/gop.h" 1
# 32 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../drivers/gop/gop.h"
# 1 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 1 3 4
# 33 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../drivers/gop/gop.h" 2


# 1 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../drivers/gop/../../defs/stdarg_myos.h" 1
# 9 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../drivers/gop/../../defs/stdarg_myos.h"
typedef __builtin_va_list va_list;
# 19 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../drivers/gop/../../defs/stdarg_myos.h"
       
# 36 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../drivers/gop/gop.h" 2
# 1 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../drivers/gop/../../trace.h" 1
# 37 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../drivers/gop/gop.h" 2
# 1 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../drivers/gop/../../memory/memory.h" 1
# 38 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../drivers/gop/gop.h" 2
# 1 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../drivers/gop/../../memory/allocator/uefi_memory.h" 1
# 39 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../drivers/gop/gop.h" 2




static inline void plot_pixel(GOP_PARAMS* gop, uint32_t x, uint32_t y, uint32_t color) {
    uint32_t* fb = (uint32_t*)(uintptr_t)gop->FrameBufferBase;
    uint32_t stride = gop->PixelsPerScanLine;
    fb[y * stride + x] = color;
}

static inline uint32_t char_width(void) { return 8 * 1; }
static inline uint32_t line_height(void) { return 16 * 1; }

void draw_char(GOP_PARAMS* gop, char c, uint32_t x, uint32_t y, uint32_t color);
void draw_string(GOP_PARAMS* gop, const char* s, uint32_t x, uint32_t y, uint32_t color);

void gop_printf(uint32_t color, const char* fmt, ...);
void gop_put_char(GOP_PARAMS* gop, char c, uint32_t color);
void gop_puts(GOP_PARAMS* gop, const char* s, uint32_t color);
void gop_scroll(GOP_PARAMS* gop);
void gop_clear_screen(GOP_PARAMS* gop, uint32_t color);
void gop_print_dec(GOP_PARAMS* gop, unsigned val, uint32_t color);
void gop_print_hex(GOP_PARAMS* gop, uint64_t val, uint32_t color);
# 16 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/paging.h" 2
# 1 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/../../trace.h" 1
# 17 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/paging.h" 2
# 27 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/paging.h"
typedef enum _FLAGS {
 PAGE_PRESENT = 1 << 0,
 PAGE_RW = 1 << 1,
 PAGE_USER = 1 << 2,
    PAGE_PWT = 0x8,
    PAGE_PCD = 0x10,
    PAGE_ACCESSED = 0x20,
    PAGE_DIRTY = 0x40,
    PAGE_PS = 0x80,
    PAGE_GLOBAL = 0x100,
} FLAGS;


extern uint64_t __pd_start;
extern uint64_t __pt_start;
extern uint64_t __pt_end;

void paging_init(void);
void set_page_writable(void* virtualaddress, 
# 45 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/paging.h" 3 4
                                            _Bool 
# 45 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/paging.h"
                                                 writable);
void set_page_user_access(void* virtualaddress, 
# 46 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/paging.h" 3 4
                                               _Bool 
# 46 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/paging.h"
                                                    user_accessible);
void map_page(void* virtualaddress, void* physicaladdress, uint64_t flags);
void map_range_identity(uint64_t start, uint64_t end, uint64_t flags);

# 49 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/paging.h" 3 4
_Bool 
# 49 "kernel/cpu/scheduler/../thread/../../memory/../memory/paging/paging.h"
    unmap_page(void* virtualaddress);




void enable_paging(uint64_t pml4_phys);


void disable_paging(void);
# 16 "kernel/cpu/scheduler/../thread/../../memory/memory.h" 2
# 1 "kernel/cpu/scheduler/../thread/../../memory/../trace.h" 1
# 17 "kernel/cpu/scheduler/../thread/../../memory/memory.h" 2

extern uint8_t kernel_end;
extern uint8_t kernel_start;
extern const size_t kernel_length;


extern uint8_t bss_start;
extern uint8_t bss_end;

void zero_bss(void);






extern uintptr_t heap_current_end;




typedef struct _BLOCK_HEADER {
    size_t size;
    struct _BLOCK_HEADER* next;
} BLOCK_HEADER;


void init_heap(void);


void* kmemset(void* dest, int val, uint32_t len);


void* kmemcpy(void* dest, const void* src, uint32_t len);







void* MtAllocateMemory(size_t size, size_t align);





void MtFreeMemory(void* ptr);
# 12 "kernel/cpu/scheduler/../thread/thread.h" 2
# 1 "kernel/cpu/scheduler/../thread/../scheduler/scheduler.h" 1
# 13 "kernel/cpu/scheduler/../thread/thread.h" 2
# 48 "kernel/cpu/scheduler/../thread/thread.h"
void MtCreateThread(Thread* thread, void(*entry)(void), void* stackTop, 
# 48 "kernel/cpu/scheduler/../thread/thread.h" 3 4
                                                                       _Bool 
# 48 "kernel/cpu/scheduler/../thread/thread.h"
                                                                            kernelThread);
# 90 "kernel/cpu/scheduler/../cpu.h" 2





void read_interrupt_frame(INT_FRAME* frame);
# 110 "kernel/cpu/scheduler/../cpu.h"
static inline void enqueue(Queue* queue, Thread* thread) {
 tracelast_func("enqueue");
 thread->nextThread = 
# 112 "kernel/cpu/scheduler/../cpu.h" 3 4
                     ((void *)0)
# 112 "kernel/cpu/scheduler/../cpu.h"
                         ;
 if (!queue->head) queue->head = thread;
 else queue->tail->nextThread = thread;
 queue->tail = thread;
}

static inline Thread* dequeue(Queue* queue) {
 tracelast_func("dequeue");
 Thread* thread = queue->head;
 if (!thread) return 
# 121 "kernel/cpu/scheduler/../cpu.h" 3 4
                    ((void *)0)
# 121 "kernel/cpu/scheduler/../cpu.h"
                        ;

 queue->head = thread->nextThread;
 if (!queue->head) queue->tail = 
# 124 "kernel/cpu/scheduler/../cpu.h" 3 4
                                ((void *)0)
# 124 "kernel/cpu/scheduler/../cpu.h"
                                    ;
 return thread;
}

void InitCPU(void);

extern CPU cpu;
# 10 "kernel/cpu/scheduler/scheduler.h" 2
# 1 "kernel/cpu/scheduler/../../memory/memory.h" 1
# 11 "kernel/cpu/scheduler/scheduler.h" 2

extern CPU cpu;
# 27 "kernel/cpu/scheduler/scheduler.h"
void InitScheduler(void);


void Schedule(void);


void Yield(void);
# 8 "kernel/cpu/scheduler/scheduler.c" 2
# 1 "kernel/cpu/scheduler/../../bugcheck/bugcheck.h" 1
# 9 "kernel/cpu/scheduler/../../bugcheck/bugcheck.h"
# 1 "/home/kali/Desktop/Operating System/tools64/lib/gcc/x86_64-elf/10.3.0/include/stddef.h" 1 3 4
# 10 "kernel/cpu/scheduler/../../bugcheck/bugcheck.h" 2


# 1 "kernel/cpu/scheduler/../../bugcheck/../cpu/irql/irql.h" 1
# 13 "kernel/cpu/scheduler/../../bugcheck/bugcheck.h" 2
# 1 "kernel/cpu/scheduler/../../bugcheck/../drivers/gop/gop.h" 1
# 14 "kernel/cpu/scheduler/../../bugcheck/bugcheck.h" 2
# 1 "kernel/cpu/scheduler/../../bugcheck/../trace.h" 1
# 15 "kernel/cpu/scheduler/../../bugcheck/bugcheck.h" 2


typedef enum _BUGCHECK_CODES {
    DIVIDE_BY_ZERO,
    SINGLE_STEP,
    NON_MASKABLE_INTERRUPT,
    BREAKPOINT,
    OVERFLOW,
    BOUNDS_CHECK,
    INVALID_OPCODE,
    NO_COPROCESSOR,
    DOUBLE_FAULT,
    COPROCESSOR_SEGMENT_OVERRUN,
    INVALID_TSS,
    SEGMENT_SELECTOR_NOTPRESENT,
    STACK_SEGMENT_OVERRUN,
    GENERAL_PROTECTION_FAULT,
    PAGE_FAULT,
    RESERVED,
    FLOATING_POINT_ERROR,
    ALIGNMENT_CHECK,
    SEVERE_MACHINE_CHECK,

    MEMORY_MAP_SIZE_OVERRUN = 0xBEEF,
    MANUALLY_INITIATED_CRASH = 0xBABE,
    BAD_PAGING = 0xBAD,
    BLOCK_DEVICE_LIMIT_REACHED = 0x420,
    NULL_POINTER_DEREFERENCE = 0xDEAD,
    FILESYSTEM_PANIC = 0xFA11,
    UNABLE_TO_INIT_TRACELASTFUNC = 0xACE,
    FRAME_LIMIT_REACHED = 0xBADA55,
    IRQL_NOT_LESS_OR_EQUAL = 0x1337,
    INVALID_IRQL_SUPPLIED = 0x69420,
    NULL_CTX_RECEIVED = 0xF1FA,
    THREAD_EXIT_FAILURE = 0x123123FF,
    BAD_AHCI_COUNT,
    AHCI_INIT_FAILED,
    MEMORY_LIMIT_REACHED,
} BUGCHECK_CODES;


void MtBugcheck(CTX_FRAME* context, INT_FRAME* int_frame, BUGCHECK_CODES err_code, uint32_t additional, 
# 56 "kernel/cpu/scheduler/../../bugcheck/bugcheck.h" 3 4
                                                                                                       _Bool 
# 56 "kernel/cpu/scheduler/../../bugcheck/bugcheck.h"
                                                                                                            isAdditionals);
# 9 "kernel/cpu/scheduler/scheduler.c" 2



#pragma GCC push_options
#pragma GCC optimize ("O0")
void save_context(CTX_FRAME* regs)
__attribute__((naked, noinline));
void restore_context(const CTX_FRAME* regs)
__attribute__((naked, noinline));
#pragma GCC pop_options





# 23 "kernel/cpu/scheduler/scheduler.c" 3 4
_Bool 
# 23 "kernel/cpu/scheduler/scheduler.c"
    isScheduleDpcQueued = 
# 23 "kernel/cpu/scheduler/scheduler.c" 3 4
                          0
# 23 "kernel/cpu/scheduler/scheduler.c"
                               ;


Thread idleThread;


static uint8_t idleStack[4096] __attribute__((aligned(16)));
extern void kernel_idle_checks(void);


void InitScheduler(void) {
    tracelast_func("InitScheduler");
    cpu.schedulerEnabled = 
# 35 "kernel/cpu/scheduler/scheduler.c" 3 4
                          1
# 35 "kernel/cpu/scheduler/scheduler.c"
                              ;




    CTX_FRAME cfm;
    kmemset(&cfm, 0, sizeof(cfm));


    cfm.rsp = (uint64_t)(idleStack + 4096);
    cfm.rip = (uint64_t)kernel_idle_checks;


    idleThread.registers = cfm;
    idleThread.threadState = RUNNING;
    idleThread.nextThread = 
# 50 "kernel/cpu/scheduler/scheduler.c" 3 4
                           ((void *)0)
# 50 "kernel/cpu/scheduler/scheduler.c"
                               ;


    cpu.currentThread = &idleThread;


    cpu.readyQueue.head = cpu.readyQueue.tail = 
# 56 "kernel/cpu/scheduler/scheduler.c" 3 4
                                               ((void *)0)
# 56 "kernel/cpu/scheduler/scheduler.c"
                                                   ;
}


static void enqueue_runnable(Thread* t) {
    tracelast_func("enqueue_runnable");
    if (t->threadState == RUNNING) {
        t->threadState = READY;
        enqueue(&cpu.readyQueue, t);
    }
}

void Schedule(void) {
    tracelast_func("Schedule");
    if (isScheduleDpcQueued) {
        isScheduleDpcQueued = 
# 71 "kernel/cpu/scheduler/scheduler.c" 3 4
                             0
# 71 "kernel/cpu/scheduler/scheduler.c"
                                  ;
    }
    IRQL oldIrql;
    MtRaiseIRQL(DISPATCH_LEVEL, &oldIrql);

    Thread* prev = cpu.currentThread;
    if (prev != &idleThread) {
        save_context(&prev->registers);

        enqueue_runnable(prev);
    }

    Thread* next = dequeue(&cpu.readyQueue);
    if (!next) {
        next = &idleThread;
    }

    next->threadState = RUNNING;
    cpu.currentThread = next;
    MtLowerIRQL(oldIrql);
    restore_context(&next->registers);
}

void Yield(void) {
    tracelast_func("Yield");
    Schedule();
}
