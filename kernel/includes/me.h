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

#define MSR_IA32_DEBUGCTL   0x1D9
#define MSR_LASTBRANCH_TOS  0x1C9
#define MSR_LASTBRANCH_FROM0 0x680
#define MSR_LASTBRANCH_TO0   0x6C0
#define DPC_TARGET_CURRENT  0xFF

#include <stdint.h>
#include <stdbool.h>
#include "annotations.h"
#include "macros.h"
#include "../mtstatus.h"
#include "../intrinsics/intrin.h"
#include "../intrinsics/atomic.h"

// Other includes:	
#include "mm.h"
#include "mh.h"
#include "ms.h"
#include "core.h"
// ------------------ UNIONS ------------------

// ------------------ ENUMERATORS ------------------

#define TICK_MS 4
typedef enum _TimeSliceTicks {
	LOW_TIMESLICE_TICKS = 16 / TICK_MS,  /* 40 ms  */
	DEFAULT_TIMESLICE_TICKS = 40 / TICK_MS,  /* 100 ms */
	HIGH_TIMESLICE_TICKS = 100 / TICK_MS   /* 250 ms */
} TimeSliceTicks, *PTimeSliceTicks;

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
	KERNEL_STACK_OVERFLOWN, // A kernel stack has been overflown (and didnt hit the guard page) (detected by canary)
	KMODE_EXCEPTION_NOT_HANDLED, // A kernel mode exception hasn't been handled (an __except block hasn't been handled)
	PFN_DATABASE_INIT_FAILURE,
	VA_SPACE_INIT_FAILURE,
	POOL_INIT_FAILURE,
	BAD_POOL_CALLER,
	ATTEMPTED_WRITE_TO_READONLY_MEMORY,
	INVALID_INITIALIZATION_PHASE,
	PAGE_FAULT_IN_FREED_NONPAGED_POOL,
	PAGE_FAULT_IN_FREED_PAGED_POOL,
	ATTEMPTED_SWITCH_FROM_DPC,
	INVALID_INTERRUPT_REQUEST,
	MANUALLY_INITIATED_CRASH2,
	PSMGR_INIT_FAILED,
	PSWORKER_INIT_FAILED,
	DPC_NOT_INITIALIZED,
	CID_TABLE_NULL,
	INVALID_PROCESS_ATTACH_ATTEMPT,
	CRITICAL_PROCESS_DIED,
} BUGCHECK_CODES;

// ------------------ STRUCTURES ------------------

typedef void (*DebugCallback)(void*);

typedef struct _DEBUG_ENTRY {
	void* Address;
	DebugCallback Callback;
} DEBUG_ENTRY;

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
	DEBUG_LEN_BYTE = 0b00,
	DEBUG_LEN_WORD = 0b01,
	DEBUG_LEN_QWORD = 0b10, // Only valid in long mode
	DEBUG_LEN_DWORD = 0b11
} DEBUG_LENGTH;

typedef struct _DBG_CALLBACK_INFO {
	void* Address;           /* breakpoint address (DRx) */
	PTRAP_FRAME trap;		/* trap frame captured */
	int   BreakIdx;         /* which DRx (0..3) fired */
	uint64_t Dr6;           /* raw DR6 value at time of trap */
} DBG_CALLBACK_INFO;

// Forward declaration
struct _DPC;

typedef void (DEFERRED_ROUTINE)(
	struct _DPC* Dpc,
	void* DeferredContext,
	void* SystemArgument1,
	void* SystemArgument2
	);

typedef DEFERRED_ROUTINE* PDEFERRED_ROUTINE;

typedef struct _DPC {
	// Next/Prev pointers for doubly linked list of DPCs.
	DOUBLY_LINKED_LIST DpcListEntry;

	// Pointer to deferred routine.
	PDEFERRED_ROUTINE DeferredRoutine;
	void* DeferredContext;
	void* SystemArgument1;
	void* SystemArgument2;

	// Points to the DPC_DATA struct of the current processor when queued
	// If NULL, the DPC is NOT queued.
	volatile void* DpcData;

	// Determines if it goes to tail or head of queue.
	enum _DPC_PRIORITY priority;

	// Determines to which CPU this DPC is supposed to be executed on, this allows multiple re-entracy.
	uint8_t CpuNumber; // 0xFF means current CPU, else its per lapic id.
} DPC, *PDPC;

