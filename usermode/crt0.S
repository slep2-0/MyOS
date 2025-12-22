    .text
    .global _start
_start:
    xor %rbp, %rbp

    /* Align the stack to 16 bytes (so syscalls using ProbeForRead wont fail) */
    and $-16, %rsp

    call main
    /* TODO ExitProcess syscall */
.loop:
    jmp .loop
