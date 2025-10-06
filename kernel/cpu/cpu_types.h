#ifndef X86_CPU_TYPES_H
#define X86_CPU_TYPES_H
#pragma once

/*
 * x86_cpu_types.h
 *
 * Clean, well-organized CPU / scheduling / sync types for MatanelOS.
 * - Packed frames where required.
 * - Embedded spinlocks (do not make them pointers).
 *
 * Author: reorganized by ChatGPT for readability.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

    /* --------------------------------------------------------------------------
     * REMINDERS / DEVELOPMENT HINTS
     * --------------------------------------------------------------------------
     *
     * - SPINLOCKs should be embedded in structs, not pointers (see REMINDER).
     * - Keep packing only for frames that are saved/restored by asm stubs.
     *
     */
#ifdef REMINDER
    _Static_assert(false, "Remember that SPINLOCKS inside of a struct shouldn't be pointers, but embedded within. (so SPINLOCK and not SPINLOCK*) (also on any really, like Queue*, or pointers that don't have initialization function. (caused a page fault)");
#endif

    /* --------------------------------------------------------------------------
     * Forward declarations
     * -------------------------------------------------------------------------- */
    typedef struct _Thread   Thread;
    typedef struct _PROCESS  PROCESS;
    typedef struct _DPC      DPC;
    typedef struct _Queue    Queue;
    typedef struct _CPU      CPU;
    typedef struct _MUTEX    MUTEX;
    typedef struct _SINGLE_LINKED_LIST SINGLE_LINKED_LIST;
    typedef struct _DOUBLY_LINKED_LIST DOUBLY_LINKED_LIST;

    /* --------------------------------------------------------------------------
     * Basic enums / core types
     * -------------------------------------------------------------------------- */

    /**
    * Single Linked List - Next -> Next -> Next
    * Doubly Linked List - Next / Prev -> Next / Prev
    */

    typedef struct _SINGLE_LINKED_LIST {
        SINGLE_LINKED_LIST* Next;
    } SINGLE_LINKED_LIST;

    typedef struct _DOUBLY_LINKED_LIST {
        DOUBLY_LINKED_LIST* Blink;
        DOUBLY_LINKED_LIST* Flink;
    } DOUBLY_LINKED_LIST;

     /**
      * THREAD_STATE - high-level thread lifecycle states
      */
    typedef enum _THREAD_STATE {
        RUNNING,
        READY,
        BLOCKED,
        TERMINATING,
        TERMINATED,
        ZOMBIE
    } THREAD_STATE;

    /**
     * IRQL - Interrupt Request Levels (masks IRQ lines and changes kernel behavior).
     *
     * Notes:
     *  - PASSIVE_LEVEL = 0 : normal thread execution
     *  - DISPATCH_LEVEL  = 2 : scheduler disabled; page-faults are fatal until handlers exist
     *  - Device DIRQLs chosen so IRQn + DIRQL = PROFILE_LEVEL (27)
     *  - HIGH_LEVEL masks everything (NMI / machine-check)
     */
    typedef enum _IRQL {
        PASSIVE_LEVEL = 0,
        DISPATCH_LEVEL = 2,
        PROFILE_LEVEL = 27,
        CLOCK_LEVEL = 28,
        IPI_LEVEL = 29,
        POWER_LEVEL = 30,
        HIGH_LEVEL = 31
    } IRQL;

    /* --------------------------------------------------------------------------
     * Spinlock
     * -------------------------------------------------------------------------- */

     /**
      * SPINLOCK - a tiny embedded spinlock representation.
      *
      * Implementation note: keep this embedded (not a pointer) inside structures.
      */
    typedef struct _SPINLOCK {
        volatile uint32_t locked; /* 0 = unlocked, 1 = locked */
    } SPINLOCK;

    /* --------------------------------------------------------------------------
     * Packed exception / interrupt and context frames
     * -------------------------------------------------------------------------- */

#pragma pack(push, 1)
     /**
      * INT_FRAME
      * - Software representation of an interrupt/exception frame.
      * - Packed to match assembler save/restore layout.
      */
    typedef struct _INT_FRAME {
        uint64_t vector;
        uint64_t error_code;
        uint64_t rip;
        uint64_t cs;
        uint64_t rflags;
        uint64_t rsp; /* always present in our software frame */
        uint64_t ss;  /* always present in our software frame */
    } INT_FRAME;
#pragma pack(pop)

#pragma pack(push, 1)
    /**
     * CTX_FRAME
     * - Context saved/restored during a thread switch.
     * - Order must match asm save/restore stubs.
     */
    typedef struct _CTX_FRAME {
        uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
        uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
        uint64_t rsp;
        uint64_t rip;
        uint64_t rflags;
    } CTX_FRAME;
