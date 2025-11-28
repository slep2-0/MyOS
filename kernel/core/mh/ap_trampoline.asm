; ap_trampoline.asm
section .text
%define AP_TRAMP_PHYS 0x7000
%define AP_TRAMP_APMAIN_OFFSET 0x1000
%define AP_TRAMP_PML4_OFFSET 0x2000
%define COM_BASE 0x3F8

org AP_TRAMP_PHYS

[bits 16]

_start:
    cli
    cld

    ; Print 'R' to COM1 (signal AP reached)
    mov dx, COM_BASE
    mov al, 'R'
    out dx, al

    ; zero segments out
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000 ; this wont be used anyway..

    ; Load GDT
    lgdt [gdt_descriptor]

    ; Enable protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; far jump to protected entry
jmp_protected:
    jmp 0x08:protected_entry

pm_failed:
    mov dx, COM_BASE
    mov al, 'F'
    out dx, al
    hlt

; -----------------------------
; GDT (flat memory model)
; -----------------------------
gdt_start:
    gdt_null:
        dq 0x0
    gdt_code32:                  ; selector 0x08
        dw 0xFFFF
        dw 0x0000
        db 0x00
        db 10011010b
        db 11001111b
        db 0x00
    gdt_data32:                  ; selector 0x10
        dw 0xFFFF
        dw 0x0000
        db 0x00
        db 10010010b
        db 11001111b
        db 0x00
    gdt_code64:                  ; selector 0x18 (L=1, D/B=0)
        dw 0x0000
        dw 0x0000
        db 0x00
        db 10011010b
        db 00100000b
        db 0x00
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; -----------------------------
; Protected mode entry (32-bit)
; -----------------------------
[bits 32]
protected_entry:
    ; Print 'P' to COM1 (signal protected mode, hope it works)
    mov dx, COM_BASE
    mov al, 'P'
    out dx, al

    ; verify protected mode is enabled
    mov eax, cr0
    test eax, 1
    jz pm_failed


    ; Reload segment registers
    mov ax, 0x10     ; data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Temporary Stack
    mov esp, 0x7000

    ; Switch to long mode now - Set CR4. Physical Address Extension (pae)
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax

    ; Load CR3 with PML4 physical (low 32 bits) - grab it from the predefined offset. (set in smp.c)
    mov eax, dword [AP_TRAMP_PHYS + AP_TRAMP_PML4_OFFSET]
    mov cr3, eax

    ; Set EFER.Long Mode Enable
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr

    ; Enable Paging (set paging enable in cr0)
    mov eax, cr0
    or eax, 0x80000000 ; resolves to the bit in CR0 (bit 5)
    mov cr0, eax

    ; now do a far jump into 64-bit code selector (0x18)
    jmp 0x18:long_mode_entry

[bits 64]
long_mode_entry:
    ; signal long mode reached
    mov dx, COM_BASE
    mov al, 'L'
    out dx, al

    ; setup data segments as flat (the 0x10 is data32, it will be set for compatiblity incase smth happens)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set to OUR stack that was allocated by the prepare_percpu routine.

    mov rsi, AP_TRAMP_PHYS + AP_TRAMP_APMAIN_OFFSET
    mov rax, qword [rsi] ; it is mapped
    ; jump to ap_main
    jmp rax

; since i didnt have the halt loop it would just keep going to memory and crash.
.hlt_loop:
    hlt
    jmp .hlt_loop

; pad the rest of the 4kib page as 0.
times 4096 - ($ - _start) db 0
