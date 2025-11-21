/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Implementation of handler functions for interrupts.
 */

#include "../../includes/mh.h"
#include "../../includes/me.h"
#include "../../includes/mg.h"
#include "../../includes/ps.h"
#include "../../includes/md.h"
#include "../../trace.h"

#define PRINT_ALL_REGS_AND_HALT(ctxptr, intfrptr)                     \
    do {                                                             \
        gop_printf(COLOR_RED,                                         \
            "RAX=%p RBX=%p RCX=%p RDX=%p\n"           \
            "RSI=%p RDI=%p RBP=%p RSP=%p\n"           \
            "R8 =%p R9 =%p R10=%p R11=%p\n"           \
            "R12=%p R13=%p R14=%p R15=%p\n"           \
            "RIP=%p RFLAGS=%p\n",                               \
            (ctxptr)->rax, (ctxptr)->rbx, (ctxptr)->rcx, (ctxptr)->rdx, \
            (ctxptr)->rsi, (ctxptr)->rdi, (ctxptr)->rbp, (intfrptr)->rsp, \
            (ctxptr)->r8, (ctxptr)->r9, (ctxptr)->r10, (ctxptr)->r11,    \
            (ctxptr)->r12, (ctxptr)->r13, (ctxptr)->r14, (ctxptr)->r15, \
            (intfrptr)->rip, (intfrptr)->rflags);                       \
        __hlt();                                                      \
    } while (0)

// NOTE TO SELF: DO NOT PUT TRACELAST_FUNC HERE, THESE ARE INTERRUPT/EXCEPTION HANDLERS!


extern uint32_t cursor_x;
extern uint32_t cursor_y;
extern GOP_PARAMS gop_local;

static void MiHandleTimer(bool schedulerEnabled, PTRAP_FRAME trap) {
    // revamp this stupid coding flow
    if (!MeGetCurrentProcessor()->schedulePending) {
        if (schedulerEnabled) {
            if (MeGetCurrentProcessor()->currentThread) {
                if (__sync_sub_and_fetch(&MeGetCurrentProcessor()->currentThread->TimeSlice, 1) <= 0) {
                    MeGetCurrentProcessor()->currentThread->TimeSlice = MeGetCurrentProcessor()->currentThread->TimeSliceAllocated;
                    tracelast_func("Queuing DPC in timer_handler, and saving regs.");
                    /* Setup of DPC */
                    PETHREAD thread_to_save = PsGetCurrentThread();
                    TRAP_FRAME* saved_regs = &thread_to_save->InternalThread.TrapRegisters;

                    // Values taken from the interrupt frame
                    saved_regs->rip = trap->rip;
                    saved_regs->rsp = trap->rsp;
                    saved_regs->rflags = trap->rflags;

                    // General-purpose registers from the context frame
                    saved_regs->r15 = trap->r15;
                    saved_regs->r14 = trap->r14;
                    saved_regs->r13 = trap->r13;
                    saved_regs->r12 = trap->r12;

                    saved_regs->r11 = trap->r11;
                    saved_regs->r10 = trap->r10;
                    saved_regs->r9 = trap->r9;
                    saved_regs->r8 = trap->r8;

                    saved_regs->rbp = trap->rbp;
                    saved_regs->rdi = trap->rdi;
                    saved_regs->rsi = trap->rsi;

                    saved_regs->rcx = trap->rcx;
                    saved_regs->rbx = trap->rbx;
                    saved_regs->rdx = trap->rdx;
                    saved_regs->rax = trap->rax;

                    saved_regs->cs = trap->cs;
                    saved_regs->ss = trap->ss;
                    /* Ended */

                    DPC* SchedDpc = &MeGetCurrentProcessor()->TimerExpirationDPC;
                    SchedDpc->Next = NULL;
                    SchedDpc->CallbackRoutine = MeScheduleDPC;
                    SchedDpc->Arg1 = NULL;
                    SchedDpc->Arg2 = NULL;
                    SchedDpc->Arg3 = NULL;
                    SchedDpc->priority = HIGH_PRIORITY;

                    MeQueueDPC(SchedDpc);
                    /// DO NOT SET schedule_needed TO TRUE HERE, IT WILL BE SET IN ScheduleDPC!!
                }
                else {
                    tracelast_func("Did not queue DPC in timer handler. Reason: Thread's timeslice isn't over.");
                }
            }
            else {
                tracelast_func("Did not queue DPC in timer handler. Reason: Thread is NULL (no current thread)");
            }
        }
        else {
            tracelast_func("Did not queue DPC in timer handler. Reason: Scheduler isn't enabled..");
        }
    }
    else {
        tracelast_func("Did not queue DPC in timer handler. Reason: Schedule DPC is already pending..");
    }
}

