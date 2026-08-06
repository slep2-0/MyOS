; MeUserApcDispatcher - Handles User Mode APCs delivered by the kernel.
; MeUserExceptionDispatcher - Handles user mode exceptions.
; Other privates - __try __except implementation helpers.

; The kernel sets the thread execution point to the 2 first functions.

%define Syscall_MtContinue 12

%define LANGUAGE_CONTEXT_RBX  0x00
%define LANGUAGE_CONTEXT_RBP  0x08
%define LANGUAGE_CONTEXT_R12  0x10
%define LANGUAGE_CONTEXT_R13  0x18
%define LANGUAGE_CONTEXT_R14  0x20
%define LANGUAGE_CONTEXT_R15  0x28
%define LANGUAGE_CONTEXT_RSP  0x30
%define LANGUAGE_CONTEXT_RIP  0x38

%define LANGUAGE_CONTEXT_SIZE 0x40

section .text.mtapi progbits alloc exec nowrite align=16
global MeUserApcDispatcher
MeUserApcDispatcher:
	; Save the CONTEXT pointer supplied by the kernel in R8. The normal APC
	; routine does not receive it, but MtContinue needs it after that routine
	; returns so execution can resume at the interrupted user instruction.
	push r8

	; Call NormalRoutine
	; RDI Is the function pointer, so we will shift the args
	mov r9, rdi         ; temporarily hold NormalRoutine
    mov rdi, rsi        ; Arg 1: NormalContext
    mov rsi, rdx        ; Arg 2: SysArg1
    mov rdx, rcx        ; Arg 3: SysArg2
    call r9             ; Call NormalRoutine(NormalContext, SysArg1, SysArg2)

	; Recover the CONTEXT pointer as MtContinue's first argument.
	pop rdi

	; MtContinue validates the CONTEXT and restores the interrupted user state.
	mov rax, Syscall_MtContinue
	mov r10, rcx
	syscall

	; If we reach here, UD2, we should NEVER return to here.
	ud2

extern MtpUserExceptionDispatcher

section .text.mtapi progbits alloc exec nowrite align=16
global MeUserExceptionDispatcher
MeUserExceptionDispatcher:
	; RDI = ExceptionRecord
	; RSI = ContextRecord
	; JMP directly, it does not modify the stack so no need for sub
	; Besides, this is a NORETURN, it will either call MtContinue, Terminate the thread, or trap into an __except handler
	jmp MtpUserExceptionDispatcher

global MtpSaveLanguageContext
MtpSaveLanguageContext:
	; Saves callee-saved registers to the stack
	; Saves the return address as the RIP
	; RDI -  PMT_LANGUAGE_CONTEXT Context
		
	; Save the callee-base registers first.
	mov [rdi + LANGUAGE_CONTEXT_RBX], rbx
	mov [rdi + LANGUAGE_CONTEXT_RBP], rbp
	mov [rdi + LANGUAGE_CONTEXT_R12], r12
	mov [rdi + LANGUAGE_CONTEXT_R13], r13
	mov [rdi + LANGUAGE_CONTEXT_R14], r14
	mov [rdi + LANGUAGE_CONTEXT_R15], r15

	; Modify special registers.
	lea rax, [rsp + 8]
    mov [rdi + LANGUAGE_CONTEXT_RSP], rax ; Move the stack + 8 (skip return address) to RSP.
	mov rax, [rsp]
    mov [rdi + LANGUAGE_CONTEXT_RIP], rax ; Move the return address to the RIP

	; Return zero now.
	xor eax, eax
	ret

global MtpRestoreLanguageContext
MtpRestoreLanguageContext:
	; Restores what MtpSaveLanguageContext pushed
	; RDI - PMT_LANGUAGE_CONTEXT Context
	; RSI - int ReturnValue
	; This function, does not return (we jump to saved RIP)
    mov rdx, [rdi + LANGUAGE_CONTEXT_RIP]
    mov rcx, [rdi + LANGUAGE_CONTEXT_RSP]

    mov rbx, [rdi + LANGUAGE_CONTEXT_RBX]
    mov rbp, [rdi + LANGUAGE_CONTEXT_RBP]
    mov r12, [rdi + LANGUAGE_CONTEXT_R12]
    mov r13, [rdi + LANGUAGE_CONTEXT_R13]
    mov r14, [rdi + LANGUAGE_CONTEXT_R14]
    mov r15, [rdi + LANGUAGE_CONTEXT_R15]

	; Move the return value, and saved RSP
	; Then jump to saved RIP.
    mov eax, esi
    mov rsp, rcx
    jmp rdx
	
; Restore the protected function's nonvolatile registers so its filter can use
; the original locals, but remain on the active exception-dispatch stack. The
; kernel-owned dispatch is completed later by the filter's MtContinue call.
global MtpRestoreLanguageContextForFilter
MtpRestoreLanguageContextForFilter:
    mov rdx, [rdi + LANGUAGE_CONTEXT_RIP]
    lea rcx, [rsp + 8]

    mov rbx, [rdi + LANGUAGE_CONTEXT_RBX]
    mov rbp, [rdi + LANGUAGE_CONTEXT_RBP]
    mov r12, [rdi + LANGUAGE_CONTEXT_R12]
    mov r13, [rdi + LANGUAGE_CONTEXT_R13]
    mov r14, [rdi + LANGUAGE_CONTEXT_R14]
    mov r15, [rdi + LANGUAGE_CONTEXT_R15]

    mov eax, esi
    mov rsp, rcx
    jmp rdx
