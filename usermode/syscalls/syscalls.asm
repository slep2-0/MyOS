; /*++
; 
; Module Name:
; 
; 	syscalls.asm (user mode)
; 
; Purpose:
; 
; 	This module contains the implementation of calling the system calls of MatanelOS.
; 
; Author:
; 
; 	slep (Matanel) 2025.
; 
; Revision History:
; 
; --*/

; ARGUMENTS TO DELIVER (IN ORDER): rdi, rsi, rdx, (rcx to r10), r8, r9, (rest on stack)

; Native syscall stubs are exported by MTDLL through the generated MTE export
; directory so applications can opt into the unstable mtnative.h interface.
section .text.mtapi progbits alloc exec nowrite align=16

; MTSTATUS
; MtAllocateVirtualMemory(
;     IN HANDLE Process,
;     _In_Opt _Out_Opt void** BaseAddress,
;     IN size_t NumberOfBytes,
;     IN uint8_t AllocationType
; ); 
; Syscall number is 0.

global MtAllocateVirtualMemory
MtAllocateVirtualMemory:
	mov rax, 0 ; Syscall number
	; Since RCX is the fourth argument and is replaced by the RIP of return, we just make R10 the new fourth argument.
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtOpenProcess(
;     IN uint32_t ProcessId,
;     OUT PHANDLE ProcessHandle,
;     IN ACCESS_MASK DesiredAccess
; );
; Syscall number is 1.

global MtOpenProcess
MtOpenProcess:
	mov rax, 1
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtTerminateProcess(
;     IN HANDLE ProcessHandle,
;     IN MTSTATUS ExitStatus
; );
; Syscall number is 2.

global MtTerminateProcess
MtTerminateProcess:
	mov rax, 2
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtReadFile(
;     IN HANDLE FileHandle,
;     IN uint64_t FileOffset,
;     OUT void* Buffer,
;     IN size_t BufferSize,
;     _Out_Opt size_t* BytesRead
; );
; Syscall number is 3.

global MtReadFile
MtReadFile:
	mov rax, 3
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtWriteFile(
;     IN HANDLE FileHandle,
;     IN uint64_t FileOffset,
;     IN void* Buffer,
;     IN size_t BufferSize,
;     _Out_Opt size_t* BytesWritten
; );
; Syscall number is 4.

global MtWriteFile
MtWriteFile:
	mov rax, 4
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtCreateFile(
;     IN const char* path,
;     IN ACCESS_MASK DesiredAccess,
;     OUT PHANDLE FileHandleOut
; );
; Syscall number is 5.

global MtCreateFile
MtCreateFile:
	mov rax, 5
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtClose(
;     IN HANDLE hObject
; );
; Syscall number is 6.

global MtClose
MtClose:
	mov rax, 6
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtTerminateThread(
;     IN HANDLE ThreadHandle,
;     IN MTSTATUS ExitStatus
; );
; Syscall number is 7.

global MtTerminateThread
MtTerminateThread:
	mov rax, 7
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtQueryVirtualMemory(
;     IN HANDLE ProcessHandle,
;     IN void* BaseAddress,
;     OUT PMEMORY_BASIC_INFORMATION MemoryInformation
; );
; Syscall number is 8.

global MtQueryVirtualMemory
MtQueryVirtualMemory:
	mov rax, 8
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtProtectVirtualMemory(
;     IN HANDLE ProcessHandle,
;     IN OUT void** BaseAddress,
;     IN OUT size_t* RegionSize,
;     IN USER_PROTECTION_TYPE NewProtection,
;     OUT USER_PROTECTION_TYPE* OldProtection
; );
; Syscall number is 9.

global MtProtectVirtualMemory
MtProtectVirtualMemory:
	mov rax, 9
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtFreeVirtualMemory(
;     IN HANDLE ProcessHandle,
;     IN OUT void** BaseAddress,
;     IN OUT size_t* NumberOfBytes,
;     IN enum _FREE_TYPE FreeType
; );
; Syscall number is 10.

global MtFreeVirtualMemory
MtFreeVirtualMemory:
	mov rax, 10
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtCreateThread(
;     IN HANDLE ProcessHandle,
;     IN THREAD_START_ROUTINE StartRoutine,
;     IN void* Argument,
;     OUT PHANDLE ThreadHandle
; );
; Syscall number is 11.

global MtCreateThread
MtCreateThread:
	mov rax, 11
	mov r10, rcx
	syscall
	ret

; NORETURN void MtContinue(const CONTEXT* ContextRecord);
; Syscall number is 12.
global MtContinue
MtContinue:
	mov rax, 12
	mov r10, rcx
	syscall
	ud2

; MTSTATUS
; MtDelayExecution(
;     IN bool Alertable,
;     IN uint64_t Milliseconds
; );
; Syscall number is 13.

global MtDelayExecution
MtDelayExecution:
	mov rax, 13
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtWaitForSingleObject(
;     IN HANDLE ObjectHandle,
;     IN uint64_t Milliseconds,
;     IN bool Alertable
; );
; Syscall number is 14.

global MtWaitForSingleObject
MtWaitForSingleObject:
	mov rax, 14
	mov r10, rcx
	syscall
	ret