#pragma pack(pop)

#pragma pack(push, 1)
    typedef struct _TRAP_FRAME {
        uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
        uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
        uint64_t rsp;
        uint64_t rip;
        uint64_t rflags;
        uint64_t ss;
        uint64_t cs;
    } TRAP_FRAME;
#pragma pack(pop)

    /* --------------------------------------------------------------------------
     * Generic queue used for ready/wait lists
     * -------------------------------------------------------------------------- */

     /**
      * Queue - simple singly-linked thread queue with embedded spinlock.
      *
      * - head/tail point to Thread nodes
      * - lock protects head/tail and traversal operations
      */
    typedef struct _Queue {
        Thread* head;
        Thread* tail;
        SPINLOCK lock; /* embedded spinlock (do not change from embedded) */
    } Queue;

    /* --------------------------------------------------------------------------
     * Time slice constants
     * -------------------------------------------------------------------------- */

#define TICK_MS 4

     /**
      * timeSliceTicks - how many kernel ticks a thread receives for a timeslice.
      * Values expressed in ticks (TICK_MS) — keep integer division deliberate.
      */
    typedef enum _timeSliceTicks {
        LOW_TIMESLICE_TICKS = 16 / TICK_MS,  /* 4 ms  */
        DEFAULT_TIMESLICE_TICKS = 40 / TICK_MS,  /* 10 ms */
        HIGH_TIMESLICE_TICKS = 100 / TICK_MS   /* 25 ms */
    } timeSliceTicks;

    /* --------------------------------------------------------------------------
     * Events (synchronization primitives)
     * -------------------------------------------------------------------------- */

     /**
      * EVENT_TYPE - controls wake behavior
      */
    typedef enum _EVENT_TYPE {
        NotificationEvent,   /* wake all waiting threads */
        SynchronizationEvent /* wake one thread at a time */
    } EVENT_TYPE;

    /**
     * EVENT - kernel event object
     * - Embedded SPINLOCK and Queue for waiting threads.
     */
    typedef struct _EVENT {
        EVENT_TYPE type;              /* Notification vs Synchronization */
        volatile bool signaled;       /* current state */
        SPINLOCK lock;                /* protects signaled + waitingQueue */
        Queue waitingQueue;           /* threads waiting on this event */
    } EVENT;

    /* --------------------------------------------------------------------------
     * Thread structure
     * -------------------------------------------------------------------------- */

    enum _THREAD_TYPE {
        THREAD_USER = 1 << 0,
        THREAD_KERNEL = 1 << 1
    };

     /**
      * Thread - thread control block (TCB)
      *
      * Layout notes:
      *  - registers (CTX_FRAME) first so the asm context switch can operate easily
      *  - offsets are validated by _Static_asserts at bottom
      */
    typedef struct _Thread {
        TRAP_FRAME registers;    /* saved register/context frame */
        THREAD_STATE threadState; /* at offset 0x88 (asserted later) */
        uint32_t timeSlice;     /* remaining ticks until preemption */
        uint32_t origTimeSlice; /* original allocated slice for bookkeeping */
        Thread* nextThread;     /* singly-linked list pointer for queues */
        uint32_t TID;           /* thread id */
        void* startStackPtr;    /* original/allocated stack start to free */
        uint64_t userStackVa; /* top of user stack va */
        EVENT* CurrentEvent; /* ptr to current EVENT if any. */
        struct _PROCESS* ParentProcess; /* pointer to the parent process of the thread */
        //uint64_t ThreadType; /* type of thread, user mode?, kernel mode? */
        /* TODO: priority, affinity, wait list, etc. */
    } Thread;


    /* --------------------------------------------------------------------------
    * Process structure
    * -------------------------------------------------------------------------- */

    enum PROCESS_STATE {
        PROCESS_RUNNING = 1 << 0, // A thread in the process is currently running
        PROCESS_READY = 1 << 1,  // The process is ready to run. (essentially its threads)
        PROCESS_WAITING = 1 << 2,  // Waiting on a mutual exclusion or just sleeping.
        PROCESS_TERMINATING = 1 << 3,   // A process is ongoing termination in the kernel.
        PROCESS_TERMINATED = 1 << 4,  // Process is terminated, yet the memory stays.
        PROCESS_SUSPENDED = 1 << 5  // The process has been suspended by the kernel, either by choice or forcefully, mm.
    };

        /**
         * Process - Represents an executing program.
         *
         * Encapsulates the program's execution context, including
         * threads, virtual memory space, handles, and system resources.
         */

