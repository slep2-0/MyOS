#ifndef MATANEL_EXECUTIVE_H
#define MATANEL_EXECUTIVE_H

/*++

Module Name:

	me.h

Purpose:

	This module contains the header files & prototypes required for the executive layer of MatanelOS.

Author:

	slep (Matanel) 2025.

Revision History:

--*/

#include <stdint.h>
#include <stdbool.h>
#include "annotations.h"
#include "macros.h"
#include "../mtstatus.h"
#include "../intrinsics/intrin.h"
#include "../intrinsics/atomic.h"

// Other includes:	
#include "mm.h"
#include "ms.h"
#include "ps.h"
#include "core.h"
// ------------------ UNIONS ------------------

// ------------------ ENUMERATORS ------------------

#define TICK_MS 4
typedef enum _TimeSliceTicks {
	LOW_TIMESLICE_TICKS = 16 / TICK_MS,  /* 4 ms  */
	DEFAULT_TIMESLICE_TICKS = 40 / TICK_MS,  /* 10 ms */
	HIGH_TIMESLICE_TICKS = 100 / TICK_MS   /* 25 ms */
} TimeSliceTicks, *PTimeSliceTicks;

typedef enum _PRIVILEGE_MODE {
	KernelMode = 0,
	UserMode = 1
} PRIVILEGE_MODE, *PPRIVILEGE_MODE;

typedef enum _WAIT_REASON {
	Mutex = 0,
	Sleeping = 1,
} WAIT_REASON;

typedef enum _DPC_PRIORITY {
	NO_PRIORITY = 0,
	LOW_PRIORITY = 25,
	MEDIUM_PRIORITY = 50,
	HIGH_PRIORITY = 75,
	SYSTEM_PRIORITY = 99
} DPC_PRIORITY;


// DEPRECATED - New list should be in BUGCODES.H explaining each with its parameters. TODO
// Bugcheck error code enums, use same exception list from CPU.
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
	/// Custom ones
	MEMORY_MAP_SIZE_OVERRUN = 0xBEEF, // The memory map has grown beyond the limit (unused).
	MANUALLY_INITIATED_CRASH = 0xBABE, // A function has manually initiated a bugcheck for testing/unknown reasons with this specific code.
	BAD_PAGING = 0xBAD, // A paging function that fails when it shouldn't.
	BLOCK_DEVICE_LIMIT_REACHED = 0x420, // Something tried to register a block device, but the limit has been reached, bugcheck system.
	NULL_POINTER_DEREFERENCE = 0xDEAD, // Attempted dereference of a null pointer.
	FILESYSTEM_PANIC = 0xFA11, // FileSystem PANIC, usually something wrong has happened
	UNABLE_TO_INIT_TRACELASTFUNC = 0xACE, // TraceLastFunc init failed in kernel_main
	FRAME_LIMIT_REACHED = 0xBADA55, // frame limit reached when trying to allocate a physical frame.
	IRQL_NOT_LESS_OR_EQUAL = 0x1337, // Access to functions while going over the max IRQL set for them. Or lowering to higher IRQL than current IRQL.
	IRQL_NOT_GREATER_OR_EQUAL = 0x1338, // Raising IRQL to an IRQL level that is lower than the current one.
	INVALID_IRQL_SUPPLIED = 0x69420, // Invalid IRQL supplied to raising / lowering IRQL.
	NULL_CTX_RECEIVED = 0xF1FA, // A null context frame has been received to a function.
	THREAD_EXIT_FAILURE = 0x123123FF, // A thread exitted but did not schedule (somehow).
	BAD_AHCI_COUNT, // AHCI Count has went over the required limit
	AHCI_INIT_FAILED, // Initialization of AHCI has failed..
	MEMORY_LIMIT_REACHED, // The amount of physical memory has reached its maximum, allocation has failed.
	HEAP_ALLOCATION_FAILED, // Allocating from the HEAP failed for an unknown reason.
	NULL_THREAD, // A thread given to the scheduler is NULL.
	FATAL_IRQL_CORRUPTION, // IRQL Has been corrupted, somehow. Probably a buffer overflow.
	THREAD_ID_CREATION_FAILURE, // Creation of a TID (Thread ID) has failed due to reaching maximum TIDs in use by the system.
	FRAME_ALLOCATION_FAILED, // Allocating a physical frame from the frame bitmap has failed.
	FRAME_BITMAP_CREATION_FAILURE, // Creating the frame bitmap resulted in a failure.
	ASSERTION_FAILURE, // Runtime Assertion Failure (assert())
	MEMORY_INVALID_FREE,
	MEMORY_CORRUPT_HEADER,
	MEMORY_DOUBLE_FREE,
	MEMORY_CORRUPT_FOOTER,
	GUARD_PAGE_DEREFERENCE, // A guard page has been dereferenced.
	KERNEL_STACK_OVERFLOWN // A kernel stack has been overflown (and didnt hit the guard page) (detected by canary)
} BUGCHECK_CODES;

// ------------------ STRUCTURES ------------------

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
	struct _DEBUG_REGISTERS debugRegs;
	struct _PAGE_PARAMETERS pageParams;
} IPI_PARAMS;

