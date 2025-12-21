    .text
    .global _start
_start:
    xor %rbp, %rbp
    call main
    /* TODO ExitProcess syscall */
.loop:
    jmp .loop
