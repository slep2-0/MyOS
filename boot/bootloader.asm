; MatanelOS Bootloader Stage 1
; Workflow:
; QEMU Loads this into the first bootsector -> The bootloader loads -> interrupts the bios to load the next 4 sectors -> jumps to the start of the second bootloader IF the bios has loaded successfully || if not, HALT the CPU.

; Origin is always at 0x7C00, thats where the BIOS loads it.
org 0x7C00
boot_drive db 0 ; BIOS Drive Number (0x00 - first floppy, 0x80 - first HDD) -> MUST reside here, because it will be read from later.

start:
	cli ; Disable interrupts
	mov [boot_drive], dl    ; store drive letter.
	xor ax, ax
	mov ds, ax ; Setup Data Segment
	mov es, ax ; Setup Extra Segment
	mov ss, ax
	mov sp, 0x7C00 ; Stack grows down now from 0x0000:7C00
	
	; Display LOADING message.
	mov si, msg_loading
	call print_string
	
	; Load the second stage bootloader, first fill the sectors (4 sectors)
	mov bx, 0x8000 ; points to the start of the second bootloader
	mov dh, 0 ; Head 0
	mov dl,	[boot_drive] ; Drive number passed by the bios
	mov ch, 0 ; Cylinder 0
	mov cl, 2 ; Sector 2 (first one is sector 1)
	mov al, 4 ; Number of sectors to read.
	mov ah, 0x02 ; BIOS read sector function
	int 0x13 ; BIOS Disk Service
	
	; if carry flag is set, BIOS returned an error, if so, halt.
	jc disk_error
	
	; else, we have succesfully loaded the second Stage, so jump to it.
	jmp 0x0000:0x8000
	; ---<>--- END --<>-- ;
	
; print_string helper function
print_string:
	mov ah, 0x0E ; BIOS TType function to output a character into the screen.
.next_char:
	lodsb ; Load SI into AL and move 1 forward in SI (increment ptr by 1)
	cmp al, 0 ; if we are null terminator, jmp to done.
	je .done
	int 0x10 ; bios video service - will output the char.
	jmp .next_char
.done:
	ret ; return to original caller.

; incase of an error when loading the second stage bootloader, jmp here and halt.
disk_error:
	mov si, msg_error
	call print_string
	hlt ; HALT the cpu.

; Messages
msg_loading db 'Loading MatanelOS Second Stage...',0
msg_error db ' -----> Disk Read Error!',0

; Pad up to 510 bytes, then + 2 bytes for the signature (a bootloader MUST be 512 bytes at the first stage)
times 510 - ( $ - $$ ) db 0 ; fill until 510 bytes
dw 0xAA55 ; signature, 2 bytes -> TOTAL: 512 BYTES.