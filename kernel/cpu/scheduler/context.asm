; PROJECT:      MatanelOS Kernel
; LICENSE:      GPLv3
; PURPOSE:      Registration context saving for a thread/process. (processes are threads internally)

; save_context(CTX_FRAME* dst)
global save_context
section .text
save_context:
    ; SysV: dst in rdi
    mov     rax, rdi        ; stash dst in rax

    ; push GPRs in CTX_FRAME order
    push    r15
    push    r14
    push    r13
    push    r12
    push    r11
    push    r10
    push    r9
    push    r8
    push    rbp
    push    rdi             ; original dst goes on stack, but we've stashed it
    push    rsi
    push    rdx
    push    rcx
    push    rbx
    push    rax             ; this push is the stashed dst, not the GPR we want!
    push    rsp             ; final push for rsp

    ; copy CTX_FRAME from [rsp] to [rax]
    mov     rsi, rsp        ; source
    mov     rdi, rax        ; destination (stashed)
    mov     rcx, 136        ; 17 registers × 8 bytes
    rep     movsb

    add     rsp, 136        ; pop all pushed GPRs
    ret

; restore_context(CTX_FRAME* src)
global restore_context
restore_context:
    ; SysV: src in rdi
    mov     rsi, rdi        ; source = &CTX_FRAME
    lea     rdi, [rsp - 136] ; target stack space = current_rsp - CTX_SZ
    mov     rcx, 136
    rep     movsb           ; copy CTX_FRAME onto the stack

    ; pop GPRs in reverse order (matching pushes)
    pop     rsp
    pop     rax
    pop     rbx
    pop     rcx
    pop     rdx
    pop     rsi
    pop     rdi
    pop     rbp
    pop     r8
    pop     r9
    pop     r10
    pop     r11
    pop     r12
    pop     r13
    pop     r14
    pop     r15

    iretq