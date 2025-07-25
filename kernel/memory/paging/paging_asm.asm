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

; void disable_paging(void);
global disable_paging
disable_paging:
    ; Read current CR0 value
    mov rax, cr0
    
    ; Clear PG bit (bit 31)
    and eax, 0x7FFFFFFF
    
    ; Write back to CR0
    mov cr0, rax
    
    ret

; void load_cr3(uint64_t pml4_phys);
global load_cr3
load_cr3:
    mov cr3, rdi
    ret

; uint64_t read_cr3(void);
global read_cr3
read_cr3:
    mov rax, cr3
    ret

; uint64_t read_cr0(void);
global read_cr0
read_cr0:
    mov rax, cr0
    ret

; void write_cr0(uint64_t value);
global write_cr0
write_cr0:
    mov cr0, rdi
    ret

; void flush_tlb(uint64_t virtual_addr);
global flush_tlb
flush_tlb:
    invlpg [rdi]
    ret

; void flush_all_tlb(void);
global flush_all_tlb
flush_all_tlb:
    mov rax, cr3
    mov cr3, rax    ; Reloading CR3 flushes entire TLB
    ret