#define PROCESS_STACK_SIZE (32*1024) // 32 KiB
#define PROCESS_STACK_ALIGNMENT 16 // Alignment of 16 Bytes.

    typedef struct _PROCESS {
        uint32_t PID; // Process Identifier, unique identifier to the process.
        struct _PROCESS* ParentProcess; // Parent Process Pointer (should always be one)
        char ImageName[24]; // Process image name, detected in headers.
        uint64_t ProcessState;
        uint32_t priority; // TODO
        uint64_t* PageDirectoryVirtual; // PML4 of the Process, per process page tables, TODO.
        uintptr_t PageDirectoryPhysical; // Physical address of the PML4 of the process.
        uint64_t CreationTime; // Timestamp of creation, seconds from 1970 January 1st. (may change)
        // SID TODO. - User info as well, when users.
        uint64_t ImageBase; // Base Pointer of loaded process memory.
        void* FileBuffer; // Pointer of allocated file buffer contents, to free when done.

        // Synchorinzation for internal functions.
        SPINLOCK ProcessLock; // An internal spinlock so multiple kernel threads cant change the process structure at the same time.

        // Thread infos
        Thread* MainThread; // Pointer to the main thread created for the process.
        Queue AllThreads; // A singular linked list of pointers to the current threads of the process. (inserted with each new creation)
        uint32_t NumThreads; // Unsigned 32 bit integer representing the amount of threads the process has.
        uint64_t NextStackTop; // A 64 bit value representing the next stack top for a newly created thread
    } PROCESS;

    /* --------------------------------------------------------------------------
     * Deferred Procedure Calls (DPC)
     * -------------------------------------------------------------------------- */

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
        DPC_CPU_ALLOCATED,
        /* TODO: more kinds */
    } DPC_KIND;

    /**
     * DPC - Deferred Procedure Call structure used by the kernel's DPC queue.
     *
     * Callback signature intentionally takes 4 parameters to match compiler/calling convention.
     */

#define PENDING_DPC_BUCKETS 16
    typedef struct _DPC {
        DPC* Next; /* Next DPC in pending queue */
        void (*CallbackRoutine)(DPC* arg1, void* arg2, void* arg3, void* arg4);
        void* Arg1;
        void* Arg2;
        void* Arg3;
        DPC_KIND Kind;
        DPC_PRIORITY priority; /* higher runs earlier */
        _Atomic(uint8_t)Queued;
    } DPC;

    /* --------------------------------------------------------------------------
     * Interprocessor Interrupt Structure
    * -------------------------------------------------------------------------- */

    typedef void (*DebugCallback)(void*);

    typedef struct _DEBUG_ENTRY {
        void* Address;
        DebugCallback Callback;
    } DEBUG_ENTRY;

    typedef struct _DEBUG_REGISTERS {
        uint64_t dr7;
        uint64_t address;
        DebugCallback callback;
    } DEBUG_REGISTERS;

    typedef struct _PAGE_PARAMETERS {
        uint64_t addressToInvalidate;
    } PAGE_PARAMETERS;

    typedef struct _IPI_PARAMS {
        DEBUG_REGISTERS debugRegs;
        PAGE_PARAMETERS pageParams;
    } IPI_PARAMS;

    /* --------------------------------------------------------------------------
     * Per-CPU structure
     * -------------------------------------------------------------------------- */

    /**
    * CPU_FLAGS - Bitflags for the CPU Flags field in the CPU struct for the current CPU.
    *
    */
    typedef enum _CPU_FLAGS {
        CPU_ONLINE = 1 << 0,  // 0b0001
        CPU_HALTED = 1 << 1,  // 0b0010
        CPU_DOING_IPI = 1 << 2,  // 0b0100
        CPU_UNAVAILABLE = 1 << 3   // 0b1000
    } CPU_FLAGS;

    // DPC Embedded struct into CPU.
    struct _DPC_QUEUE {
        DPC* dpcQueueHead;
        DPC* dpcQueueTail;
        SPINLOCK lock;
        _Atomic(DPC*)pendingHeads[PENDING_DPC_BUCKETS];
    };

