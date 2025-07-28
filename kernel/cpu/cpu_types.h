#ifndef X86_CPU_TYPES_H
#define X86_CPU_TYPES_H

// Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Global - Per CPU.

typedef enum _THREAD_STATE { RUNNING, READY, BLOCKED, TERMINATING, TERMINATED } THREAD_STATE;

// Forwards
typedef struct _Thread Thread;
typedef struct _DPC DPC;
typedef struct _Queue Queue;
typedef struct _CPU CPU;

// Scheduling disabling is by flipping a global flag, I should make a CPU structure that is the current CPU states and data.
// Interrupts are disabled based on the IRQL <=, so even at the IRQL level they are in they are disabled.
typedef enum _IRQL {
    PASSIVE_LEVEL = 0,   // Normal thread execution, all interrupts enabled.
    DISPATCH_LEVEL = 2,   // Scheduler disabled, page faults fatal. (page faults are always fatal for now, until I implement an exception handler)
    // Device DIRQLs (chosen so IRQn + DIRQL = PROFILE_LEVEL (27))
    DIRQL_SECONDARY_ATA = 12,   // IRQ15 – Secondary ATA Channel  
    DIRQL_PRIMARY_ATA = 13,     // IRQ14 – Primary ATA Channel  
    DIRQL_FPU = 14,             // IRQ13 – FPU / Coprocessor  
    DIRQL_MOUSE = 15,           // IRQ12 – Mouse  
    DIRQL_PERIPHERAL11 = 16,    // IRQ11 – Free for peripherals  
    DIRQL_PERIPHERAL10 = 17,    // IRQ10 – Free for peripherals  
    DIRQL_PERIPHERAL9 = 18,     // IRQ9  – Free / redirected cascade  
    DIRQL_RTC = 19,             // IRQ8  – RTC / CMOS Alarm  
    DIRQL_LPT1 = 20,            // IRQ7  – LPT1 / Printer  
    DIRQL_FLOPPY = 21,          // IRQ6  – Floppy Disk  
    DIRQL_SOUND_LPT2 = 22,      // IRQ5  – Sound Card / LPT2  
    DIRQL_COM1 = 23,            // IRQ4  – Serial COM1  
    DIRQL_COM2 = 24,            // IRQ3  – Serial COM2  
    DIRQL_CASCADE = 25,         // IRQ2  – Cascade (IRQs 8–15)  
    DIRQL_KEYBOARD = 26,        // IRQ1  – Keyboard  
    DIRQL_TIMER = 27,           // IRQ0  – System Timer  
    PROFILE_LEVEL = 27,  // Profile timer interrupts (alias of DIRQL_TIMER)
    CLOCK_LEVEL = 28,  // Clock/timer interrupts (second-level timer) (actual clock IRQ timer, for scheduler, time-of-day clock, and general timers, even context switching)
    SYNCH_LEVEL = 29,  // Synchronization level (internal kernel use) (unused in my kernel (until SMP), this level is used for multi core CPU synchronization)
    POWER_LEVEL = 30,  // Power failure interrupts
    HIGH_LEVEL = 31   // NMI and machine‑check (non‑maskable) (also gets set at bugchecks, to mask all interrupts)
} IRQL;



#pragma pack(push, 1)
typedef struct _INT_FRAME {
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;         // Always present in our software frame
    uint64_t ss;          // Always present in our software frame
} INT_FRAME;
#pragma pack(pop)

// Context frame for saving/restoring thread state
#pragma pack(push, 1)
typedef struct _CTX_FRAME {
    // General‑purpose registers, in whatever order your save/restore stub uses
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rsp;
    uint64_t rip;
    // no more vector and err_num, since it's better for clarity now.
} CTX_FRAME;
#pragma pack(pop)

// In order for compatiblity with the interrupt service routines, and the stub. I'm still gonna retain the old REGS struct, with a different name.
#pragma pack(push, 1)
typedef struct _INTERRUPT_FULL_REGS {
	uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
	uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    // HAS NO RSP, INTERRUPT MUST NOT USE IT!
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
	// CPU Registers for context switching
	CTX_FRAME registers;

	// Scheduling metadata
	THREAD_STATE threadState;
	// Remaining ticks until switch.
	///uint32_t timeSlice; /// TODO , is this used? -- IT IS NOT USED CURRENTLY - As the timer interrupt fires and queues a DPC, the timeSlice doesn't decide when the thread will yield, it's the same for each thread in the system - could be changed.
	Thread* nextThread; // For queue linking.
	// TODO later: priority, affinity, wait list pointer, etc.
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
    /// TODO more dpcs..
} DPC_KIND;

typedef struct _DPC {
    DPC* Next; // Next DPC in the pending queue.
    void (*callbackWithCtx)(void* ctx); // Callback entry for this DPC, along with context register info.
    void (*callback)(void); /// Callback without any CONTEXT (no registers), used to invoke DPC's like scheduler.
    CTX_FRAME* ctx; // Caller supplied context pointer (registers)
    DPC_KIND Kind;
    bool hasCtx;
    DPC_PRIORITY priority; // A higher value will run earlier than the lower value
} DPC;

typedef struct _CPU {
	IRQL currentIrql; // Current IRQL global state.
	bool schedulerEnabled; // True/False value if the global scheduler is enabled.
	Thread* currentThread; // Pointer to the current thread struct.
	Queue readyQueue; // Queue for the next scheduling.
} CPU;

#endif
