; PROJECT:      MatanelOS Kernel
; LICENSE:      GPLv3
; PURPOSE:      Corrected context switching routine.

%include "offsets.inc"

section .text
; void restore_context(PITHREAD Thread, PITHREAD PreviousThread);
; System V ABI: Thread in RDI, PreviousThread in RSI
global restore_context
restore_context:
    cli

    ; Kernel threads should run with live GS as the CPU pointer and the SWAPGS
    ; shadow also pointing at the CPU. If we scheduled here from a user
    ; interrupt, IA32_KERNEL_GS_BASE may still contain the previous user TEB.
    mov   rbx, [gs:PROCESSOR_self]
    mov   ecx, IA32_KERNEL_GS_BASE
    mov   rax, rbx
    mov   rdx, rax
    shr   rdx, 32
    wrmsr

    ; A kernel thread may have blocked while attached to another process.
    ; Restore the active APC process, not necessarily ETHREAD.ParentProcess.
    mov   rax, [rdi + ITHREAD_ApcState + APC_STATE_SavedApcProcess]
    mov   rax, [rax + EPROCESS_InternalProcess + IPROCESS_PageDirectoryPhysical]
    mov   cr3, rax

    ; In 64-bit mode IRETQ consumes SS:RSP even when CPL does not change.
    ; Build the complete five-qword frame below the saved post-call RSP.
    lea   rax, [rdi + ITHREAD_TrapRegisters]
    mov   rdx, [rax + TRAP_FRAME_rsp]
    mov   rsp, rdx

    ; The kernel stack has been replaced, we will now NULL out the previous thread ActiveProcessor
    ; Why do it when only we switched stacks? Because once the ActiveProcessor becomes NULL,
    ; another processor might start to use the thread, and so consume his stack, so we must be in a safe stack
    ; that no other processor might touch to continue.

    ; A NULL PreviousThread means there is no outgoing stack owner to release.
    test rsi, rsi
    jz .no_previous_thread

    ; RDI and RSI are both the same thread
    ; most likely this is the only thread to be enqueued in the system
    ; we must not null ourselves out
    cmp rsi, rdi
    je .no_previous_thread

    ; NULL out ActiveProcessor
    mov qword [rsi + ITHREAD_ActiveProcessor], 0

.no_previous_thread:
    push  KERNEL_SS
    push  rdx
    push  qword [rax + TRAP_FRAME_rflags]
    push  KERNEL_CS
    push  qword [rax + TRAP_FRAME_rip]

    ; Restore all general-purpose registers.
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

    mov   rax, [rax + TRAP_FRAME_rax]
    iretq

; void restore_user_context_to_user(PETHREAD Thread, PITHREAD PreviousThread);
; System V ABI: Thread in RDI, PreviousThread in RSI
global restore_user_context_to_user
restore_user_context_to_user:
    ; Restore a thread whose saved continuation is in CPL 3.
    cli

    ; Do not depend on the previous SWAPGS pairing here. A thread can block in a
    ; syscall with IA32_KERNEL_GS_BASE holding its user TEB, while a fresh user
    ; thread may have no useful value there at all. Set both sides explicitly:
    ; - KERNEL_GS_BASE must be the CPU pointer for the next syscall/interrupt.
    ; - live GS must be the user TEB by the time IRETQ lands in CPL 3.
    mov rbx, [gs:PROCESSOR_self]
    mov ecx, IA32_KERNEL_GS_BASE
    mov rax, rbx
    mov rdx, rax
    shr rdx, 32
    wrmsr

    mov rax, [rdi + ETHREAD_InternalThread + ITHREAD_ApcState + APC_STATE_SavedApcProcess]
    mov rax, [rax + EPROCESS_InternalProcess + IPROCESS_PageDirectoryPhysical]
    mov cr3, rax ; Exchange.

    ; Preserve the TEB before restoring RDI, then address the trap frame at its
    ; generated offset. ITHREAD now begins with a dispatcher header.
    mov   rdx, [rdi + ETHREAD_Teb]
    lea   rax, [rdi + ETHREAD_InternalThread + ITHREAD_TrapRegisters]

    ; Move from PreviousThread's stack onto NextThread's kernel stack.
    mov rsp, [rdi + ETHREAD_InternalThread + ITHREAD_KernelStack]

    ; The kernel stack has been replaced, we will now NULL out the previous thread ActiveProcessor
    ; Why do it when only we switched stacks? Because once the ActiveProcessor becomes NULL,
    ; another processor might start to use the thread, and so consume his stack, so we must be in a safe stack
    ; that no other processor might touch to continue.

    ; A NULL PreviousThread means there is no outgoing stack owner to release.
    test rsi, rsi
    jz .no_previous_thread_userion

    ; We must not NULL out our own thread ActiveProcessor
    lea rcx, [rdi + ETHREAD_InternalThread]
    cmp rsi, rcx
    je .no_previous_thread_userion

    ; NULL Out ActiveProcessor
    mov qword [rsi + ITHREAD_ActiveProcessor], 0

