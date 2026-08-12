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
#include "../../includes/ms.h"
#include "../../includes/exception.h"
#include "../../includes/fs.h"
#include "../../assert.h"

NORETURN
void
MtpScheduleBlockedSyscall(
    IN PITHREAD Thread,
    IN MTSTATUS ReturnStatus
)

/*++

    Routine Description:

        Captures a blocked user syscall's eventual CPL3 return frame in the
        scheduler-owned register area, then switches away without preserving a
        kernel continuation inside the syscall.

    Arguments:

        Thread -

            Current user thread whose live syscall frame will be captured.

        ReturnStatus -

            Native status placed in RAX when the thread resumes in user mode.

    Return Value:

        This routine does not return. The thread later resumes directly at the
        user instruction following SYSCALL.

--*/

{
    PTRAP_FRAME Source = Thread->SyscallTrap;
    PTRAP_FRAME Target = &Thread->TrapRegisters;

    assert(Source != NULL);

    // TrapRegisters is also the timer/interrupt scheduler save area. Publish
    // this return frame with local preemption disabled and keep interrupts off
    // until the immediately following Schedule call consumes it.
    MeDisableInterrupts();

    Target->r15 = Source->r15;
    Target->r14 = Source->r14;
    Target->r13 = Source->r13;
    Target->r12 = Source->r12;
    Target->r11 = Source->r11;
    Target->r10 = Source->r10;
    Target->r9 = Source->r9;
    Target->r8 = Source->r8;
    Target->rbp = Source->rbp;
    Target->rdi = Source->rdi;
    Target->rsi = Source->rsi;
    Target->rdx = Source->rdx;
    Target->rcx = Source->rcx;
    Target->rbx = Source->rbx;
    Target->rax = ReturnStatus;

    Target->vector = 0;
    Target->error_code = 0;

    // SYSCALL saved the user return RIP in RCX, user RFLAGS in R11, and the
    // entry stub saved the user RSP in the otherwise-unused vector slot.
    Target->rip = Source->rcx;
    Target->cs = USER_CS;
    Target->rflags = Source->r11 | (1ULL << 9);
    Target->rsp = Source->vector;
    Target->ss = USER_SS;

    // The captured context now owns the return path. Do not retain a pointer
    // into the syscall stack that Schedule is about to abandon.
    Thread->SyscallTrap = NULL;

    Schedule();
    UNREACHABLE_CODE();
}

static
VAD_FLAGS
MtpUserAllocationTypeToVadFlags(
    IN USER_PROTECTION_TYPE AllocationType
)

/*++

    Routine description:

        Converts public virtual-allocation flags to internal VAD flags.

    Arguments:

        [IN] AllocationType - Public virtual-allocation flags supplied by the caller.

    Return Values:

        The internal VAD flags corresponding to the public allocation type.

--*/

{
    VAD_FLAGS Flags = VAD_FLAG_NONE;

    switch (AllocationType) {
    case PAGE_EXECUTE_READWRITE:
        Flags = (VAD_FLAGS)((unsigned int)Flags |
            (unsigned int)VAD_FLAG_EXECUTE);
        Flags = (VAD_FLAGS)((unsigned int)Flags |
            (unsigned int)VAD_FLAG_READ);
        Flags = (VAD_FLAGS)((unsigned int)Flags |
            (unsigned int)VAD_FLAG_WRITE);
        break;

    case PAGE_EXECUTE_READ:
        Flags = (VAD_FLAGS)((unsigned int)Flags |
            (unsigned int)VAD_FLAG_EXECUTE);
        Flags = (VAD_FLAGS)((unsigned int)Flags |
            (unsigned int)VAD_FLAG_READ);
        break;

    case PAGE_READWRITE:
        Flags = (VAD_FLAGS)((unsigned int)Flags |
            (unsigned int)VAD_FLAG_READ);
        Flags = (VAD_FLAGS)((unsigned int)Flags |
            (unsigned int)VAD_FLAG_WRITE);
        break;

    case PAGE_READONLY:
        Flags = (VAD_FLAGS)((unsigned int)Flags |
            (unsigned int)VAD_FLAG_READ);
        break;

    case PAGE_NOACCESS:
        Flags = (VAD_FLAGS)((unsigned int)Flags |
            (unsigned int)VAD_FLAG_RESERVED);
        break;

    default:
        break;
    }

    return Flags;
}

static
USER_PROTECTION_TYPE
MtpVadFlagsToUserAllocationType(
    IN VAD_FLAGS VadFlags
)

/*++

    Routine description:

        Converts internal VAD flags to public virtual-allocation flags.

    Arguments:

        [IN] VadFlags - Internal VAD flags to translate.

    Return Values:

        The public protection value corresponding to the VAD flags.

--*/

{
    if ((VadFlags & (VAD_FLAG_EXECUTE | VAD_FLAG_READ | VAD_FLAG_WRITE)) ==
        (VAD_FLAG_EXECUTE | VAD_FLAG_READ | VAD_FLAG_WRITE))
    {
        return PAGE_EXECUTE_READWRITE;
    }
    else if ((VadFlags & (VAD_FLAG_EXECUTE | VAD_FLAG_READ)) ==
        (VAD_FLAG_EXECUTE | VAD_FLAG_READ))
    {
        return PAGE_EXECUTE_READ;
    }
    else if ((VadFlags & (VAD_FLAG_READ | VAD_FLAG_WRITE)) ==
        (VAD_FLAG_READ | VAD_FLAG_WRITE))
    {
        return PAGE_READWRITE;
    }
    else if (VadFlags & VAD_FLAG_READ)
    {
        return PAGE_READONLY;
    }
    else if (VadFlags & VAD_FLAG_RESERVED)
    {
        return PAGE_NOACCESS;
    }

    // Fallback if no match found
    return PAGE_NOACCESS;
}

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
        [IN]    uint8_t AllocationType - USER_PROTECTION_TYPE Enum specifying which type of PTE flags the allocation should have. (executable, writable, none)

    Return Values:

        Various MTSTATUS Status codes.

--*/

