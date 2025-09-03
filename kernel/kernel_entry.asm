[bits 64]
default rel

extern kernel_main
extern __stack_end
global _start

section .data
align 16
gdt_start:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF  ; kernel code: L=1, D=0
    dq 0x00CF92000000FFFF  ; kernel data
    dq 0x00AFFA000000FFFF  ; user code
    dq 0x00CFF2000000FFFF  ; user data
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dq gdt_start

section .text
_start:
    mov  al, 0xFF
    out  0x80, al

    ; Signify that we have reached the entrypoint for debugging. 
    mov rax, 0xFFFFFFFACCE55
    mov dr2, rax

    lgdt [gdt_descriptor]

    push 0x08
    lea  rax, [.reload_segments]
    push rax
    retfq

.reload_segments:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax

    mov rsp, [rdi + 0x38]
    mov rbp, rsp

    lea  rax, [kernel_main]   ; or: mov rax, kernel_main -- RDI is the BOOT_INFO ptr.
    call rax

.halt:
    cli
    hlt
    jmp  .halt
