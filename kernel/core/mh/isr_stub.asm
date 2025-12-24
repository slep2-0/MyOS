; * PROJECT:     MatanelOS Kernel (64-bit ISR Stubs)
; * PURPOSE:     64-bit compatible assembly stubs for interrupt handling.
BITS 64
DEFAULT REL

%include "offsets.inc"

; Extern the ISR handler.
extern MhHandleInterrupt

; Extern the DPC handler.
extern MeRetireDPCs

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
    ; cpu pushes error code
%else
    push    0
%endif
    push    %1 ; Push Vector Exception Number (vec_num).
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
    push    %1 + 32    ; Push IRQ number + 32 (vec_num)
    jmp     isr_common_stub64
%endmacro

; ISR Routines with names:

; ---------------------------------------------
; APC ISR Stub
; ---------------------------------------------
global isr_apc
isr_apc:
    cli
    push 0 ; Dummy Error Code
    push VECTOR_APC ; This now expands to something something (statically)
    jmp isr_common_stub64

; ---------------------------------------------
; DPC ISR Stub
; ---------------------------------------------
global isr_dpc
isr_dpc:
    cli
    push 0              ; Dummy error code
    push VECTOR_DPC     ; This now expands to 192 (0xC0) statically
    jmp isr_common_stub64

; ---------------------------------------------
; IPI ISR Stub
; ---------------------------------------------
global isr_ipi
isr_ipi:
    cli
    push 0              ; Dummy error code
    push VECTOR_IPI     ; This now expands to 240 (0xF0) statically
    jmp isr_common_stub64

; ---------------------------------------------
; LAPIC_CLOCK ISR Stub
; ---------------------------------------------
global isr_clock
isr_clock:
    cli
    push 0 ; Dummy
    push VECTOR_CLOCK
    jmp isr_common_stub64

;---------------------------------------------------------------------------
; Common stub for all ISRs and IRQs in 64-bit long mode
; Stack layout after entry:
; [rsp + 0]   = vector number (last pushed by macro)
; [rsp + 8]   = error code (pushed by cpu or dummy 0)
; [rsp + 16]  = RIP (pushed by cpu)
; [rsp + 24]  = CS (pushed by cpu)
; [rsp + 32]  = RFLAGS (pushed by cpu)
; [rsp + 40]  = RSP (pushed always if x86-64)
; [rsp + 48]  = SS (pushed always if x86-64)
;---------------------------------------------------------------------------
global isr_common_stub64

isr_common_stub64:
    ; Execute SWAPGS if we came from user mode
    test byte [rsp + 24], 3
    jnz .do_swap

    ; We came from kernel mode, check if IA32_KERNEL_GS_BASE holds a kernel pointer
    ; If it does, it means we are in a GS gap, where the REAL gs is in there
    ; And we are in the user ptr.
    push rax
    push rcx
    push rdx

    mov ecx, IA32_KERNEL_GS_BASE
    rdmsr

    ; If the high 32bits are non zero, it means KERNEL_GS_BASE holds a kernel ptr (our CPU ptr)
    test edx, edx
    jz .kernel_is_safe
    swapgs

.kernel_is_safe:
    pop rdx
    pop rcx
    pop rax
    jmp .skip_swapgs

.do_swap:
    swapgs

.skip_swapgs:
    ; Save all general purpose registers
    ; Push in reverse order so TRAP_FRAME struct matches
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
    
; Set up parameters for C function call
.gustavofring_ascendz_ofirs_mcdonalds_4life:

    ; First parameter (vector number) in RDI
    mov     rdi, [rsp + 120]        ; vector number

.welcome_to_los_santos_2
    ; RSI is the second parameter in the System V ABI calling convention. It is our TRAP_FRAME, its the start of the stack basically. (first is r15, so like in the struct)
    mov     rsi, rsp

.begin_call
    
    ; Call C interrupt handler
    sub     rsp, 8
    call    MhHandleInterrupt
    add     rsp, 8

extern Schedule

%ifndef MT_NO_PREEMPTION
    ; DPC Revision, just check for schedule (DPC Retirement in MhHandleInterrupt)
    jmp .check_for_schedule

.check_for_schedule
    ; (if we are at DISPATCH_LEVEL schedulerEnabled should be false)
    ; Now let's check if the scheduler is enabled, so we can't pre-empt the current running thread, even if it's timeslice has expired.
    cmp byte [gs:PROCESSOR_schedulerEnabled], 0 ; Changed to direct comparison, as the load from before loaded additional 7 bytes after schedulerEnabled, which corrupted it, I hate silent bugs.
    jz .exit

    ; Check if we need to schedule, by fetching the current CPU schedulePending flag.
    cmp byte [gs:PROCESSOR_schedulePending], 0
    jz .exit ; No schedule pending...

    ; All DPCs retired, and a schedule is pending, Schedule. (and clear the pending flag, so we dont always re-enter)
    mov byte [gs:PROCESSOR_schedulePending], 0
%else
    ; Kernel compiled with no preemption, jump directly to exit.
    jmp .exit
%endif

; scheduler routine
.linkinpark:
    ; Schedule now. - pop all gprs, remove vector and err code, and pop 5 qwords that the CPU pushed since we will 100% not return.
    pop    r15
    pop    r14
    pop    r13
    pop    r12
    pop    r11
    pop    r10
    pop    r9
    pop    r8
    pop    rbp
    pop    rdi
    pop    rsi
    pop    rdx
    pop    rcx
    pop    rbx
    pop    rax

    ; remove vector and err code from the stack
    add rsp, 16

    ; pop the 5 qwords the CPU pushed.
    add rsp, 40

    ; Swapgs should be handled here by the scheduler routine, if we switch to another kernel thread, it wont execute swapgs, else it would (in restore_user_context)
    jmp Schedule ; Changed to jmp instruction, the function is a NORETURN
    int 8 ; Double Fault if we reached here, which we should never.

.exit:
    cli
    ; cleanup the IST stack (in older versions, it didnt and it could have overflown overtime, and probably would have with continuous thread use.)
    ; first pop all gprs
    pop    r15
    pop    r14
    pop    r13
    pop    r12
    pop    r11
    pop    r10
    pop    r9
    pop    r8
    pop    rbp
    pop    rdi
    pop    rsi
    pop    rdx
    pop    rcx
    pop    rbx
    pop    rax

    ; remove vector and err code from the stack
    add rsp, 16

    ; We must check if we came from user mode in order to execute swapgs.
    test    qword [rsp + 8], 3    ; test low 2 bits (CS & 3)
    jz      .no_swap_back
    swapgs

.no_swap_back:
    ; Return from interrupt. pops all CPU pushed regs from the stack back (5 qwords.)
    iretq

;---------------------------------------------------------------------------
; Instantiate ISRs 0-31 (cpu Exceptions)
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

; Custom ISR's
DEFINE_ISR 254 ; LAPIC Spurious Interrupt Vector

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