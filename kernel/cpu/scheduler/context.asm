; PROJECT:      MatanelOS Kernel
; LICENSE:      GPLv3
; PURPOSE:      Corrected context switching routines.
section .text

; void save_context(CTX_FRAME* dst);
; System V ABI: dst is in rdi
global save_context
save_context:
    ; CRITICAL: Save the original values of registers we'll use as temporaries
    mov   [rdi + 0x70], rax       ; Save original RAX first
    mov   [rdi + 0x68], rbx       ; Save original RBX first
    
    ; Now we can use RAX and RBX as working registers
    mov   rbx, rdi                ; RBX = context pointer
    
    ; Save RIP (return address from caller)
    mov   rax, [rsp]
    mov   [rbx + 0x80], rax       ; ctx->rip
    
    ; Save RSP (caller's stack pointer, before the call)
    lea   rax, [rsp + 8]          ; RSP + 8 to account for return address pushed by call
    mov   [rbx + 0x78], rax       ; ctx->rsp
    
    ; Save GPRs (RAX and RBX already saved above)
    mov   [rbx + 0x00], r15
    mov   [rbx + 0x08], r14
    mov   [rbx + 0x10], r13
    mov   [rbx + 0x18], r12
    mov   [rbx + 0x20], r11
    mov   [rbx + 0x28], r10
    mov   [rbx + 0x30], r9
    mov   [rbx + 0x38], r8
    mov   [rbx + 0x40], rbp
    mov   [rbx + 0x48], rdi       ; Save original RDI (the context pointer)
    mov   [rbx + 0x50], rsi
    mov   [rbx + 0x58], rdx
    mov   [rbx + 0x60], rcx
    
    ; RAX and RBX were already saved at the beginning
    ret

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