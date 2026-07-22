bits 64

section .text

global memcpy
memcpy:
    mov rax, rdi
    mov rcx, rdx
    rep movsb
    ret

global memset
memset:
    mov r8, rdi
    mov rcx, rdx
    mov al, sil
    rep stosb
    mov rax, r8
    ret

global memmove
memmove:
    mov rax, rdi
    test rdx, rdx
    jz .move_done
    cmp rdi, rsi
    jbe .move_forward
    lea r8, [rsi + rdx]
    cmp rdi, r8
    jae .move_forward
    lea rdi, [rdi + rdx - 1]
    lea rsi, [rsi + rdx - 1]
    mov rcx, rdx
    std
    rep movsb
    cld
    ret
.move_forward:
    mov rcx, rdx
    rep movsb
.move_done:
    ret

global memcmp
memcmp:
    xor eax, eax
    test rdx, rdx
    jz .compare_done
.compare_loop:
    movzx eax, byte [rdi]
    movzx ecx, byte [rsi]
    sub eax, ecx
    jne .compare_done
    inc rdi
    inc rsi
    dec rdx
    jne .compare_loop
    xor eax, eax
.compare_done:
    ret
