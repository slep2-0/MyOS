; ==============================================================================
; stage2.asm - Second Stage Bootloader
;
; Features:
;   - Reads FAT32 BIOS Parameter Block (BPB).
;   - Finds and loads a file ("KERNEL  BIN") from the root directory.
;   - Uses LBA addressing (int 0x13, ah=42h) to read from disk.
;   - Gathers memory map via INT 0x15, EAX=0xE820.
;   - Enables the A20 line.
;   - Enters 32-bit Protected Mode.
;   - Jumps to the loaded file at 0x10000.
;
; ==============================================================================
org 0x8000
[bits 16]

jmp stage2_main

; ------------------------------------------------------------------------------
; BIOS Parameter Block (BPB) for FAT32
; This structure maps to the boot sector loaded by Stage 1 at 0x7C00.
; We are loaded at 0x8000, so we look back to 0x7C00 for this info.
; ------------------------------------------------------------------------------
bpb_BytesPerSector:     dw 0  ; Filled at runtime
bpb_SectorsPerCluster:  db 0  ; Filled at runtime
bpb_ReservedSectors:    dw 0  ; Filled at runtime
bpb_NumFATs:            db 0  ; Filled at runtime
bpb_SectorsPerFAT:      dd 0  ; Filled at runtime
bpb_RootCluster:        dd 0  ; Filled at runtime

; ------------------------------------------------------------------------------
; Disk Address Packet (DAP) for extended disk reads (int 0x13, ah=42h)
; ------------------------------------------------------------------------------
dap:
    .size       db 0x10         ; Size of the packet (16 bytes)
    .reserved   db 0
    .sectors    dw 1            ; Number of sectors to read
    .buffer     dw 0            ; Target buffer (offset)
    .segment    dw 0            ; Target buffer (segment)
    .lba_low    dd 0            ; Lower 32 bits of LBA
    .lba_high   dd 0            ; Upper 32 bits of LBA (not used for LBA28)

; ------------------------------------------------------------------------------
; Variables
; ------------------------------------------------------------------------------
drive_number        db 0        ; Boot drive number, passed by BIOS
fat_lba             dd 0        ; LBA of the first FAT
first_data_sector   dd 0        ; LBA of the first data sector (start of cluster area)
file_cluster        dd 0        ; Starting cluster of the file to load
file_load_segment   dw 0x1000   ; We will load the kernel loader to 0x1000:0000 (0x10000)
filename_to_find    db "KERNEL  BIN" ; 8.3 filename to find in the root directory

; ==============================================================================
; Main Entry Point
; ==============================================================================
stage2_main:
    ; --- Initial Setup ---
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000  ; Stack grows down from 0x7000
    sti

	; Clear the screen.
	mov ah, 0x00
    mov al, 0x03
    int 0x10

    ; The BIOS stores the boot drive in DL. Save it immediately.
    mov byte [drive_number], 0x80

    ; --- Parse BPB from Stage 1's memory location ---
    ; Stage 1 is at 0x7C00. We read the BPB values from there.
    mov ax, 0x7C00
    mov ds, ax
    mov ax, [ds:0x0B]   ; Bytes per Sector
    mov [bpb_BytesPerSector], ax
    mov al, [ds:0x0D]   ; Sectors per Cluster
    mov [bpb_SectorsPerCluster], al
    mov ax, [ds:0x0E]   ; Reserved Sectors
    mov [bpb_ReservedSectors], ax
    mov al, [ds:0x10]   ; Number of FATs
    mov [bpb_NumFATs], al
    mov eax, [ds:0x24]  ; Sectors per FAT (32-bit)
    mov [bpb_SectorsPerFAT], eax
    mov eax, [ds:0x2C]  ; Root Directory Cluster
    mov [bpb_RootCluster], eax
    xor ax, ax          ; Reset DS to 0
    mov ds, ax

    ; --- Calculate critical LBA locations ---
    ; LBA of FAT = Reserved Sectors
    mov eax, 0
    mov ax, [bpb_ReservedSectors]
    mov [fat_lba], eax

    ; LBA of the first data sector = Reserved Sectors + (Num FATs * Sectors per FAT)
    mov eax, [bpb_SectorsPerFAT]
    movzx ebx, byte [bpb_NumFATs]
    mul ebx
    add eax, [fat_lba]
    mov [first_data_sector], eax

    ; --- Load Root Directory and Find File ---
    call load_root_directory
    jc disk_error       ; If carry is set, reading failed
    call find_file
    jc file_not_found   ; If carry is set, file wasn't in the directory

    ; --- Load the File Cluster by Cluster ---
    mov eax, [file_cluster]
    call load_file
    jc disk_error

    ; --- Get Memory Map (E820) ---
    call get_memory_map

    ; --- Switch to Protected Mode ---
    call enter_protected_mode

; This part should not be reached if all goes well
hang:
    hlt
    jmp hang

