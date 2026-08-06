section .text progbits alloc exec nowrite align=16

extern MtpSaveLanguageContext
extern MtpRestoreLanguageContext

%define CONTEXT_RBX 0x00
%define CONTEXT_RBP 0x08
%define CONTEXT_R12 0x10
%define CONTEXT_R13 0x18
%define CONTEXT_R14 0x20
%define CONTEXT_R15 0x28
%define CONTEXT_RSP 0x30
%define CONTEXT_RIP 0x38
%define CONTEXT_SIZE 0x40
%define FRAME_SIZE   0x48

%define TOKEN_RBX 0x1122334455667788
%define TOKEN_RBP 0x2233445566778899
%define TOKEN_R12 0x33445566778899AA
%define TOKEN_R13 0x445566778899AABB
%define TOKEN_R14 0x5566778899AABBCC
%define TOKEN_R15 0x66778899AABBCCDD
%define TOKEN_LOCAL 0x778899AABBCCDDEE
%define RESTORE_VALUE 0x6124

global MtpRunLanguageContextAssemblyTest
MtpRunLanguageContextAssemblyTest:
    ; Preserve the test caller's nonvolatile state before installing tokens.
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    sub rsp, FRAME_SIZE

    mov rbx, TOKEN_RBX
    mov rbp, TOKEN_RBP
    mov r12, TOKEN_R12
    mov r13, TOKEN_R13
    mov r14, TOKEN_R14
    mov r15, TOKEN_R15

    mov rdi, rsp
    call MtpSaveLanguageContext
.saved_return:
    test eax, eax
    jnz .restored

    cmp qword [rsp + CONTEXT_RBX], rbx
    jne .fail_rbx_capture
    cmp qword [rsp + CONTEXT_RBP], rbp
    jne .fail_rbp_capture
    cmp qword [rsp + CONTEXT_R12], r12
    jne .fail_r12_capture
    cmp qword [rsp + CONTEXT_R13], r13
    jne .fail_r13_capture
    cmp qword [rsp + CONTEXT_R14], r14
    jne .fail_r14_capture
    cmp qword [rsp + CONTEXT_R15], r15
    jne .fail_r15_capture
    cmp qword [rsp + CONTEXT_RSP], rsp
    jne .fail_rsp_capture

    lea rax, [rel .saved_return]
    cmp qword [rsp + CONTEXT_RIP], rax
    jne .fail_rip_capture

    mov rax, TOKEN_LOCAL
    mov [rsp + CONTEXT_SIZE], rax

    xor ebx, ebx
    xor ebp, ebp
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d

    mov rdi, rsp
    mov esi, RESTORE_VALUE
    call MtpRestoreLanguageContext
    ud2

.restored:
    cmp eax, RESTORE_VALUE
    jne .fail_return_value

    mov rax, TOKEN_RBX
    cmp rbx, rax
    jne .fail_rbx_restore
    mov rax, TOKEN_RBP
    cmp rbp, rax
    jne .fail_rbp_restore
    mov rax, TOKEN_R12
    cmp r12, rax
    jne .fail_r12_restore
    mov rax, TOKEN_R13
    cmp r13, rax
    jne .fail_r13_restore
    mov rax, TOKEN_R14
    cmp r14, rax
    jne .fail_r14_restore
    mov rax, TOKEN_R15
    cmp r15, rax
    jne .fail_r15_restore
    mov rax, TOKEN_LOCAL
    cmp [rsp + CONTEXT_SIZE], rax
    jne .fail_stack_local

    xor eax, eax
    jmp .finish

.fail_rbx_capture:  mov eax, 1
    jmp .finish
.fail_rbp_capture:  mov eax, 2
    jmp .finish
.fail_r12_capture:  mov eax, 3
    jmp .finish
.fail_r13_capture:  mov eax, 4
    jmp .finish
.fail_r14_capture:  mov eax, 5
    jmp .finish
.fail_r15_capture:  mov eax, 6
    jmp .finish
.fail_rsp_capture:  mov eax, 7
    jmp .finish
.fail_rip_capture:  mov eax, 8
    jmp .finish
.fail_return_value: mov eax, 9
    jmp .finish
.fail_rbx_restore:  mov eax, 10
    jmp .finish
.fail_rbp_restore:  mov eax, 11
    jmp .finish
.fail_r12_restore:  mov eax, 12
    jmp .finish
.fail_r13_restore:  mov eax, 13
    jmp .finish
.fail_r14_restore:  mov eax, 14
    jmp .finish
.fail_r15_restore:  mov eax, 15
    jmp .finish
.fail_stack_local:  mov eax, 16

.finish:
    add rsp, FRAME_SIZE
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

global MtpCauseReadAccessViolation
global MtpReadAccessViolationInstruction
global MtpReadAccessViolationResume
MtpCauseReadAccessViolation:
MtpReadAccessViolationInstruction:
    ; The test filter redirects RIP past this deliberate no-access read.
    mov rax, [rdi]
MtpReadAccessViolationResume:
    mov eax, 0x6400
    ret

global MtpCauseIntegerDivideByZero
global MtpIntegerDivideInstruction
global MtpIntegerDivideResume
MtpCauseIntegerDivideByZero:
    mov eax, 1
    xor edx, edx
    xor ecx, ecx
MtpIntegerDivideInstruction:
    ; Divide by zero enters the real processor #DE path.
    div ecx
MtpIntegerDivideResume:
    mov eax, 0x6501
    ret

global MtpCauseInvalidOpcode
global MtpInvalidOpcodeInstruction
global MtpInvalidOpcodeResume
MtpCauseInvalidOpcode:
MtpInvalidOpcodeInstruction:
    ; UD2 enters the real processor #UD path without relying on bad code bytes.
    ud2
MtpInvalidOpcodeResume:
    mov eax, 0x6502
    ret

global MtpCausePrivilegedInstruction
global MtpPrivilegedInstruction
global MtpPrivilegedInstructionResume
MtpCausePrivilegedInstruction:
MtpPrivilegedInstruction:
    ; HLT at CPL3 enters #GP and must be classified as privileged.
    hlt
MtpPrivilegedInstructionResume:
    mov eax, 0x6503
    ret

global MtpCauseGeneralProtectionAccess
global MtpGeneralProtectionAccessInstruction
global MtpGeneralProtectionAccessResume
MtpCauseGeneralProtectionAccess:
MtpGeneralProtectionAccessInstruction:
    ; A noncanonical operand enters #GP rather than the page-fault path.
    mov rax, [rdi]
MtpGeneralProtectionAccessResume:
    mov eax, 0x6504
    ret
