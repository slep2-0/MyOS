[bits 32]

; Multiboot 2 header must be in the first 8KB of the binary
section .multiboot
    align 8
    dd 0xE85250D6           ; magic number
    dd 0x00010003           ; flags (align modules + memory info)
    dd -(0xE85250D6 + 0x00010003)  ; checksum

section .text
    global _start
    extern kernel_main

_start:
    call kernel_main
    hlt