extern void lapic_eoi(void);

void MiLapicInterrupt(bool schedulerEnabled, PTRAP_FRAME trap) {
    MiHandleTimer(schedulerEnabled, trap);
    lapic_eoi();
}

void MiInterprocessorInterrupt (
    void
) 

/*++

    Routine description : Handles an interprocessor interupt.

    Arguments:

        None. (arguments to IPI's are taken from the PROCESSOR struct, accessed in MeGetCurrentProcessor)

    Return Values:

        None.

--*/

{
    PPROCESSOR cpu = MeGetCurrentProcessor();
    InterlockedOrU64(&cpu->flags, CPU_DOING_IPI);
    uint64_t addr = cpu->IpiParameter.debugRegs.address;
    CPU_ACTION action = cpu->IpiAction;
    int idx = find_available_debug_reg();
    switch (action) {
    case CPU_ACTION_STOP:
        // explicit action to halt, since we are in an interrupt, unless an NMI somehow comes, we will stay stopped.
        // clear the flag before we halt so BSP can continue iterations
        cpu->IpiSeq = 0;
        InterlockedAndU64(&cpu->flags, ~CPU_DOING_IPI);
        for (;;) __hlt();
    case CPU_ACTION_PERFORM_TLB_SHOOTDOWN:
        invlpg((void*)cpu->IpiParameter.pageParams.addressToInvalidate);
        break;
    case CPU_ACTION_PRINT_ID:
        gop_printf(COLOR_RED, "[CPU-IPI] Hello from CPU ID: %d\n", cpu->lapic_ID);
        break;
    case CPU_ACTION_WRITE_DEBUG_REGS:
        if (idx == -1) break;
        __write_dr(7, cpu->IpiParameter.debugRegs.dr7);
        __write_dr(idx, cpu->IpiParameter.debugRegs.address);
        MeGetCurrentProcessor()->DebugEntry[idx].Address = (void*)cpu->IpiParameter.debugRegs.address;
        MeGetCurrentProcessor()->DebugEntry[idx].Callback = cpu->IpiParameter.debugRegs.callback;
        break;
    case CPU_ACTION_CLEAR_DEBUG_REGS:
        for (int i = 0; i < 4; i++) {
            if ((uint64_t)MeGetCurrentProcessor()->DebugEntry[i].Address == addr) {
                __write_dr(i, 0);

                /* Clear DR7 bits for this index (local enable and RW/LEN group) */
                uint64_t dr7 = __read_dr(7);
                /* clear local enable bit */
                dr7 &= ~(1ULL << (i * 2));
                /* clear RW/LEN 4-bit group */
                uint64_t mask = 0xFULL << (16 + 4 * i);
                dr7 &= ~mask;
                __write_dr(7, dr7);

                /* Clear status DR6 too */
                __write_dr(6, 0);
                MeGetCurrentProcessor()->DebugEntry[i].Address = NULL;
                MeGetCurrentProcessor()->DebugEntry[i].Callback = NULL;
                break;
            }
        }
    }

    InterlockedAndU64(&cpu->flags, ~CPU_DOING_IPI);
    if (action != CPU_ACTION_STOP) {
        InterlockedAndU64(&cpu->flags, ~CPU_DOING_IPI);
        cpu->IpiSeq = 0; // Signal completion for non-halting actions.
    }
}

void 
MiPageFault (
    IN  PTRAP_FRAME trap
) 

/*++

    Routine description : Handles a page fault that occured in the current CPU.

    Arguments:

        Pointer to the current TRAP_FRAME.

    Return Values:

        None. Function would bugcheck/return if conditions are met.

--*/

{
    uint64_t fault_addr;
    // cr2 holds the faulty address that caused the page fault.
    
    __asm__ __volatile__ (
    "movq %%cr2, %0"
    : "=r"(fault_addr)
	);

    /*++
    
    Page fault bugcheck parameters:

    Parameter 1: Memory address referenced. (CR2)
    Parameter 2: (decimal) 0 - Read Operation, 2 - Write Operation, 10 - Execute operation (unused for now, NX hasn't been turned on for now)
    Parameter 3: Address that referenced memory (RIP)
    Parameter 4: CPU Error code pushed.

    --*/

    

    MTSTATUS status = MmAccessFault(trap->error_code, fault_addr, MeGetPreviousMode(), trap);

    if (MT_FAILURE(status)) {
        // If MmAccessFault returned a failire (e.g MT_ACCESS_VIOLATION), but hasn't bugchecked, we check for exception handlers in the current thread
        //if (ExpIsExceptionHandlerPresent(PsGetCurrentThread())) {
        //    ExpDispatchException(trap);
        //    return;
        //}

        //else {
            // No kernel exception handler present, bugcheck.
            MeBugCheckEx(
                KMODE_EXCEPTION_NOT_HANDLED,
                (void*)MT_ACCESS_VIOLATION,
                (void*)fault_addr,
                NULL,
                NULL
            );
        //}
    }

    // MmAccessFault returned MT_SUCCESS, that means it handled the fault (filled the PTE, etc.), we return to original instruction and re-run.
    return;
}

