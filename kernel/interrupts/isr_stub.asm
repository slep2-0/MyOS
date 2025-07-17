; * PROJECT:     MatanelOS Kernel
; * LICENSE:     NONE
; * PURPOSE:	 Assembly implmentation to forward all 
[bits 32]


extern isr_common_stub

%macro DEFINE_ISR_NO_ERRCODE 1
global isr%1
isr%1:
    cli
    push dword 0
    push dword %1
    jmp isr_common_stub
%endmacro

%macro DEFINE_ISR_ERRCODE 1
global isr%1
isr%1:
    cli
    push dword %1
    jmp isr_common_stub
%endmacro


; CPU Exceptions 0-31 - Added implementation for error code ISR's, before it was a placeholder, now if we get a page fault we can know we got it.
DEFINE_ISR_NO_ERRCODE 0    ; #DE Divide Error (Divide-by-zero) — no error code
DEFINE_ISR_NO_ERRCODE 1    ; #DB Debug Exception — no error code
DEFINE_ISR_NO_ERRCODE 2    ; NMI Non-Maskable Interrupt — no error code
DEFINE_ISR_NO_ERRCODE 3    ; #BP Breakpoint Exception — no error code
DEFINE_ISR_NO_ERRCODE 4    ; #OF Overflow Exception — no error code
DEFINE_ISR_NO_ERRCODE 5    ; #BR BOUND Range Exceeded — no error code
DEFINE_ISR_NO_ERRCODE 6    ; #UD Invalid Opcode — no error code
DEFINE_ISR_NO_ERRCODE 7    ; #NM Device Not Available (No Math Coprocessor) — no error code
DEFINE_ISR_ERRCODE    8    ; #DF Double Fault — has error code
DEFINE_ISR_NO_ERRCODE 9    ; Coprocessor Segment Overrun (reserved/obsolete) — no error code
DEFINE_ISR_ERRCODE    10   ; #TS Invalid TSS — has error code
DEFINE_ISR_ERRCODE    11   ; #NP Segment Not Present — has error code
DEFINE_ISR_ERRCODE    12   ; #SS Stack-Segment Fault — has error code
DEFINE_ISR_ERRCODE    13   ; #GP General Protection Fault — has error code
DEFINE_ISR_ERRCODE    14   ; #PF Page Fault — has error code
DEFINE_ISR_NO_ERRCODE 15   ; Reserved — no error code
DEFINE_ISR_NO_ERRCODE 16   ; #MF x87 FPU Floating-Point Error — no error code
DEFINE_ISR_ERRCODE    17   ; #AC Alignment Check — has error code
DEFINE_ISR_NO_ERRCODE 18   ; #MC Machine Check — no error code (uses MSRs)
DEFINE_ISR_NO_ERRCODE 19   ; #XM SIMD Floating-Point Exception — no error code
DEFINE_ISR_NO_ERRCODE 20   ; Reserved — no error code
DEFINE_ISR_NO_ERRCODE 21   ; Reserved — no error code
DEFINE_ISR_NO_ERRCODE 22   ; Reserved — no error code
DEFINE_ISR_NO_ERRCODE 23   ; Reserved — no error code
DEFINE_ISR_NO_ERRCODE 24   ; Reserved — no error code
DEFINE_ISR_NO_ERRCODE 25   ; Reserved — no error code
DEFINE_ISR_NO_ERRCODE 26   ; Reserved — no error code
DEFINE_ISR_NO_ERRCODE 27   ; Reserved — no error code
DEFINE_ISR_NO_ERRCODE 28   ; Reserved — no error code
DEFINE_ISR_NO_ERRCODE 29   ; Reserved — no error code
DEFINE_ISR_NO_ERRCODE 30   ; Reserved — no error code
DEFINE_ISR_NO_ERRCODE 31   ; Reserved — no error code

; IRQs 0-15 map to vectors 32-47
%macro DEFINE_IRQ 1
global irq%1
irq%1:
	CLI
	PUSH DWORD %1+32 ; vector = IRQ# + 32
	PUSH DWORD 0 ; No error code.
	JMP isr_common_stub
%endmacro

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