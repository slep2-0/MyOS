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
    cmp dword [gs:PROCESSOR_currentIrql], DISPATCH_LEVEL
    jae .BugCheckIrql

    ; Preserve the caller's flags before CLI. The saved continuation must
    ; resume with the original IF state, not with our handoff masking.
    pushfq

    ; TrapRegisters is shared with the interrupt-driven scheduler save path.
    ; Keep this publication atomic through the handoff to Schedule.
    cli

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

    ; Recover the pre-CLI flags and restore RSP to its function-entry value.
    pop     rbx
    mov     [rdi + TRAP_FRAME_rflags], rbx

    ; Save the caller's post-call RSP.  [rsp] is the return address, so
    ; restoring the entry RSP itself would replay that slot and leak eight
    ; bytes on every yield.
    lea     rbx, [rsp + 8]
    mov     [rdi + TRAP_FRAME_rsp], rbx

    ; Save RIP: the return address on the stack is at [rsp].
    mov     rbx, [rsp]
    mov     [rdi + TRAP_FRAME_rip], rbx

    ; This continuation resumes after MsYieldExecution in CPL 0, even when the
    ; owning thread is a user thread. Record that fact explicitly so the
    ; scheduler never has to infer the required GS/IRET path from RIP.
    mov     qword [rdi + TRAP_FRAME_cs], KERNEL_CS

    ; Now call scheduler to pick another thread.
    ; scheduler must not return to this code; it should context-switch away.
    sub rsp, 8 ; RSP %16 == 0
    call    Schedule
    
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