NORETURN
void 
MiDoubleFault(
    IN  PTRAP_FRAME trap
) 

/*++

    Routine description : Handles a double fault exception that has happened on the current CPU.

    Arguments:

        Pointer to the current TRAP_FRAME.

    Return Values:

        None. This function will never return to caller.

--*/

{
    /*++

    Double Fault bugcheck parameters:

    Parameter 1: Address at the time of the fault.
    Parameter 2,3,4: NULL.

    --*/

    MeBugCheckEx(DOUBLE_FAULT, (void*)(uintptr_t)trap->rip, NULL, NULL, NULL);
}

void 
MiDivideByZero (
    PTRAP_FRAME trap
) 

/*++

    Routine description : Handles a divide by zero on the current CPU.

    Arguments:

        Pointer to the current TRAP_FRAME.

    Return Values:

        None. This function currently does not return to caller.

--*/

{

    /*++

    Divide By Zero bugcheck parameters:

    Parameter 1: Address at the time of the fault.
    Parameter 2,3,4: NULL.

    --*/
    
    // When user mode processes and threads are fully established, this should generate an ACCESS_VIOLATION. TODO
    if (MeGetPreviousMode() == UserMode) {
        // guard it for now.
        MeBugCheckEx(ASSERTION_FAILURE, (void*)"MiDivideByZero", (void*)"A Fault in user mode occured, division error, implement.", NULL, NULL);
    }

    MeBugCheckEx(DIVIDE_BY_ZERO, (void*)(uintptr_t)trap->rip, NULL, NULL, NULL);

}

void 
MiDebugTrap (
    PTRAP_FRAME trap
) 

/*++

    Routine description : Handles a debug trap (either a single step from a debugger, or a debug register has been hit)

    Arguments:

        Pointer to the current TRAP_FRAME.

    Return Values:

        None.

--*/

{
#ifndef GDB
    /* read debug status */
    uint64_t dr6 = __read_dr(6);
    
    if (dr6 & 0xF) {
        /* For each possible debug register 0..3 check B0..B3 */
        for (int i = 0; i < 4; ++i) {
            if (dr6 & (1ULL << i)) {
                /* If a callback is registered, call it. Provide both address and context. */
                if (MeGetCurrentProcessor()->DebugEntry[i].Callback) {
                    DBG_CALLBACK_INFO info = {
                        .Address = MeGetCurrentProcessor()->DebugEntry[i].Address,
                        .trap = trap,
                        .BreakIdx = i,
                        .Dr6 = dr6
                    };

                    /* Call the user-registered callback. It receives &info (void*). */
                    MeGetCurrentProcessor()->DebugEntry[i].Callback(&info);
                }
                else {
                    /* no callback registered for this DRx: print for debug and continue */
                    gop_printf(0xFFFFFF00, "DEBUG: DR%d fired at addr %p but no callback\n", i, (void*)__read_dr(i));
                }
            }
        }

        /* Clear the status bits in DR6 so INT1 won't fire again for the same event.
           Writing zero clears B0..B3 and other status bits per Intel spec. */
        __write_dr(6, 0);
        return;
    }
    else if (dr6 & (1 << 14)) {
        // Single Step. -- Ignore for now, we don't have a custom debugger yet, and QEMU sets its own.
        return;
    }
#else
    UNREFERENCED_PARAMETER(trap);
    __write_DR(6, 0);
    return;
#endif
}

NORETURN
void 
MiNonMaskableInterrupt ( 
    PTRAP_FRAME trap
) 

/*++

    Routine description : Handles an NMI (Non Maskable Interrupt), generated by the CPU.

    Arguments:

        Pointer to the current TRAP_FRAME.

    Return Values:

        None. This function will never return.

--*/

{
    /*++

    NMI bugcheck parameters:

    No Parameters.

    --*/
    UNREFERENCED_PARAMETER(trap);
    MeBugCheck(NON_MASKABLE_INTERRUPT);
}

