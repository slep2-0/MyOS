; PROJECT:      MatanelOS Kernel
; LICENSE:      GPLv3
; PURPOSE:      CPUID Checks and Functions.
[bits 64]

; bool checkcpuid(void);
global checkcpuid
checkcpuid:
    pushfq                     ; save original RFLAGS
    pop rax                    ; RAX = original RFLAGS
    mov rcx, rax               ; RCX = original (for later restore)
    xor rax, 0x00200000        ; flip ID bit
    push rax
    popfq                      ; try to load modified RFLAGS
    pushfq
    pop rax                    ; RAX = modified RFLAGS
    push rcx
    popfq                      ; restore original RFLAGS
    xor rax, rcx               ; see if ID bit changed
    shr rax, 21                ; move ID bit to bit 0
    and rax, 1
    ret