typedef enum _CPU_FLAGS {
	CPU_ONLINE = 1 << 0,  // 0b0001
	CPU_HALTED = 1 << 1,  // 0b0010
	CPU_DOING_IPI = 1 << 2,  // 0b0100
	CPU_UNAVAILABLE = 1 << 3   // 0b1000
} CPU_FLAGS;

// DPC Embedded struct into CPU.
typedef struct _DPC_DATA {
	DOUBLY_LINKED_LIST DpcListHead;
	SPINLOCK DpcLock;
	volatile uint32_t DpcQueueDepth;
	volatile uint32_t DpcCount; // Statistics
} DPC_DATA, *PDPC_DATA;

#define LASTFUNC_BUFFER_SIZE 128
#define LASTFUNC_HISTORY_SIZE 25

#define KERNEL_CS       0x08    // Entry 1: Kernel Code
#define KERNEL_DS       0x10    // Entry 2: Kernel Data  
#define KERNEL_SS       0x10    // Same as KERNEL_DS (data segment used for stack)
#define USER_DS         0x1B    // Entry 3: User Data 
#define USER_CS         0x23    // Entry 4: User Code (CPL=3)
#define USER_SS         USER_DS    // Same as USER_DS 
#define INITIAL_RFLAGS  0x202
#define USER_RFLAGS     0x246 // IF=1, IOPL=0

typedef struct _APC_STATE {
	uint64_t SavedCr3;
	PEPROCESS SavedApcProcess;
	bool AttachedToProcess;
	IRQL PreviousIrql;
} APC_STATE, *PAPC_STATE;

typedef struct _IPROCESS {
	uintptr_t PageDirectoryPhysical;		// Physical Address of the PML4 of the process.
	struct _SPINLOCK ProcessLock;			// Internal Spinlock for process field manipulation safety.
	uint32_t ProcessState;					// Current process state.
} IPROCESS, *PIPROCESS;

typedef struct _ITHREAD {
	struct _TRAP_FRAME TrapRegisters;					   // Trap Registers used for context switching, saving, and alternation.
	uint32_t ThreadState;								   // Current thread state, presented by the THREAD_STATE enumerator.
	void* StackBase;									   // Base of the thread's stack (allocated), used for also freeing it by the memory manager (Mm).
	bool IsLargeStack;									   // Indicates if the stack allocated to the thread is a LargeStack or not. (Kernel stack only)
	void* KernelStack;									   // The threads stack when in kernel space.
	enum _TimeSliceTicks TimeSlice;						   // Current timeslice remaining until thread's forceful pre-emption.
	enum _TimeSliceTicks TimeSliceAllocated;			   // Original timeslice given to the thread, used for restoration when it's current one is over.
	enum _PRIVILEGE_MODE PreviousMode;					   // Previous mode of the thread (used to indicate whether it called a kernel service in kernel mode, or in user mode)			
	struct _APC_STATE ApcState;							   // Current thread's APC State.
	struct _WAIT_BLOCK WaitBlock;						   // Wait block of the current thread, defines a list of which events the thread is waiting on (mutex event, general sleeping)
} ITHREAD, *PITHREAD;

