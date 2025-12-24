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