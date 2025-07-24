; PROJECT:     MatanelOS Kernel
; LICENSE:     NONE
; PURPOSE:     Bootstrap to set up stack and call C kernel_main

bits 64
extern kernel_main
extern __stack_end
global _start

section .text
_start:
    ; The UEFI bootloader calls: KernelEntry(BOOT_INFO*)
    ; BOOT_INFO* is passed in RDI per the System V AMD64 ABI.
    
    ; (Optional) debug poke: light up QEMU ‘POST’ light
    mov     al, 0xFF
    out     0x80, al

    ; Set up our own stack
    ; __stack_end is defined in linker.ld as the top of the 8 KiB stack
    lea     rsp, [rel __stack_end]
    mov     rbp, rsp

    ; Call into C world: kernel_main(BOOT_INFO* in RDI)
    call    kernel_main

.halt:
    cli
    hlt
    jmp     .halt
