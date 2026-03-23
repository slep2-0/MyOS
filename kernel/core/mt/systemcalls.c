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

static
VAD_FLAGS
MtpUserAllocationTypeToVadFlags(
    IN USER_PROTECTION_TYPE AllocationType
)

{
    switch (AllocationType) {
        case PAGE_EXECUTE_READWRITE:
            return VAD_FLAG_EXECUTE | VAD_FLAG_READ | VAD_FLAG_WRITE;
        case PAGE_EXECUTE_READ:
            return VAD_FLAG_EXECUTE | VAD_FLAG_READ;
        case PAGE_READWRITE:
            return VAD_FLAG_READ | VAD_FLAG_WRITE;
        case PAGE_READONLY:
            return VAD_FLAG_READ;
        case PAGE_NOACCESS:
            return VAD_FLAG_RESERVED;
    }

    return VAD_FLAG_NONE;
}

static
USER_PROTECTION_TYPE
MtpVadFlagsToUserAllocationType(
    IN VAD_FLAGS VadFlags
)
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

    // Dereference.
    ObDereferenceObject(ProcessToTerminate);

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

MTSTATUS
MtClose(
    IN HANDLE hObject
)

{
    // Easiest syscall yet, just call internal function.
    return HtClose(hObject);
}

MTSTATUS
MtTerminateThread(
    IN HANDLE ThreadHandle,
    IN MTSTATUS ExitStatus
)

{
    // Attempt to reference thread, or if it is ourselves use ourselves.
    MTSTATUS Status;
    PETHREAD Thread;
    if (ThreadHandle == MtCurrentThread()) {
        // Check if we are the last thread of the process.
        if (PsGetCurrentProcess()->NumThreads == 1) {
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

    // Dereference thread (that we referenced earlier).
    ObDereferenceObject(Thread);

    // Call internal function.
    return PsTerminateThread(Thread, ExitStatus);
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
    assert((uintptr_t)BaseAddress <= MmHighestUserAddress);
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

MTSTATUS
MtProtectVirtualMemory(
    IN HANDLE ProcessHandle,
    IN OUT void** BaseAddress,
    IN OUT size_t* RegionSize,
    IN USER_PROTECTION_TYPE NewProtection,
    OUT USER_PROTECTION_TYPE* OldProtection
)

{
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

    // All validations have been passed, now time to do the actual stuff.
    uintptr_t ProtectStart = (uintptr_t)PAGE_ALIGN(CapturedBaseAddress);
    uintptr_t ProtectEnd = ProtectStart + ALIGN_UP(CapturedRegionSize, VirtualPageSize) - 1;

    // Acquire exclusive.
    MsAcquirePushLockExclusive(&Process->VadLock);

    PMMVAD Vad = MiFindVadInternal(Process, ProtectStart, false);

    // Validate VAD exists and entirely encompasses the request.
    if (!Vad || ProtectEnd > Vad->EndVa) {
        MsReleasePushLockExclusive(&Process->VadLock);
        ObDereferenceObject(Process);
        return MT_INVALID_ADDRESS;
    }

    try {
        *OldProtection = MtpVadFlagsToUserAllocationType(Vad->Flags);
        gop_printf(COLOR_RED, "**[SYSCALL-VIRTPROT] Returning OldProtection %x**\n", MtpVadFlagsToUserAllocationType(Vad->Flags));
        *RegionSize = (ProtectEnd - ProtectStart) + 1;
    } except{
        MsReleasePushLockExclusive(&Process->VadLock);
        ObDereferenceObject(Process);
        return GetExceptionCode();
    } end_try;

    VAD_FLAGS NewVadFlags = MtpUserAllocationTypeToVadFlags(NewProtection);

    // If programmer == dumb (or forgetful)
    if (Vad->Flags == NewVadFlags) {
        MsReleasePushLockExclusive(&Process->VadLock);
        ObDereferenceObject(Process);
        return MT_SUCCESS;
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
        ObDereferenceObject(Process);
        return MT_NO_RESOURCES;
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
                    New.Hard.Present = 0;
                    break;
                case PAGE_EXECUTE_READWRITE:
                    New.Hard.Write = 1;
                    New.Hard.NoExecute = 0;
                    break;
                case PAGE_EXECUTE_READ:
                    New.Hard.Write = 0;
                    New.Hard.NoExecute = 0;
                    break;
                case PAGE_READWRITE:
                    New.Hard.Write = 1;
                    New.Hard.NoExecute = 1;
                    break;
                case PAGE_READONLY:
                    New.Hard.Write = 0;
                    New.Hard.NoExecute = 1;
                    break;
                }
            }
            else {
                // Swapped out, update software flags.
                New.Soft.SoftwareFlags |= (NewVadFlags & VAD_FLAG_READ) ? PROT_KERNEL_READ : 0;
                New.Soft.SoftwareFlags |= (NewVadFlags & VAD_FLAG_WRITE) ? PROT_KERNEL_WRITE : 0;
                New.Soft.SoftwareFlags |= (NewVadFlags & VAD_FLAG_EXECUTE) ? PROT_KERNEL_NOEXECUTE : 0;
                New.Soft.SoftwareFlags |= PROT_KERNEL_USER;
            }

        } while (!MiAtomicSetPte(Pte, New.Value, Expected.Value));
    }

    // Flush TLB since we modified several PTEs.
    MiReloadTLBs();

    MsReleasePushLockExclusive(&Process->VadLock);
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

{
    // Validate argument.
    MTSTATUS Status = ProbeForRead(ThreadHandle, sizeof(HANDLE), _Alignof(HANDLE));
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
            ObDereferenceObject(Process);
            return GetExceptionCode();
        }
        end_try;
    }

    ObDereferenceObject(Process);
    return Status;
}

void
MtContinue(
    PTRAP_FRAME OldTrapFrame
)

{
    // Access the current thread trap frame.
    PTRAP_FRAME CurrentTrap = MeGetCurrentThread()->SyscallTrap;

    // Attempt to copy the old trap frame into the current one.
    // This would essentially return the thread to his old trap context before the user mode APC Dipatch.
    // First, turn off the flag.
    MeGetCurrentProcessor()->ApcRoutineActive = false;

    try {
        kmemcpy(CurrentTrap, OldTrapFrame, sizeof(TRAP_FRAME));
    } except{
        assert(false, "Exception in copying TRAP_FRAME, this shouldn't happen.");
        PsTerminateThread(PsGetCurrentThread(), MT_APC_ERROR);
    }
    end_try;
}