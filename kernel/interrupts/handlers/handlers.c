/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Implementation of handler functions for interrupts.
 */

#include "handlers.h"
#include "../idt.h"

// NOTE TO SELF: DO NOT PUT TRACELAST_FUNC HERE, THESE ARE INTERRUPT/EXCEPTION HANDLERS!

static bool extended_scancode = false;

CTX_FRAME ctxfr;
static DPC keyboardDpc = {
    .Next = NULL,
    .callback = NULL,
    .callbackWithCtx = keyboard_dpc,
    .ctx = &ctxfr,
    .Kind = NO_KIND,
    .hasCtx = true,
    .priority = MEDIUM_PRIORITY
};

void keyboard_handler() {
    unsigned char scancode = __inbyte(KEYBOARD_DATA_PORT);

    // Extended scancode recognition.
    if (scancode == 0xE0) {
        extended_scancode = true;
        __outbyte(0x20, PIC_EOI); //ack
        return;
    }

    ctxfr.rax = scancode;
    ctxfr.rbx = extended_scancode;
    extended_scancode = false;
    MtQueueDPC(&keyboardDpc);

    // Send End Of Interrupt (EOI) to the PIC.
    __outbyte(0x20, PIC_EOI); // Only sent to master since this is a master interrupt.
}

void init_timer(unsigned long int frequency) {
    unsigned long int divisor = 1193180 / frequency;

    // Send the command byte
    __outbyte(0x43, 0x36);  // Channel 0, lobyte/hibyte, mode 3 (square wave)

    // Send the frequency divisor
    __outbyte(0x40, (unsigned char)(divisor & 0xFF));       // Low byte
    __outbyte(0x40, (unsigned char)((divisor >> 8) & 0xFF)); // High byte
}


// REVISE THAT SCHEDULEDPC WILL ALSO SEND CTX.

static DPC scheduleDpc = {
    .Next = NULL,
    .callback = ScheduleDPC,
    .callbackWithCtx = NULL,
    .ctx = NULL,
    .Kind = DPC_SCHEDULE,
    .hasCtx = false,
    .priority = HIGH_PRIORITY
};

extern volatile bool schedule_pending;

void timer_handler() {
    if (!schedule_pending) {
        if (cpu.currentThread && cpu.currentThread->timeSlice-- <= 0) {
            cpu.currentThread->timeSlice = cpu.currentThread->origTimeSlice;
            MtQueueDPC(&scheduleDpc);
            /// DO NOT SET schedule_needed TO TRUE HERE, IT WILL BE SET IN ScheduleDPC!!
        }
    }
}

void ata_handler(void) {
    // Just send EOI to both PICS.
    __outbyte(0xA0, 0x20);
    __outbyte(0x20, 0x20);
}

void pagefault_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    uint64_t fault_addr;
    // cr2 holds the faulty address that caused the page fault.
    
    __asm__ __volatile__ (
    "movq %%cr2, %0"
    : "=r"(fault_addr)
	);
    MtBugcheck(ctx, intfr, PAGE_FAULT, fault_addr, true);
    
    // __hlt(); -> When an HLT instruction is called when the CPU is in interrupt mode, (interrupts are already disabled to let this interrupt go through), iretd never executes, and so the CPU Is just stuck in place. Only an NMI or SMI can wake the processor back up
    // NMI - Non Maskable Interrupt, happens when a watchdog timer expires, system bus errors, (or memory parity errors, that rarily occur in modern systems).
    // SMI - A System Managment Interrupt is a special kind of an interurpt that also masks over HLT (even when interrupts are already disabled, like NMI), that is used for thermal throttling of the CPU, power management, or hardware emulation.
}

void doublefault_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    // We reached a double fault, an exception within an exception handler.
    // To not reach a triple fault, we will bugcheck the system (similar to a windows bugcheck - BSOD)
    MtBugcheck(ctx, intfr, DOUBLE_FAULT, 0, false);
}

void dividebyzero_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    UNREFERENCED_PARAMETER(ctx);
    UNREFERENCED_PARAMETER(intfr);
    // handle diving by zero.
    gop_printf(0xFFFF0000, "\nERROR: Dividing by zero is not allowed.\n");
}