typedef struct _WAIT_BLOCK {
	struct _SINGLE_LINKED_LIST WaitBlockList;	// List entry of the current wait block of the thread.
	void* Object;								// Pointer to the object it is currently waiting on (indicated which one by WaitReason)
	enum _WAIT_REASON WaitReason;				// Defines which object the thread is currently waiting on (indicated by the _WAIT_REASON Enumerator)
} WAIT_BLOCK, *PWAIT_BLOCK;

typedef struct _TRAP_FRAME {
	uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
	uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
	uint64_t vector;
	uint64_t error_code;
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ss; 
} TRAP_FRAME, *PTRAP_FRAME;

typedef enum _DEBUG_ACCESS_MODE {
	DEBUG_ACCESS_EXECUTE = 0b00,    // Break on instruction execution
	DEBUG_ACCESS_WRITE = 0b01,    // Break on data writes
	DEBUG_ACCESS_IO = 0b10,    // Break on I/O read or write (legacy)
	DEBUG_ACCESS_READWRITE = 0b11   // Break on data reads or writes
} DEBUG_ACCESS_MODE;

typedef enum _DEBUG_LENGTH {
	DEBUG_LEN_1 = 0b00,
	DEBUG_LEN_2 = 0b01,
	DEBUG_LEN_8 = 0b10, // Only valid in long mode
	DEBUG_LEN_4 = 0b11
} DEBUG_LENGTH;

typedef struct _DBG_CALLBACK_INFO {
	void* Address;           /* breakpoint address (DRx) */
	PTRAP_FRAME trap;		/* trap frame captured */
	int   BreakIdx;         /* which DRx (0..3) fired */
	uint64_t Dr6;           /* raw DR6 value at time of trap */
} DBG_CALLBACK_INFO;

#define PENDING_DPC_BUCKETS 16
typedef struct _DPC {
	struct _DPC* Next; /* Next DPC in pending queue */
	void (*CallbackRoutine)(DPC* arg1, void* arg2, void* arg3, void* arg4);
	void* Arg1;
	void* Arg2;
	void* Arg3;
	enum _DPC_PRIORITY priority; /* higher runs earlier */
	_Atomic(uint8_t)Queued;
} DPC, *PDPC;

typedef enum _CPU_FLAGS {
	CPU_ONLINE = 1 << 0,  // 0b0001
	CPU_HALTED = 1 << 1,  // 0b0010
	CPU_DOING_IPI = 1 << 2,  // 0b0100
	CPU_UNAVAILABLE = 1 << 3   // 0b1000
} CPU_FLAGS;

// DPC Embedded struct into CPU.
typedef struct _DPC_QUEUE {
	struct _DPC* dpcQueueHead;
	struct _DPC* dpcQueueTail;
	struct _SPINLOCK lock;
	_Atomic(DPC*)pendingHeads[PENDING_DPC_BUCKETS];
} DPC_QUEUE, * PDPC_QUEUE;

#define LASTFUNC_BUFFER_SIZE 128
#define LASTFUNC_HISTORY_SIZE 25
// Default timeslice for a new thread.
#define DEFAULT_TIMESLICE 1

#define KERNEL_CS       0x08    // Entry 1: Kernel Code
#define KERNEL_DS       0x10    // Entry 2: Kernel Data  
#define KERNEL_SS       0x10    // Same as KERNEL_DS (data segment used for stack)
#define USER_CS         0x1B    // Entry 3: User Code (for future)
#define USER_DS         0x23    // Entry 4: User Data (for future)
#define USER_SS         0x23    // Same as USER_DS (for future)
#define INITIAL_RFLAGS  0x202
#define USER_RFLAGS     0x246 // IF=1, IOPL=0, CPL=3

typedef struct _LASTFUNC_HISTORY {
	uint8_t names[LASTFUNC_HISTORY_SIZE][LASTFUNC_BUFFER_SIZE];
	int     current_index;
} LASTFUNC_HISTORY;

typedef struct _IPROCESS {
	uintptr_t PageDirectoryPhysical;		// Physical Address of the PML4 of the process.
	uint64_t* PageDirectoryVirtual;			// Virtual Address of the PML4 of the process. (accessible in kernel pages)
	struct _SPINLOCK ProcessLock;			// Internal Spinlock for process field manipulation safety.
	enum _PROCESS_STATE ProcessState;		// Current process state.
} IPROCESS, *PIPROCESS;

typedef struct _ITHREAD {
	struct _TRAP_FRAME TrapRegisters;					   // TRAP Registers used for context switching, saving, and alternation.
	enum _THREAD_STATE ThreadState;						   // Current thread state, presented by the THREAD_STATE enumerator.
	void* StackBase;									   // Base of the thread's stack, used for also freeing it by the memory manager (Mm).
	enum _TimeSliceTicks TimeSlice;						   // Current timeslice remaining until thread's forceful pre-emption.
	enum _TimeSliceTicks TimeSliceAllocated;			   // Original timeslice given to the thread, used for restoration when it's current one is over.
	struct _SINGLE_LINKED_LIST NextThread;				   // A singular linked list representing the next thread.
	enum _PRIVILEGE_MODE PreviousMode;					   // Previous mode of the thread (used to indicate whether it called a kernel service in kernel mode, or in user mode)			
	struct _WAIT_BLOCK WaitBlock;						   // Wait block of the current thread, defines a list of which events the thread is waiting on (mutex event, general sleeping)
} ITHREAD, *PITHREAD;

