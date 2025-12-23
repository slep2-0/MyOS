    .text
    .global _start
_start:
    xor %rbp, %rbp

    /* Align the stack to 16 bytes (so syscalls using ProbeForRead wont fail) */
    and $-16, %rsp

    call main
    /* Main Returned, terminate the current process. */
    mov $0xFFFFFFFF, %rdi /* ; -1 in int32_t (signed) (MtCurrentProcess())
    ; Lets check what main returned, if it returned 0 it is MT_SUCCESS, else, it is MT_GENERAL_FAILURE.
    ; MTDLL should send to the processes that are waiting on a kill port or whatever it is the return value MAIN returned.
    */
    cmp $0, %rax
    je .set_success
    jmp .set_general_failure

.set_general_failure:
    mov $0xC0000009, %rsi
    jmp .make_syscall

.set_success:
    mov $0, %rsi

.make_syscall:
    call MtTerminateProcess

.loop:
    jmp .loop