[bits 64]
section .text

; void enable_paging(uint64_t pml4_phys);
; RDI contains the first argument (pml4_phys) in System V ABI
global enable_paging
enable_paging:
    ; Load PML4 physical address into CR3
    mov cr3, rdi ; RDI Contains the pml4 address pushed from the C Code.
    
    ; Read current CR0 value
    mov rax, cr0
    
    ; Set PG (bit 31) and WP (bit 16) bits
    or eax, 0x80000000    ; Set PG bit
    or eax, 0x00010000    ; Set WP bit
    
    ; Write back to CR0 to enable paging
    mov cr0, rax
    
    ret

global switch_higher
switch_higher:
    ; Reload CR3 First
    mov cr3, rdi ; RDI contains this CR3, we basically flush global TLB.
    jmp rsi
