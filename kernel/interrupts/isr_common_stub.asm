; * PROJECT:     MatanelOS Kernel
; * LICENSE:     NONE
; * PURPOSE:	 Assembly Implementation to call the function for an interrupt.
[bits 32]

global isr_common_stub
extern isr_handler

[bits 32]
global isr_common_stub
extern isr_handler

isr_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs
    
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Stack layout at this point:
    ; [esp + 0]  = gs
    ; [esp + 4]  = fs  
    ; [esp + 8]  = es
    ; [esp + 12] = ds
    ; [esp + 16] = edi (from pusha)
    ; [esp + 20] = esi
    ; [esp + 24] = ebp
    ; [esp + 28] = esp (original)
    ; [esp + 32] = ebx
    ; [esp + 36] = edx
    ; [esp + 40] = ecx
    ; [esp + 44] = eax
    ; [esp + 48] = error_code (pushed by stub)
    ; [esp + 52] = vector_num (pushed by stub)
    ; [esp + 56] = eip (pushed by CPU)
    ; [esp + 60] = cs (pushed by CPU)
    ; [esp + 64] = eflags (pushed by CPU)
    
    ; Push pointer to register structure (esp)
    push esp
    
    ; Push vector number (it's at esp + 56 now, but esp + 52 before we pushed esp)
    push dword [esp + 56]  ; vector_num
    
    call isr_handler ; Call our isr_handler in isr.c
    add esp, 8            ; clean stack (2 parameters)
    
    ; Send EOI if IRQ
    mov eax, [esp + 52]   ; vector number
    cmp eax, 32
    jl .no_eoi
    mov al, 0x20
    out 0x20, al
    cmp eax, 40
    jl .no_slave
    out 0xA0, al
.no_slave:
.no_eoi:
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8            ; skip error code + vec num
    sti
    iretd