/*++

Module Name:

    cid.c

Purpose:

    This translation unit contains the implementation of client IDS of processes and threads. (PID/TID)

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/ps.h"
#include "../../includes/ob.h"
#include "../../includes/ht.h"
#include "../../assert.h"

PHANDLE_TABLE PspCidTable = NULL; // The main table.

void
PsInitializeCidTable(
    void
)

/*++

    Routine description:

        Initializes the CID Table.

    Arguments:

        None.

    Return Values:

        None, on failure it bugchecks.

    Notes:

        This player is ban evading, check his CID (by ascendz).

--*/

{
    PspCidTable = HtCreateHandleTable(&PsInitialSystemProcess);
    if (!PspCidTable) MeBugCheck(CID_TABLE_NULL);

    // Claim the first handle, HANDLE 4 (pid) is the PID of the SystemProcess, it must not be used.
    HtCreateHandle(PspCidTable, &PsInitialSystemProcess, MT_PROCESS_ALL_ACCESS);
}

HANDLE
PsAllocateProcessId(
    IN  PEPROCESS Process
)

/*++

    Routine description:

        Creates a PID for the specified Process.

    Arguments:

        [IN]    PEPROCESS Process - The process to create the PID for.

    Return Values:

        The HANDLE (pid) for the process, or MT_INVALID_HANDLE on failure.

--*/

{
    // Basically, return the handle from the PspCidTable.
    // The PID/TID is a NULL access, it is only used to identify a process
    // But to NOT authenticate it, routines like MtOpenProcess (future) would check the HANDLE of a process itself
    // (e.g PspCreateProcess returns it), but not the PID, dumbo bumbo.
    return HtCreateHandle(PspCidTable, (void*)Process, 0);
}

HANDLE
PsAllocateThreadId(
    IN  PETHREAD Thread
)

/*++

    Routine description:

        Creates a TID for the specified thread.

    Arguments:

        [IN]    PETHREAD Thread - The thread to create the TID for.

    Return Values:

        The HANDLE (tid) for the thread, or MT_INVALID_HANDLE on failure.

--*/

{
    return HtCreateHandle(PspCidTable, (void*)Thread, 0);
}

PEPROCESS
PsLookupProcessByProcessId(
    IN HANDLE ProcessId
)

/*++

    Routine description:

        Finds the process associated with the PID given.

    Arguments:

        [IN]    HANDLE ProcessId - The PID of the process.

    Return Values:

        Pointer to Process associated with the PID, or NULL if none / Object type is not a process.

     Notes:

        This routine references the Object, meaning after use you MUST dereference the object when done.

--*/

{
    // Take the object reference while the CID table entry is locked. Looking
    // up a raw pointer and referencing it afterwards races object deletion.
    void* Object = HtReferenceObject(PspCidTable, ProcessId, NULL);
    if (!Object) return NULL;

    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    if (Header->Type != PsProcessType) {
        // Caller specified an ID which is not a process, i.e, a thread.
        ObDereferenceObject(Object);
        return NULL;
    }

    return Object;
}

PETHREAD
PsLookupThreadByThreadId(
    IN HANDLE ThreadId
)

/*++

    Routine description:

        Finds the thread associated with the TID given.

    Arguments:

        [IN]    HANDLE ThreadId - The TID of the thread.

    Return Values:

        Pointer to Thread associated with the TID, or NULL if none / Object type is not a thread.

    Notes:

        This routine references the Object, meaning after use you MUST dereference the object when done.

--*/

{
    void* Object = HtReferenceObject(PspCidTable, ThreadId, NULL);
    if (!Object) return NULL;

    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    if (Header->Type != PsThreadType) {
        // Caller specified an ID which is not a thread, i.e, a process.
        ObDereferenceObject(Object);
        return NULL;
    }

    return Object;
}

void
PsFreeCid(
    IN HANDLE Cid
)

/*++

    Routine description:

        Frees the CID (PID,TID)

    Arguments:

        [IN]    HANDLE Cid - CID Allocated.

    Return Values:

        None.

    Notes:

        This function DOES NOT dereference thee process/thread, you must do so yourself AFTER calling this function.

--*/

{
    HtDeleteHandle(PspCidTable, Cid);
}