void MiBreakpoint (
    PTRAP_FRAME trap
)

/*++

    Routine description : Handles a breakpoint instruction (INT3)

    Arguments:

        Pointer to the current TRAP_FRAME.

    Return Values:

        None. This function will never return.

--*/


{
    gop_printf(COLOR_RED, "**INT3 Breakpoint hit at: %p - Halting.\n", trap->rip);
    __hlt();
}

void MiOverflow(PTRAP_FRAME trap) {
    MeBugCheckEx(OVERFLOW, (void*)trap->rip, NULL, NULL, NULL);
}

void MiBoundsCheck(PTRAP_FRAME trap) {
    // bugcheck too, this is kernel mode.
    MeBugCheckEx(BOUNDS_CHECK, (void*)trap->rip, NULL, NULL, NULL);
}

void MiInvalidOpcode(PTRAP_FRAME trap) {
    MeBugCheckEx(INVALID_OPCODE, (void*)trap->rip, NULL, NULL, NULL);
}

void MiNoCoprocessor(PTRAP_FRAME trap) {
    // rarely triggered, if a floating point chip is not integrated, or is not attached, bugcheck.
    MeBugCheckEx(NO_COPROCESSOR, (void*)trap->rip, NULL, NULL, NULL);
}

void MiCoprocessorSegmentOverrun(PTRAP_FRAME trap) {
    // quite literally impossible in protected or long mode, since CPU's don't generate this exception on these modes, but if they did, bugcheck, severe code.
    MeBugCheckEx(COPROCESSOR_SEGMENT_OVERRUN, (void*)trap->rip, NULL, NULL, NULL);
}

void MiInvalidTss(PTRAP_FRAME trap) {
    // a tss is when the CPU hardware switches (usually does not happen, since OS'es implement switching in software, like process timer context switch, all in software)
    // if it did happen though, we bugcheck.
    MeBugCheckEx(INVALID_TSS, (void*)trap->rip, NULL, NULL, NULL);
}

void MiSegmentSelectorNotPresent(PTRAP_FRAME trap) {
    // this happens when the CPU loads a segment that points to a valid descriptor, that is marked as "not present" (that the present bit is 0), which means it's swapped out to disk.
    // we don't have disk paging right now, we don't even have a current user mode or stable memory for now, so we just bugcheck.
    MeBugCheckEx(SEGMENT_SELECTOR_NOTPRESENT, (void*)trap->rip, NULL, NULL, NULL);
}

void MiStackSegmentOverrun(PTRAP_FRAME trap) {
    // this happens when the stack pointer (esp, rsp, sp on 16 bit) moves OUTSIDE the bounds of the current stack segment, this is different from a stack overflow at the software level, this is a hardware level exception.
    // segment limits on protected mode usually gets switched off, so if this happens just bugcheck.
    MeBugCheckEx(STACK_SEGMENT_OVERRUN, (void*)trap->rip, NULL, NULL, NULL);
}

void MiGeneralProtectionFault(PTRAP_FRAME trap) {
    // important exception, view error code and bugcheck with it
    MeBugCheckEx(GENERAL_PROTECTION_FAULT, (void*)trap->rip, NULL, NULL, NULL);
}

void MiFloatingPointError(PTRAP_FRAME trap) {
    UNREFERENCED_PARAMETER(trap);
    // this occurs when a floating point operation has an error, (even division by zero floating point will get here), or underflow/overflow
    gop_printf(0xFFFF0000, "Error: Floating Point error, have you done a correct calculation?\n");
}

void MiAlignmentCheck(PTRAP_FRAME trap) {
    // 3 conditions must be met in-order for this to even reach.
    // CR0.AM (Alignment Mask) must be set to 1.
    // EFLAGS.AC (Alignment Check) must be set to 1.
    // CPL (user mode or kernel mode) must be set to 3. (user mode only)
    // If all are 1 and a stack alignment occurs (when doing char* ptr = kmalloc(64, 16); then writing like this *((uint32_t*)ptr) = 0xdeadbeef; // It's an unaligned write, writing more than there is.
    // for now, bugcheck.
    MeBugCheckEx(ALIGNMENT_CHECK, (void*)trap->rip, NULL, NULL, NULL);
}

void MiMachineCheck(PTRAP_FRAME trap) {
    // creepy.
    // This happens when the machine has a SEVERE problem, memory faults, CPU internal fault, all of that, the cpu registers this.
    // obviously bugcheck.
    MeBugCheckEx(SEVERE_MACHINE_CHECK, (void*)trap->rip, NULL, NULL, NULL);
}

