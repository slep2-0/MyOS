; PROJECT:      MatanelOS Kernel
; LICENSE:      GNU GPL-3.0
; PURPOSE:      Pushing registers to the REGS struct, used for bugchecking.

global read_context_frame
global read_interrupt_frame
section .text

read_context_frame:
    ; RDI = pointer to CTX_FRAME struct
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
    mov [rdi + 0x48], rdi ; Note: rdi is also input pointer (the argument)
    mov [rdi + 0x50], rsi
    mov [rdi + 0x58], rdx
    mov [rdi + 0x60], rcx
    mov [rdi + 0x68], rbx
    mov [rdi + 0x70], rax

    ; Move RSP.
    mov [rdi + 0x78], rsp

    ; Used to be other stuff here, now we have the INT_FRAME struct, so no longer. (separation)

    ret

read_interrupt_frame:
    ; RDI = pointer to INT_FRAME struct
    ; Save special registers into struct at RDI (regs) + offset.

    ; To signify this is not from an interrupt service routine, mark vector as maximum WORD (max unsigned 16 bit value).
    mov qword [rdi + 0x00], 0x000000000000FFFF ; since vector is uint64_t.
    
    ; Error code
    mov qword [rdi + 0x08], 0

    ; RIP - address AFTER call, so we fetch it with a trick.
    call .get_rip
.get_rip:
    pop rax
    mov [rdi + 0x10], rax

    ; CS
    mov ax, cs
    movzx rax, ax
    mov [rdi + 0x18], rax

    ; RFLAGS
    pushfq
    pop rax
    ; RAX is RFLAGS now, since RFLAGS gets pushed last.
    mov [rdi + 0x20], rax
    ret