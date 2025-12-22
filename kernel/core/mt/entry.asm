; /*++
; 
; Module Name:
; 
;     syscall.asm
; 
; Purpose:
; 
;     This module contains the entry point of the syscall instruction.
; 
; Author:
; 
;     slep (Matanel) 2025.
; 
; Revision History:
; 
; --*/

BITS 64
DEFAULT REL
%include "offsets.inc"

; void MtSyscallHandler(void);
extern MtSyscallHandler

; void MtSyscallEntry(void);
global MtSyscallEntry
MtSyscallEntry:
    ; Switch GS to Kernel Base immediately.
    swapgs

    ; Save the current user stack.
    mov [gs:PROCESSOR_UserRsp], rsp

    ; Switch to kernel stack.
    ; Lets use the stack pointer as a scratch to set itself
    ; Its not dangerous or anything, really.
    mov rsp, [gs:PROCESSOR_currentThread]
    ; RSP - PITHREAD
    mov rsp, [rsp + ITHREAD_KernelStack]

    ; We now push the registers onto the stack.
    ; We must push how the TRAP frame expects.
    push    rax ; Syscall Number
    push    rbx
    push    rcx ; RIP To return To.
    push    rdx ; Syscall third argument
    push    rsi ; Syscall second argument
    push    rdi ; Syscall first argument
    push    rbp
    push    r8  ; Syscall fifth argument
    push    r9  ; Syscall sixth argument - above is UserRsp (on stack).
    push    r10 ; Syscall fourth argument
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15

    ; Enter C Handler, it would grab the TRAP_FRAME of the current thread inside of it already.
    cld

    ; Move the trap frame into the first arg.
    mov rdi, rsp

    call MtSyscallHandler

    ; Disable Interrupts before restoring registers.
    ; (If we get interrupts the trap frame changes on context switch..)
    cli 

    ; Restore General Purpose Registers
    pop    r15
    pop    r14
    pop    r13
    pop    r12
    pop    r11
    pop    r10
    pop    r9
    pop    r8
    pop    rbp
    pop    rdi
    pop    rsi
    pop    rdx
    pop    rcx
    pop    rbx
    pop    rax

    ; Recover User Stack Pointer
    mov rsp, [gs:PROCESSOR_UserRsp]

    ; Switch GS back to User Base
    swapgs

    ; Return to User Mode
    ; If you are wondering why in the fuck did I put o64 in here and not sysretq??? Its because nasm doesnt support it, and its AT&T, in shorter words
    ; I had to debug this for an hour, and at the start it thought sysretq was a label so it corrupted the code to another function
    ; yeah this is not fun..
    o64 sysret