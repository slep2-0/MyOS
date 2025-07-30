; * PROJECT:     MatanelOS Kernel (64-bit ISR Stubs)
; * PURPOSE:     64-bit compatible assembly stubs for interrupt handling.
BITS 64
DEFAULT REL

; Extern the ISR handler.
extern isr_handler64

; Extern the DPC handler.
extern DispatchDPC

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
; Stack layout after entry:
; [rsp + 0]   = vector number (last pushed by macro)
; [rsp + 8]   = error code (pushed by CPU or dummy 0)
; [rsp + 16]  = RIP (pushed by CPU)
; [rsp + 24]  = CS (pushed by CPU)
; [rsp + 32]  = RFLAGS (pushed by CPU)
; [rsp + 40]  = RSP (pushed by CPU if privilege change)
; [rsp + 48]  = SS (pushed by CPU if privilege change)
;---------------------------------------------------------------------------
global isr_common_stub64
isr_common_stub64:
    ; Save all general purpose registers
    ; Push in reverse order so REGS struct matches
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
    
    ; Stack layout after register pushes (15 registers * 8 bytes = 120 bytes):
    ; [rsp + 0]   = r15 (last pushed)
    ; [rsp + 8]   = r14
    ; ...
    ; [rsp + 112] = rax (first pushed)
    ; [rsp + 120] = vector number
    ; [rsp + 128] = error code
    ; [rsp + 136] = RIP
    ; [rsp + 144] = CS
    ; [rsp + 152] = RFLAGS
    ; [rsp + 160] = RSP (if privilege change)
    ; [rsp + 168] = SS (if privilege change)
    
    ; Set up parameters for C function call
    ; First parameter (vector number) in RDI
    mov     rdi, [rsp + 120]        ; vector number
    ; Second parameter (pointer to REGS struct) in RSI
    mov     rsi, rsp                ; pointer to start of pushed registers
    
    ; Save vector number for EOI logic (function call may clobber RDI)
    mov     r10, rdi
    
    ; Call C interrupt handler
    call    isr_handler64
    
    ; Send EOI (End of Interrupt) if this was an IRQ (vector >= 32)
    cmp     r10, 32
    jl      .no_eoi
    
    ; Send EOI to master PIC
    mov     al, 0x20
    out     0x20, al
    
    ; If IRQ >= 8, also send EOI to slave PIC
    cmp     r10, 40
    jl      .no_slave_eoi
    out     0xA0, al
.no_slave_eoi:

.no_eoi:

    ; Restore all general purpose registers in reverse order
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
    
    ; Clean up vector and error code from stack
    add     rsp, 16
    
    ; Re-enable interrupts and return
    sti
    iretq

;---------------------------------------------------------------------------
; Instantiate ISRs 0-31 (CPU Exceptions)
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
; Instantiate IRQs 0-15 (Hardware Interrupts)
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