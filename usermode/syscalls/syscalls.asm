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

; MtContinue is not here, syscall number is 12. (look at apcdispatch.asm)

; MTSTATUS
; MtSleep(
;     IN uint64_t Milliseconds
; );
; Syscall number is 13.

global MtSleep
MtSleep:
	mov rax, 13
	mov r10, rcx
	syscall
	ret

; MTSTATUS
; MtWaitForSingleObject(
;     IN HANDLE ObjectHandle,
;     IN uint64_t Milliseconds
; );
; Syscall number is 14.

global MtWaitForSingleObject
MtWaitForSingleObject:
	mov rax, 14
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
