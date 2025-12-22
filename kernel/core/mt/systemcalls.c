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
#include "../../includes/exception.h"

MTSTATUS
MtAllocateVirtualMemory(
    IN HANDLE ProcessHandle,
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t NumberOfBytes,
    IN uint8_t AllocationType
)

{
    // We must allocate more than 0 bytes. (it will be page size anyway, so..)
    if (!NumberOfBytes) return MT_INVALID_PARAM;

    // Handle checking.
    PEPROCESS Process;
    MTSTATUS Status;
    if (ProcessHandle == MtCurrentProcess()) {
        // Current process allocation.
        Process = PsGetCurrentProcess();
        // Reference it so it doesnt die.
        ObReferenceObject(Process);
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
    if (AllocationType == PAGE_EXECUTE_READWRITE) {
        Flags = VAD_FLAG_EXECUTE | VAD_FLAG_READ | VAD_FLAG_WRITE;
    }
    else if (AllocationType == PAGE_EXECUTE_READ) {
        Flags = VAD_FLAG_EXECUTE | VAD_FLAG_READ;
    }
    else if (AllocationType == PAGE_READWRITE) {
        Flags = VAD_FLAG_READ | VAD_FLAG_WRITE;
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
    // Todo ProbeForWrite..
    UNREFERENCED_PARAMETER(ProcessId); UNREFERENCED_PARAMETER(ProcessHandle); UNREFERENCED_PARAMETER(DesiredAccess);
    return MT_NOT_IMPLEMENTED;
}