{
    // We must allocate more than 0 bytes. (it will be page size anyway, so..)
    if (!NumberOfBytes) return MT_INVALID_PARAM;

    // Address checking.
    MTSTATUS Status = ProbeForRead(BaseAddress, sizeof(void*), _Alignof(void*));
    if (MT_FAILURE(Status)) return Status;

    void* KernelBaseAddressBecauseWeDontTrustUserMode = NULL;

    try {
        KernelBaseAddressBecauseWeDontTrustUserMode = *BaseAddress;
    } except{
        return GetExceptionCode();
    }
    end_try;

    // Handle checking.
    PEPROCESS Process;
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

        case PAGE_READONLY:
            Flags = VAD_FLAG_READ;
            break;

        case PAGE_NOACCESS:
            Flags = VAD_FLAG_RESERVED;
            break;

        default:
            Flags = VAD_FLAG_NONE;
            break;
    }

    if (Flags != VAD_FLAG_NONE) {
        Status = MmAllocateVirtualMemory(Process, &KernelBaseAddressBecauseWeDontTrustUserMode, NumberOfBytes, Flags);
    }
    else {
        Status = MT_INVALID_PARAM;
    }

    if (MT_SUCCEEDED(Status)) {
        try {
            *BaseAddress = KernelBaseAddressBecauseWeDontTrustUserMode;
        } except{
            // I'll keep memory comitted.
            ObDereferenceObject(Process);
            return GetExceptionCode();
        }
        end_try;
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
    ObDereferenceObject(Process);
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
    bool IsCurrentProcess = ProcessHandle == MtCurrentProcess();
    if (IsCurrentProcess) {
        ProcessToTerminate = PsGetCurrentProcess();
        ObReferenceObject(ProcessToTerminate);
        gop_printf(COLOR_RED, "[PROCESS-TERMINATE] Process (%s) called upon to terminate itself from this existence of the virtual world. | Status: %x\n", ProcessToTerminate->ImageName, ExitStatus);
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
        gop_printf(
            COLOR_RED,
            "[PROCESS-TERMINATE] Process (%s) called to be terminated by process pid %d (%s). | Status: %x\n",
            ProcessToTerminate->ImageName,              // %s
            PsGetCurrentProcess()->PID,                 // %d
            PsGetCurrentProcess()->ImageName,           // %s
            ExitStatus                                  // %x
        );
    }

    // Self-termination does not return, so release its temporary reference
    // before entering the exit path. Remote targets stay referenced through
    // PsTerminateProcess so a concurrent close cannot invalidate the pointer.
    if (IsCurrentProcess) ObDereferenceObject(ProcessToTerminate);

    // Kill the process.
    Status = PsTerminateProcess(ProcessToTerminate, ExitStatus);

    if (!IsCurrentProcess) ObDereferenceObject(ProcessToTerminate);

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
    if (BufferSize == 0) return MT_INVALID_PARAM;

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

    // Do not copy if an internal filesystem violates the buffer contract.
    if (KernelBytesRead > BufferSize) {
        MmFreePool(KernelBuffer);
        ObDereferenceObject(FileObject);
        return MT_IO_ERROR;
    }

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

/*++

    Routine description:

        Writes a user buffer to an open file handle.

    Arguments:

        [IN] FileHandle - Handle to the file used by the operation.
        [IN] FileOffset - Byte offset in the backing file.
        [IN OUT] Buffer - Buffer used to transfer the data.
        [IN] BufferSize - Size of Buffer in bytes.
        [OUT] BytesWritten - Receives the number of bytes written.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    // We must be at IRQL that is less or equal than APC_LEVEL (so we can bring in pageable memory, both for user memory and kernel memory)
    assert(MeGetCurrentIrql() <= APC_LEVEL);
    if (BufferSize == 0) return MT_INVALID_PARAM;

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

    if (KernelBytesWritten > BufferSize) {
        MmFreePool(KernelBuffer);
        ObDereferenceObject(FileObject);
        return MT_IO_ERROR;
    }

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
    IN FILE_CREATION_DISPOSITION CreationDisposition,
    OUT PHANDLE FileHandleOut
)

/*++

    Routine description:

        Creates or opens a file and returns a user handle.

    Arguments:

        [IN] path - Filesystem path of the target object.
        [IN] DesiredAccess - Access mask required by the caller.
        [IN] CreationDisposition - Action to take when the file exists or is absent.
        [OUT] FileHandleOut - Receives the file handle.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

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
        CreationDisposition,
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

MTSTATUS
MtClose(
    IN HANDLE hObject
)

/*++

    Routine description:

        Closes a handle in the calling process and releases the handle's object
        reference.

    Arguments:

        hObject - Handle owned by the calling process.

    Return Values:

        MT_SUCCESS on success, or MT_INVALID_HANDLE for an invalid, closed, or
        non-closeable pseudo handle.

--*/

{
    // Easiest syscall yet, just call internal function.
    return HtClose(hObject);
}

MTSTATUS
MtTerminateThread(
    IN HANDLE ThreadHandle,
    IN MTSTATUS ExitStatus
)

/*++

    Routine description:

        Terminates a thread identified by a user handle.

    Arguments:

        [IN] ThreadHandle - Handle to the target thread.
        [IN] ExitStatus - Termination status to record.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    // Attempt to reference thread, or if it is ourselves use ourselves.
    MTSTATUS Status;
    PETHREAD Thread;
    bool IsCurrentThread = ThreadHandle == MtCurrentThread();
    if (IsCurrentThread) {
        // Check if we are the last thread of the process.
        PEPROCESS CurrentProcess = PsGetCurrentProcess();
        MsAcquirePushLockShared(&CurrentProcess->ThreadListLock);
        bool IsLastThread = CurrentProcess->NumThreads == 1;
        MsReleasePushLockShared(&CurrentProcess->ThreadListLock);
        if (IsLastThread) {
            // Illegal. (MtTerminateProcess(MtCurrentProcess(), status) must be called instead)
            return MT_CANT_TERMINATE_SELF;
        }

        // Current Thread.
        Thread = PsGetCurrentThread();

        if (!ObReferenceObject(Thread)) {
            // This shouldnt be possible, we are ourselves, but we are dead?
            assert(false);
            return MT_PROCESS_IS_TERMINATING;
        }

        // Successful.
        Status = MT_SUCCESS;
    }
    else {
        // Remote Thread
        Status = ObReferenceObjectByHandle(
            ThreadHandle,
            MT_THREAD_TERMINATE,
            PsThreadType,
            (void**)&Thread,
            NULL
        );
    }

    // Check if success.
    if (MT_FAILURE(Status)) {
        return Status;
    }

    // Keep a remote target alive until its termination APC is safely queued.
    // A self-termination path never returns to release the reference.
    if (IsCurrentThread) ObDereferenceObject(Thread);

    // Call internal function.
    Status = PsTerminateThread(Thread, ExitStatus);
    if (!IsCurrentThread) ObDereferenceObject(Thread);
    return Status;
}

MTSTATUS
MtQueryVirtualMemory(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    OUT PMEMORY_BASIC_INFORMATION MemoryInformation
)

/*++

    Routine description:

        System call for querying virtual memory pages.
        Note that this supports only user mode pages.

    Arguments:

        [IN]    HANDLE ProcessHandle - Handle for the process to query memory for. Use MtCurrentProcess to signify the current process
        [IN]    void* BaseAddress - The base address of the memory region. This value is rounded down to the nearest page boundary.
        [OUT]   PMEMORY_BASIC_INFORMATION MemoryInformation - Pointer to buffer that receives the specified information of the page(s).

    Return Values:

        Various MTSTATUS Status codes.

--*/

{
    if (!MI_IS_CANONICAL_ADDR(BaseAddress) || (uintptr_t)BaseAddress > MmHighestUserAddress) return MT_INVALID_ADDRESS;

    // Check if the process is ours.
    PEPROCESS Process;
    MTSTATUS Status;

    if (ProcessHandle == MtCurrentProcess()) {
        // Our process.
        Process = PsGetCurrentProcess();

        // Reference it so it doesnt die.
        if (!ObReferenceObject(Process)) {
            // Process has died mid syscall.
            return MT_PROCESS_IS_TERMINATING;
        }

        Status = MT_SUCCESS;
    }
    else {
        // Remote process, reference.
        Status = ObReferenceObjectByHandle(
            ProcessHandle,
            MT_PROCESS_QUERY_INFO,
            PsProcessType,
            (void**)&Process,
            NULL
        );
    }

    if (MT_FAILURE(Status)) return Status;

    // Check if the buffer supplied is all good before actually checking protection.
    Status = ProbeForRead(MemoryInformation, sizeof(MEMORY_BASIC_INFORMATION), _Alignof(MEMORY_BASIC_INFORMATION));
    if (MT_FAILURE(Status)) {
        ObDereferenceObject(Process);
        return Status;
    }

    // Round the address to the nearest page boundary.
    uintptr_t RoundedAddress = (uintptr_t)PAGE_ALIGN(BaseAddress);

    // Acquire the VAD lock before we find the vad and do the region size check.
    // Since we cannot have the VAD paged out (freed) mid operations due to another thread running MmFreeVirtualMemory.
    MsAcquirePushLockShared(&Process->VadLock);

    // Check for the VAD.
    MEMORY_BASIC_INFORMATION BasicInfo;
    PMMVAD Vad = MiFindVadInternal(Process, RoundedAddress, false);

    // NO VAD
    if (!Vad) {
        // The page is not allocated.
        BasicInfo.BaseAddress = (void*)RoundedAddress;
        BasicInfo.Protection = (USER_PROTECTION_TYPE)UINT32_MAX; // undefined.
        BasicInfo.RegionSize = MiGetRegionSizeInternal(NULL, RoundedAddress, Process, false);
    }

    // VAD
    else {
        // Page is allocated.
        BasicInfo.BaseAddress = (void*)Vad->StartVa;
        BasicInfo.Protection = MtpVadFlagsToUserAllocationType(Vad->Flags);
        BasicInfo.RegionSize = MiGetRegionSizeInternal(Vad, 0, Process, false);
    }

    // Release VAD Lock.
    MsReleasePushLockShared(&Process->VadLock);

    // Return it to the user.
    try {
        kmemcpy(MemoryInformation, &BasicInfo, sizeof(MEMORY_BASIC_INFORMATION));
    } except{
        ObDereferenceObject(Process);
        return GetExceptionCode();
    }
    end_try;

    // Successful.
    ObDereferenceObject(Process);
    return MT_SUCCESS;
}

FORCEINLINE
bool
IsValidProtection(
    IN USER_PROTECTION_TYPE Type
)

/*++

    Routine description:

        Reports whether a public virtual-memory protection value is valid.

    Arguments:

        [IN] Type - Type of object or operation.

    Return Values:

        A nonzero value when the reported condition holds, or zero otherwise.

--*/

{
    return (Type == PAGE_EXECUTE_READ ||
        Type == PAGE_EXECUTE_READWRITE ||
        Type == PAGE_READWRITE ||
        Type == PAGE_READONLY ||
        Type == PAGE_NOACCESS);
}

MTSTATUS
MtProtectVirtualMemory(
    IN HANDLE ProcessHandle,
    IN OUT void** BaseAddress,
    IN OUT size_t* RegionSize,
    IN USER_PROTECTION_TYPE NewProtection,
    OUT USER_PROTECTION_TYPE* OldProtection
)

/*++

    Routine description:

        Changes protection on a virtual-memory range in a target process.

    Arguments:

        [IN] ProcessHandle - Handle to the target process.
        [IN] BaseAddress - Base address requested or returned by the mapping operation.
        [IN] RegionSize - Size of the region in bytes.
        [IN] NewProtection - Protection to apply to the virtual range.
        [OUT] OldProtection - Receives the protection that was replaced.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    if (!IsValidProtection(NewProtection)) return MT_INVALID_PARAM;

    // Check if user address is valid.
    MTSTATUS Status = ProbeForRead(BaseAddress, sizeof(void*), _Alignof(void*));
    if (MT_FAILURE(Status)) return Status;
    Status = ProbeForRead(RegionSize, sizeof(size_t), _Alignof(size_t));
    if (MT_FAILURE(Status)) return Status;
    Status = ProbeForRead(OldProtection, sizeof(USER_PROTECTION_TYPE), _Alignof(USER_PROTECTION_TYPE));
    if (MT_FAILURE(Status)) return Status;

    // Attempt to capture the base address and required region size.
    size_t CapturedRegionSize = 0;
    void* CapturedBaseAddress = 0;

    try {
        CapturedBaseAddress = *BaseAddress;
        CapturedRegionSize = *RegionSize;
    } except{
        return GetExceptionCode();
    }
    end_try;

    // Check process handle.
    PEPROCESS Process;

    if (ProcessHandle == MtCurrentProcess()) {
        // Current Process.
        Process = PsGetCurrentProcess();
        Status = MT_SUCCESS;

        if (!ObReferenceObject(Process)) {
            return MT_PROCESS_IS_TERMINATING;
        }
    }
    else {
        // Remote process, reference handle.
        Status = ObReferenceObjectByHandle(
            ProcessHandle,
            MT_PROCESS_VM_OPERATION,
            PsProcessType,
            (void**)&Process,
            NULL
        );
    }

    if (MT_FAILURE(Status)) return Status;

    // Resolve the page-aligned range without allowing a zero-length request or
    // integer wraparound to turn it into an unrelated address range.
    uintptr_t RequestedStart = (uintptr_t)CapturedBaseAddress;
    if (CapturedRegionSize == 0 || RequestedStart < USER_VA_START ||
        RequestedStart > MmHighestUserAddress) {
        ObDereferenceObject(Process);
        return MT_INVALID_PARAM;
    }

    uintptr_t ProtectStart = (uintptr_t)PAGE_ALIGN(RequestedStart);
    uintptr_t LeadingBytes = RequestedStart - ProtectStart;
    if (CapturedRegionSize > SIZE_MAX - LeadingBytes) {
        ObDereferenceObject(Process);
        return MT_INVALID_PARAM;
    }

    size_t SpannedRegionSize = CapturedRegionSize + LeadingBytes;
    if (SpannedRegionSize > SIZE_MAX - (VirtualPageSize - 1)) {
        ObDereferenceObject(Process);
        return MT_INVALID_PARAM;
    }

    size_t AlignedRegionSize = ALIGN_UP(SpannedRegionSize, VirtualPageSize);
    if (AlignedRegionSize == 0 ||
        AlignedRegionSize - 1 > MmHighestUserAddress - ProtectStart) {
        ObDereferenceObject(Process);
        return MT_INVALID_ADDRESS;
    }

    uintptr_t ProtectEnd = ProtectStart + AlignedRegionSize - 1;
    USER_PROTECTION_TYPE ReturnedOldProtection;
    APC_STATE AttachState = { 0 };
    bool Attached = false;

    if (!MsAcquireRundownProtection(&Process->ProcessRundown)) {
        ObDereferenceObject(Process);
        return MT_PROCESS_IS_TERMINATING;
    }

    if (PsGetCurrentProcess() != Process) {
        MeAttachProcess(&Process->InternalProcess, &AttachState);
        if (!AttachState.AttachedToProcess) {
            MsReleaseRundownProtection(&Process->ProcessRundown);
            ObDereferenceObject(Process);
            return MT_INVALID_STATE;
        }
        Attached = true;
    }

    // Acquire exclusive.
    MsAcquirePushLockExclusive(&Process->VadLock);

    PMMVAD Vad = MiFindVadInternal(Process, ProtectStart, false);

    // Validate VAD exists and entirely encompasses the request.
    if (!Vad || ProtectEnd > Vad->EndVa) {
        MsReleasePushLockExclusive(&Process->VadLock);
        if (Attached) MeDetachProcess(&AttachState);
        MsReleaseRundownProtection(&Process->ProcessRundown);
        ObDereferenceObject(Process);
        return MT_INVALID_ADDRESS;
    }

    ReturnedOldProtection = MtpVadFlagsToUserAllocationType(Vad->Flags);

    // Strip out protection VAD Flags and keep other ones
    const VAD_FLAGS ProtectionStateMask = VAD_FLAG_READ | VAD_FLAG_WRITE |
        VAD_FLAG_EXECUTE | VAD_FLAG_RESERVED;

    // Install new vad flags.
    VAD_FLAGS NewVadFlags = (Vad->Flags & ~ProtectionStateMask) |
        MtpUserAllocationTypeToVadFlags(NewProtection);

    // If programmer == dumb (or forgetful)
    if (Vad->Flags == NewVadFlags) {
        MsReleasePushLockExclusive(&Process->VadLock);
        goto WriteOutputs;
    }

    // Determine which split
    bool NeedsLeftSplit = (ProtectStart > Vad->StartVa);
    bool NeedsRightSplit = (ProtectEnd < Vad->EndVa);

    PMMVAD LeftVad = NULL;
    PMMVAD RightVad = NULL;
    if (NeedsLeftSplit) LeftVad = MiAllocateVad();
    if (NeedsRightSplit) RightVad = MiAllocateVad();

    // Allocation failure.
    if ((NeedsLeftSplit && !LeftVad) || (NeedsRightSplit && !RightVad)) {
        if (LeftVad) MiFreeVad(LeftVad);
        if (RightVad) MiFreeVad(RightVad);
        MsReleasePushLockExclusive(&Process->VadLock);
        if (Attached) MeDetachProcess(&AttachState);
        MsReleaseRundownProtection(&Process->ProcessRundown);
        ObDereferenceObject(Process);
        return MT_NO_RESOURCES;
    }

    bool LeftFileReferenced = false;
    bool RightFileReferenced = false;
    if ((Vad->Flags & VAD_FLAG_MAPPED_FILE) && Vad->File) {
        if (NeedsLeftSplit) {
            LeftFileReferenced = ObReferenceObject(Vad->File);
        }
        if (NeedsRightSplit) {
            RightFileReferenced = ObReferenceObject(Vad->File);
        }

        if ((NeedsLeftSplit && !LeftFileReferenced) ||
            (NeedsRightSplit && !RightFileReferenced)) {
            if (LeftFileReferenced) ObDereferenceObject(Vad->File);
            if (RightFileReferenced) ObDereferenceObject(Vad->File);
            if (LeftVad) MiFreeVad(LeftVad);
            if (RightVad) MiFreeVad(RightVad);
            MsReleasePushLockExclusive(&Process->VadLock);
            if (Attached) MeDetachProcess(&AttachState);
            MsReleaseRundownProtection(&Process->ProcessRundown);
            ObDereferenceObject(Process);
            return MT_OBJECT_DELETED;
        }
    }

    // Shrink the middle vad to avoid expanding the first node.
    uintptr_t OrigStart = Vad->StartVa;
    uintptr_t OrigEnd = Vad->EndVa;
    VAD_FLAGS OrigFlags = Vad->Flags;
    uint64_t OrigFileOffset = Vad->FileOffset;

    // Adjust the middle (the original node now)
    Vad->StartVa = ProtectStart;
    Vad->EndVa = ProtectEnd;
    Vad->Flags = NewVadFlags;
    if (Vad->Flags & VAD_FLAG_MAPPED_FILE) {
        Vad->FileOffset += (ProtectStart - OrigStart);
    }

    // Insert the left.
    if (NeedsLeftSplit) {
        LeftVad->StartVa = OrigStart;
        LeftVad->EndVa = ProtectStart - 1;
        LeftVad->Flags = OrigFlags;
        LeftVad->OwningProcess = Process;
        LeftVad->File = Vad->File;
        LeftVad->FileOffset = OrigFileOffset;
        Process->VadRoot = MiInsertVadNode(Process->VadRoot, LeftVad);
    }

    // Insert the right.
    if (NeedsRightSplit) {
        RightVad->StartVa = ProtectEnd + 1;
        RightVad->EndVa = OrigEnd;
        RightVad->Flags = OrigFlags;
        RightVad->OwningProcess = Process;
        RightVad->File = Vad->File;
        if (RightVad->Flags & VAD_FLAG_MAPPED_FILE) {
            RightVad->FileOffset = OrigFileOffset + ((ProtectEnd + 1) - OrigStart);
        }
        Process->VadRoot = MiInsertVadNode(Process->VadRoot, RightVad);
    }

    // Update PTEs now.
    for (uintptr_t Addr = ProtectStart; Addr <= ProtectEnd; Addr += VirtualPageSize) {
        PMMPTE Pte = MiGetPtePointer(Addr);
        MMPTE Expected;
        MMPTE New;

        do {
            Expected.Value = Pte->Value;
            New.Value = Expected.Value;

            // Modify the PTE only if its present.
            if (Expected.Hard.Present) {
                switch (NewProtection) {
                case PAGE_NOACCESS:
                    // Keep ownership of the PFN and revoke CPL3 access. Simply
                    // clearing Present would reinterpret the hardware User bit
                    // as the software Transition bit in this PTE layout.
                    New.Hard.User = 0;
                    New.Hard.Write = 0;
                    New.Hard.NoExecute = 1;
                    break;
                case PAGE_EXECUTE_READWRITE:
                    New.Hard.User = 1;
                    New.Hard.Write = 1;
                    New.Hard.NoExecute = 0;
                    break;
                case PAGE_EXECUTE_READ:
                    New.Hard.User = 1;
                    New.Hard.Write = 0;
                    New.Hard.NoExecute = 0;
                    break;
                case PAGE_READWRITE:
                    New.Hard.User = 1;
                    New.Hard.Write = 1;
                    New.Hard.NoExecute = 1;
                    break;
                case PAGE_READONLY:
                    New.Hard.User = 1;
                    New.Hard.Write = 0;
                    New.Hard.NoExecute = 1;
                    break;
                }
            }
            else {
                // Swapped out, update software flags.
                New.Soft.SoftwareFlags &= ~(PROT_KERNEL_READ |
                    PROT_KERNEL_WRITE | PROT_KERNEL_NOEXECUTE);
                New.Soft.SoftwareFlags |= (NewVadFlags & VAD_FLAG_READ) ? PROT_KERNEL_READ : 0;
                New.Soft.SoftwareFlags |= (NewVadFlags & VAD_FLAG_WRITE) ? PROT_KERNEL_WRITE : 0;
                New.Soft.SoftwareFlags |= (NewVadFlags & VAD_FLAG_EXECUTE) ? 0 : PROT_KERNEL_NOEXECUTE;
                New.Soft.SoftwareFlags |= PROT_KERNEL_USER;
            }

        } while (!MiAtomicSetPte(Pte, New.Value, Expected.Value));
    }

    // Flush TLB since we modified several PTEs.
    MiReloadTLBs();

    MsReleasePushLockExclusive(&Process->VadLock);

WriteOutputs:
    if (Attached) MeDetachProcess(&AttachState);

    // User output pages are deliberately touched after dropping VadLock. A
    // fault here must be able to acquire that lock to resolve the output page.
    try {
        *BaseAddress = (void*)ProtectStart;
        *RegionSize = AlignedRegionSize;
        *OldProtection = ReturnedOldProtection;
    } except{
        MsReleaseRundownProtection(&Process->ProcessRundown);
        ObDereferenceObject(Process);
        return GetExceptionCode();
    }
    end_try;

#ifdef DEBUG
    gop_printf(
        COLOR_RED,
        "**[SYSCALL-VIRTPROT] Returning OldProtection %x**\n",
        ReturnedOldProtection
    );
#endif

    MsReleaseRundownProtection(&Process->ProcessRundown);
    ObDereferenceObject(Process);
    return MT_SUCCESS;
}

MTSTATUS
MtFreeVirtualMemory(
    IN HANDLE ProcessHandle,
    IN OUT void** BaseAddress,
    IN OUT size_t* NumberOfBytes,
    IN enum _FREE_TYPE FreeType
)

/*++

    Routine description:

        Releases or decommits virtual memory in a target process.

    Arguments:

        [IN] ProcessHandle - Handle to the target process.
        [IN] BaseAddress - Base address requested or returned by the mapping operation.
        [IN] NumberOfBytes - Size of the virtual region in bytes.
        [IN] FreeType - Requested release or decommit operation.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    // Address validations.
    MTSTATUS Status = ProbeForRead(BaseAddress, sizeof(void*), _Alignof(void*));
    if (MT_FAILURE(Status)) return Status;
    Status = ProbeForRead(NumberOfBytes, sizeof(size_t), _Alignof(size_t));
    if (MT_FAILURE(Status)) return Status;

    void* KernelBase = NULL;
    size_t KernelNumberOfBytes = 0;

    // Attempt to read into kernel buffers.
    try {
        KernelBase = *BaseAddress;
        KernelNumberOfBytes = *NumberOfBytes;
    } except{
        return GetExceptionCode();
    }
    end_try;

    PEPROCESS Process = NULL;
    // Get Process.
    if (ProcessHandle == MtCurrentProcess()) {
        Process = PsGetCurrentProcess();

        if (!ObReferenceObject(Process)) {
            return MT_PROCESS_IS_TERMINATING;
        }

        Status = MT_SUCCESS;
    }
    else {
        // Remote process.
        Status = ObReferenceObjectByHandle(
            ProcessHandle,
            MT_PROCESS_VM_OPERATION,
            PsProcessType,
            (void**)&Process,
            NULL
        );
    }

    if (MT_FAILURE(Status)) return Status;

    // Call internal function.
    Status = MmFreeVirtualMemory(
        Process,
        &KernelBase,
        &KernelNumberOfBytes,
        FreeType
    );

    if (MT_SUCCEEDED(Status)) {
        try {
            *BaseAddress = KernelBase;
            *NumberOfBytes = KernelNumberOfBytes;
        } except{
            ObDereferenceObject(Process);
            return GetExceptionCode();
        }
        end_try;
    }

    ObDereferenceObject(Process);
    return Status;
}

MTSTATUS
MtCreateThread(
    IN HANDLE ProcessHandle,
    IN THREAD_START_ROUTINE StartRoutine,
    IN void* Argument,
    OUT PHANDLE ThreadHandle
)

/*++

    Routine description:

        Creates a thread in a target process and returns its user handle.

    Arguments:

        [IN] ProcessHandle - Handle to the target process.
        [IN] StartRoutine - Entry routine executed by the created thread or test phase.
        [IN] Argument - System-call argument value being probed or interpreted.
        [OUT] ThreadHandle - Receives the created thread handle.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    // Validate argument.
    MTSTATUS Status = ProbeForRead(ThreadHandle, sizeof(HANDLE), _Alignof(HANDLE));
    if (MT_FAILURE(Status)) return Status;

    // The initial RIP is restored through a user trap frame.
    Status = ProbeForRead((const void*)StartRoutine, 1, _Alignof(char));
    if (MT_FAILURE(Status)) return Status;

    PEPROCESS Process;
    if (ProcessHandle == MtCurrentProcess()) {
        Process = PsGetCurrentProcess();

        if (!ObReferenceObject(Process)) {
            return MT_PROCESS_IS_TERMINATING;
        }

        Status = MT_SUCCESS;
    }
    else {
        Status = ObReferenceObjectByHandle(
            ProcessHandle,
            MT_PROCESS_CREATE_THREAD,
            PsProcessType,
            (void**)&Process,
            NULL
        );
    }

    if (MT_FAILURE(Status)) return Status;

    // Call internal function.
    HANDLE KThreadHandle;

    Status = PsCreateThread(
        Process,
        &KThreadHandle,
        StartRoutine,
        Argument,
        DEFAULT_TIMESLICE_TICKS,
        NULL
    );

    bool Succeeded = MT_SUCCEEDED(Status);

    if (Succeeded) {
        try {
            *ThreadHandle = KThreadHandle;
        } except{
            HtCloseEx(Process->ObjectTable, KThreadHandle);
            ObDereferenceObject(Process);
            return GetExceptionCode();
        }
        end_try;
    }

    ObDereferenceObject(Process);
    return Status;
}

extern NORETURN void restore_user_context_to_user(PETHREAD Thread, PITHREAD PreviousThread);

NORETURN void
MtContinue(
    IN const CONTEXT* ContextRecord
)

/*++

    Routine description:

        Completes a user APC OR a user exception after its normal routine returns in MTDLL. The
        supplied public CONTEXT describes the user execution state that was
        interrupted when the APC/exception was delivered.

        The context is copied from user memory, validated, converted into a
        fresh kernel TRAP_FRAME, and restored directly to user mode. This
        service never returns to the MTDLL dispatcher.

    Arguments:

        [IN] ContextRecord - User-mode CONTEXT saved by APC/exception delivery.

    Return Values:

        None. Invalid state or context terminates the calling thread with
        MT_APC_ERROR; a valid context resumes through IRETQ.

--*/

{
    PETHREAD Thread = PsGetCurrentThread();
    bool UserApcActive =
        Thread->InternalThread.UserApcActive;

    bool UserExceptionActive =
        InterlockedLoadAcquire(
            &Thread->InternalThread.UserExceptionActive
        );

    // Catch both illegal situations
    // A user APC and an exception cannot be active at the same time
    // Or if both are false, MtContinue is illegal to be called.
    if (UserApcActive == UserExceptionActive ||
        Thread->InternalThread.PreviousMode != UserMode) {
        PspExitThread(MT_INVALID_STATE);
    }

    // Enforce NONCONTINUABLE in the kernel, since user mode apps can STILL call MtContinue themselves, with a noncontinuable exception.
    if (UserExceptionActive &&
        (Thread->InternalThread.PendingExceptionRecord.ExceptionFlags &
            MT_EXCEPTION_NONCONTINUABLE)) {
        PspExitThread(MT_INVALID_STATE);
    }

    CONTEXT CapturedContext;
    MTSTATUS Status = ProbeForRead(
        ContextRecord,
        sizeof(CapturedContext),
        _Alignof(CONTEXT)
    );
    if (MT_FAILURE(Status)) {
        PspExitThread(MT_APC_ERROR);
    }

    Status = MT_SUCCESS;
    try {
        kmemcpy(&CapturedContext, ContextRecord, sizeof(CapturedContext));
    } except{
        Status = GetExceptionCode();
    }
    end_try;

    if (MT_FAILURE(Status)) {
        PspExitThread(MT_APC_ERROR);
    }

    TRAP_FRAME RestoredFrame = { 0 };
    Status = ExpApplyUserContextToTrapFrame(
        &CapturedContext,
        &RestoredFrame
    );
    if (MT_FAILURE(Status)) {
        PspExitThread(MT_APC_ERROR);
    }

    // Disable interrupts from here, if we get scheduled after setting RestoredFrame, the ISR will overwrite RestoredFrame inside of TrapRegisters.
    MeDisableInterrupts();

    Thread->InternalThread.TrapRegisters = RestoredFrame;

    if (UserApcActive) {
        Thread->InternalThread.UserApcActive = false;
    }
    else {
        InterlockedStoreRelease(
            &Thread->InternalThread.UserExceptionActive,
            false
        );
    }

    Thread->InternalThread.SyscallTrap = NULL;
    MeGetCurrentProcessor()->ApcRoutineActive = false;
    MePrepareUserDispatchForReturn(&Thread->InternalThread.TrapRegisters);
    restore_user_context_to_user(Thread, NULL);
}

MTSTATUS
MtDelayExecution(
    IN bool Alertable,
    IN uint64_t Milliseconds
)

/*++

    Routine Description:

        Delays execution of the calling user thread for a relative interval.
        The native service delegates the wait mechanics to MsDelayExecution.

    Arguments:

        Alertable -

            Requests alertable delay semantics. APC interruption is reserved
            until alertable dispatcher waits are implemented.

        Milliseconds -

            Relative delay in milliseconds. Zero yields the current quantum.

    Return Value:

        Returns the completion status from MsDelayExecution.

--*/

{
    return MsDelayExecution(UserMode, Alertable, Milliseconds);
}

static void
MtpRetainMutexOwnershipReference(
    IN PMUTEX Mutex
)

/*++

    Routine description:

        Retains the mutex object while it is linked into a thread ownership list.

    Arguments:

        [IN] Mutex - Mutex object affected by the operation.

    Return Values:

        None.

--*/

{
    // The syscall already owns a transient reference, so this cannot fail
    // unless object reference accounting is corrupt.
    if (!ObReferenceObject(Mutex)) {
        MeBugCheckEx(MEMORY_CORRUPT_HEADER, Mutex, RETADDR(0), NULL, NULL);
    }

    IRQL OldIrql;
    MsAcquireSpinlock(&Mutex->Header.Lock, &OldIrql);
    assert(Mutex->OwnerThread == PsGetCurrentThread());
    if (Mutex->ObjectOwnerReferences == UINT32_MAX) {
        MsReleaseSpinlock(&Mutex->Header.Lock, OldIrql);
        MeBugCheckEx(MEMORY_OVERFLOW_DETECTION, Mutex, RETADDR(0), NULL, NULL);
    }
    Mutex->ObjectOwnerReferences++;
    MsReleaseSpinlock(&Mutex->Header.Lock, OldIrql);
}

static void
MtpReleaseMutexOwnershipReference(
    IN PMUTEX Mutex
)

/*++

    Routine description:

        Releases the reference retained for mutex ownership tracking.

    Arguments:

        [IN] Mutex - Mutex object affected by the operation.

    Return Values:

        None.

--*/

{
    IRQL OldIrql;
    MsAcquireSpinlock(&Mutex->Header.Lock, &OldIrql);
    assert(Mutex->ObjectOwnerReferences != 0);
    Mutex->ObjectOwnerReferences--;
    MsReleaseSpinlock(&Mutex->Header.Lock, OldIrql);

    ObDereferenceObject(Mutex);
}

MTSTATUS
MtWaitForSingleObject(
    IN HANDLE ObjectHandle,
    IN uint64_t Milliseconds,
    IN bool Alertable
)

/*++

    Routine description:

        Resolves a waitable handle with MT_SYNCHRONIZE access and waits for its
        dispatcher object. The object remains referenced for the entire wait.

    Arguments:

        ObjectHandle - Handle to a waitable dispatcher object.
        Milliseconds - Relative timeout in milliseconds, zero, or MT_INFINITE.
        Alertable - Requests alertable behavior when user APC waits are added.

    Return Values:

        MT_SUCCESS, MT_TIMEOUT, MT_MUTEX_ABANDONED, or an object-manager/wait
        failure status.

--*/

{
    MTSTATUS Status;
    void* Object = NULL;

    // Reference the object, get its type.
    Status = ObReferenceObjectByHandle(
        ObjectHandle,
        MT_SYNCHRONIZE,
        NULL,
        &Object,
        NULL
    );

    if (MT_FAILURE(Status)) return Status;

    // Reject self waits explicitly, else, it would cause a user thread deadlock, until termination.
    if (Object == PsGetCurrentProcess() || Object == PsGetCurrentThread()) {
        ObDereferenceObject(Object);
        return MT_INVALID_PARAM;
    }

    // Call internal function
    Status = MsWaitForSingleObject(Object, UserMode, Alertable, Milliseconds);

    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    if (Header->Type == MsMutexType &&
        (Status == MT_SUCCESS || Status == MT_MUTEX_ABANDONED)) {
        // Mutex ownership outlives this syscall. Retain one reference for each
        // recursive acquisition so closing the last handle cannot free an
        // owned mutex still linked in the current thread's owner list.
        MtpRetainMutexOwnershipReference((PMUTEX)Object);
    }

    ObDereferenceObject(Object);
    return Status;
}

MTSTATUS
MtCreateEvent(
    OUT PHANDLE EventHandle,
    IN ACCESS_MASK DesiredAccess,
    IN EVENT_TYPE EventType,
    IN bool InitialState,
    _In_Opt const char* Name // unsupported currently
)

/*++

    Routine description:

        Creates and initializes an unnamed event object and publishes a handle
        in the calling process.

    Arguments:

        EventHandle - Receives the new handle.
        DesiredAccess - Access mask granted to the new handle.
        EventType - NotificationEvent or SynchronizationEvent.
        InitialState - Initial signaled state.
        Name - Reserved; named objects are not implemented.

    Return Values:

        MT_SUCCESS or a parameter, probing, allocation, access, or handle-table
        failure status.

--*/

{
    if (Name != NULL) {
        // Names arent supported currently.
        return MT_NOT_IMPLEMENTED;
    }

    if (EventType != NotificationEvent && EventType != SynchronizationEvent) {
        return MT_INVALID_PARAM;
    }

    // Validate OUT arg.
    MTSTATUS Status = ProbeForRead(EventHandle, sizeof(HANDLE), _Alignof(HANDLE));
    if (MT_FAILURE(Status)) {
        return Status;
    }

    // If names are supported and one is given, then search for the object name database if it is in there (atomically or anything or locks idk)
    // And acquire it with EVENT_ALL_ACCESS, currently there arent names so we dont do that, if they want cross-process events they can use DuplicateHandle, which isnt implemented currently.
    void* Object = NULL;
    Status = ObCreateObject(MsEventType, sizeof(EVENT), &Object);
    if (MT_FAILURE(Status)) {
        return Status;
    }

    // Object is created and has the initial reference count.
    // Initialize the Event.
    DISPATCHER_TYPE Type = (EventType == NotificationEvent) ? DispatcherNotificationEvent : DispatcherSynchronizationEvent;
    MsInitializeEvent((PEVENT)Object, Type, InitialState);

    // Create the handle and return.
    HANDLE CapturedHandle = MT_INVALID_HANDLE;
    Status = ObCreateHandleForObject(Object, DesiredAccess, &CapturedHandle);

    if (MT_FAILURE(Status)) {
        ObDereferenceObject(Object);
        return Status;
    }

    // Handle is created, attempt to give back to user.
    try {
        *EventHandle = CapturedHandle;
    }
    except{
        // The handle was published only in the kernel table, so remove it and
        // then release the creator's original reference.
        HtClose(CapturedHandle);
        ObDereferenceObject(Object);
        return GetExceptionCode();
    }
    end_try;

    // The handle now owns the lasting reference; release the creator reference.
    ObDereferenceObject(Object);
    return MT_SUCCESS;
}

MTSTATUS
MtQueryEvent(
    IN HANDLE EventHandle,
    OUT bool* SignalState
)

/*++

    Routine description:

        Queries an event's current signaled state through a handle with
        MT_EVENT_QUERY_STATE access.

    Arguments:

        EventHandle - Handle to an event object.
        SignalState - Receives true when the event is signaled.

    Return Values:

        MT_SUCCESS or a probing, handle, type, or access failure status.

--*/

{
    MTSTATUS Status = ProbeForRead(SignalState, sizeof(bool), _Alignof(bool));
    if (MT_FAILURE(Status)) return Status;

    void* Object = NULL;
    Status = ObReferenceObjectByHandle(EventHandle, MT_EVENT_QUERY_STATE, MsEventType, &Object, NULL);
    if (MT_FAILURE(Status)) return Status;

    PEVENT Event = (PEVENT)Object;
    // dumb assertion tho
    assert(Event->Header.Type == DispatcherSynchronizationEvent || Event->Header.Type == DispatcherNotificationEvent);

    // Acquire dispatcher lock and check signal state.
    bool Signaled = false;
    IRQL dispatcherIrql;
    MsAcquireSpinlock(&Event->Header.Lock, &dispatcherIrql);
    Signaled = Event->Header.SignalState;
    MsReleaseSpinlock(&Event->Header.Lock, dispatcherIrql);

    try {
        *SignalState = Signaled;
        Status = MT_SUCCESS;
    }
    except{
        Status = GetExceptionCode();
    }
    end_try;

    ObDereferenceObject(Object);
    return Status;
}

MTSTATUS
MtSetEvent(
    IN HANDLE EventHandle,
    _Out_Opt bool* PreviousState
)

/*++

    Routine description:

        Atomically records the previous state and signals an event through a
        handle with MT_EVENT_MODIFY_STATE access.

    Arguments:

        EventHandle - Handle to an event object.
        PreviousState - Optionally receives the state before this operation.

    Return Values:

        MT_SUCCESS or a probing, handle, type, access, or dispatcher failure.

--*/

{
    MTSTATUS Status;

    if (PreviousState) {
        Status = ProbeForRead(PreviousState, sizeof(bool), _Alignof(bool));
        if (MT_FAILURE(Status)) return Status;
    }

    void* Object = NULL;
    Status = ObReferenceObjectByHandle(
        EventHandle,
        MT_EVENT_MODIFY_STATE,
        MsEventType,
        &Object,
        NULL
    );

    if (MT_FAILURE(Status)) return Status;
    PEVENT Event = (PEVENT)Object;

    bool SignalState = false;
    Status = MsSetEventEx(Event, PreviousState ? &SignalState : NULL);
    ObDereferenceObject(Object);

    if (PreviousState) {
        try {
            *PreviousState = SignalState;
        }
        except{
            Status = GetExceptionCode();
        }
        end_try;
    }

    return Status;
}

MTSTATUS
MtResetEvent(
    IN HANDLE EventHandle,
    _Out_Opt bool* PreviousState
)

/*++

    Routine description:

        Atomically records the previous state and resets an event through a
        handle with MT_EVENT_MODIFY_STATE access.

    Arguments:

        EventHandle - Handle to an event object.
        PreviousState - Optionally receives the state before this operation.

    Return Values:

        MT_SUCCESS or a probing, handle, type, or access failure status.

--*/

{
    MTSTATUS Status;
    if (PreviousState) {
        Status = ProbeForRead(PreviousState, sizeof(bool), _Alignof(bool));
        if (MT_FAILURE(Status)) return Status;
    }

    void* Object = NULL;
    Status = ObReferenceObjectByHandle(
        EventHandle,
        MT_EVENT_MODIFY_STATE,
        MsEventType,
        &Object,
        NULL
    );

    if (MT_FAILURE(Status)) return Status;
    PEVENT Event = (PEVENT)Object;

    bool SignalState = MsResetEvent(Event);
    ObDereferenceObject(Object);

    if (PreviousState) {
        try {
            *PreviousState = SignalState;
        }
        except{
            Status = GetExceptionCode();
        }
        end_try;
    }

    return Status;
}

MTSTATUS
MtCreateMutex(
    OUT PHANDLE MutexHandle,
    IN ACCESS_MASK DesiredAccess,
    IN bool InitialOwner,
    _In_Opt const char* Name
)

/*++

    Routine description:

        Creates an unnamed mutex object, optionally acquires initial ownership,
        and publishes a handle in the calling process.

    Arguments:

        MutexHandle - Receives the new mutex handle.
        DesiredAccess - Access mask granted to the new handle.
        InitialOwner - Acquires the mutex for the caller before publication.
        Name - Reserved; named objects are not implemented.

    Return Values:

        MT_SUCCESS or a parameter, probing, allocation, access, wait, or
        handle-table failure status.

--*/

{
    if (Name != NULL) {
        return MT_NOT_IMPLEMENTED;
    }

    // Validate mutex out handle
    MTSTATUS Status = ProbeForRead(MutexHandle, sizeof(HANDLE), _Alignof(HANDLE));
    if (MT_FAILURE(Status)) return Status;

    // Create the mutex
    void* Object = NULL;
    Status = ObCreateObject(MsMutexType, sizeof(MUTEX), &Object);
    if (MT_FAILURE(Status)) return Status;

    // Initialize it
    PMUTEX Mutex = (PMUTEX)Object;
    Status = MsInitializeMutexObject(Mutex);
    if (MT_FAILURE(Status)) {
        ObDereferenceObject(Object);
        return Status;
    }

    // Create the handle before acquiring the mutex (if InitalOwner)
    // For now I'll use a Cleanup goto bcz idc
    HANDLE OutMutexHandle = MT_INVALID_HANDLE;
    bool InitialOwnershipReferenced = false;
    Status = ObCreateHandleForObject(Object, DesiredAccess, &OutMutexHandle);
    if (MT_FAILURE(Status)) goto Cleanup;

    // If the user wants to be the initial owner
    // Acquire the mutex before writing back the handle to avoid races (actually to avoid programmer mistakes but ok)
    // Should be instant acquire.
    if (InitialOwner) {
        Status = MsWaitForSingleObject(Mutex, KernelMode, false, 0);
        assert(MT_SUCCEEDED(Status));
        if (MT_FAILURE(Status)) goto Cleanup;
        MtpRetainMutexOwnershipReference(Mutex);
        InitialOwnershipReferenced = true;
    }

    // Alright, write back to the user now
    try {
        *MutexHandle = OutMutexHandle;
        Status = MT_SUCCESS;
    } except{
        Status = GetExceptionCode();
        leave;
    }
    end_try;

Cleanup:
    if (MT_FAILURE(Status)) {
        if (InitialOwnershipReferenced) {
            MTSTATUS ReleaseStatus = MsReleaseMutexObject(Mutex);
            assert(ReleaseStatus == MT_SUCCESS);
            (void)ReleaseStatus;
            MtpReleaseMutexOwnershipReference(Mutex);
        }
        if (OutMutexHandle != MT_INVALID_HANDLE) {
            HtClose(OutMutexHandle);
        }
    }

    // The handle and any initial-ownership reference now carry the lasting
    // lifetime. Release ObCreateObject's original reference in every path.
    ObDereferenceObject(Object);

    return Status;
}

MTSTATUS
MtQueryMutex(
    IN HANDLE MutexHandle,
    OUT MUTEX_BASIC_INFORMATION* Information
)

/*++

    Routine description:

        Returns a consistent snapshot of a mutex's signal, ownership, and
        abandonment state through MT_MUTEX_QUERY_STATE access.

    Arguments:

        MutexHandle - Handle to a mutex object.
        Information - Receives MUTEX_BASIC_INFORMATION.

    Return Values:

        MT_SUCCESS or a probing, handle, type, or access failure status.

--*/

{
    // Validate pointer
    MTSTATUS Status = ProbeForRead(Information, sizeof(MUTEX_BASIC_INFORMATION), _Alignof(MUTEX_BASIC_INFORMATION));
    if (MT_FAILURE(Status)) return Status;

    // Validate handle
    void* Object;
    Status = ObReferenceObjectByHandle(
        MutexHandle,
        MT_MUTEX_QUERY_STATE,
        MsMutexType,
        &Object,
        NULL
    );

    if (MT_FAILURE(Status)) return Status;

    // Extract the header info now under its lock
    PMUTEX Mutex = (PMUTEX)Object;
    MUTEX_BASIC_INFORMATION KernelInfo;
    IRQL dispatcherIrql;
    MsAcquireSpinlock(&Mutex->Header.Lock, &dispatcherIrql);
    KernelInfo.Abandoned = Mutex->Abandoned;
    KernelInfo.OwnedByCaller = (Mutex->OwnerThread == PsGetCurrentThread());
    KernelInfo.SignalState = Mutex->Header.SignalState;
    MsReleaseSpinlock(&Mutex->Header.Lock, dispatcherIrql);

    // Dereference the object, we dont need it anymore.
    ObDereferenceObject(Object);
    Status = MT_SUCCESS;

    // Alright attempt to return it back to the user.
    try {
        kmemcpy(Information, &KernelInfo, sizeof(KernelInfo));
    }
    except{
        Status = GetExceptionCode();
    }
    end_try;

    return Status;
}

MTSTATUS
MtReleaseMutex(
    IN HANDLE MutexHandle,
    _Out_Opt int32_t* PreviousCount
)

/*++

    Routine description:

        Releases one recursion level of a mutex owned by the calling thread and
        drops the corresponding ownership-held object reference.

    Arguments:

        MutexHandle - Handle to the owned mutex.
        PreviousCount - Optionally receives the pre-release signal state.

    Return Values:

        MT_SUCCESS, MT_MUTEX_NOT_OWNED, or a probing/handle/type/access failure.

--*/

{
    // Validate the pointer if present
    MTSTATUS Status;
    if (PreviousCount) {
        Status = ProbeForRead(PreviousCount, sizeof(int32_t), _Alignof(int32_t));
        if (MT_FAILURE(Status)) return Status;
    }

    // Validate handle
    void* Object = NULL;
    Status = ObReferenceObjectByHandle(
        MutexHandle,
        MT_SYNCHRONIZE,
        MsMutexType,
        &Object,
        NULL
    );

    if (MT_FAILURE(Status)) return Status;
    PMUTEX Mutex = (PMUTEX)Object;
    int32_t KPreviousCount;

    // Check if user wants a prev count.
    if (PreviousCount) {
        IRQL dispatcherIrql;
        MsAcquireSpinlock(&Mutex->Header.Lock, &dispatcherIrql);
        KPreviousCount = Mutex->Header.SignalState;
        MsReleaseSpinlock(&Mutex->Header.Lock, dispatcherIrql);
    }

    // Release mutex
    Status = MsReleaseMutexObject(Mutex);
    if (MT_FAILURE(Status)) {
        ObDereferenceObject(Object);
        return Status;
    }

    // Each successful user acquisition retained one owner reference. Drop one
    // for this release, including recursive releases.
    MtpReleaseMutexOwnershipReference(Mutex);

    // Write back to user if he wants a prev count.
    // Failure here means the mutex has been released, should we return a warning success instead of a failure?
    if (PreviousCount) {
        try {
            *PreviousCount = KPreviousCount;
            Status = MT_SUCCESS;
        }
        except{
            Status = GetExceptionCode();
        }
        end_try;
    }

    ObDereferenceObject(Object);
    return Status;
}

MTSTATUS
MtCreateSemaphore(
    OUT PHANDLE SemaphoreHandle,
    IN ACCESS_MASK DesiredAccess,
    IN int32_t InitialCount,
    IN int32_t MaximumCount,
    _In_Opt const char* Name // unsupported currently
)

/*++

    Routine description:

        Creates an unnamed semaphore with the requested initial and maximum
        permit counts and publishes a handle in the calling process.

    Arguments:

        SemaphoreHandle - Receives the new semaphore handle.
        DesiredAccess - Access mask granted to the new handle.
        InitialCount - Initial available permit count.
        MaximumCount - Maximum permit count.
        Name - Reserved; named objects are not implemented.

    Return Values:

        MT_SUCCESS or a parameter, probing, allocation, access, or handle-table
        failure status.

--*/

{
    if (Name != NULL) {
        return MT_NOT_IMPLEMENTED;
    }

    if (InitialCount < 0 || MaximumCount <= 0 || InitialCount > MaximumCount) {
        // Validate usermode params before we give this to the internal kernel function, there it would actually bugcheck (or assert)
        return MT_INVALID_PARAM;
    }

    // Validate handle
    MTSTATUS Status = ProbeForRead(SemaphoreHandle, sizeof(HANDLE), _Alignof(HANDLE));
    if (MT_FAILURE(Status)) return Status;

    // Alright, create the object.
    void* Object = NULL;
    Status = ObCreateObject(MsSemaphoreType, sizeof(SEMAPHORE), &Object);
    if (MT_FAILURE(Status)) return Status;

    // Now initalize the semaphore
    PSEMAPHORE Semaphore = (PSEMAPHORE)Object;
    MsInitializeSemaphore(Semaphore, InitialCount, MaximumCount);

    // Good, create the handle for it now.
    HANDLE KSemaphoreHandle = MT_INVALID_HANDLE;
    Status = ObCreateHandleForObject(Object, DesiredAccess, &KSemaphoreHandle);
    if (MT_FAILURE(Status)) {
        ObDereferenceObject(Object);
        return Status;
    }

    // Write the handle back to the user
    try {
        *SemaphoreHandle = KSemaphoreHandle;
        Status = MT_SUCCESS;
    }
    except{
        Status = GetExceptionCode();
    }
    end_try;

    if (MT_FAILURE(Status)) {
        HtClose(KSemaphoreHandle);
    }

    ObDereferenceObject(Object);
    return Status;
}

MTSTATUS
MtQuerySemaphore(
    IN HANDLE SemaphoreHandle,
    OUT SEMAPHORE_BASIC_INFORMATION* Information
)

/*++

    Routine description:

        Returns a consistent semaphore count/limit snapshot through
        MT_SEMAPHORE_QUERY_STATE access.

    Arguments:

        SemaphoreHandle - Handle to a semaphore object.
        Information - Receives SEMAPHORE_BASIC_INFORMATION.

    Return Values:

        MT_SUCCESS or a probing, handle, type, or access failure status.

--*/

{
    // Validate ptr before
    MTSTATUS Status = ProbeForRead(Information, sizeof(SEMAPHORE_BASIC_INFORMATION), _Alignof(SEMAPHORE_BASIC_INFORMATION));
    if (MT_FAILURE(Status)) return Status;

    // Reference
    void* Object;
    Status = ObReferenceObjectByHandle(
        SemaphoreHandle,
        MT_SEMAPHORE_QUERY_STATE,
        MsSemaphoreType,
        &Object,
        NULL
    );

    if (MT_FAILURE(Status)) return Status;

    // Acquire lock
    IRQL dispatcherIrql;
    SEMAPHORE_BASIC_INFORMATION KInfo;
    PSEMAPHORE Semaphore = (PSEMAPHORE)Object;
    MsAcquireSpinlock(&Semaphore->Header.Lock, &dispatcherIrql);

    // Write counts while locked.
    KInfo.CurrentCount = Semaphore->Header.SignalState;
    KInfo.MaximumCount = Semaphore->Limit;

    MsReleaseSpinlock(&Semaphore->Header.Lock, dispatcherIrql);

    // Dereference object, not needed.
    ObDereferenceObject(Object);

    // Write back to user.
    try {
        kmemcpy(Information, &KInfo, sizeof(SEMAPHORE_BASIC_INFORMATION));
        Status = MT_SUCCESS;
    } except{
        Status = GetExceptionCode();
    }
    end_try;

    return Status;
}

MTSTATUS
MtReleaseSemaphore(
    IN HANDLE SemaphoreHandle,
    IN int32_t ReleaseCount,
    _Out_Opt int32_t* PreviousCount
)

/*++

    Routine description:

        Atomically validates and releases semaphore permits through
        MT_SEMAPHORE_MODIFY_STATE access.

    Arguments:

        SemaphoreHandle - Handle to a semaphore object.
        ReleaseCount - Positive number of permits to release.
        PreviousCount - Optionally receives the count before the release.

    Return Values:

        MT_SUCCESS, MT_SEMAPHORE_LIMIT_EXCEEDED, or a parameter, probing,
        handle, type, or access failure status.

--*/

{
    // Basic validation first for a semaphore
    if (ReleaseCount <= 0) {
        return MT_INVALID_PARAM;
    }

    MTSTATUS Status;
    if (PreviousCount) {
        Status = ProbeForRead(PreviousCount, sizeof(int32_t), _Alignof(int32_t));
        if (MT_FAILURE(Status)) return Status;
    }

    // Attempt reference
    void* Object;
    Status = ObReferenceObjectByHandle(
        SemaphoreHandle,
        MT_SEMAPHORE_MODIFY_STATE,
        MsSemaphoreType,
        &Object,
        NULL
    );

    if (MT_FAILURE(Status)) return Status;

    PSEMAPHORE Semaphore = (PSEMAPHORE)Object;

    int32_t KPreviousCount = 0;
    Status = MsReleaseSemaphoreChecked(
        Semaphore,
        ReleaseCount,
        &KPreviousCount
    );
    ObDereferenceObject(Object);
    if (MT_FAILURE(Status)) return Status;

    // Attempt to transfer prevcount if wanted
    if (PreviousCount) {
        try {
            *PreviousCount = KPreviousCount;
            Status = MT_SUCCESS;
        } except{
            Status = GetExceptionCode();
        }
        end_try;
    }

    return Status;
}

MTSTATUS
MtQueryInformationProcess(
    IN HANDLE ProcessHandle,
    IN PROCESSINFOCLASS ProcessInformationClass,
    OUT void* ProcessInformation,
    IN size_t ProcessInformationLength,
    _Out_Opt uint32_t* ReturnLength
)

/*++

    Routine description:

        Queries information about a process through a handle with
        MT_PROCESS_QUERY_INFO access. ProcessBasicInformation returns a
        synchronized snapshot of identity, PEB, and exit status. ExitStatus is
        MT_PENDING until the process dispatcher object becomes permanently
        signaled, then it is the final published process status.

    Arguments:

        ProcessHandle - Handle to the process to query.
        ProcessInformationClass - Selects the requested information structure.
        ProcessInformation - Receives the selected process information.
        ProcessInformationLength - Size in bytes of ProcessInformation.
        ReturnLength - Optionally receives the required information size.

    Return Values:

        MT_SUCCESS or an information-class, length, probing, handle, type, or
        access failure status.

--*/

{
    // Validate pointers
    MTSTATUS Status = ProbeForRead(ProcessInformation, ProcessInformationLength, _Alignof(void*));
    if (MT_FAILURE(Status)) return Status;

    if (ReturnLength) {
        Status = ProbeForRead(ReturnLength, sizeof(uint32_t), _Alignof(uint32_t));
        if (MT_FAILURE(Status)) return Status;
    }

    // Attempt reference
    void* Object = NULL;
    Status = ObReferenceObjectByHandle(
        ProcessHandle,
        MT_PROCESS_QUERY_INFO,
        PsProcessType,
        &Object,
        NULL
    );

    if (MT_FAILURE(Status)) return Status;

    PEPROCESS Process = (PEPROCESS)Object;

    // From now dereference is a must when exiting.
    // Now check for the process info class, see if it even matches the size.
    switch (ProcessInformationClass) {
    case ProcessBasicInformation:

        // Validate the info size is corect.
        if (ProcessInformationLength != sizeof(PROCESS_BASIC_INFORMATION)) {
            Status = MT_INFO_LENGTH_MISMATCH;

            if (ReturnLength) {
                try {
                    *ReturnLength = sizeof(PROCESS_BASIC_INFORMATION);
                } except{
                    // Its an access violation, but do we overwrite the Status?
                    Status = GetExceptionCode();
                }
                end_try;
            }

            break;
        }

        // Valid procinfo, fill it in.
        PROCESS_BASIC_INFORMATION KProcInfo = { 0 };

        IRQL OldIrql;
        MsAcquireSpinlock(&Process->InternalProcess.Header.Lock, &OldIrql);

        bool ProcessStillRunning =
            Process->InternalProcess.Header.SignalState == 0;

        KProcInfo.ExitStatus = ProcessStillRunning
            ? MT_PENDING
            : Process->ExitStatus;

        KProcInfo.PebBaseAddress = Process->Peb;
        KProcInfo.UniqueProcessId = Process->PID;
        KProcInfo.ParentUniqueProcessId = Process->ParentProcess;

        MsReleaseSpinlock(&Process->InternalProcess.Header.Lock, OldIrql);

        try {
            *(PROCESS_BASIC_INFORMATION*)ProcessInformation = KProcInfo;
        } except{
            Status = GetExceptionCode();
            break;
        }
        end_try;

        if (ReturnLength) {
            try {
                *ReturnLength = sizeof(PROCESS_BASIC_INFORMATION);
            } except{
                // Its an access violation, but do we overwrite the Status?
                Status = GetExceptionCode();
                break;
            }
            end_try;
        }

        break;
    default:
        Status = MT_INVALID_INFO_CLASS;
        break;
    }

    ObDereferenceObject(Object);
    return Status;
}

MTSTATUS
MtQueryInformationThread(
    IN HANDLE ThreadHandle,
    IN THREADINFOCLASS ThreadInformationClass,
    OUT void* ThreadInformation,
    IN size_t ThreadInformationLength,
    _Out_Opt uint32_t* ReturnLength
)

/*++

    Routine description:

        Queries information about a thread through a handle with
        MT_THREAD_QUERY_INFO access. ThreadBasicInformation returns a
        synchronized snapshot of identity, TEB, and exit status. ExitStatus is
        MT_PENDING until the thread dispatcher object becomes permanently
        signaled, then it is the final published thread status.

    Arguments:

        ThreadHandle - Handle to the thread to query.
        ThreadInformationClass - Selects the requested information structure.
        ThreadInformation - Receives the selected thread information.
        ThreadInformationLength - Size in bytes of ThreadInformation.
        ReturnLength - Optionally receives the required information size.

    Return Values:

        MT_SUCCESS or an information-class, length, probing, handle, type, or
        access failure status.

--*/

{
    // Validate pointers
    MTSTATUS Status = ProbeForRead(ThreadInformation, ThreadInformationLength, _Alignof(void*));
    if (MT_FAILURE(Status)) return Status;

    if (ReturnLength) {
        Status = ProbeForRead(ReturnLength, sizeof(uint32_t), _Alignof(uint32_t));
        if (MT_FAILURE(Status)) return Status;
    }

    // Reference thread
    void* Object = NULL;
    Status = ObReferenceObjectByHandle(
        ThreadHandle,
        MT_THREAD_QUERY_INFO,
        PsThreadType,
        &Object,
        NULL
    );

    if (MT_FAILURE(Status)) return Status;

    PETHREAD Thread = (PETHREAD)Object;

    switch (ThreadInformationClass) {
    case ThreadBasicInformation:

        // Validate the info size is corect.
        if (ThreadInformationLength != sizeof(THREAD_BASIC_INFORMATION)) {
            Status = MT_INFO_LENGTH_MISMATCH;

            if (ReturnLength) {
                try {
                    *ReturnLength = sizeof(THREAD_BASIC_INFORMATION);
                } except{
                    // Its an access violation, but do we overwrite the Status?
                    Status = GetExceptionCode();
                }
                end_try;
            }

            break;
        }

        // Valid procinfo, fill it in.
        THREAD_BASIC_INFORMATION KPThreadInfo = { 0 };

        IRQL OldIrql;
        MsAcquireSpinlock(&Thread->InternalThread.Header.Lock, &OldIrql);

        bool ThreadStillRunning =
            Thread->InternalThread.Header.SignalState == 0;

        KPThreadInfo.ExitStatus = ThreadStillRunning
            ? MT_PENDING
            : Thread->ExitStatus;

        KPThreadInfo.TebBaseAddress = Thread->Teb;
        KPThreadInfo.UniqueThreadId = Thread->TID;
        KPThreadInfo.UniqueProcessId = Thread->ParentProcess->PID;

        MsReleaseSpinlock(&Thread->InternalThread.Header.Lock, OldIrql);

        try {
            *(THREAD_BASIC_INFORMATION*)ThreadInformation = KPThreadInfo;
        } except{
            Status = GetExceptionCode();
            break;
        }
        end_try;

        if (ReturnLength) {
            try {
                *ReturnLength = sizeof(THREAD_BASIC_INFORMATION);
            } except{
                    // Its an access violation, but do we overwrite the Status?
                    Status = GetExceptionCode();
                    break;
            }
            end_try;
        }

        break;
    default:
        Status = MT_INVALID_INFO_CLASS;
        break;
    }

    ObDereferenceObject(Object);
    return Status;
}

MTSTATUS
MtSuspendThread(
    IN HANDLE ThreadHandle,
    _Out_Opt uint32_t* PreviousSuspendCount
)

/*++

    Routine description:

        Increments a thread suspend count and arranges suspension when required.

    Arguments:

        [IN] ThreadHandle - Handle to the target thread.
        [IN] PreviousSuspendCount - Number of previous suspend entries.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    // If pointer is present validate it
    MTSTATUS Status;
    if (PreviousSuspendCount) {
        Status = ProbeForRead(PreviousSuspendCount, sizeof(uint32_t), _Alignof(uint32_t));
        if (MT_FAILURE(Status)) return Status;
    }

    void* Object;
    Status = ObReferenceObjectByHandle(
        ThreadHandle,
        MT_THREAD_SUSPEND_RESUME,
        PsThreadType,
        &Object,
        NULL
    );

    if (MT_FAILURE(Status)) return Status;

    // We can cast to PITHREAD too since its guranteed to be at the top of ETHREAD, but we will abide to the kernel, brainwash.
    PETHREAD Thread = (PETHREAD)Object;

    // Call kernel function

    uint32_t KPrevCount = 0;

    Status = MeSuspendThread(&Thread->InternalThread, &KPrevCount);

    // Attempt to get back to the user.
    if (MT_SUCCEEDED(Status) && PreviousSuspendCount) {
        try {
            *PreviousSuspendCount = KPrevCount;
        } except{
            Status = GetExceptionCode();
        }
        end_try;
    }

    ObDereferenceObject(Object);
    return Status;
}

MTSTATUS
MtResumeThread(
    IN HANDLE ThreadHandle,
    _Out_Opt uint32_t* PreviousSuspendCount
)

/*++

    Routine description:

        Decrements a thread suspend count and releases its suspend wait at zero.

    Arguments:

        [IN] ThreadHandle - Handle to the target thread.
        [IN] PreviousSuspendCount - Number of previous suspend entries.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    // If pointer is present validate it
    MTSTATUS Status;
    if (PreviousSuspendCount) {
        Status = ProbeForRead(PreviousSuspendCount, sizeof(uint32_t), _Alignof(uint32_t));
        if (MT_FAILURE(Status)) return Status;
    }

    void* Object;
    Status = ObReferenceObjectByHandle(
        ThreadHandle,
        MT_THREAD_SUSPEND_RESUME,
        PsThreadType,
        &Object,
        NULL
    );

    if (MT_FAILURE(Status)) return Status;

    // We can cast to PITHREAD too since its guranteed to be at the top of ETHREAD, but we will abide to the kernel, brainwash.
    PETHREAD Thread = (PETHREAD)Object;

    // Call kernel function

    uint32_t KPrevCount = 0;

    Status = MeResumeThread(&Thread->InternalThread, &KPrevCount);

    // Attempt to get back to the user.
    if (MT_SUCCEEDED(Status) && PreviousSuspendCount) {
        try {
            *PreviousSuspendCount = KPrevCount;
        } except{
            Status = GetExceptionCode();
        }
        end_try;
    }

    ObDereferenceObject(Object);
    return Status;
}

MTSTATUS
MtRaiseException(
    IN const EXCEPTION_RECORD* ExceptionRecord
    /* IN PCONTEXT ContextRecord */ // unused, when debugger supports arrives, then this can be used.
    // honestly i have no idea why its even here, but ok.
    /* IN bool FirstChance */ // when dbg arrives
)