; MTSTATUS MtCreateEvent(PHANDLE, ACCESS_MASK, EVENT_TYPE, bool, const char*);
; Syscall number is 15.
global MtCreateEvent
MtCreateEvent:
	mov rax, 15
	mov r10, rcx
	syscall
	ret

; MTSTATUS MtQueryEvent(HANDLE, bool*);
; Syscall number is 16.
global MtQueryEvent
MtQueryEvent:
	mov rax, 16
	mov r10, rcx
	syscall
	ret

; MTSTATUS MtSetEvent(HANDLE, bool*);
; Syscall number is 17.
global MtSetEvent
MtSetEvent:
	mov rax, 17
	mov r10, rcx
	syscall
	ret

; MTSTATUS MtResetEvent(HANDLE, bool*);
; Syscall number is 18.
global MtResetEvent
MtResetEvent:
	mov rax, 18
	mov r10, rcx
	syscall
	ret

; MTSTATUS MtCreateMutex(PHANDLE, ACCESS_MASK, bool, const char*);
; Syscall number is 19.
global MtCreateMutex
MtCreateMutex:
	mov rax, 19
	mov r10, rcx
	syscall
	ret

; MTSTATUS MtQueryMutex(HANDLE, MUTEX_BASIC_INFORMATION*);
; Syscall number is 20.
global MtQueryMutex
MtQueryMutex:
	mov rax, 20
	mov r10, rcx
	syscall
	ret

; MTSTATUS MtReleaseMutex(HANDLE, int32_t*);
; Syscall number is 21.
global MtReleaseMutex
MtReleaseMutex:
	mov rax, 21
	mov r10, rcx
	syscall
	ret

; MTSTATUS MtCreateSemaphore(PHANDLE, ACCESS_MASK, int32_t, int32_t, const char*);
; Syscall number is 22.
global MtCreateSemaphore
MtCreateSemaphore:
	mov rax, 22
	mov r10, rcx
	syscall
	ret

; MTSTATUS MtQuerySemaphore(HANDLE, SEMAPHORE_BASIC_INFORMATION*);
; Syscall number is 23.
global MtQuerySemaphore
MtQuerySemaphore:
	mov rax, 23
	mov r10, rcx
	syscall
	ret

; MTSTATUS MtReleaseSemaphore(HANDLE, int32_t, int32_t*);
; Syscall number is 24.
global MtReleaseSemaphore
MtReleaseSemaphore:
	mov rax, 24
	mov r10, rcx
	syscall
	ret


; MTSTATUS
; MtQueryInformationProcess(
;     IN HANDLE ProcessHandle,
;     IN PROCESSINFOCLASS ProcessInformationClass,
;     OUT void* ProcessInformation,
;     IN size_t ProcessInformationLength,
;     _Out_Opt uint32_t* ReturnLength
; )
; Syscall number is 25
global MtQueryInformationProcess
MtQueryInformationProcess:
	mov rax, 25
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtQueryInformationThread(
;     IN HANDLE ThreadHandle,
;     IN THREADINFOCLASS ThreadInformationClass,
;     OUT void* ThreadInformation,
;     IN size_t ThreadInformationLength,
;     _Out_Opt uint32_t* ReturnLength
; )
; Syscall number is 26
global MtQueryInformationThread
MtQueryInformationThread:
	mov rax, 26
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtSuspendThread(
;     IN HANDLE ThreadHandle,
;     _Out_Opt uint32_t* PreviousSuspendCount
; );
; Syscall number is 27
global MtSuspendThread
MtSuspendThread:
	mov rax, 27
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtResumeThread(
;     IN HANDLE ThreadHandle,
;     _Out_Opt uint32_t* PreviousSuspendCount
; );
; Syscall number is 28
global MtResumeThread
MtResumeThread:
	mov rax, 28
	mov r10, rcx
	syscall
	ret

; MTSTATUS MtRaiseException(const EXCEPTION_RECORD*);
; Syscall number is 29.
global MtRaiseException
MtRaiseException:
    mov rax, 29
    mov r10, rcx
    syscall
    ret

; MTSTATUS MtCreateSection(PHANDLE, ACCESS_MASK, HANDLE);
; Syscall number is 30.
global MtCreateSection
MtCreateSection:
	mov rax, 30
	mov r10, rcx
	syscall
	ret

; MTSTATUS MtMapViewOfSection(HANDLE, HANDLE, void**, void**, size_t*);
; Syscall number is 31.
global MtMapViewOfSection
MtMapViewOfSection:
	mov rax, 31
	mov r10, rcx
	syscall
	ret

; MTSTATUS MtUnmapViewOfSection(HANDLE, void*);
; Syscall number is 32.
global MtUnmapViewOfSection
MtUnmapViewOfSection:
	mov rax, 32
	mov r10, rcx
	syscall
	ret

; MTSTATUS MtCreateProcess(const MT_CREATE_PROCESS_PARAMETERS*, PHANDLE);
; Syscall number is 33.
global MtCreateProcess
MtCreateProcess:
	mov rax, 33
	mov r10, rcx
	syscall
	ret

; TO BE RETIRED
global MtPrintConsole
MtPrintConsole:
	mov rax, 255
	mov r10, rcx
	syscall
	ret
