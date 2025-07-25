; PROJECT:      MatanelOS Kernel
; LICENSE:      GNU GPL-3.0
; PURPOSE:      Pushing registers to the REGS struct, used for bugchecking.

global read_registers
section .text

read_registers:
    ; RDI = pointer to REGS struct
    ; Save general-purpose registers into struct at RDI (regs) + offset.
    mov [rdi + 0x00], r15
    mov [rdi + 0x08], r14
    mov [rdi + 0x10], r13
    mov [rdi + 0x18], r12
    mov [rdi + 0x20], r11
    mov [rdi + 0x28], r10
    mov [rdi + 0x30], r9
    mov [rdi + 0x38], r8

    mov [rdi + 0x40], rbp
    mov [rdi + 0x48], rdi ; Note: rdi is also input pointer
    mov [rdi + 0x50], rsi
    mov [rdi + 0x58], rdx
    mov [rdi + 0x60], rcx
    mov [rdi + 0x68], rbx
    mov [rdi + 0x70], rax

    ; Special registers: vector, error_code, RIP, CS, RFLAGS
    ; vector = 0xFFFF
    mov word [rdi + 0x78], 0xFFFF
    mov word [rdi + 0x7A], 0x0000  ; zero upper word of 64-bit vector

    ; error_code = 0
    mov qword [rdi + 0x80], 0

    ; RIP = address after call (get it with call/pop trick)
    call .get_rip
.get_rip:
    pop rax
    mov [rdi + 0x88], rax

    ; CS
    mov ax, cs
    movzx rax, ax
    mov [rdi + 0x90], rax

    ; RFLAGS
    pushfq
    pop rax
    mov [rdi + 0x98], rax

    ret