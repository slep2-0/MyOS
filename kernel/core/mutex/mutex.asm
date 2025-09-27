; PROJECT:      MatanelOS Kernel
; LICENSE:      GPLv3
; PURPOSE:      Thread sleeping function
section .text
BITS 64
DEFAULT REL
extern Schedule

; void MtSleepCurrentThread(void);
global MtSleepCurrentThread
; void MtSleepCurrentThread(CTX_FRAME* threadRegisters)
MtSleepCurrentThread:
    ; rdi - threads registers
    ; save general-purpose registers into thread ctx
    mov     [rdi + 0x00], r15
    mov     [rdi + 0x08], r14
    mov     [rdi + 0x10], r13
    mov     [rdi + 0x18], r12
    mov     [rdi + 0x20], r11
    mov     [rdi + 0x28], r10
    mov     [rdi + 0x30], r9
    mov     [rdi + 0x38], r8
    mov     [rdi + 0x40], rbp
    mov     [rdi + 0x48], rdi ; Its okay to save RDI as the ptr to the threads struct, as that is what it got set to at the start anyawy.
    mov     [rdi + 0x50], rsi
    mov     [rdi + 0x58], rdx
    mov     [rdi + 0x60], rcx
    mov     [rdi + 0x68], rbx
    mov     [rdi + 0x70], rax

    ; save RSP
    mov     [rdi + 0x78], rsp

    ; save RIP: the return address on the stack is at [rsp] (caller pushed it when it called MtSleepCurrentThread)
    mov     rbx, [rsp]          ; rbx = address after the `call MtSleepCurrentThread` in caller
    mov     [rdi + 0x80], rbx

    ; save RFLAGS
    pushfq  ; push RFLAGS onto the stack
    pop     rbx ; pop it into RBX
    mov     [rdi + 0x88], rbx   ; store it.

    ; Now call scheduler to pick another thread.
    ; scheduler must not return to this code; it should context-switch away.
    sub rsp, 8 ; RSP %16 == 0
    call    Schedule
    add rsp, 8 ; I mean, it shouldn't return, but do it anyways.

    ; never returns here
    int 18 ; Machine Check Exception.