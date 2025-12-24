/*++

Module Name:

    mt.h

Purpose:

    This module contains the header files & prototypes required for user mode interactions with kernel services. (System Calls)

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#ifndef X86_MATANEL_MT_H
#define X86_MATANEL_MT_H

#include "core.h"

// Maximum number of syscalls
#define MAX_SYSCALLS 256
typedef uint64_t(*SyscallHandler)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

#define MtCurrentProcess() -1
#define MtCurrentThread() -2

typedef enum _USER_ALLOCATION_TYPE {
    PAGE_EXECUTE_READ = 0x10, // PRESENT
    PAGE_EXECUTE_READWRITE = 0x20, // PRESENT | RW
    PAGE_READWRITE = 0x30, // PRESENT | RW | NX
    PAGE_READONLY = 0x40, // PRESENT | NX
    PAGE_NOACCESS = 0x50 // NONE.
} USER_ALLOCATION_TYPE;

void
MtSetupSyscall(
    void
);

void
MtSyscallHandler(
    IN PTRAP_FRAME TrapFrame
);

// System calls
MTSTATUS
MtAllocateVirtualMemory(
    IN HANDLE Process,
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t NumberOfBytes,
    IN uint8_t AllocationType
);

MTSTATUS
MtOpenProcess(
    IN uint32_t ProcessId,
    OUT PHANDLE ProcessHandle,
    IN ACCESS_MASK DesiredAccess
);

MTSTATUS
MtTerminateProcess(
    IN HANDLE ProcessHandle,
    IN MTSTATUS ExitStatus
);

MTSTATUS
MtReadFile(
    IN HANDLE FileHandle,
    IN uint64_t FileOffset,
    OUT void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesRead
);

MTSTATUS
MtWriteFile(
    IN HANDLE FileHandle,
    IN uint64_t FileOffset,
    IN void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesWritten
);

MTSTATUS
MtCreateFile(
    IN const char* path,
    IN ACCESS_MASK DesiredAccess,
    OUT PHANDLE FileHandleOut
);

#endif