; PROJECT:      MatanelOS Kernel
; LICENSE:      GNU GPL-3.0
; PURPOSE:      Pushing registers to the REGS struct, used for bugchecking.

global read_interrupt_frame
section .text
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