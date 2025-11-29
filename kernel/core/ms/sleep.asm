; PROJECT:      MatanelOS Kernel
; LICENSE:      GPLv3
; PURPOSE:      Thread sleeping function
section .text
BITS 64
DEFAULT REL
extern Schedule
extern MeBugCheckEx
%include "offsets.inc"

global MsYieldExecution
; void MsYieldExecution(TRAP_FRAME* threadRegisters)
MsYieldExecution:
    ; Check current IRQL, if we are at DISPATCH_LEVEL or above, we bugcheck.
    mov r15, gs:[0]
    mov r15d, [r15 + PROCESSOR_currentIrql]          ; (MeGetCurrentProcessor()->currentIrql)
    cmp r15d, DISPATCH_LEVEL
    jae .BugCheckIrql

    ; rdi - threads registers
    ; save general-purpose registers into thread ctx
    mov     [rdi + TRAP_FRAME_r15], r15
    mov     [rdi + TRAP_FRAME_r14], r14
    mov     [rdi + TRAP_FRAME_r13], r13
    mov     [rdi + TRAP_FRAME_r12], r12
    mov     [rdi + TRAP_FRAME_r11], r11
    mov     [rdi + TRAP_FRAME_r10], r10
    mov     [rdi + TRAP_FRAME_r9], r9
    mov     [rdi + TRAP_FRAME_r8], r8
    mov     [rdi + TRAP_FRAME_rbp], rbp
    mov     [rdi + TRAP_FRAME_rdi], rdi ; Its okay to save RDI as the ptr to the threads struct, as that is what it got set to at the start anyawy.
    mov     [rdi + TRAP_FRAME_rsi], rsi
    mov     [rdi + TRAP_FRAME_rdx], rdx
    mov     [rdi + TRAP_FRAME_rcx], rcx
    mov     [rdi + TRAP_FRAME_rbx], rbx
    mov     [rdi + TRAP_FRAME_rax], rax

    ; save RSP
    mov     [rdi + TRAP_FRAME_rsp], rsp

    ; save RIP: the return address on the stack is at [rsp] (caller pushed it when it called MtSleepCurrentThread)
    mov     rbx, [rsp]          ; rbx = address after the `call MtSleepCurrentThread` in caller
    mov     [rdi + TRAP_FRAME_rip], rbx

    ; save RFLAGS
    pushfq  ; push RFLAGS onto the stack
    pop     rbx ; pop it into RBX
    mov     [rdi + TRAP_FRAME_rflags], rbx   ; store it.

    ; Now call scheduler to pick another thread.
    ; scheduler must not return to this code; it should context-switch away.
    sub rsp, 8 ; RSP %16 == 0
    call    Schedule
    add rsp, 8 ; I mean, it shouldn't return, but do it anyways.

    ; never returns here
    int 18 ; Machine Check Exception.

.BugCheckIrql:
    ; rdi, rsi, rdx, rcx, r8, r9.
    mov rdi, ATTEMPTED_SWITCH_FROM_DPC ; BugCheckCode
    mov rsi, rsp                       ; BugCheckParameter1
    mov rdx, NULL
    mov rcx, NULL
    mov r8, NULL
    call MeBugCheckEx