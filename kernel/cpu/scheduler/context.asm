; PROJECT:      MatanelOS Kernel
; LICENSE:      GPLv3
; PURPOSE:      Corrected context switching routines.

section .text

; void save_context(CTX_FRAME* dst);
; System V ABI: dst is in rdi
global save_context
save_context:
    mov   rbx, rdi

    ; Save RIP (return address from caller)
    mov   rax, [rsp]
    mov   [rbx + 0x80], rax       ; ctx->rip

    ; Save GPRs
    mov   [rbx + 0x00], r15
    mov   [rbx + 0x08], r14
    mov   [rbx + 0x10], r13
    mov   [rbx + 0x18], r12
    mov   [rbx + 0x20], r11
    mov   [rbx + 0x28], r10
    mov   [rbx + 0x30], r9
    mov   [rbx + 0x38], r8
    mov   [rbx + 0x40], rbp
    mov   [rbx + 0x48], rdi
    mov   [rbx + 0x50], rsi
    mov   [rbx + 0x58], rdx
    mov   [rbx + 0x60], rcx
    mov   [rbx + 0x68], rbx
    mov   [rbx + 0x70], rax
    mov   [rbx + 0x78], rsp       ; save RSP

    ret

; void restore_context(CTX_FRAME* src);
; System V ABI: src in RDI
global restore_context
restore_context:
    ; RDI = &CTX_FRAME
    mov   rax, rdi               ; RAX <- frame pointer

    ; 1 - restore all general‑purpose regs except RSP and RIP
    mov   r15, [rax + 0x00]
    mov   r14, [rax + 0x08]
    mov   r13, [rax + 0x10]
    mov   r12, [rax + 0x18]
    mov   r11, [rax + 0x20]
    mov   r10, [rax + 0x28]
    mov    r9, [rax + 0x30]
    mov    r8, [rax + 0x38]
    mov   rbp, [rax + 0x40]
    mov   rdi, [rax + 0x48]
    mov   rsi, [rax + 0x50]
    mov   rdx, [rax + 0x58]
    mov   rcx, [rax + 0x60]
    mov   rbx, [rax + 0x68]
    ; RAX will be moved into, but first we must restore the RIP.

    ; 2 - grab the saved RIP into a temp (we'll jump to it in a moment)
    mov   r11, [rax + 0x80]      ; R11 <- saved RIP

    ; 3 - switch the stack
    mov   rsp, [rax + 0x78]      ; RSP <- saved stack pointer

    ; 4 - finally restore RAX itself
    mov   rax, [rax + 0x70]      ; RAX <- saved RAX

    ; 5 - jump into the saved RIP
    jmp   r11