.no_previous_thread_userion:
    ; Build the privilege-changing IRETQ frame on the current kernel stack.
    push qword [rax + TRAP_FRAME_ss] ; SS
    push qword [rax + TRAP_FRAME_rsp] ; RSP
    push qword [rax + TRAP_FRAME_rflags] ; RFLAGS
    push qword [rax + TRAP_FRAME_cs] ; CS
    push qword [rax + TRAP_FRAME_rip] ; RIP
    ; Restore all general-purpose registers.
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

    ; Set live GS to this thread's TEB at the last practical point. The shadow
    ; was set to the CPU above, so the next SWAPGS has a deterministic pair.
    wrgsbase rdx

    mov   rdx, [rax + TRAP_FRAME_rdx]
    mov   rcx, [rax + TRAP_FRAME_rcx]
    mov   rbx, [rax + TRAP_FRAME_rbx]

    mov   rax, [rax + TRAP_FRAME_rax]

    ; Return to user mode.
    iretq

; void restore_user_context_to_kernel(PETHREAD Thread, PITHREAD PreviousThread);
; System V ABI: Thread in RDI, PreviousThread in RSI
global restore_user_context_to_kernel
restore_user_context_to_kernel:
    ; Resume a user-owned thread that blocked while still executing kernel
    ; syscall code. Live GS stays on the CPU; KERNEL_GS_BASE becomes the TEB
    ; that the syscall exit's SWAPGS must restore later.
    cli

    ; This path resumes a user thread while its saved RIP is still kernel-side
    ; code. Keep live GS as the CPU pointer, but restore the user GS shadow so
    ; the later SWAPGS on syscall/interrupt exit returns to the thread's TEB.
    mov rax, [rdi + ETHREAD_Teb]
    mov ecx, IA32_KERNEL_GS_BASE
    mov rdx, rax
    shr rdx, 32
    wrmsr

    mov rax, [rdi + ETHREAD_InternalThread + ITHREAD_ApcState + APC_STATE_SavedApcProcess]
    mov rax, [rax + EPROCESS_InternalProcess + IPROCESS_PageDirectoryPhysical]
    mov cr3, rax ; Exchange.

    ; ITHREAD begins with a dispatcher header, not the saved trap frame.
    lea   rax, [rdi + ETHREAD_InternalThread + ITHREAD_TrapRegisters]

    ; The saved RSP/RIP came from MsYieldExecution and describe a CPL 0
    ; continuation. Long-mode IRETQ still requires the complete frame.
    mov   rdx, [rax + TRAP_FRAME_rsp]
    mov   rsp, rdx

    ; The kernel stack has been replaced, we will now NULL out the previous thread ActiveProcessor
    ; Why do it when only we switched stacks? Because once the ActiveProcessor becomes NULL,
    ; another processor might start to use the thread, and so consume his stack, so we must be in a safe stack
    ; that no other processor might touch to continue.

    ; A NULL PreviousThread means there is no outgoing stack owner to release.
    test rsi, rsi
    jz .no_previous_thread_kernelios

    ; RDI is the selected ETHREAD while RSI is the outgoing ITHREAD.
    ; Do not release ownership when the scheduler selected the same thread.
    lea rcx, [rdi + ETHREAD_InternalThread]
    cmp rsi, rcx
    je .no_previous_thread_kernelios

    ; NULL out ActiveProcessor
    mov qword [rsi + ITHREAD_ActiveProcessor], 0

.no_previous_thread_kernelios:

    push  KERNEL_SS
    push  rdx
    push  qword [rax + TRAP_FRAME_rflags]
    push  KERNEL_CS
    push  qword [rax + TRAP_FRAME_rip]

    ; Restore all general-purpose registers.
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

    mov   rax, [rax + TRAP_FRAME_rax]

    ; IRET restores RIP, IF, and the saved post-call RSP together.
    iretq