typedef struct _PROCESSOR {
	struct _PROCESSOR* self; // A pointer to the current CPU Struct, used internally by functions, see MtStealThread in scheduler.c, or MeGetCurrentProcessor.
	// If this is ever switched from a 4 byte integer, check assembly for direct cmp. (like in sleep.asm)
	enum _IRQL currentIrql; // An integer that represents the current interrupt request level of the CPU. Declares which LAPIC & IOAPIC interrupts are masked
	volatile bool schedulerEnabled; // A boolean value that indicates if the scheduler is allowed to be called after an interrupt.
	struct _ITHREAD* currentThread; // Current thread that is being executed in the CPU.
	struct _Queue readyQueue; // Queue of thread pointers to be scheduled.
	uint32_t ID; // ID is also the index for cpus (e.g cpus[3] so .ID is 3)
	uint32_t lapic_ID; // Internal APIC id of the CPU.
	void* VirtStackTop; // Pointer to top of CPU Stack.
	void* tss; // Task State Segment ptr.
	void* Rsp0; // General RSP for interrupts & syscalls (entry only) & exceptions.
	void* IstPFStackTop; // Page Fault IST Stack
	void* IstDFStackTop; // Double Fault IST Stack
	volatile uint64_t flags; // CPU Flags (CPU_FLAGS enum), contains the current state of the CPU, in bitfields.
	bool schedulePending; // A boolean value that indicates if a schedule is currently pending on the CPU
	uint64_t* gdt; // A pointer to the current GDT of the CPU (set in the CPUs AP entry), does not include BSP GDT.
	struct _DPC* CurrentDeferredRoutine; // Current deferred routine that is executed by the CPU.
	struct _ETHREAD* idleThread; // Idle thread for the current CPU.
	volatile uint64_t MailboxLock; // 0 = Free, 1 = Locked by a sender
	volatile uint64_t IpiSeq;
	volatile enum _CPU_ACTION IpiAction; // IPI Action specified in the function.
	volatile IPI_PARAMS IpiParameter; // Optional parameter for IPI's, usually used for functions, primarily TLB Shootdowns.
	volatile uint32_t* LapicAddressVirt; // Virtual address of the Local APIC MMIO Address (mapped)
	uintptr_t LapicAddressPhys; // Physical address of the Local APIC MMIO

	/* Statically Special Allocated DPCs */
	struct _DPC TimerExpirationDPC;
	struct _DPC	ReaperDPC;
	/* End Statically Special Allocated DPCs */

	// Additional DPC Fields
	DPC_DATA DpcData;					 // The main DPC queue
	volatile bool DpcRoutineActive;      // TRUE if inside MeRetireDPCs
	volatile uint32_t TimerRequest;      // Non-zero if timers need processing (unused)
	uintptr_t TimerHand;                 // Context for timer expiration (unused)

	// Additional APC Fields
	volatile bool ApcRoutineActive; // True if inside MeRetireAPCs

	// Fields for depth and performance analysis
	uint32_t MaximumDpcQueueDepth;
	uint32_t MinimumDpcRate;
	uint32_t DpcRequestRate;

	// Interrupt requests
	volatile bool DpcInterruptRequested; // True if we requested an interrupt to handle deferred procedure calls.
	volatile bool ApcInterruptRequested; // (Undeveloped yet) True if we requested an interrupt for APCs.

	// Scheduler Lock
	SPINLOCK SchedulerLock;

	// Per CPU Lookaside pools
	POOL_DESCRIPTOR LookasidePools[MAX_POOL_DESCRIPTORS];

	struct _DEBUG_ENTRY DebugEntry[4]; // Per CPU Structure that contains debug entries for each debug register.
	void* IstTimerStackTop;
	void* IstIpiStackTop;

	// Zombie Thread (for deferred reference deletion)
	PITHREAD ZombieThread;

	// Syscall data
	uint64_t UserRsp; // User saved RSP during syscall handling.
} PROCESSOR, *PPROCESSOR;

// ------------------ FUNCTIONS ------------------


NORETURN
void
MeBugCheck(
	IN enum _BUGCHECK_CODES BugCheckCode
);

NORETURN
void
MeBugCheckEx(
	IN enum _BUGCHECK_CODES	BugCheckCode,
	IN void* BugCheckParameter1,
	IN void* BugCheckParameter2,
	IN void* BugCheckParameter3,
	IN void* BugCheckParameter4
);

FORCEINLINE
PPROCESSOR
MeGetCurrentProcessor (void)
	// Routine Description:
	// This function returns the current address of the PROCESSOR struct. - Note this should only be used in kernel mode with the appropriate GS value.
{
	return (PPROCESSOR)__readgsqword(0); // Only works because we have a self pointer at offset 0 in the struct.
}

FORCEINLINE
void
MeAcquireSchedulerLock(void)

