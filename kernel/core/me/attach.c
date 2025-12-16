/*++

Module Name:

	attach.c

Purpose:

	This module contains the implementation of process attaching.

Author:

	slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/me.h"
#include "../../includes/ps.h"

void
MeAttachProcess(
	IN PIPROCESS Process,
	OUT PAPC_STATE ApcState
)

/*++

	Routine description:

		Attach to a process address space, this routine should be managed carefully, and have simple code between the attaching and detaching.
		

	Arguments:

		[IN]	PIPROCESS Process - Pointer to process to attach to (IPROCESS)
		[OUT]	PAPC_STATE - Pointer to store the state in resident memory.

	Return Values:

		None.

	Notes:

		DPCs CANNOT attach to a different process.

--*/

{
	if (MeIsExecutingDpc()) {
		// CANNOT Attach to a process while executing a DPC.
		MeBugCheckEx(
			INVALID_PROCESS_ATTACH_ATTEMPT,
			(void*)Process,
			(void*)(uintptr_t)RETADDR(0),
			(void*)MeIsExecutingDpc(),
			NULL
		);
	}

	PITHREAD CurrentThread = MeGetCurrentThread();
	if (unlikely(!CurrentThread)) return;

	// Save the process we were running on.
	ApcState->SavedApcProcess = CurrentThread->ApcState.SavedApcProcess;
	ApcState->SavedCr3 = __read_cr3();
	ApcState->AttachedToProcess = true;

	// Raise to SYNCH and lock scheduler.
	// TODO SYNCH
	MeAcquireSchedulerLock();

	// Switch identity to new process.
	CurrentThread->ApcState.SavedApcProcess = PsGetEProcessFromIProcess(Process);
	CurrentThread->ApcState.AttachedToProcess = true;

	// Switch CR3s.
	uint64_t TargetCr3 = Process->PageDirectoryPhysical;
	if (ApcState->SavedCr3 != TargetCr3) {
		__write_cr3(TargetCr3);
	}
}

void
MeDetachProcess(
	IN PAPC_STATE ApcState
)

/*++

	Routine description:

		Detach from a process address space.

	Arguments:

		[IN]	PAPC_STATE ApcState - The APC_STATE stored by MeAttachProcess.

	Return Values:

		None.

--*/

{
	PITHREAD CurrentThread = MeGetCurrentThread();
	if (unlikely(!CurrentThread)) return;
	if (!ApcState->AttachedToProcess) return;

	// Restore original CR3.
	uint64_t CurrentCr3 = __read_cr3();
	if (CurrentCr3 != ApcState->SavedCr3) {
		__write_cr3(ApcState->SavedCr3);
	}

	// Restore thread's identity to original process.
	CurrentThread->ApcState.SavedApcProcess = ApcState->SavedApcProcess;
	CurrentThread->ApcState.AttachedToProcess = ApcState->AttachedToProcess;

	// Restore scheduler lock / IRQL.
	// TODO SYNCH LEVEL
	MeReleaseSchedulerLock();

	// Clear attached state.
	ApcState->AttachedToProcess = false;
}