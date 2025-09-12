#ifndef X86_CPU_TYPES_H
#define X86_CPU_TYPES_H

// Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef REMINDER
_Static_assert(false, "Remember that SPINLOCKS inside of a struct shouldn't be pointers, but embedded within. (so SPINLOCK and not SPINLOCK*) (also on any really, like Queue*, or pointers that don't have initialization function. (caused a page fault)")
#endif

// Global - Per CPU.

typedef enum _THREAD_STATE { RUNNING, READY, BLOCKED, TERMINATING, TERMINATED, ZOMBIE } THREAD_STATE;

// Forwards
typedef struct _Thread Thread;
typedef struct _DPC DPC;
typedef struct _Queue Queue;
typedef struct _CPU CPU;
typedef struct _MUTEX MUTEX;

// Scheduling disabling is by flipping a global flag, I should make a CPU structure that is the current CPU states and data.
// Interrupts are disabled based on the IRQL <=, so even at the IRQL level they are in they are disabled.

// Interrupt Request Level - Changing this will mask IRQ lines, as well as disabling features of the OS/CPU, like disabling the scheduler when raisng to >= DISPATCH_LEVEL.
typedef enum _IRQL {
    PASSIVE_LEVEL = 0,   // Normal thread execution, all interrupts enabled.
    DISPATCH_LEVEL = 2,   // Scheduler disabled, page faults fatal. (page faults are always fatal for now, until I implement an exception handler TODO)
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
    HIGH_LEVEL = 31   // NMI and machine-check (non-maskable) (also gets set at bugchecks, to mask all interrupts)
} IRQL;

typedef struct _SPINLOCK {
    volatile uint32_t locked;
} SPINLOCK;

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
    // General-purpose registers, in whatever order your save/restore stub uses
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rsp;
    uint64_t rip;
} CTX_FRAME;
#pragma pack(pop)

typedef struct _Queue {
	Thread* head;
	Thread* tail;
    SPINLOCK lock;
} Queue;

#define TICK_MS 4

/// <summary>
/// Used to determine how much time in ms a thread should have before being involuntarily relinquished.
/// </summary>
typedef enum _timeSliceTicks {
    LOW_TIMESLICE_TICKS = 16 / TICK_MS,   // 4 MS
    DEFAULT_TIMESLICE_TICKS = 40 / TICK_MS,   // 10 MS
    HIGH_TIMESLICE_TICKS = 100 / TICK_MS,   // 25 MS
} timeSliceTicks;

typedef enum _EVENT_TYPE {
    NotificationEvent,   // Wake all waiting threads
    SynchronizationEvent // Wake one thread at a time
} EVENT_TYPE;

typedef struct _EVENT {
    EVENT_TYPE type;          // Type of event
    bool signaled;            // Current state: signaled or not
    SPINLOCK lock;           // Protects the event
    Queue waitingQueue;      // Threads waiting on this event
} EVENT;

typedef struct _Thread {
	// CPU Registers for context switching
	CTX_FRAME registers;
	// Scheduling metadata
	THREAD_STATE threadState;
    // Remaining ticks until switch. (use enum timeSliceTicks)
    uint32_t timeSlice;
    uint32_t origTimeSlice;
    // The next thread in the link.
	Thread* nextThread;
    // Thread ID.
    uint32_t TID;
    // Original Stack Pointer (to free)
    void* startStackPtr;
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
    void (*CallbackRoutine)(DPC* arg1, void* arg2, void* arg3, void* arg4); // Function prototype MUST take 4 parameters, to match compiler expectations, but they may be ignored with UNREFERENCED_PARAMETER.
    void* Arg1; // You may supply null.
    void* Arg2; // You may supply null.
    void* Arg3; // You may supply null.
    DPC_KIND Kind;
    DPC_PRIORITY priority; // A higher value will run earlier than the lower value
} DPC;

typedef struct _CPU {
	IRQL currentIrql; // Current IRQL global state.
	bool schedulerEnabled; // True/False value if the global scheduler is enabled.
	Thread* currentThread; // Pointer to the current thread struct.
	Queue readyQueue; // Queue for the next scheduling.
} CPU;

/* MUTEX Struct */
typedef struct _MUTEX {
    uint32_t ownerTid;
    EVENT SynchEvent;
    bool locked;
    SPINLOCK lock;
} MUTEX;

#ifndef _MSC_VER
_Static_assert(sizeof(CTX_FRAME) == 0x88, "CTX_FRAME must be 0x88 bytes");
_Static_assert(offsetof(CTX_FRAME, rsp) == 0x78, "CTX_FRAME.rsp offset must be 0x78");
_Static_assert(offsetof(CTX_FRAME, rip) == 0x80, "CTX_FRAME.rip offset must be 0x80");

_Static_assert(sizeof(Thread) >= 0xA0, "Thread must be at least 0xA0 bytes");
_Static_assert(offsetof(Thread, threadState) == 0x88, "Thread.threadState offset must be 0x88");
_Static_assert(offsetof(Thread, timeSlice) == 0x8C, "Thread.timeSlice offset must be 0x8C");
_Static_assert(offsetof(Thread, origTimeSlice) == 0x90, "Thread.origTimeSlice offset must be 0x90");
_Static_assert(offsetof(Thread, nextThread) == 0x98, "Thread.nextThread offset must be 0x98");
_Static_assert(sizeof(SPINLOCK) == 4, "SPINLOCK must be 4 bytes");
_Static_assert(_Alignof(SPINLOCK) >= 4, "SPINLOCK alignment must be >= 4");
#endif

#endif