#define LASTFUNC_BUFFER_SIZE 128
#define LASTFUNC_HISTORY_SIZE 25

    typedef struct {
        uint8_t names[LASTFUNC_HISTORY_SIZE][LASTFUNC_BUFFER_SIZE];
        int     current_index;
    } LASTFUNC_HISTORY;

     /**
      * CPU - per-CPU runtime state (for single-core keep one global CPU instance).
      *
      * - self: pointer to self CPU struct, used internally.
      * - currentIrql: mask/level of interrupts
      * - schedulerEnabled: global scheduling on/off
      * - currentThread: pointer to running thread
      * - readyQueue: queue of ready threads
      * ... more.
      */
    typedef struct _CPU {
        struct _CPU* self; // A pointer to the current CPU Struct, used internally by functions, see MtStealThread in scheduler.c
        enum _IRQL currentIrql; // An integer that represents the current interrupt request level of the CPU. Declares which IOAPIC interrupts are masked
        volatile bool schedulerEnabled; // A boolean value that indicates if the scheduler is allowed to be called after an interrupt.
        struct _Thread* currentThread; // Current thread that is being executed in the CPU.
        struct _Queue readyQueue; // Queue of thread pointers to be scheduled.
        uint32_t ID; // ID is also the index for cpus (e.g cpus[3] so .ID is 3)
        uint32_t lapic_ID; // Internal APIC id of the CPU.
        void* VirtStackTop; // Pointer to top of CPU Stack.
        void* tss; // Task State Segment top pointer
        void* IstPFStackTop; // Page Fault IST Stack
        void* IstDFStackTop; // Double Fault IST Stack
        volatile uint64_t flags; // CPU Flags (CPU_FLAGS enum), contains the current state of the CPU.
        bool schedulePending; // A boolean value that indicates if a schedule is currently pending on the CPU.
        uint64_t* gdt; // A pointer to the current GDT of the CPU (set in the CPUs AP entry), does not include BSP GDT.
        struct _DPC_QUEUE DeferredRoutineQueue; // Deferred Routine queue, used to RetireDPCs that exist after an interrupt
        struct _DPC* CurrentDeferredRoutine; // Current deferred routine that is executed by the CPU.
        struct _DPC AllocatedDPC; // CPU Allocated DPC routine to be used if memory allocation is unable to be used at the current context.
        Thread idleThread; // Idle thread for the current CPU.
        volatile uint64_t IpiSeq; 
        volatile uint32_t IpiAction; // IPI Action specified in the function.
        volatile IPI_PARAMS IpiParameter; // Optional parameter for IPI's, usually used for functions, primarily TLB Shootdowns.
        volatile uint32_t* LapicAddressVirt; // Virtual address of the Local APIC MMIO Address (mapped)
        uintptr_t LapicAddressPhys; // Physical address of the Local APIC MMIO
        LASTFUNC_HISTORY* lastfuncBuffer; // Per CPU Buffer for the latest functions trace, allocated dynamically. (ptr)
        volatile bool DeferredRoutineActive; // Per CPU Flag that indicates if the RetireDPCs call is active and retiring DPCs (to prevent re-entracy)
        
        /* Statically Special Allocated DPCs */
        DPC TimerExpirationDPC;
        /* End Statically Special Allocated DPCs */

        DEBUG_ENTRY DebugEntry[4]; // Per CPU Strucutre that contains debug entries for each debug register.
    } CPU;

    /* --------------------------------------------------------------------------
     * MUTEX - mutual exclusion primitive
     * -------------------------------------------------------------------------- */

    typedef struct _MUTEX {
        uint32_t ownerTid;  /* owning thread id (0 if none) */
        EVENT SynchEvent;   /* event used for waking waiters */
        bool locked;        /* fast-check boolean (protected by lock) */
        SPINLOCK lock;      /* protects ownerTid/locked and wait list */
        struct _Thread* ownerThread; /* pointer to current thread that holds the mutex */
    } MUTEX;

    /* --------------------------------------------------------------------------
     * Compile-time assertions (kept from original file)
     * -------------------------------------------------------------------------- */

#ifndef _MSC_VER
_Static_assert(sizeof(CTX_FRAME) == 0x90, "CTX_FRAME must be 0x90 bytes");
_Static_assert(sizeof(Thread) >= 0xA0, "Thread must be at least 0xA0 bytes");
_Static_assert(offsetof(Thread, ParentProcess) == 0xD8, "Thread.ParentProcess offset must be 0xD8");
_Static_assert(offsetof(PROCESS, PageDirectoryPhysical) == 0x40, "PROCESS.PageDirectoryPhysical must be at offset 0x40");
_Static_assert(sizeof(SPINLOCK) == 4, "SPINLOCK must be 4 bytes");
_Static_assert(_Alignof(SPINLOCK) >= 4, "SPINLOCK alignment must be >= 4");
#endif

#ifdef __cplusplus
}
#endif

#endif /* X86_CPU_TYPES_H */
