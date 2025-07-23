; ==============================================================================
; bootloader_stage2.asm — Second‑stage bootloader (FAT32 loader)
; ==============================================================================
org 0x8000
[bits 16]

jmp start

; ------------------------------------------------------------------------------
; BPB fields (we’ll fill these at runtime from the sector at 0x7C00)
; ------------------------------------------------------------------------------
bpb_BytesPerSector     dw 0
bpb_SectorsPerCluster  db 0
bpb_ReservedSectors    dw 0
bpb_NumFATs            db 0
bpb_SectorsPerFAT      dd 0
bpb_RootCluster        dd 0

; ------------------------------------------------------------------------------
; Disk Address Packet for INT 13h/AH=42h (extended LBA read)
; ------------------------------------------------------------------------------
dap:
    db 16,0              ; size=16, reserved=0
    dw 1                 ; sectors=1  (we’ll override if we want more)
    dw 0                 ; buffer offset (filled below)
    dw 0                 ; buffer segment (filled below)
    dd 0                 ; lba_low   (filled below)
    dd 0                 ; lba_high  (must remain zero)

; ------------------------------------------------------------------------------
; Working variables
; ------------------------------------------------------------------------------
drive_number      db 0
fat_lba           dd 0
first_data_sector dd 0
file_cluster      dd 0

; ------------------------------------------------------------------------------
; print_str: BIOS teletype BIOS AH=0Eh
; Input: DS:SI → zero‑terminated string
; ------------------------------------------------------------------------------
print_str:
    mov ah, 0x0E
.next_char:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .next_char
.done:
    ret

; ==============================================================================
; Entry point
; ==============================================================================
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    sti

    ; clear screen (mode 3)
    mov ah, 0x00
    mov al, 0x03
    int 0x10

    ; remember BIOS drive in DL
    mov [drive_number], dl

    ; ── Parse BPB from the original boot‑sector at 0x7C00 ───────────────────────
    push ds
      xor ax, ax
      mov ds, ax
      mov si, 0x7C00

      mov ax, [si + 0x0B]        ; BytesPerSector
      mov [bpb_BytesPerSector], ax

      mov al, [si + 0x0D]        ; SectorsPerCluster
      mov [bpb_SectorsPerCluster], al

      mov ax, [si + 0x0E]        ; ReservedSectors
      mov [bpb_ReservedSectors], ax

      mov al, [si + 0x10]        ; NumFATs
      mov [bpb_NumFATs], al

      mov eax, [si + 0x24]       ; SectorsPerFAT (32‑bit)
      mov [bpb_SectorsPerFAT], eax

      mov eax, [si + 0x2C]       ; RootCluster
      mov [bpb_RootCluster], eax
    pop ds

    ; ── Compute FAT start and data start ───────────────────────────────────────
    mov ax, [bpb_ReservedSectors]
    mov [fat_lba], eax

    mov eax, [bpb_SectorsPerFAT]
    movzx ebx, byte [bpb_NumFATs]
    mul ebx                     ; EAX = sectors_per_fat * num_fats
    add eax, [fat_lba]
    mov [first_data_sector], eax

    ; ── Load the root directory cluster at ES:BX = 0:0x9000 ────────────────────
    mov eax, [bpb_RootCluster]
    call cluster_to_lba         ; EAX = LBA of root cluster

    ; build DAP for that LBA
    xor eax, eax
    mov al, [bpb_SectorsPerCluster] ; number of sectors
    mov ah, 0
    mov [dap+2], ax            ; dap.sectors

    mov word [dap+4], 0x0000   ; dap.offset = 0
    mov word [dap+6], 0x9000   ; dap.segment = 0x9000

    mov [dap+8], eax           ; dap.lba_low = EAX
    mov dword [dap+12], 0      ; dap.lba_high = 0

    call read_sectors
    jc disk_error

    ; for now just show success
    mov si, msg_ok
    call print_str
    jmp $

; ==============================================================================
; cluster_to_lba:
;    EAX = cluster → compute EAX = first_data_sector + (cluster–2)*spc
; ==============================================================================
cluster_to_lba:
    sub eax, 2
    movzx ebx, byte [bpb_SectorsPerCluster]
    mul ebx
    add eax, [first_data_sector]
    ret

; ==============================================================================
; read_sectors: INT 13h AH=42h, DS:SI→DAP
; (requires dap.lba_high == 0)
; ==============================================================================
read_sectors:
    mov ah, 0x42
    mov dl, [drive_number]
    mov si, dap
    int 0x13
    ret

; ==============================================================================
; error handler
; ==============================================================================
disk_error:
    mov si, msg_diskerr
    call print_str
    hlt

; ==============================================================================
; messages
; ==============================================================================
msg_ok      db 'ROOT‑DIR loaded OK!',0x0D,0x0A,0
msg_diskerr db 'FATAL: Disk read error!',0x0D,0x0A,0

; pad to 2 KiB (4×512)
times 2048-($-$$) db 0
