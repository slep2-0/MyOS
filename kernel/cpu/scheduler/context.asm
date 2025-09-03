; PROJECT:      MatanelOS Kernel
; LICENSE:      GPLv3
; PURPOSE:      Corrected context switching routine.
section .text

; void restore_context(CTX_FRAME* src);
; System V ABI: src in RDI
global restore_context
restore_context:
    
    ; RDI = &CTX_FRAME
    mov   rax, rdi               ; RAX <- frame pointer
    
    ; 1 - Switch to saved stack FIRST
    mov   rsp, [rax + 0x78]      ; RSP <- saved stack pointer
    
    ; 2 - Push saved RIP onto the new stack
    push  qword [rax + 0x80]     ; Push saved RIP for ret
    
    ; 3 - Restore all general-purpose registers
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
    
    ; 4 - Finally restore RAX itself
    mov   rax, [rax + 0x70]      ; RAX <- saved RAX

    ; 5 - Return to saved RIP (pops from stack)
    ret