/*++

    Routine description:

        The routine raises an exception, registers the current thread exception pending status
        and delivers the exception to MTDLL in user mode when returning from this system call.

    Arguments:

        [IN] ExceptionRecord - Pointer to exception record structure on the user mode stack

    Return Values:

        This function maybe returns, when one of the following situations occur -

        Copying the stack exception record to the kernel has failed, could be a
        MT_DATATYPE_MISALIGNMENT, MT_ACCESS_VIOLATION, or any other ProbeForRead/try except exception code.
        This also returns an error if ExpPublishUserException fails.

        The Handler has chosen to Continue execution, possibly with modified registers.

--*/

{
    MTSTATUS Status = ProbeForRead(ExceptionRecord, sizeof(EXCEPTION_RECORD), _Alignof(EXCEPTION_RECORD));
    if (MT_FAILURE(Status)) return Status;

    // Copy over the stack EXCEPTION_RECORD to here
    // This could also be heap allocated, saying stack is kinda half-truth.
    EXCEPTION_RECORD KExceptionRecord;
    
    try {
        kmemcpy(&KExceptionRecord, ExceptionRecord, sizeof(EXCEPTION_RECORD));
    } except {
        return GetExceptionCode();
    }
    end_try;

    // We can do real work now.
    // Real work - call 1 function, easy.
    // All we do is publish the exception so at system call exit, interrupt exit or scheduler exit, because the last things can still happen here.
    // Or if it returns a failure then a failure
    return ExpPublishUserException(&KExceptionRecord);
}

