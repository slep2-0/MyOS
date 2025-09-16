; PROJECT:      MatanelOS Kernel
; LICENSE:      GPLv3
; PURPOSE:      Thread sleeping function
section .text
BITS 64
DEFAULT REL
extern Schedule

; void MtSleepCurrentThread(void);
global MtSleepCurrentThread
MtSleepCurrentThread:
    push rax ; save to stack
    
    ; rax := pointer to currentThread
    mov rax, gs:[0]
    mov rax, [rax + 0x10]

    ; save general-purpose registers into thread ctx
    mov     [rax + 0x00], r15
    mov     [rax + 0x08], r14
    mov     [rax + 0x10], r13
    mov     [rax + 0x18], r12
    mov     [rax + 0x20], r11
    mov     [rax + 0x28], r10
    mov     [rax + 0x30], r9
    mov     [rax + 0x38], r8
    mov     [rax + 0x40], rbp
    mov     [rax + 0x48], rdi
    mov     [rax + 0x50], rsi
    mov     [rax + 0x58], rdx
    mov     [rax + 0x60], rcx
    mov     [rax + 0x68], rbx
    pop qword [rax + 0x70]

    ; save RSP
    mov     [rax + 0x78], rsp

    ; save RIP: the return address on the stack is at [rsp] (caller pushed it when it called MtSleepCurrentThread)
    mov     rbx, [rsp]          ; rbx = address after the `call MtSleepCurrentThread` in caller
    mov     [rax + 0x80], rbx

    ; Now call scheduler to pick another thread.
    ; scheduler must not return to this code; it should context-switch away.
    sub rsp, 8 ; RSP %16 == 0
    call    Schedule
    add rsp, 8 ; I mean, it shouldn't return, but do it anyways.

    ; never returns here
    int 18 ; Machine Check Exception.