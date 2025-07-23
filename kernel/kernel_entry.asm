bits 64
default rel

extern kernel_main
global _start

section .text
_start:
    ; UEFI passes first argument (GOP_PARAMS*) in RCX, System V wants it in RDI
    mov     rdi, rcx

    mov al, 0xFF       ; Set I/O port just for debug
    out 0x80, al       ; Optional debug if emulating (QEMU sees this)

    call kernel_main

.halt:
    cli
    hlt
    jmp .halt
