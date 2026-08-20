; PROJECT:      MatanelOS Kernel
; LICENSE:      GPLv3
; PURPOSE:      CPUID Checks and Functions.
[bits 64]

; bool checkcpuid(void);
global checkcpuid

; Routine description:
;     Tests whether the processor exposes the CPUID instruction by toggling
;     and checking the ID flag in RFLAGS.
;
; Arguments:
;     None.
;
; Return values:
;     RAX = 1 when CPUID is supported, or 0 otherwise.
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
