; PROJECT:     MatanelOS Kernel
; LICENSE:     NONE
; PURPOSE:     Bootstrap to set up GDT, stack and call C kernel_main
[bits 64]

extern kernel_main
extern __stack_end
global _start

section .data
; Our own GDT - 5 entries
align 16
gdt_start:
    ; Entry 0: Null descriptor
    dq 0x0000000000000000
    
    ; Entry 1: Kernel Code Selector (0x08)
    ; Base=0, Limit=0xFFFFF, Present=1, DPL=0, Type=Code, Long=1
    dq 0x00AF9A000000FFFF
    
    ; Entry 2: Kernel Data Selector (0x10) 
    ; Base=0, Limit=0xFFFFF, Present=1, DPL=0, Type=Data
    dq 0x00CF92000000FFFF
    
    ; Entry 3: User Code Selector (0x18) - for future use
    ; Base=0, Limit=0xFFFFF, Present=1, DPL=3, Type=Code, Long=1
    dq 0x00AFFA000000FFFF
    
    ; Entry 4: User Data Selector (0x20) - for future use
    ; Base=0, Limit=0xFFFFF, Present=1, DPL=3, Type=Data
    dq 0x00CFF2000000FFFF
gdt_end:

; GDT descriptor
gdt_descriptor:
    dw gdt_end - gdt_start - 1    ; Limit
    dq gdt_start                  ; Base address

section .text
_start:
    mov al, 0xFF
    out 0x80, al

    lgdt [rel gdt_descriptor]

    push 0x08
    lea rax, [.reload_segments]
    push rax
    retfq

.reload_segments:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    lea rsp, [rel __stack_end]
    mov rbp, rsp

    call kernel_main

.halt:
    cli
    hlt
    jmp .halt