; ==============================================================================
; FUNCTION: load_root_directory
; Loads the first cluster of the root directory into a temporary buffer.
; On exit: Carry flag set on error.
; ==============================================================================
load_root_directory:
    ; Convert root directory cluster to LBA using the standard formula
    mov eax, [bpb_RootCluster]
    call cluster_to_lba
    mov [dap.lba_low], eax

    ; Setup DAP to read one cluster
    movzx ax, byte [bpb_SectorsPerCluster]
    mov [dap.sectors], ax
    mov word [dap.buffer], 0x9000   ; Load root dir to 0:9000
    mov word [dap.segment], 0x0000

    call read_sectors
    ret

; ==============================================================================
; FUNCTION: find_file
; Searches the buffer at 0x9000 for the file specified in 'filename_to_find'.
; On success: Carry flag is clear, 'file_cluster' is populated.
; On failure: Carry flag is set.
; ==============================================================================
find_file:
    mov si, filename_to_find
    mov di, 0x9000      ; Start of root directory buffer
    mov cx, 1024        ; Search up to 1024 entries (arbitrary limit)
.search_loop:
    push di
    push si
    mov bx, 11          ; Compare 11 bytes (8.3 name)
.compare_loop:
    mov al, [si]
    cmp al, [di]
    jne .next_entry
    inc si
    inc di
    dec bx
    jnz .compare_loop

    ; --- File found! ---
    pop si
    pop di
    mov ax, [di + 0x1A] ; Low word of starting cluster
    mov dx, [di + 0x14] ; High word of starting cluster
    shl edx, 16
    or eax, edx
    mov [file_cluster], eax
    clc                 ; Clear carry to signal success
    ret

.next_entry:
    pop si
    pop di
    add di, 32          ; Move to the next 32-byte directory entry
    loop .search_loop

    stc                 ; Set carry to signal "not found"
    ret

; ==============================================================================
; FUNCTION: load_file
; Loads a file starting at the cluster in EAX, cluster by cluster.
; On exit: Carry flag set on error.
; ==============================================================================
load_file:
    mov [file_cluster], eax
    mov bx, [file_load_segment] ; Target segment for loading
.load_loop:
    ; --- Convert current cluster to LBA ---
    mov eax, [file_cluster]
    call cluster_to_lba
    mov [dap.lba_low], eax

    ; --- Setup DAP to read one cluster ---
    movzx ax, byte [bpb_SectorsPerCluster]
    mov [dap.sectors], ax
    mov word [dap.buffer], 0x0000
    mov [dap.segment], bx
    call read_sectors
    jc .error

    ; --- Find next cluster from FAT ---
    mov eax, [file_cluster]
    call get_next_cluster
    jc .error
    mov [file_cluster], eax

    ; Check for end-of-chain marker
    cmp eax, 0x0FFFFFF8
    jae .done_loading

    ; --- Advance buffer for next cluster ---
    ; Calculate cluster size in bytes, then in paragraphs (16 bytes)
    mov eax, 0
    mov ax, [bpb_BytesPerSector]
    movzx ecx, byte [bpb_SectorsPerCluster]
    mul ecx
    shr eax, 4  ; Divide by 16 to get number of paragraphs
    add bx, ax  ; Add to segment
    jmp .load_loop

.done_loading:
    clc
    ret
.error:
    stc
    ret

; ==============================================================================
; FUNCTION: get_next_cluster
; Reads the FAT to find the next cluster in a chain.
; In: EAX = current cluster number
; Out: EAX = next cluster number, Carry set on error
; ==============================================================================
get_next_cluster:
    ; Calculate offset in FAT: cluster_number * 4
    shl eax, 2
    mov edi, eax    ; edi = FAT offset in bytes

    ; Calculate sector of FAT to read: FAT offset / bytes_per_sector
    mov ecx, 0
    mov cx, [bpb_BytesPerSector]
    div ecx         ; eax = sector number within FAT, edx = offset in sector
    mov esi, edx    ; esi = offset in sector

    ; Add FAT's starting LBA
    add eax, [fat_lba]

    ; Setup DAP to read 1 sector from FAT
    mov [dap.lba_low], eax
    mov word [dap.sectors], 1
    mov word [dap.buffer], 0x7000   ; Use stack area as temp buffer
    mov word [dap.segment], 0x0000
    call read_sectors
    jc .error

    ; Get the 4-byte entry from the buffer
    mov eax, [0x7000 + esi]
    clc
    ret
.error:
    stc
    ret

; ==============================================================================
; FUNCTION: cluster_to_lba
; Converts a cluster number to a Linear Block Address (LBA).
; This now uses the standard FAT32 formula, as seen in ReactOS's bootloader.
; LBA = first_data_sector + (cluster - 2) * sectors_per_cluster
; In: EAX = cluster number
; Out: EAX = LBA
; ==============================================================================
cluster_to_lba:
    sub eax, 2  ; Cluster numbers are 2-based, so subtract 2
    movzx ebx, byte [bpb_SectorsPerCluster]
    mul ebx
    add eax, [first_data_sector]
    ret

