; stage2.asm - Fixed version
org 0x8000
[bits 16]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    
    ; Clear screen
    mov ah, 0x00
    mov al, 0x03
    int 0x10
    
    ; Load kernel - FIX: Use the correct drive number
    mov ax, 0x1000      ; segment for 0x10000
    mov es, ax
    xor bx, bx          ; offset 0x0000
    mov ah, 0x02        ; BIOS read sectors function
    mov al, 15         	; number of sectors to read -> increased to 15, kernel binary size has increased, safety precaution.
    mov ch, 0           ; cylinder 0
    mov cl, 6           ; sector 6 (stage1=1 + stage2=4 + 1 = sector 6)
    mov dh, 0           ; head 0
    mov dl, 0x80    	; Use the SAME drive as stage 1 (stored at 0x7C00) -- I didn't read from it at the end, because I explictly said to load from an hard drive in QtEMU, besides, who uses floppy disks today?
    int 0x13            ; BIOS disk interrupt
    jc disk_error       ; jump if carry flag set (error)
    
    ; Load GDT
    lgdt [gdt_descriptor]
    
    ; Enable A20 line
    call enable_a20
    
    ; Enable protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    ; Far jump to flush prefetch queue
    jmp 0x08:protected_mode_entry

enable_a20:
    in al, 0x92
    or al, 0x02
    out 0x92, al
    ret

gdt_start:
    gdt_null:
        dq 0x0
    gdt_code:
        dw 0xFFFF
        dw 0x0000
        db 0x00
        db 10011010b
        db 11001111b
        db 0x00
    gdt_data:
        dw 0xFFFF
        dw 0x0000
        db 0x00
        db 10010010b
        db 11001111b
        db 0x00

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
gdt_end:

[bits 32]
protected_mode_entry:
    ; Set up segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    
    ; Write directly to VGA memory to show we're in protected mode
    mov edi, 0xB8000
    mov al, 'P'
    mov ah, 0x0F
    mov [edi], ax
    add edi, 2
    mov al, 'M'
    mov [edi], ax
    add edi, 2
    mov al, '!'
    mov [edi], ax
    
    ; Small delay to see the message
    mov ecx, 0x1000000
.pm_delay:
    loop .pm_delay
    
    ; Jump to kernel
    jmp dword 0x08:0x10000
    
disk_error:
    mov si, msg_disk_error
    call print_string
    hlt
    
print_string:
    mov ah, 0x0E
.next_char:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .next_char
.done:
    ret

msg_disk_error db 'DISK ERROR!', 0xD, 0xA, 0

; Pad to 2048 bytes
times 2048 - ($ - $$) db 0