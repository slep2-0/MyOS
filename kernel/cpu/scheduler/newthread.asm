; PROJECT:      MatanelOS Kernel
; LICENSE:      GPLv3
; PURPOSE:      Thread entry trampoline - called when a new thread is first scheduled.

global thread_entry_trampoline
thread_entry_trampoline:
    ; RSP currently points to the INT_FRAME (at vector field)
    ; We need to skip vector and error_code to get to where iretq expects
    add rsp, 16     ; Skip vector (8 bytes) + error_code (8 bytes)
    
    ; Now RSP points to rip field, which is exactly where iretq expects
    iretq