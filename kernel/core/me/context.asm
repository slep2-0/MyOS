; PROJECT:      MatanelOS Kernel
; LICENSE:      GPLv3
; PURPOSE:      Corrected context switching routine.

%include "offsets.inc"

section .text
; void restore_context(TRAP_FRAME* src);
; System V ABI: src in RDI
global restore_context
restore_context:
    
    ; RDI = &CTX_FRAME
    mov   rax, rdi               ; RAX <- frame pointer
    
    ; 1 - Switch to saved stack FIRST
    mov   rsp, [rax + TRAP_FRAME_rsp]      ; RSP <- saved stack pointer
    
    ; 2 - Push saved RIP onto the new stack
    push  qword [rax + TRAP_FRAME_rip]     ; Push saved RIP for ret

    push  qword [rax + TRAP_FRAME_rflags]     ; Push saved RFLAGS
    popfq                        ; Pop into RFLAGS
    
    
    ; 3 - Restore all general-purpose registers
    mov   r15, [rax + TRAP_FRAME_r15]
    mov   r14, [rax + TRAP_FRAME_r14]
    mov   r13, [rax + TRAP_FRAME_r13]
    mov   r12, [rax + TRAP_FRAME_r12]
    mov   r11, [rax + TRAP_FRAME_r11]
    mov   r10, [rax + TRAP_FRAME_r10]
    mov    r9, [rax + TRAP_FRAME_r9]
    mov    r8, [rax + TRAP_FRAME_r8]
    mov   rbp, [rax + TRAP_FRAME_rbp]
    mov   rdi, [rax + TRAP_FRAME_rdi]
    mov   rsi, [rax + TRAP_FRAME_rsi]
    mov   rdx, [rax + TRAP_FRAME_rdx]
    mov   rcx, [rax + TRAP_FRAME_rcx]
    mov   rbx, [rax + TRAP_FRAME_rbx]

    ; 4 - Finally restore RAX itself
    mov   rax, [rax + TRAP_FRAME_rax]      ; RAX <- saved RAX

    ; 5 - Return to saved RIP (pops from stack)
    ret

; void restore_user_context(PETHREAD Thread);
global restore_user_context
restore_user_context:
    ; We are in user mode, not only we restore registers, but we also switch CR3, and segments.
    ; First, switch into the process's CR3, since we are STILL in CPL 0, the kernel mapping will still hold true.
    mov rax, [rdi + ETHREAD_ParentProcess]
    mov rax, [rax + IPROCESS_PageDirectoryPhysical]
    mov cr3, rax ; Exchange.

    ; Now that we are in the user mapping, we must switch to the thread's registers.
    mov   rax, rdi ; TRAP_FRAME registers
    
    ; 2 - Push all saved interrupt registers into the stack for IRETQ
    push qword [rax + TRAP_FRAME_ss] ; SS
    push qword [rax + TRAP_FRAME_rsp] ; RSP
    push qword [rax + TRAP_FRAME_rflags] ; RFLAGS
    push qword [rax + TRAP_FRAME_cs] ; CS
    push qword [rax + TRAP_FRAME_rip] ; RIP
    ; Those are all of the registers needed for IRETQ.
    
    ; 3 - Restore all general-purpose registers
    mov   r15, [rax + TRAP_FRAME_r15]
    mov   r14, [rax + TRAP_FRAME_r14]
    mov   r13, [rax + TRAP_FRAME_r13]
    mov   r12, [rax + TRAP_FRAME_r12]
    mov   r11, [rax + TRAP_FRAME_r11]
    mov   r10, [rax + TRAP_FRAME_r10]
    mov    r9, [rax + TRAP_FRAME_r9]
    mov    r8, [rax + TRAP_FRAME_r8]
    mov   rbp, [rax + TRAP_FRAME_rbp]
    mov   rdi, [rax + TRAP_FRAME_rdi]
    mov   rsi, [rax + TRAP_FRAME_rsi]
    mov   rdx, [rax + TRAP_FRAME_rdx]
    mov   rcx, [rax + TRAP_FRAME_rcx]
    mov   rbx, [rax + TRAP_FRAME_rbx]

    ; 4 - Finally restore RAX itself
    mov   rax, [rax + TRAP_FRAME_rax]      ; RAX <- saved RAX
    ;swapgs
    iretq