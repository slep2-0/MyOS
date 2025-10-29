; PROJECT:      MatanelOS Kernel
; LICENSE:      GPLv3
; PURPOSE:      Corrected context switching routine.
section .text
; void restore_context(TRAP_FRAME* src);
; System V ABI: src in RDI
global restore_context
restore_context:
    
    ; RDI = &CTX_FRAME
    mov   rax, rdi               ; RAX <- frame pointer
    
    ; 1 - Switch to saved stack FIRST
    mov   rsp, [rax + 0x78]      ; RSP <- saved stack pointer
    
    ; 2 - Push saved RIP onto the new stack
    push  qword [rax + 0x80]     ; Push saved RIP for ret

    push  qword [rax + 0x88]     ; Push saved RFLAGS
    popfq                        ; Pop into RFLAGS
    
    
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

; void restore_user_context(Thread* src);
; System V ABI: src in RDI
global restore_user_context
restore_user_context:
    ; We are in user mode, not only we restore registers, but we also switch CR3, and segments.
    ; First, switch into the process's CR3, since we are STILL in CPL 0, the kernel mapping will still hold true.
    mov rax, [rdi + 0xD8] ; Move into rax the ptr to ParentProcess of the thread.
    mov rax, [rax + 0x40] ; Move into rax the physical address of the CR3 of the process.
    mov cr3, rax ; Exchange.

    ; Now that we are in the user mapping, we must switch to the thread's registers.
    mov   rax, rdi ; TRAP_FRAME registers
    
    ; 2 - Push all saved interrupt registers into the stack for IRETQ
    push qword [rax + 0x90] ; SS
    push qword [rax + 0x78] ; RSP
    push qword [rax + 0x88] ; RFLAGS
    push qword [rax + 0x98] ; CS
    push qword [rax + 0x80] ; RIP
    ; Those are all of the registers needed for IRETQ.
    
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
    swapgs
    iretq