typedef struct _PROCESSOR {
	struct _PROCESSOR* self; // A pointer to the current CPU Struct, used internally by functions, see MtStealThread in scheduler.c
	enum _IRQL currentIrql; // An integer that represents the current interrupt request level of the CPU. Declares which LAPIC & IOAPIC interrupts are masked
	volatile bool schedulerEnabled; // A boolean value that indicates if the scheduler is allowed to be called after an interrupt.
	struct _ITHREAD* currentThread; // Current thread that is being executed in the CPU.
	struct _Queue readyQueue; // Queue of thread pointers to be scheduled.
	uint32_t ID; // ID is also the index for cpus (e.g cpus[3] so .ID is 3)
	uint32_t lapic_ID; // Internal APIC id of the CPU.
	void* VirtStackTop; // Pointer to top of CPU Stack.
	void* tss; // Task State Segment ptr.
	void* IstPFStackTop; // Page Fault IST Stack
	void* IstDFStackTop; // Double Fault IST Stack
	volatile uint64_t flags; // CPU Flags (CPU_FLAGS enum), contains the current state of the CPU.
	bool schedulePending; // A boolean value that indicates if a schedule is currently pending on the CPU
	uint64_t* gdt; // A pointer to the current GDT of the CPU (set in the CPUs AP entry), does not include BSP GDT.
	struct _DPC_QUEUE DeferredRoutineQueue; // Deferred Routine queue, used to RetireDPCs that exist after an interrupt
	struct _DPC* CurrentDeferredRoutine; // Current deferred routine that is executed by the CPU.
	struct _ETHREAD idleThread; // Idle thread for the current CPU.
	volatile uint64_t IpiSeq;
	volatile enum _CPU_ACTION IpiAction; // IPI Action specified in the function.
	volatile IPI_PARAMS IpiParameter; // Optional parameter for IPI's, usually used for functions, primarily TLB Shootdowns.
	volatile uint32_t* LapicAddressVirt; // Virtual address of the Local APIC MMIO Address (mapped)
	uintptr_t LapicAddressPhys; // Physical address of the Local APIC MMIO
	struct _LASTFUNC_HISTORY* lastfuncBuffer; // Per CPU Buffer for the latest functions trace, allocated dynamically. (ptr)
	volatile bool DeferredRoutineActive; // Per CPU Flag that indicates if the RetireDPCs call is active and retiring DPCs (to prevent re-entracy)

	/* Statically Special Allocated DPCs */
	struct _DPC TimerExpirationDPC;
	struct _DPC	ReaperDPC;
	/* End Statically Special Allocated DPCs */

	// Per CPU Lookaside pools
	POOL_DESCRIPTOR LookasidePools[MAX_POOL_DESCRIPTORS];

	struct _DEBUG_ENTRY DebugEntry[4]; // Per CPU Structure that contains debug entries for each debug register.
	void* IstTimerStackTop;
	void* IstIpiStackTop;
} PROCESSOR, *PPROCESSOR;

// ------------------ FUNCTIONS ------------------

FORCEINLINE
PPROCESSOR
MeGetCurrentProcessor (void)
	// Routine Description:
	// This function returns the current address of the PROCESSOR struct. - Note this should only be used in kernel mode with the appropriate GS value.
{
	return (PPROCESSOR)__readgsqword(0);
}

FORCEINLINE
IRQL
MeGetCurrentIrql(void)

/*++

	Routine description : Retrieves the IRQL of the current processor.

	Arguments:

		None.

	Return Values:

		Current IRQL at time of call.

--*/

{
	return MeGetCurrentProcessor()->currentIrql;
}

NORETURN
void
MeBugCheck(
	IN enum _BUGCHECK_CODES BugCheckCode
);

NORETURN
void 
MeBugCheckEx(
	IN enum _BUGCHECK_CODES	BugCheckCode,
	IN void*	BugCheckParameter1,
	IN void*	BugCheckParameter2,
	IN void*	BugCheckParameter3,
	IN void*	BugCheckParameter4
);

void
MeRaiseIrql(
	IN IRQL NewIrql,
	OUT PIRQL OldIrql
);

void 
MeLowerIrql(
	IN IRQL NewIrql
);

void
_MeSetIrql(
	IN IRQL NewIrql
);

void
MeQueueDPC(
	IN   PDPC dpc
);

void
MeRetireDPCs(
	void
);

void CleanStacks(DPC* dpc, void* thread, void* allocatedDPC, void* arg4);
void MeScheduleDPC(DPC* dpc, void* arg2, void* arg3, void* arg4);
void InitScheduler(void);

NORETURN
void
Schedule(void);

#endif