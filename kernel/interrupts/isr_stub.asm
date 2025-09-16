; * PROJECT:     MatanelOS Kernel (64-bit ISR Stubs)
; * PURPOSE:     64-bit compatible assembly stubs for interrupt handling.
BITS 64
DEFAULT REL

; Extern the ISR handler.
extern isr_handler64

; Extern the DPC handler.
extern RetireDPCs

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
    ; cpu0 pushes error code
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

;---------------------------------------------------------------------------
; Common stub for all ISRs and IRQs in 64-bit long mode
; Stack layout after entry:
; [rsp + 0]   = vector number (last pushed by macro)
; [rsp + 8]   = error code (pushed by cpu0 or dummy 0)
; [rsp + 16]  = RIP (pushed by cpu0)
; [rsp + 24]  = CS (pushed by cpu0)
; [rsp + 32]  = RFLAGS (pushed by cpu0)
; [rsp + 40]  = RSP (pushed by cpu0 if privilege change)
; [rsp + 48]  = SS (pushed by cpu0 if privilege change)
;---------------------------------------------------------------------------
global isr_common_stub64

isr_common_stub64:
    ; Save all general purpose registers
    ; Push in reverse order so CTX_FRAME struct matches
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
    
    ; THEN - Save the current frame into the CTX_FRAME of the thread.
.first_check
    mov rax, gs:[0]
    movzx eax, byte [rax + 0xC] ; cpu.schedulerEnabled
    test eax, eax
    jz .no_thread ; If schedulerEnabled false, jump to ISR setup.
    jmp .save_thread_ctx ; ELSE (schedulerEnabled TRUE), jump to saving the thread's context.

