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
#include "../../includes/fs.h"
#include "../../assert.h"

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

/*++

    Routine description:

        System call for user process handle open.

    Arguments:

        [IN] uint32_t ProcessId - The PID of the process to open.
        [OUT] PHANDLE ProcessHandle - Pointer to store handle of opened process.
        [IN] ACCESS_MASK DesiredAccess - The desired access to have for the process.

    Return Values:

        Various MTSTATUS Status codes.

--*/

{
    // TODO SIDS, check if the user process is allowed to open another process handle.
    // TODO PPL, check if the user proecss is allowed to a process handle to ProcessId, check if its protection level is higher or equal.
    // https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-process_protection_level_information
    // For now, we just disregard a process with PID 4 since its the system process.
    MTSTATUS Status;
    if (ProcessId == 4) return MT_ACCESS_DENIED;

    if (MeGetPreviousMode() == UserMode) {
        Status = ProbeForRead(ProcessHandle, sizeof(HANDLE), _Alignof(HANDLE));
        if (MT_FAILURE(Status)) return Status;
    }

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

/*++

    Routine description:

        System call for user process termination.

    Arguments:

        [IN] HANDLE ProcessHandle - The process that is to be terminated (special handles allowed)
        [IN] MTSTATUS ExitStatus - The status the process will exit in (and its threads)

    Return Values:

        Various MTSTATUS Status codes.
        Or none if current process.

--*/

{
    PEPROCESS ProcessToTerminate;
    MTSTATUS Status;
    if (ProcessHandle == MtCurrentProcess()) {
        ProcessToTerminate = PsGetCurrentProcess();
        gop_printf(COLOR_RED, "[PROCESS-TERMINATE] Process %p called upon to terminate itself from this existence of the virtual world. | Status: %p\n", (void*)(uintptr_t)ProcessToTerminate, (void*)(uintptr_t)ExitStatus);
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
        gop_printf(COLOR_RED, "[PROCESS-TERMINATE] Process %p called to be terminated. | Status: %p\n", (void*)(uintptr_t)ProcessToTerminate, (void*)(uintptr_t)ExitStatus);
    }

    // Kill the process.
    Status = PsTerminateProcess(ProcessToTerminate, ExitStatus);

    // Return status, if it wasnt ourselves who were killed.
    return Status;
}

MTSTATUS
MtReadFile(
    IN HANDLE FileHandle,
    IN uint64_t FileOffset,
    OUT void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesRead
)

/*++

    Routine description:

        System call for file reading.

    Arguments:

        [IN] HANDLE FileHandle - The handle of the file opened from MtCreateFile.
        [IN] uint64_t FileOffset - File offset in bytes to start reading from.
        [OUT] void* Buffer - The buffer to store read bytes in.
        [IN] size_t BufferSize - The size of the buffer in bytes.
        [OUT OPTIONAL] size_t* BytesRead - Optionally supply a pointer to store how many bytes were read to the buffer given.

    Return Values:

        Various MTSTATUS Status codes.

--*/

{
    // We must be at IRQL that is less or equal than APC_LEVEL (so we can bring in pageable memory, both for user memory and kernel memory)
    assert(MeGetCurrentIrql() <= APC_LEVEL);
    // Attempt reference of handle.
    MTSTATUS Status;
    PFILE_OBJECT FileObject;
    PRIVILEGE_MODE PreviousMode = MeGetPreviousMode();
    Status = ObReferenceObjectByHandle(
        FileHandle,
        MT_FILE_READ_DATA,
        FsFileType,
        (void**)&FileObject,
        NULL
    );
    if (MT_FAILURE(Status)) return Status;

    // Before everything, lets probe the buffer given. (if we came from user mode that is)
    if (PreviousMode == UserMode) {
        Status = ProbeForRead(Buffer, BufferSize, _Alignof(char));
        if (MT_FAILURE(Status)) {
            // Invalid buffer.
            ObDereferenceObject(FileObject);
            return Status;
        }
    }

    if (BytesRead && PreviousMode == UserMode) {
        Status = ProbeForRead(BytesRead, sizeof(size_t), _Alignof(size_t));
        if (MT_FAILURE(Status)) {
            // Invalid buffer
            ObDereferenceObject(FileObject);
            return Status;
        }
    }

    // Create a paged pool large enough for the buffer size given.
    void* KernelBuffer = MmAllocatePoolWithTag(PagedPool, BufferSize, 'fubk'); // kbuf
    if (!KernelBuffer) {
        ObDereferenceObject(FileObject);
        return MT_NO_MEMORY;
    }

    size_t KernelBytesRead = 0;

    // Call the FS layer.
    Status = FsReadFile(
        FileObject,
        FileOffset,
        KernelBuffer,
        BufferSize,
        &KernelBytesRead
    );

    // If we got EOF we dont return a full failure, and we still copy the data.
    // Else, we got a failure and we free and return.
    if (MT_FAILURE(Status) && KernelBytesRead == 0) {
        MmFreePool(KernelBuffer);
        ObDereferenceObject(FileObject);
        return Status;
    }

    // Write back to user buffer based on bytes read.
    try {
        kmemcpy(Buffer, KernelBuffer, KernelBytesRead);
    } except{
        // Exception gotten on copying to user buffer, we abort and return failure.
        MmFreePool(KernelBuffer);
        ObDereferenceObject(FileObject);
        return GetExceptionCode();
    } end_try;

    // Free the kernel buffer now.
    MmFreePool(KernelBuffer);

    if (BytesRead) {
        // Write back how many bytes we read.
        try {
            *BytesRead = KernelBytesRead;
        } except{
                // Exception gotten on copying to user bytes read, BUT we successfully written everything to the buffer
                // We return last exception code still, their problem.
                ObDereferenceObject(FileObject);
                return GetExceptionCode();
        } end_try;
    }

    // Everything's good, dereference object, return to caller.
    ObDereferenceObject(FileObject);
    return MT_SUCCESS;
}

MTSTATUS
MtWriteFile(
    IN HANDLE FileHandle,
    IN uint64_t FileOffset,
    IN void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesWritten
)

{
    // We must be at IRQL that is less or equal than APC_LEVEL (so we can bring in pageable memory, both for user memory and kernel memory)
    assert(MeGetCurrentIrql() <= APC_LEVEL);
    // Attempt reference of handle.
    MTSTATUS Status;
    PFILE_OBJECT FileObject;
    PRIVILEGE_MODE PreviousMode = MeGetPreviousMode();
    Status = ObReferenceObjectByHandle(
        FileHandle,
        MT_FILE_WRITE_DATA,
        FsFileType,
        (void**)&FileObject,
        NULL
    );
    if (MT_FAILURE(Status)) return Status;

    // Before everything, lets probe the buffer given. (if we came from user mode that is)
    if (PreviousMode == UserMode) {
        Status = ProbeForRead(Buffer, BufferSize, _Alignof(char));
        if (MT_FAILURE(Status)) {
            // Invalid buffer.
            ObDereferenceObject(FileObject);
            return Status;
        }
    }

    if (BytesWritten && PreviousMode == UserMode) {
        Status = ProbeForRead(BytesWritten, sizeof(size_t), _Alignof(size_t));
        if (MT_FAILURE(Status)) {
            // Invalid buffer
            ObDereferenceObject(FileObject);
            return Status;
        }
    }

    // Create a paged pool large enough for the buffer size given.
    void* KernelBuffer = MmAllocatePoolWithTag(PagedPool, BufferSize, 'fubk'); // kbuf
    if (!KernelBuffer) {
        ObDereferenceObject(FileObject);
        return MT_NO_MEMORY;
    }

    // Begin copying from user buffer to kernel buffer.
    try {
        kmemcpy(KernelBuffer, Buffer, BufferSize);
    } except{
        // Access violation while copying from user buffer.
        // We return last exception code still, their problem.
        ObDereferenceObject(FileObject);
        MmFreePool(KernelBuffer);
        return GetExceptionCode();
    } end_try;

    size_t KernelBytesWritten = 0;

    // Call the FS layer.
    Status = FsWriteFile(
        FileObject,
        FileOffset,
        KernelBuffer,
        BufferSize,
        &KernelBytesWritten
    );

    // If we got EOF we dont return a full failure, and we still copy the data.
    // Else, we got a failure and we free and return.
    if (MT_FAILURE(Status) && KernelBytesWritten == 0) {
        MmFreePool(KernelBuffer);
        ObDereferenceObject(FileObject);
        return Status;
    }

    // Free the kernel buffer now.
    MmFreePool(KernelBuffer);

    if (BytesWritten) {
        // Write back how many bytes we read.
        try {
            *BytesWritten = KernelBytesWritten;
        } except{
            // Exception gotten on copying to user bytes read, BUT we successfully written everything to the buffer
            // We return last exception code still, their problem.
            ObDereferenceObject(FileObject);
            return GetExceptionCode();
        } end_try;
    }

    // Everything's good, dereference object, return to caller.
    ObDereferenceObject(FileObject);
    return MT_SUCCESS;
}

MTSTATUS 
MtCreateFile(
    IN const char* path,
    IN ACCESS_MASK DesiredAccess,
    OUT PHANDLE FileHandleOut
)

{
    // We must be at IRQL that is less or equal than APC_LEVEL (FileSystem requirements)
    assert(MeGetCurrentIrql() <= APC_LEVEL);
    MTSTATUS Status;
    HANDLE KernelHandle;
    PRIVILEGE_MODE PreviousMode = MeGetPreviousMode();
    char KernelPath[MAX_PATH];

    // Check if address given is good for writing.
    if (PreviousMode == UserMode) {
        Status = ProbeForRead(FileHandleOut, sizeof(HANDLE), _Alignof(HANDLE));
        if (MT_FAILURE(Status)) return Status;

        // This is a pointer without knowledge of how large it is, but we do know whats the maximum path length, so we scan by that.
        Status = ProbeForRead(path, MAX_PATH, _Alignof(char));
        if (MT_FAILURE(Status)) return Status;
    }

    // Copy user ptr to kernel.
    try {
        // Ensures null termination.
        kstrncpy(KernelPath, path, MAX_PATH);
    } except{
        // Invalid char pointer.
        return GetExceptionCode();
    } end_try;

    // Now call filesystem layer.
    Status = FsCreateFile(
        KernelPath,
        DesiredAccess,
        &KernelHandle
    );
    if (MT_FAILURE(Status)) return Status;

    // Good, we opened/created the file, now we attempt to return back to caller.
    try {
        // Write the handle to the user handle ptr.
        *FileHandleOut = KernelHandle;
    } except{
        // Exception while writing to user, close handle and return.
        HtClose(KernelHandle);
        return GetExceptionCode();
    } end_try;

    // Successful.
    return MT_SUCCESS;
}