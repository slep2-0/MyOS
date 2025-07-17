
[bits 32]

extern kernel_main ; Declare kernel_main from C code

global _start     ; Export _start symbol for the linker
_start:
    ; The bootloader already sets the stack pointer (esp).
    ; Here you could clear the .bss section if you had one.
	; I choose not to, as there is no need for it.
    call kernel_main ; Call your C kernel function - for me it's kernel_main, but you may re-implement naming however you like.
    hlt              ; Halt the CPU if kernel_main ever returns