{
	PPROCESSOR cpu = MeGetCurrentProcessor();
	// Acquire the spinlock. (FIXME MsAcquireSpinlockAtSynchLevel(&cpu->SchedulerLock)
	while (__sync_lock_test_and_set(&cpu->SchedulerLock.locked, 1)) {
		__asm__ volatile("pause" ::: "memory"); /* x86 pause — CPU relax hint */
	}
	// Memory barrier to prevent instruction reordering
	__asm__ volatile("" ::: "memory");
	cpu->schedulerEnabled = false;
}

FORCEINLINE
void
MeReleaseSchedulerLock(void)

{
	PPROCESSOR cpu = MeGetCurrentProcessor();
	cpu->schedulerEnabled = true;
	// Release the spinlock. (FIXME MsReleaseSpinlockFromSynchLevel(&cpu->SchedulerLock)
	__asm__ volatile("" ::: "memory");
	__sync_lock_release(&cpu->SchedulerLock.locked);
}

extern uint32_t g_cpuCount;

FORCEINLINE
uint8_t
MeGetActiveProcessorCount(void)

{
	return (uint8_t)g_cpuCount; // The reason we cast to uint8_t is because we would never have more than 255 Cpus in the system, not guranteed, though, :) 
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
#ifdef DEBUG
	IRQL returningIrql = (IRQL)__readgsqword(FIELD_OFFSET(PROCESSOR, currentIrql));
	if (returningIrql > HIGH_LEVEL) MeBugCheck(INVALID_IRQL_SUPPLIED);
	return returningIrql;
#else
	return (IRQL)__readgsqword(FIELD_OFFSET(PROCESSOR, currentIrql));
#endif
}


FORCEINLINE
PITHREAD
MeGetCurrentThread(void)

/*++

	Routine description : Retrieves the current running thread on the processor.

	Arguments:

		None.

	Return Values:

		Current thread running on time of call (this thread)

--*/

{
	return (PITHREAD)__readgsqword(FIELD_OFFSET(PROCESSOR, currentThread));
}

FORCEINLINE
bool
MeIsExecutingDpc(void)

{
	return (bool)__readgsqword(FIELD_OFFSET(PROCESSOR, DpcRoutineActive));
}

void
MeInitializeProcessor(
	IN PPROCESSOR CPU,
	IN bool InitializeStandardRoutine,
	IN bool AreYouAP
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
MeSetTargetProcessorDpc(
	IN PDPC Dpc,
	IN uint32_t CpuNumber
);

void
MeInitializeDpc(
	IN PDPC DpcAllocated,
	IN PDEFERRED_ROUTINE DeferredRoutine,
	IN void* DeferredContext,
	IN DPC_PRIORITY DeferredPriority
);

bool
MeInsertQueueDpc(
	IN PDPC Dpc,
	IN void* SystemArgument1,
	IN void* SystemArgument2
);

bool
MeRemoveQueueDpc(
	IN PDPC Dpc
);

void
MeRetireDPCs(
	void
);

void CleanStacks(DPC* dpc, void* thread, void* allocatedDPC, void* arg4);
void ReapOb(DPC* dpc, void* DeferredContext, void* SystemArgument1, void* SystemArgument2);
void InitScheduler(void);

void
MeAttachProcess(
	IN PIPROCESS Process,
	OUT PAPC_STATE ApcState
);

void
MeDetachProcess(
	IN PAPC_STATE ApcState
);

NORETURN
void
Schedule(void);

FORCEINLINE
PRIVILEGE_MODE
MeGetPreviousMode(
	void
)

{
	PITHREAD CurrentThread = MeGetCurrentThread();
	if (CurrentThread) {
		return CurrentThread->PreviousMode;
	}
	else {
		// No thread is active on the current processor (not even a kernel one), this is early init.
		return KernelMode;
	}
}

void
MeEnableInterrupts(
	IN bool EnabledBefore
);

bool
MeDisableInterrupts(
	void
);

bool
MeAreInterruptsEnabled(
	void
);

// smp.c
PPROCESSOR MeGetProcessorBlock(uint8_t ProcessorNumber);

#endif