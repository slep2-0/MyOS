[bits 64]
section .text

; void enable_paging(uint64_t pml4_phys);
; RDI contains the first argument (pml4_phys) in System V ABI
global enable_paging
enable_paging:
    ; Load PML4 physical address into CR3
    mov cr3, rdi ; RDI Contains the pml4 address pushed from the C Code.
    ret