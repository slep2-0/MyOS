; * PROJECT:     MatanelOS Kernel (64-bit ISR Stubs)
; * PURPOSE:     64-bit compatible assembly stubs for interrupt handling.
BITS 64
DEFAULT REL

; Extern the ISR handler.
extern isr_handler64
;---------------------------------------------------------------------------
; Macro: DEFINE_ISR
; Creates an ISR entry for exception vectors 0-31
; Pushes error code (0 if none), vector number, then jumps to common stub
;---------------------------------------------------------------------------
%macro DEFINE_ISR 1
    global isr%1
isr%1:
    cli
%if (%1 == 8) || (%1 == 10) || (%1 == 11) || (%1 == 12) || (%1 == 13) || (%1 == 14) || (%1 == 17)
    ; CPU pushes error code
%else
    push    0
%endif
    push    %1
    jmp     isr_common_stub64
%endmacro
;---------------------------------------------------------------------------
; Macro: DEFINE_IRQ
; Creates an IRQ entry for IRQ vectors 0-15 mapped to 32-47
;---------------------------------------------------------------------------
%macro DEFINE_IRQ 1
    global irq%1
irq%1:
    cli
    push    0          ; Push dummy error code
    push    %1 + 32    ; Push IRQ number + 32
    jmp     isr_common_stub64
%endmacro
;---------------------------------------------------------------------------
; Common stub for all ISRs and IRQs in 64-bit long mode
; Saves registers, calls C handler, sends EOI, restores and returns
;---------------------------------------------------------------------------
global isr_common_stub64
isr_common_stub64:
    ; Save volatile registers
    push    rax
    push    rbx
    push    rcx
    push    rdx
    push    rsi
    push    rdi
    push    rbp
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15
    ; Stack now: ... [r15] [vec_num] [err_code] [old rflags] [old cs] [old rip] ...
    ; First argument (vector) into rdi, second (pointer to regs) into rsi
    mov     rdi, [rsp + 14*8]       ; vector number (after 14 pushes)
    lea     rsi, [rsp + 15*8]       ; pointer to REGISTERS struct on stack
    call    isr_handler64
    ; Clean up pushed vec and err: add 16 bytes
    add     rsp, 16
    ; Send EOI if IRQ
    cmp     rdi, 32
    jl      .no_eoi
    mov     al, 0x20
    out     0x20, al      ; master PIC
    cmp     rdi, 40
    jl      .no_slave
    out     0xA0, al      ; slave PIC
.no_slave:
.no_eoi:
    ; Restore registers in reverse
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rbp
    pop     rdi
    pop     rsi
    pop     rdx
    pop     rcx
    pop     rbx
    pop     rax
    sti
    iretq
;---------------------------------------------------------------------------
; Instantiate ISRs 0-31
;---------------------------------------------------------------------------
DEFINE_ISR 0
DEFINE_ISR 1
DEFINE_ISR 2
DEFINE_ISR 3
DEFINE_ISR 4
DEFINE_ISR 5
DEFINE_ISR 6
DEFINE_ISR 7
DEFINE_ISR 8
DEFINE_ISR 9
DEFINE_ISR 10
DEFINE_ISR 11
DEFINE_ISR 12
DEFINE_ISR 13
DEFINE_ISR 14
DEFINE_ISR 15
DEFINE_ISR 16
DEFINE_ISR 17
DEFINE_ISR 18
DEFINE_ISR 19
DEFINE_ISR 20
DEFINE_ISR 21
DEFINE_ISR 22
DEFINE_ISR 23
DEFINE_ISR 24
DEFINE_ISR 25
DEFINE_ISR 26
DEFINE_ISR 27
DEFINE_ISR 28
DEFINE_ISR 29
DEFINE_ISR 30
DEFINE_ISR 31
;---------------------------------------------------------------------------
; Instantiate IRQs 0-15
;---------------------------------------------------------------------------
DEFINE_IRQ 0
DEFINE_IRQ 1
DEFINE_IRQ 2
DEFINE_IRQ 3
DEFINE_IRQ 4
DEFINE_IRQ 5
DEFINE_IRQ 6
DEFINE_IRQ 7
DEFINE_IRQ 8
DEFINE_IRQ 9
DEFINE_IRQ 10
DEFINE_IRQ 11
DEFINE_IRQ 12
DEFINE_IRQ 13
DEFINE_IRQ 14
DEFINE_IRQ 15