void debugsinglestep_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    UNREFERENCED_PARAMETER(ctx);
    UNREFERENCED_PARAMETER(intfr);
    gop_printf(0xFFFF0000, "\nERROR: Debugging is not currently supported, halting.\n");
    __hlt();
}

void nmi_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    // severe problem, bugchecking.
    MtBugcheck(ctx, intfr, NON_MASKABLE_INTERRUPT,0, false);
}

void breakpoint_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    UNREFERENCED_PARAMETER(ctx);
    UNREFERENCED_PARAMETER(intfr);
    gop_printf(0xFFFF0000, "\nERROR: Debugging is not currently supported, halting.\n");
    __hlt();
}

void overflow_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    // almost never happens on modern systems (compilers dont generate the INTO instruction anymore, unless manual assembly is placed)
    // just bugcheck
    MtBugcheck(ctx, intfr, OVERFLOW, 0, false);
}

void boundscheck_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    // bugcheck too, this is kernel mode.
    MtBugcheck(ctx, intfr, BOUNDS_CHECK, 0, false);
}

void invalidopcode_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    MtBugcheck(ctx, intfr, INVALID_OPCODE, 0, false);
}

void nocoprocessor_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    // rarely triggered, if a floating point chip is not integrated, or is not attached, bugcheck.
    MtBugcheck(ctx, intfr, NO_COPROCESSOR, 0, false);
}

void coprocessor_segment_overrun_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    // quite literally impossible in protected or long mode, since CPU's don't generate this exception on these modes, but if they did, bugcheck, severe code.
    MtBugcheck(ctx, intfr, COPROCESSOR_SEGMENT_OVERRUN, 0, false);
}

void invalidtss_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    // a tss is when the CPU hardware switches (usually does not happen, since OS'es implement switching in software, like process timer context switch, all in software)
    // if it did happen though, we bugcheck.
    MtBugcheck(ctx, intfr, INVALID_TSS, 0, false);
}

void segment_selector_not_present_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    // this happens when the CPU loads a segment that points to a valid descriptor, that is marked as "not present" (that the present bit is 0), which means it's swapped out to disk.
    // we don't have disk paging right now, we don't even have a current user mode or stable memory for now, so we just bugcheck.
    MtBugcheck(ctx, intfr, SEGMENT_SELECTOR_NOTPRESENT, 0, false);
}

void stack_segment_overrun_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    // this happens when the stack pointer (esp, rsp, sp on 16 bit) moves OUTSIDE the bounds of the current stack segment, this is different from a stack overflow at the software level, this is a hardware level exception.
    // segment limits on protected mode usually gets switched off, so if this happens just bugcheck.
    MtBugcheck(ctx, intfr, STACK_SEGMENT_OVERRUN, 0, false);
}

void gpf_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    // important exception, view error code and bugcheck with it
    MtBugcheck(ctx, intfr, GENERAL_PROTECTION_FAULT, 0, false);
}

void fpu_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    UNREFERENCED_PARAMETER(ctx);
    UNREFERENCED_PARAMETER(intfr);;
    // this occurs when a floating point operation has an error, (even division by zero floating point will get here), or underflow/overflow
    gop_printf(0xFFFF0000, "Error: Floating Point error, have you done a correct calculation?\n");
}

void alignment_check_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    // 3 conditions must be met in-order for this to even reach.
    // CR0.AM (Alignment Mask) must be set to 1.
    // EFLAGS.AC (Alignment Check) must be set to 1.
    // CPL (user mode or kernel mode) must be set to 3. (user mode only)
    // If all are 1 and a stack alignment occurs (when doing char* ptr = kmalloc(64, 16); then writing like this *((uint32_t*)ptr) = 0xdeadbeef; // It's an unaligned write, writing more than there is.
    // for now, bugcheck.
    MtBugcheck(ctx, intfr, ALIGNMENT_CHECK, 0, false);
}

void severe_machine_check_handler(CTX_FRAME* ctx, INT_FRAME* intfr) {
    // creepy.
    // This happens when the machine has a SEVERE problem, memory faults, CPU internal fault, all of that, the cpu registers this.
    // obviously bugcheck.
    MtBugcheck(ctx, intfr, SEVERE_MACHINE_CHECK, 0, false);
}

