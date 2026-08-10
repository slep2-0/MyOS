; * PROJECT:     MatanelOS Kernel (64-bit ISR Stubs)
; * PURPOSE:     64-bit compatible assembly stubs for interrupt handling.
BITS 64
DEFAULT REL

%include "offsets.inc"

; Extern the ISR handler.
extern MhHandleInterrupt

; Extern the DPC handler.
extern MeRetireDPCs

; True only when every STAC/CLAC use is valid and CR4.SMAP is enabled.
extern MeSmapEnabled

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
    ; RFLAGS has already been saved by the CPU.  Clear a user-controlled AC
    ; bit before any C handler touches kernel memory.  IRET restores the saved
    ; value, including when this interrupted a kernel-mode user-copy window.
    cmp byte [rel MeSmapEnabled], 0
    je .access_state_safe
    clac

.access_state_safe:
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

    ; If bit 63 is set, KERNEL_GS_BASE holds a higher-half kernel pointer
    ; (our CPU ptr), so we are in the small SWAPGS gap and must swap back.
    ; User TEB pointers can still have non-zero high32 bits, so testing EDX
    ; for non-zero here corrupts GS during interrupts inside syscalls.
    test edx, 0x80000000
    jz .kernel_is_safe
    ; Mark that this entry actually swapped. The vector is below the three
    ; scratch pushes at this point.
    bts qword [rsp + 24], 63
    swapgs

.kernel_is_safe:
    pop rdx
    pop rcx
    pop rax
    jmp .skip_swapgs

.do_swap:
    ; Bit 63 is not part of an x86 vector. Use it as an entry-local marker so
    ; exit reverses exactly the SWAPGS performed here rather than guessing from
    ; the saved CS after state may have changed.
    bts qword [rsp], 63
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
    and     edi, 0xFF               ; remove the SWAPGS marker

.welcome_to_los_santos_2:
    ; RSI is the second parameter in the System V ABI calling convention. It is our TRAP_FRAME, its the start of the stack basically. (first is r15, so like in the struct)
    mov     rsi, rsp

.begin_call:
    
    ; Call C interrupt handler
    mov r12, rsp
    and rsp, -16     ; Force 16-byte alignment safely
    call MhHandleInterrupt
    mov rsp, r12     ; Restore RSP to the trap frame

extern Schedule

%ifndef MT_NO_PREEMPTION
    ; DPC Revision, just check for schedule (DPC Retirement in MhHandleInterrupt)
    jmp .check_for_schedule

.check_for_schedule:
    ; MhHandleInterrupt restores the interrupted IRQL before returning here.
    ; DISPATCH_LEVEL and above are the sole preemption barrier.
    cmp dword [gs:PROCESSOR_currentIrql], DISPATCH_LEVEL
    jae .exit

    ; Check if we need to schedule, by fetching the current CPU schedulePending flag.
    cmp byte [gs:PROCESSOR_schedulePending], 0
    jz .exit ; No schedule pending...

    ; In 64-bit mode, hardware pushes SS:RSP for every interrupt frame. Preserve
    ; the established immediate scheduling points: returns from user mode,
    ; clock interrupts, and page faults. Other kernel events defer the pending
    ; request until the next clock tick.
    test byte [rsp + TRAP_FRAME_cs], 3
    jnz .schedule_frame_is_complete

    mov rax, [rsp + TRAP_FRAME_vector]
    and eax, 0xFF
    cmp eax, VECTOR_CLOCK
    je .schedule_frame_is_complete
    cmp eax, EXCEPTION_PAGE_FAULT
    jne .exit

.schedule_frame_is_complete:
    cmp qword [gs:PROCESSOR_currentThread], 0
    je .exit

    ; All DPCs retired, and a schedule is pending, Schedule. (and clear the pending flag, so we dont always re-enter)
    mov byte [gs:PROCESSOR_schedulePending], 0
%else
    ; Kernel compiled with no preemption, jump directly to exit.
    jmp .exit
%endif

; scheduler routine
.linkinpark:
    ; Capture at the actual switch point. A DPC can request a schedule after
    ; MiHandleTimer ran, so saving only in the C timer handler leaves a stale
    ; continuation in the thread.
    mov rsi, rsp
    mov rdi, [gs:PROCESSOR_currentThread]
    add rdi, ITHREAD_TrapRegisters
    mov rcx, SIZEOF_TRAP_FRAME / 8
    cld
    rep movsq

    ; This path is restricted to complete frames, so reclaim all 22 qwords.
    add rsp, SIZEOF_TRAP_FRAME

    ; JMP does not push a return address. Supply one alignment slot so the C
    ; scheduler receives the SysV function-entry alignment it expects.
    push qword 0
    jmp Schedule
    int 8 ; Double Fault if we reached here, which we should never.

extern MePrepareUserDispatchForReturn

.exit:
    cli

    ; Make sure that if there are user APCs in the list, we queue them and execute them when returning to user code
    ; If we do not return to user code, the function will not request a software interrupt
    ; Else, the moment we return to user code from iretq, the interrupt should execute.
    
    ; If there are any exceptions o to be delivered, the function will redirect execution to the MTDLL Exception dispatcher
    ; and attempt handling of the exception.
    ; Same goes for APCs, redirection will be applied to MeUserApcDispatcher
    mov r12, rsp
    mov rdi, r12
    and rsp, -16
    call MePrepareUserDispatchForReturn
    mov rsp, r12

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

    ; Reverse SWAPGS only when this entry marked that it performed one.
    bt      qword [rsp], 63
    jnc     .no_swap_back
    swapgs

.no_swap_back:
    ; remove vector and err code from the stack
    add rsp, 16

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
