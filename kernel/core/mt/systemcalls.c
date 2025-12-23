/*++

Module Name:

    syscall.c

Purpose:

    This module contains the list of system calls and their implementation of MatanelOS.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/mt.h"
#include "../../includes/ob.h"
#include "../../includes/mm.h"
#include "../../includes/ps.h"
#include "../../includes/mg.h"
#include "../../includes/exception.h"

MTSTATUS
MtAllocateVirtualMemory(
    IN HANDLE ProcessHandle,
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t NumberOfBytes,
    IN uint8_t AllocationType
)


/*++

    Routine description:

        System call for user virtual memory allocation (VAD)

    Arguments:

        [IN]    HANDLE ProcessHandle - Handle to process that memory should be allocated for. (special handles supported, e.g MtCurrentProcess)
        [IN OPTIONAL | OUT OPTIONAL] [PTR_TO_PTR]   void** BaseAddress - The base address to allocate memory starting from if supplied. If NULL, a free gap is chosen and used by NumberOfBytes, and *BaseAddress is set to the found start of gap.
        [IN]    size_t NumberOfBytes - The amount in virtual memory to allocate.
        [IN]    uint8_t AllocationType - USER_ALLOCATION_TYPE Enum specifying which type of PTE flags the allocation should have. (executable, writable, none)

    Return Values:

        Various MTSTATUS Status codes.

--*/

{
    // We must allocate more than 0 bytes. (it will be page size anyway, so..)
    if (!NumberOfBytes) return MT_INVALID_PARAM;

    // Handle checking.
    PEPROCESS Process;
    MTSTATUS Status;
    if (ProcessHandle == MtCurrentProcess()) {
        // Current process allocation.
        Process = PsGetCurrentProcess();
        // Reference it so it doesnt die. (and so the ObDereferenceObject at the end of the function doesnt decrement a reference by others)
        if (!ObReferenceObject(Process)) {
            // This shouldnt really be possible, as if someone called to terminate the process
            // then this thread would have been dead. (or maybe not because we are in a syscall?)
            return MT_PROCESS_IS_TERMINATING;
        }
    }
    else {
        // Another process reference.
        Status = ObReferenceObjectByHandle(
            ProcessHandle,
            MT_PROCESS_VM_OPERATION,
            PsProcessType,
            (void**)&Process,
            NULL
        );
        if (MT_FAILURE(Status)) return Status;
    }

    // Sanitize AllocationType to VAD_FLAGS.
    VAD_FLAGS Flags = VAD_FLAG_NONE;
    switch (AllocationType) {
        case PAGE_EXECUTE_READWRITE:
            Flags = VAD_FLAG_EXECUTE | VAD_FLAG_READ | VAD_FLAG_WRITE;
            break;

        case PAGE_EXECUTE_READ:
            Flags = VAD_FLAG_EXECUTE | VAD_FLAG_READ;
            break;

        case PAGE_READWRITE:
            Flags = VAD_FLAG_READ | VAD_FLAG_WRITE;
            break;

        case PAGE_NOACCESS:
            Flags = VAD_FLAG_RESERVED;
            break;

        default:
            Flags = VAD_FLAG_NONE;
            break;
    }

    if (Flags != VAD_FLAG_NONE) {
        Status = MmAllocateVirtualMemory(Process, BaseAddress, NumberOfBytes, Flags);
    }
    else {
        Status = MT_INVALID_PARAM;
    }

    // Dereference the reference made.
    ObDereferenceObject(Process);
    return Status;
}

MTSTATUS
MtOpenProcess(
    IN uint32_t ProcessId,
    OUT PHANDLE ProcessHandle,
    IN ACCESS_MASK DesiredAccess
)

{
    // TODO SIDS, check if the user process is allowed to open another process handle.
    // TODO PPL, check if the user proecss is allowed to a process handle to ProcessId, check if its protection level is higher or equal.
    // https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-process_protection_level_information
    // For now, we just disregard a process with PID 4 since its the system process.
    MTSTATUS Status;
    if (ProcessId == 4) return MT_ACCESS_DENIED;
    Status = ProbeForRead(ProcessHandle, sizeof(PHANDLE), _Alignof(PHANDLE));
    if (MT_FAILURE(Status)) return Status;

    // Retrieve the process.
    PEPROCESS Process = PsLookupProcessByProcessId(ProcessId);
    if (!Process) return MT_NOT_FOUND;

    HANDLE OutHandleBefore;
    Status = ObOpenObjectByPointer((void*)Process, PsProcessType, DesiredAccess, &OutHandleBefore);
    if (MT_FAILURE(Status)) return Status;

    // Attempt to write to user memory
    try {
        *ProcessHandle = OutHandleBefore;
    } except{
        // User gave invalid pointer, we return failure 
        HtClose(OutHandleBefore);
        return GetExceptionCode();
    }
    end_try;

    return MT_SUCCESS;
}

MTSTATUS
MtTerminateProcess(
    IN HANDLE ProcessHandle,
    IN MTSTATUS ExitStatus
)

{
    PEPROCESS ProcessToTerminate;
    MTSTATUS Status;
    if (ProcessHandle == MtCurrentProcess()) {
        ProcessToTerminate = PsGetCurrentProcess();
        gop_printf(COLOR_RED, "[PROCESS-TERMINATE] Process %p called upon to terminate itself from this existence of the virtual world.\n", (void*)(uintptr_t)ProcessToTerminate);
    }
    else {
        // Attempt reference of handle.
        Status = ObReferenceObjectByHandle(
            ProcessHandle,
            MT_PROCESS_TERMINATE,
            PsProcessType,
            (void**)&ProcessToTerminate,
            NULL
        );
        if (MT_FAILURE(Status)) return Status;
        gop_printf(COLOR_RED, "[PROCESS-TERMINATE] Process %p called to be terminated.\n", (void*)(uintptr_t)ProcessToTerminate);
    }

    // Kill the process.
    Status = PsTerminateProcess(ProcessToTerminate, ExitStatus);

    // Return status, if it wasnt ourselves who were killed.
    return Status;
}