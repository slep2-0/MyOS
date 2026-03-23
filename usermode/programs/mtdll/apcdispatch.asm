; MeUserApcDispatcher - Handles User Mode APCs delivered by the kernel.
; The kernel sets the thread execution point to this function.

%define Syscall_MtContinue 12

global MeUserApcDispatcher
MeUserApcDispatcher:
	; Save the context pointer, passed in r8
	push r8

	; Call NormalRoutine
	; RDI Is the function pointer, so we will shift the args
	mov r9, rdi         ; temporarily hold NormalRoutine
    mov rdi, rsi        ; Arg 1: NormalContext
    mov rsi, rdx        ; Arg 2: SysArg1
    mov rdx, rcx        ; Arg 3: SysArg2
    call r9             ; Call NormalRoutine(NormalContext, SysArg1, SysArg2)

	; Restore the context pointer we pushed
	pop rdi

	; Call MtContinue
	mov rax, Syscall_MtContinue
	mov r10, rcx
	syscall

	; If we reach here, UD2, we should NEVER return to here.
	ud2