.save_thread_ctx
    mov     r11, gs:[0]          ; Use r11 as a safe temporary register
    mov     r11, [r11 + 16]      ; r11 -> currentThread
    test    r11, r11
    je      .no_thread

    ; Save all GPRs from the stack to the context frame
    ; This is safer than 'mov [rax+offset], register' because it saves the original values
    mov     rdi, r11             ; Destination: &thread->registers
    mov     rsi, rsp             ; Source: Stack top
    mov     rcx, 15              ; 15 QWORDs to copy (r15 -> rax)
    rep movsq

    ; --- Save RIP and RSP ---

    ; Always save RIP (it's at the same offset in both cases)
    mov     rax, [rsp + 136]     ; Get saved RIP from the stack
    mov     [r11 + 0x80], rax    ; Save to thread->registers.rip

    ; Check if the interrupt was from user mode (CS low bits are 3)
    movzx   eax, word [rsp + 144] ; Read saved CS
    and     eax, 3
    cmp     eax, 3
    je      .save_user_rsp

.save_kernel_rsp:
    ; KERNEL MODE: RSP was not pushed. We must CALCULATE it.
    ; Original RSP = current RSP + (15 GPRs) + (vec+err) + (rip+cs+rflags)
    ; Original RSP = RSP + 120 + 16 + 24 = RSP + 160
    lea     rax, [rsp + 160]
    mov     [r11 + 0x78], rax    ; Save calculated RSP to thread->registers.rsp
    jmp     .sleepingAwake

.save_user_rsp:
    ; USER MODE: RSP was pushed by hardware. We READ it from the stack.
    ; its at rsp + 0xA0, the same as kernel rsp.
    ; why am I doing this function then? because its for robustness
    ; as a developer, this extra 1 cycle wont change anything, and helps a lot to understand the offsets and whats going on in code.
    mov     rax, [rsp + 160]
    mov     [r11 + 0x78], rax    ; Save user RSP to thread->registers.rsp
    ; no need to jump, it falls through.
    
    ; We are done saving the thread's registers into it's intended CTX_FRAME struct, continue normal execution.

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
.sleepingAwake:
.no_thread:

    movzx eax, word [rsp + 0x90]   ; read saved CS
    and   eax, 3
    cmp   eax, 3
    je .gustavofring_ascendz_ofirs_mcdonalds_4life
    jmp .welcome_to_los_santos_2

.gustavofring_ascendz_ofirs_mcdonalds_4life:

    ; First parameter (vector number) in RDI
    mov     rdi, [rsp + 120]        ; vector number

    ; Second parameter (pointer to REGS struct) in RSI
    mov     rsi, rsp                ; pointer to start of pushed registers

    ; Third paramter (pointer to interrupt frame), in RDX.
    ; We use LEA because we want the address, not it's dereference. (we want the ptr, not dereferencing it basically)
    lea rdx, [rsp + 120]
    jmp .begin_call

.welcome_to_los_santos_2
    
    ; No priv change, mov at different offsets.
    ; (they are the same, FIXME)
    mov rdi, [rsp + 120]
    mov rsi, rsp
    lea rdx, [rsp + 120]

.begin_call
    ; Save vector number for EOI logic (function call may clobber RDI)
    mov     r10, rdi
    
    ; Call C interrupt handler
    call    isr_handler64
    
    ; Send EOI (End of Interrupt) if this was an IRQ (vector >= 32)
    cmp     r10, 32
    jl      .dpc
    
    ; Send EOI to master PIC
    mov     al, 0x20
    out     0x20, al
    
    ; If IRQ >= 8, also send EOI to slave PIC
    cmp     r10, 40
    jl      .no_slave_eoi
    out     0xA0, al
.no_slave_eoi:

extern Schedule

.dpc:

    ; Dispatch ALL DPCs. -- Side Note: DPCs that WILL NOT RETURN, do not get executed, instead they place a global flag.
    ; DPCs that don't return are very much - not common, I think the Scheduler DPC is the only one that does this.
    mov rax, gs:[0] ; currentCpu.schedulerEnabled -- (will also tell if IRQL >= DISPATCH_LEVEL if set.)
    movzx eax, byte [rax + 0xC]
    test eax, eax ; Test if SET.
    jz .check_for_schedule ; If it is NOT set (which means we were already at >= DISPATCH_LEVEL), skip the retiring of DPCs
    jmp .retire ; Else, we were below DISPATCH_LEVEL so we are allowed to retire DPCs.

.retire:
    mov rax, gs:[0] ; CPU ptr
    mov rax, [rax + 0x70] ; DeferredRoutineQueue.dpcQueueHead
    test rax, rax
    jz .check_for_schedule
    call RetireDPCs

.check_for_schedule
    
    ; dev note: there is no need to compare atomically, as this is the current CPU (and this is no longer a global variable, obviously as every CPU has different timeslice.)
    ; which is why I changed to atomic operations, then reverted.

    mov rax, gs:[0]
    cmp byte [rax + 0x60], 0
    jz .done

    ; clear it, so we don't re-enter.
    mov byte [rax + 0x60], 0
    
    ; cleanup the IST stack (in older versions, it didnt and it could have overflown overtime, and probably would have with continious thread use.)
    
    ; first pop all gprs
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

    ; remove vector and err code from the stack
    add rsp, 16

    ; check if it was a privilege change (when user mode comes, this is very important)
    movzx eax, word [rsp + 8]   ; read saved CS
    and   eax, 3
    cmp   eax, 3 ; compare with user
    jne .kernelm
    jmp .userm

.kernelm:
    add rsp, 24 ; kernel mode, 3 qwords pushed.
    jmp .linkinpark

.userm:
    add rsp, 40 ; user mode, 5 qwords pushed.

.linkinpark:

    ; Enable Interrupts
    sti

    ; Schedule now.
    call Schedule

    ; If Schedule was called, it would never return here, (and in restore_context, we switch RSPs so those pops and stack cleanup will happen anyway.)

.done:

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
; Instantiate ISRs 0-31 (cpu0 Exceptions)
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
DEFINE_ISR 239 ; LAPIC
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