MTSTATUS
MtCreateSection(
    OUT PHANDLE SectionHandle,
    IN ACCESS_MASK DesiredAccess,
    IN HANDLE FileHandle
)

/*++

    Routine description:

        Creates a section object and returns a user handle.

    Arguments:

        [OUT] SectionHandle - Receives the created section handle.
        [IN] DesiredAccess - Access mask required by the caller.
        [IN] FileHandle - Handle to the file used by the operation.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    MTSTATUS Status = ProbeForRead(SectionHandle, sizeof(HANDLE), _Alignof(HANDLE));
    if (MT_FAILURE(Status)) return Status;
   
    // Reference the file handle
    void* Object = NULL;
    Status = ObReferenceObjectByHandle(
        FileHandle,
        MT_FILE_READ_DATA,
        FsFileType,
        &Object,
        NULL
    );

    if (MT_FAILURE(Status)) return Status;

    // Call internal Mm function
    void* SectionObject = NULL;
    HANDLE KernelHandle = MT_INVALID_HANDLE;
    Status = MmCreateSection(&SectionObject, (PFILE_OBJECT)Object);

    if (MT_FAILURE(Status)) {
        goto Cleanup;
    }

    // Create a handle for the section object
    Status = ObCreateHandleForObject(SectionObject, DesiredAccess, &KernelHandle);

    if (MT_FAILURE(Status)) goto Cleanup;

    // Copy handle back to user
    try {
        *SectionHandle = KernelHandle;
        Status = MT_SUCCESS;
    } except {
        Status = GetExceptionCode();
    }
    end_try;

Cleanup:

    if (MT_FAILURE(Status)) {

        if (KernelHandle != MT_INVALID_HANDLE) {
            HtClose(KernelHandle);
        }

    }

    if (SectionObject) {
        ObDereferenceObject(SectionObject);
    }

    ObDereferenceObject(Object);
    return Status;
}

MTSTATUS
MtMapViewOfSection(
    IN HANDLE SectionHandle,
    IN HANDLE ProcessHandle,
    OUT void** BaseAddress,
    OUT void** EntryPointAddress,
    OUT size_t* ViewSize
)

/*++

    Routine description:

        Maps a section view into a target process through user handles.

    Arguments:

        [IN] SectionHandle - Handle to the section being mapped.
        [IN] ProcessHandle - Handle to the target process.
        [IN] BaseAddress - Base address requested or returned by the mapping operation.
        [OUT] EntryPointAddress - Receives the mapped image entry point.
        [IN] ViewSize - Size of the view in bytes.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    // Validate every OUT ptr first.
    MTSTATUS Status = ProbeForRead(BaseAddress, sizeof(void*), _Alignof(void*));
    if (MT_FAILURE(Status)) return Status;

    Status = ProbeForRead(EntryPointAddress, sizeof(void*), _Alignof(void*));
    if (MT_FAILURE(Status)) return Status;

    Status = ProbeForRead(ViewSize, sizeof(size_t), _Alignof(size_t));
    if (MT_FAILURE(Status)) return Status;

    // Reference SectionHandle with MmSectionType and since current mapper creates a section with RWX mappings (.text .data), then
    // we must require the correct section access flags
    void* SectionObject = NULL;
    void* ProcessObject = NULL;
    bool AcquiredRundown = false;
    bool MappedView = false;

    Status = ObReferenceObjectByHandle(
        SectionHandle,
        MT_SECTION_MAP_READ | MT_SECTION_MAP_WRITE | MT_SECTION_MAP_EXECUTE,
        MmSectionType,
        &SectionObject,
        NULL
    );

    if (MT_FAILURE(Status)) goto Cleanup;

    // Reference the process the caller wants to map a section in (file/etc) with VM_OPERATION since we allocate memory in the process for this.
    Status = ObReferenceObjectByHandle(
        ProcessHandle,
        MT_PROCESS_VM_OPERATION,
        PsProcessType,
        &ProcessObject,
        NULL
    );

    if (MT_FAILURE(Status)) goto Cleanup;

    PEPROCESS Process = (PEPROCESS)ProcessObject;
    // Acquire process rundown
    if (!MsAcquireRundownProtection(&Process->ProcessRundown)) {
        Status = MT_PROCESS_IS_TERMINATING;
        goto Cleanup;
    }

    AcquiredRundown = true;

    // Call internal Mm function now
    void* KEntryPointAddress = NULL;
    void* KBaseAddress = NULL;
    Status = MmMapViewOfSection(SectionObject, Process, &KEntryPointAddress, &KBaseAddress);

    if (MT_FAILURE(Status)) goto Cleanup;

    MappedView = true;

    size_t KernelViewSize = ((PMM_SECTION)SectionObject)->ImageSize;

    // Copy all outputs back to user
    try {
        *ViewSize = KernelViewSize;
        *BaseAddress = KBaseAddress;
        *EntryPointAddress = KEntryPointAddress;
    } except{
        Status = GetExceptionCode();
        goto Cleanup;
    }
    end_try;

Cleanup:

    if (MappedView && MT_FAILURE(Status)) {
        MmUnmapViewOfSection((PEPROCESS)ProcessObject, KBaseAddress);
    }

    if (AcquiredRundown) {
        MsReleaseRundownProtection(&((PEPROCESS)(ProcessObject))->ProcessRundown);
    }

    if (SectionObject) {
        ObDereferenceObject(SectionObject);
    }

    if (ProcessObject) {
        ObDereferenceObject(ProcessObject);
    }

    // need MmUnmapViewOfSection

    return Status;
}

MTSTATUS
MtUnmapViewOfSection(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress
)

/*++

    Routine description:

        Unmaps a section view from a target process.

    Arguments:

        [IN] ProcessHandle - Handle to the target process.
        [IN] BaseAddress - Base address requested or returned by the mapping operation.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    void* Object = NULL;
    MTSTATUS Status = ObReferenceObjectByHandle(
        ProcessHandle,
        MT_PROCESS_VM_OPERATION,
        PsProcessType,
        &Object,
        NULL
    );

    if (MT_FAILURE(Status)) return Status;

    Status = MmUnmapViewOfSection((PEPROCESS)Object, BaseAddress);
    
    ObDereferenceObject(Object);
    return Status;
}

// THIS SYSCALL SHOULD NOT STAY! SINCE GOP IS TO BE RETIRED WHEN FULL OS - SYSCALL NUM - 255
MTSTATUS
MtPrintConsole(
    IN uint32_t Color,
    IN const char* String
)

/*++

    Routine description:

        Copies a user string and writes it to the kernel console.

    Arguments:

        [IN] Color - Framebuffer color used to draw the output.
        [IN] String - String read, written, or searched by the routine.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    MTSTATUS Status;
    char KernelBuffer[256]; // max safe limit

    Status = ProbeForRead((void*)String, sizeof(char), _Alignof(char));
    if (MT_FAILURE(Status)) return Status;

    try {
        // it gurantees nullterm so we are fine.
        kstrncpy(KernelBuffer, String, sizeof(KernelBuffer));
    } except{
        return GetExceptionCode();
    } end_try;

    gop_printf(Color, "[FROM USERMODE %s TID %d] %s", PsGetCurrentProcess()->ImageName, PsGetCurrentThread()->TID, KernelBuffer);

    return MT_SUCCESS;
}