; ==============================================================================
; FUNCTION: read_sectors
; Reads sectors using the extended read function (ah=42h) and the DAP.
; On exit: Carry flag set on error.
; ==============================================================================
read_sectors:
    mov ah, 0x42
    mov dl, [drive_number]
    mov si, dap
    int 0x13
    ret

; ==============================================================================
; FUNCTION: get_memory_map
; Calls INT 15h, EAX=E820h to get system memory map.
; ==============================================================================
get_memory_map:
    xor ebx, ebx            ; Start with ebx = 0
    mov di, e820_buffer     ; Buffer to store entries
.e820_loop:
    mov eax, 0xE820
    mov edx, 0x534D4150     ; 'SMAP'
    mov ecx, 24             ; ARD size
    int 0x15
    jc .e820_done           ; On error, we're done

    cmp eax, 0x534D4150     ; Check magic number
    jne .e820_done

    add di, 24              ; Next entry
    cmp ebx, 0              ; If ebx is 0, we are done
    jne .e820_loop
.e820_done:
    ret

; ==============================================================================
; FUNCTION: enter_protected_mode
; Enables A20, loads GDT, and jumps to 32-bit code.
; ==============================================================================
enter_protected_mode:
    cli
    ; Enable A20 line
    in al, 0x92
    or al, 0x02
    out 0x92, al

    ; Load GDT
    lgdt [gdt_descriptor]

    ; Set PE bit in CR0
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to flush instruction pipeline and enter PM
    jmp 0x08:protected_mode_entry

; ==============================================================================
; Messages and Data
; ==============================================================================
disk_error:
    mov si, msg_disk_error
    call print_string
    jmp hang

file_not_found:
    mov si, msg_file_not_found
    call print_string
    jmp hang

pm_failed:
    mov si, msg_pm_failed
    call print_string
    jmp hang

; ---------------------------------------------------------
; print_hex32
; Input: EAX = 32-bit value to print in hex
; ---------------------------------------------------------
print_hex32:
    push eax
    push ecx
    push edx

    mov ecx, 8              ; 8 hex digits
.print_loop:
    rol eax, 4              ; rotate left by 4 bits (MSB nibble comes to LSB)
    mov dl, al              ; isolate low 4 bits
    and dl, 0x0F
    call print_hex_digit
    loop .print_loop

    pop edx
    pop ecx
    pop eax
    ret

; ---------------------------------------------------------
; print_hex_digit
; Input: DL = nibble (0–15)
; ---------------------------------------------------------
print_hex_digit:
    cmp dl, 0x0A
    jl .digit
    add dl, 'A' - 0x0A
    jmp .print
.digit:
    add dl, '0'
.print:
    mov ah, 0x0E
    mov al, dl
    int 0x10
    ret

print_string:
    mov ah, 0x0E
.loop:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .loop
.done:
    ret

msg_disk_error      db 'FATAL: Disk read error!', 0xD, 0xA, 0
msg_file_not_found  db 'FATAL: KERNEL.BIN not found in root directory!', 0xD, 0xA, 0
msg_pm_failed       db 'FATAL: Protected Mode entry failed.', 0xD, 0xA, 0

; ------------------------------------------------------------------------------
; GDT (Global Descriptor Table)
; ------------------------------------------------------------------------------
gdt_start:
    ; Null Descriptor
    dq 0x0
    ; Code Segment (0x08)
    dw 0xFFFF       ; Limit (low)
    dw 0x0000       ; Base (low)
    db 0x00         ; Base (mid)
    db 10011010b    ; Access (Present, Ring 0, Code, Exec/Read)
    db 11001111b    ; Granularity (4K pages, 32-bit)
    db 0x00         ; Base (high)
    ; Data Segment (0x10)
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b    ; Access (Present, Ring 0, Data, Read/Write)
    db 11001111b
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ------------------------------------------------------------------------------
; Buffer for E820 memory map
; ------------------------------------------------------------------------------
e820_buffer:
    times 1024 db 0

; ==============================================================================
; 32-BIT PROTECTED MODE CODE
; ==============================================================================
[bits 32]
protected_mode_entry:
    ; Verify we are in protected mode
    mov eax, cr0
    test eax, 1
    jz pm_failed

    ; Setup segment registers for flat memory model
    mov ax, 0x10    ; 0x10 is the data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000 ; Set up a new stack

    ; Display a success message in PM
    mov edi, 0xB8000
    mov esi, msg_pm_success
    call print_string_pm

    ; Jump to the kernel loader, which was loaded at 0x10000
    ; The C kernel loader is now responsible for everything else.
    jmp 0x08:0x10000

print_string_pm:
.loop:
    mov al, [esi]
    cmp al, 0
    je .done
    mov [edi], al
    mov byte [edi+1], 0x0A ; Green on black
    add edi, 2
    inc esi
    jmp .loop
.done:
    ret

msg_pm_success db 'Stage 2: OK. Jumping to Kernel...', 0
