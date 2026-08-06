/*++

Module Name:

    exception.c

Purpose:

    This translation unit contains the implementation of exception checking & handling in MatanelOS (_try _except macros)

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/exception.h"
#include "../../includes/ps.h"
#include "../../assert.h"
#include "../../includes/me.h"

#define EXCEPTION_DISPATCHER_NAME "MeUserExceptionDispatcher"

uint64_t
ExpFindKernelModeExceptionHandler(
    uint64_t Rip
)

/*++

    Routine description:
        
        Enumerates the section provided by the linker script to find a suitable exception handler for the kernel RIP given.

    Arguments:

        [IN] uint64_t Rip - Address that caused the page fault in kernel mode.

    Return Values:

        Address of exception handler if found, else 0.

--*/

{
    // Begin the search at the first linker-published exception range.
    PEXCEPTION_RANGE Entry = __start_ex_table;

    // We could do a binary search since this is address given, but WHO cares. 
    while (Entry < __stop_ex_table) {
        // Check if the faulting RIP is within the range.
        if (Rip >= Entry->start_addr && Rip < Entry->end_addr) {
            // Found a handler!
            return Entry->handler_addr;
        }

        // Increment entry.
        Entry++;
    }

    // No exception handler found..
    return 0;
}

void
ExpCaptureContextFromTrapFrame(
    IN const TRAP_FRAME* TrapFrame,
    OUT PCONTEXT Context
)

/*++

    Routine description:

        Captures the user-visible integer and control state from the kernel's
        private trap frame into the stable public CONTEXT representation.
        Unsupported and reserved context state is cleared before publication.

    Arguments:

        [IN] TrapFrame - Trap frame containing the state to capture.

        [OUT] Context - Public context structure that receives the captured
        state.

    Return Values:

        None. Invalid internal pointers cause an assertion failure.

--*/

{
    // Catch an invalid internal caller before dereferencing either structure.
    assert(TrapFrame && Context);

    // Clear unsupported and reserved context fields before publishing the state.
    kmemset(Context, 0, sizeof(CONTEXT));

    // Advertise that the complete supported integer and control context exists.
    Context->ContextFlags = MT_CONTEXT_FULL;

    // Capture the accumulator register.
    Context->Rax = TrapFrame->rax;

    // Capture the base register.
    Context->Rbx = TrapFrame->rbx;

    // Capture the counter register.
    Context->Rcx = TrapFrame->rcx;

    // Capture the data register.
    Context->Rdx = TrapFrame->rdx;

    // Capture the source-index register.
    Context->Rsi = TrapFrame->rsi;

    // Capture the destination-index register.
    Context->Rdi = TrapFrame->rdi;

    // Capture the frame-pointer register.
    Context->Rbp = TrapFrame->rbp;

    // Capture the first extended general-purpose register.
    Context->R8 = TrapFrame->r8;

    // Capture the second extended general-purpose register.
    Context->R9 = TrapFrame->r9;

    // Capture the third extended general-purpose register.
    Context->R10 = TrapFrame->r10;

    // Capture the fourth extended general-purpose register.
    Context->R11 = TrapFrame->r11;

    // Capture the fifth extended general-purpose register.
    Context->R12 = TrapFrame->r12;

    // Capture the sixth extended general-purpose register.
    Context->R13 = TrapFrame->r13;

    // Capture the seventh extended general-purpose register.
    Context->R14 = TrapFrame->r14;

    // Capture the eighth extended general-purpose register.
    Context->R15 = TrapFrame->r15;

    // Capture the interrupted instruction pointer.
    Context->Rip = TrapFrame->rip;

    // Capture the interrupted user stack pointer.
    Context->Rsp = TrapFrame->rsp;

    // Capture the interrupted processor flags.
    Context->RFlags = TrapFrame->rflags;

    // Narrow and capture the user code-segment selector.
    Context->SegCs = (uint16_t)TrapFrame->cs;

    // Narrow and capture the user stack-segment selector.
    Context->SegSs = (uint16_t)TrapFrame->ss;
}

MTSTATUS
ExpApplyUserContextToTrapFrame(
    IN const CONTEXT* Context,
    IN OUT PTRAP_FRAME TrapFrame
)

/*++

    Routine description:

        Validates a public user-mode CONTEXT and applies its supported integer
        and control state to a candidate trap frame. Privileged selectors,
        kernel addresses, unsupported context groups, and unsafe RFLAGS bits
        are rejected or sanitized before the caller's trap frame is changed.

    Arguments:

        [IN] Context - User context containing the state to restore.

        [IN OUT] TrapFrame - Kernel trap frame that receives the validated
        user state while retaining CPU-owned trap metadata.

    Return Values:

        MT_SUCCESS if the context was applied successfully.

        MT_INVALID_PARAM if a pointer, required context group, or selector is
        invalid.

        MT_UNSUPPORTED_OP if unsupported context groups were requested.

        MT_INVALID_ADDRESS if RIP or RSP is not a valid user address.

--*/

{
    // Reject an absent public context or destination trap frame.
    if (!Context || !TrapFrame) {
        // Report the invalid internal argument to the caller.
        return MT_INVALID_PARAM;
    }

    // Require every integer and control field needed for a complete restore.
    if ((Context->ContextFlags & MT_CONTEXT_FULL) != MT_CONTEXT_FULL) {
        // Report that the supplied context is incomplete.
        return MT_INVALID_PARAM;
    }

    // Reject context groups that this kernel does not know how to restore.
    if (Context->ContextFlags & ~MT_CONTEXT_FULL) {
        // Report the unsupported restore request without modifying the trap frame.
        return MT_UNSUPPORTED_OP;
    }

    // Require the fixed user-mode code and stack selectors.
    if (Context->SegCs != USER_CS || Context->SegSs != USER_SS) {
        // Reject selectors that could construct an invalid or privileged return.
        return MT_INVALID_PARAM;
    }

    // Require nonzero, canonical instruction and stack pointers in user space.
    if (!Context->Rip || !Context->Rsp ||
        !MI_IS_CANONICAL_ADDR(Context->Rip) ||
        !MI_IS_CANONICAL_ADDR(Context->Rsp) ||
        Context->Rip > MmHighestUserAddress ||
        Context->Rsp > MmHighestUserAddress) {
        // Report that the requested user control address is invalid.
        return MT_INVALID_ADDRESS;
    }

    // Preserve CPU-owned trap metadata in a temporary candidate frame.
    TRAP_FRAME Candidate = *TrapFrame;

    // Force the candidate stack selector to the kernel-defined user selector.
    Candidate.ss = USER_SS;

    // Force the candidate code selector to the kernel-defined user selector.
    Candidate.cs = USER_CS;

    // Restore the accumulator register from the public context.
    Candidate.rax = Context->Rax;

    // Restore the base register from the public context.
    Candidate.rbx = Context->Rbx;

    // Restore the counter register from the public context.
    Candidate.rcx = Context->Rcx;

    // Restore the data register from the public context.
    Candidate.rdx = Context->Rdx;

    // Restore the source-index register from the public context.
    Candidate.rsi = Context->Rsi;

    // Restore the destination-index register from the public context.
    Candidate.rdi = Context->Rdi;

    // Restore the frame-pointer register from the public context.
    Candidate.rbp = Context->Rbp;

    // Restore the first extended general-purpose register.
    Candidate.r8 = Context->R8;

    // Restore the second extended general-purpose register.
    Candidate.r9 = Context->R9;

    // Restore the third extended general-purpose register.
    Candidate.r10 = Context->R10;

    // Restore the fourth extended general-purpose register.
    Candidate.r11 = Context->R11;

    // Restore the fifth extended general-purpose register.
    Candidate.r12 = Context->R12;

    // Restore the sixth extended general-purpose register.
    Candidate.r13 = Context->R13;

    // Restore the seventh extended general-purpose register.
    Candidate.r14 = Context->R14;

    // Restore the eighth extended general-purpose register.
    Candidate.r15 = Context->R15;

    // Restore the validated user instruction pointer.
    Candidate.rip = Context->Rip;

    // Restore the validated user stack pointer.
    Candidate.rsp = Context->Rsp;

    // Keep allowed arithmetic/control flags and force reserved bit 1 plus IF.
    Candidate.rflags = (Context->RFlags & 0x8D5ULL) | 0x202ULL;

    // Publish the fully validated candidate to the real trap frame at once.
    *TrapFrame = Candidate;

    // Report that the context restore was accepted.
    return MT_SUCCESS;
}

#ifdef DEBUG
void
ExpTestContextConversion(
    void
)

/*++

    Routine description:

        Verifies the public CONTEXT conversion boundary without entering an
        actual exception path. In particular, rejected user contexts must not
        partially modify the kernel's trap frame, and CPU-owned trap metadata
        must survive a successful restore.

    Arguments:

        None.

    Return Values:

        None. An assertion identifies the failed invariant.

--*/

{
    // Begin with a cleared synthetic trap frame for deterministic test input.
    TRAP_FRAME Source = { 0 };

    // Seed the accumulator register with a recognizable value.
    Source.rax = 0x01;

    // Seed the base register with a recognizable value.
    Source.rbx = 0x02;

    // Seed the counter register with a recognizable value.
    Source.rcx = 0x03;

    // Seed the data register with a recognizable value.
    Source.rdx = 0x04;

    // Seed the source-index register with a recognizable value.
    Source.rsi = 0x05;

    // Seed the destination-index register with a recognizable value.
    Source.rdi = 0x06;

    // Seed the frame-pointer register with a recognizable value.
    Source.rbp = 0x07;

    // Seed the first extended register with a recognizable value.
    Source.r8 = 0x08;

    // Seed the second extended register with a recognizable value.
    Source.r9 = 0x09;

    // Seed the third extended register with a recognizable value.
    Source.r10 = 0x0A;

    // Seed the fourth extended register with a recognizable value.
    Source.r11 = 0x0B;

    // Seed the fifth extended register with a recognizable value.
    Source.r12 = 0x0C;

    // Seed the sixth extended register with a recognizable value.
    Source.r13 = 0x0D;

    // Seed the seventh extended register with a recognizable value.
    Source.r14 = 0x0E;

    // Seed the eighth extended register with a recognizable value.
    Source.r15 = 0x0F;

    // Seed a valid user instruction pointer.
    Source.rip = 0x00100000;

    // Seed a valid user stack pointer.
    Source.rsp = 0x00180000;

    // Seed the normal user-mode processor flags.
    Source.rflags = USER_RFLAGS;

    // Seed the normal user-mode code selector.
    Source.cs = USER_CS;

    // Seed the normal user-mode stack selector.
    Source.ss = USER_SS;

    // Seed CPU-owned vector metadata that capture must not expose.
    Source.vector = 0x40;

    // Seed CPU-owned error metadata that capture must not expose.
    Source.error_code = 0xBEEF;

    // Allocate the public context that receives the captured state.
    CONTEXT Context;

    // Convert the synthetic private trap frame into the public representation.
    ExpCaptureContextFromTrapFrame(&Source, &Context);

    // Verify that capture advertises exactly the supported complete context.
    assert(Context.ContextFlags == MT_CONTEXT_FULL,
        "context capture published incorrect flags");

    // Verify that capture cleared every reserved ABI field.
    assert(Context.Reserved == 0 && Context.Reserved2 == 0,
        "context capture did not clear reserved fields");

    // Verify that every legacy integer register survived capture.
    assert(Context.Rax == Source.rax && Context.Rbx == Source.rbx &&
        Context.Rcx == Source.rcx && Context.Rdx == Source.rdx &&
        Context.Rsi == Source.rsi && Context.Rdi == Source.rdi &&
        Context.Rbp == Source.rbp,
        "context capture lost a legacy integer register");

    // Verify that every extended integer register survived capture.
    assert(Context.R8 == Source.r8 && Context.R9 == Source.r9 &&
        Context.R10 == Source.r10 && Context.R11 == Source.r11 &&
        Context.R12 == Source.r12 && Context.R13 == Source.r13 &&
        Context.R14 == Source.r14 && Context.R15 == Source.r15,
        "context capture lost an extended integer register");

    // Verify that capture preserved the complete user control state.
    assert(Context.Rip == Source.rip && Context.Rsp == Source.rsp &&
        Context.RFlags == Source.rflags &&
        Context.SegCs == Source.cs && Context.SegSs == Source.ss,
        "context capture lost control state");

    // Verify that unavailable debug-register state was not fabricated.
    assert(Context.Dr0 == 0 && Context.Dr1 == 0 &&
        Context.Dr2 == 0 && Context.Dr3 == 0 &&
        Context.Dr6 == 0 && Context.Dr7 == 0,
        "context capture advertised unavailable debug state");

    // Replace the captured accumulator with a restore-test value.
    Context.Rax = 0x1111111111111111ULL;

    // Replace the captured final extended register with a restore-test value.
    Context.R15 = 0x1515151515151515ULL;

    // Replace the captured instruction pointer with another valid user address.
    Context.Rip = 0x00120000;

    // Replace the captured stack pointer with another valid user address.
    Context.Rsp = 0x00190000;

    // Request every RFLAGS bit so sanitization can be verified.
    Context.RFlags = UINT64_MAX;

    // Allocate the private destination trap frame used for restore testing.
    TRAP_FRAME Destination;

    // Fill the destination with a pattern that reveals accidental field clearing.
    kmemset(&Destination, 0xA5, sizeof(Destination));

    // Preserve recognizable CPU-owned vector metadata in the destination.
    Destination.vector = 0x77;

    // Preserve recognizable CPU-owned error metadata in the destination.
    Destination.error_code = 0x12345678;

    // Apply the valid public context to the synthetic destination trap frame.
    MTSTATUS Status =
        ExpApplyUserContextToTrapFrame(&Context, &Destination);

    // Verify that the valid restore request was accepted.
    assert(Status == MT_SUCCESS,
        "valid user context was rejected");

    // Verify that every legacy integer register survived restoration.
    assert(Destination.rax == Context.Rax &&
        Destination.rbx == Context.Rbx &&
        Destination.rcx == Context.Rcx &&
        Destination.rdx == Context.Rdx &&
        Destination.rsi == Context.Rsi &&
        Destination.rdi == Context.Rdi &&
        Destination.rbp == Context.Rbp,
        "context restore lost a legacy integer register");

    // Verify that every extended integer register survived restoration.
    assert(Destination.r8 == Context.R8 &&
        Destination.r9 == Context.R9 &&
        Destination.r10 == Context.R10 &&
        Destination.r11 == Context.R11 &&
        Destination.r12 == Context.R12 &&
        Destination.r13 == Context.R13 &&
        Destination.r14 == Context.R14 &&
        Destination.r15 == Context.R15,
        "context restore lost an extended integer register");

    // Verify that restoration published the requested user control addresses.
    assert(Destination.rip == Context.Rip &&
        Destination.rsp == Context.Rsp,
        "context restore lost user control state");

    // Verify that restoration forced the fixed user-mode segment selectors.
    assert(Destination.cs == USER_CS && Destination.ss == USER_SS,
        "context restore published privileged selectors");

    // Verify that restoration removed unsafe RFLAGS bits and forced required bits.
    assert(Destination.rflags ==
        ((Context.RFlags & 0x8D5ULL) | 0x202ULL),
        "context restore did not sanitize RFLAGS");

    // Verify that restoration retained CPU-owned trap metadata.
    assert(Destination.vector == 0x77 &&
        Destination.error_code == 0x12345678,
        "context restore damaged CPU-owned trap metadata");

    // Save the accepted frame so every rejection test can prove atomic failure.
    TRAP_FRAME UnchangedFrame = Destination;

    // Copy the valid public context for the incomplete-context rejection test.
    CONTEXT InvalidContext = Context;

    // Remove the integer group so the context no longer satisfies MT_CONTEXT_FULL.
    InvalidContext.ContextFlags = MT_CONTEXT_CONTROL;

    // Attempt to apply the deliberately incomplete context.
    Status = ExpApplyUserContextToTrapFrame(
        &InvalidContext,
        &Destination
    );

    // Verify rejection and prove that no partial trap-frame update occurred.
    assert(Status == MT_INVALID_PARAM &&
        kmemcmp(&Destination, &UnchangedFrame, sizeof(Destination)) == 0,
        "incomplete context partially modified the trap frame");

    // Restore the valid baseline for the unsupported-group rejection test.
    InvalidContext = Context;

    // Request debug-register restoration, which is not implemented yet.
    InvalidContext.ContextFlags |= MT_CONTEXT_DEBUG_REGISTERS;

    // Attempt to apply the context containing the unsupported group.
    Status = ExpApplyUserContextToTrapFrame(
        &InvalidContext,
        &Destination
    );

    // Verify the unsupported result and atomic preservation of the destination.
    assert(Status == MT_UNSUPPORTED_OP &&
        kmemcmp(&Destination, &UnchangedFrame, sizeof(Destination)) == 0,
        "unsupported context flags partially modified the trap frame");

    // Restore the valid baseline for the privileged-selector rejection test.
    InvalidContext = Context;

    // Replace the user code selector with the kernel code selector.
    InvalidContext.SegCs = KERNEL_CS;

    // Attempt to apply the context containing a privileged selector.
    Status = ExpApplyUserContextToTrapFrame(
        &InvalidContext,
        &Destination
    );

    // Verify selector rejection and atomic preservation of the destination.
    assert(Status == MT_INVALID_PARAM &&
        kmemcmp(&Destination, &UnchangedFrame, sizeof(Destination)) == 0,
        "kernel selector partially modified the trap frame");

    // Restore the valid baseline for the kernel-address rejection test.
    InvalidContext = Context;

    // Replace the user instruction pointer with the first system-space address.
    InvalidContext.Rip = MmSystemRangeStart;

    // Attempt to apply the context containing a kernel instruction pointer.
    Status = ExpApplyUserContextToTrapFrame(
        &InvalidContext,
        &Destination
    );

    // Verify address rejection and atomic preservation of the destination.
    assert(Status == MT_INVALID_ADDRESS &&
        kmemcmp(&Destination, &UnchangedFrame, sizeof(Destination)) == 0,
        "kernel RIP partially modified the trap frame");

    // Restore the valid baseline for the null-stack rejection test.
    InvalidContext = Context;

    // Replace the user stack pointer with the invalid null address.
    InvalidContext.Rsp = 0;

    // Attempt to apply the context containing a null stack pointer.
    Status = ExpApplyUserContextToTrapFrame(
        &InvalidContext,
        &Destination
    );

    // Verify address rejection and atomic preservation of the destination.
    assert(Status == MT_INVALID_ADDRESS &&
        kmemcmp(&Destination, &UnchangedFrame, sizeof(Destination)) == 0,
        "invalid user RSP partially modified the trap frame");

    // Verify that a null public-context pointer is rejected.
    assert(ExpApplyUserContextToTrapFrame(NULL, &Destination) ==
        MT_INVALID_PARAM,
        "NULL context was accepted");

    // Verify that a null trap-frame pointer is rejected.
    assert(ExpApplyUserContextToTrapFrame(&Context, NULL) ==
        MT_INVALID_PARAM,
        "NULL trap frame was accepted");

    // Announce that every context conversion invariant passed.
    gop_printf(COLOR_GREEN, "EXCEPTION CONTEXT TEST PASS\n");
}
#endif

void
ExpInitializeExceptionRecord(
    MTSTATUS Status,
    const TRAP_FRAME* TrapFrame,
    PEXCEPTION_RECORD ExceptionRecord
)

/*++

    Routine description:

        Initializes a generic exception record for an exception raised at the
        instruction identified by the supplied trap frame. Generic records do
        not publish exception-specific information parameters.

    Arguments:

        [IN] Status - Exception status to publish as the exception code.

        [IN] TrapFrame - Trap frame containing the faulting instruction RIP.

        [OUT] ExceptionRecord - Record that receives the initialized exception
        information.

    Return Values:

        None.

--*/

{
    // Clear every generic field and exception-information slot in the record.
    kmemset(ExceptionRecord, 0, sizeof(EXCEPTION_RECORD));

    // Publish the raised status as the exception code.
    ExceptionRecord->ExceptionCode = Status;

    // Publish the interrupted instruction as the exception address.
    ExceptionRecord->ExceptionAddress = (void*)TrapFrame->rip;
};

void
ExpInitializeAccessViolationRecord(
    MTSTATUS Status,
    const TRAP_FRAME* TrapFrame,
    uint64_t FaultAddress,
    uint64_t PageFaultErrorCode,
    PEXCEPTION_RECORD ExceptionRecord
)

/*++

    Routine description:

        Initializes an access-violation exception record and describes whether
        the processor attempted to read, write, or execute the faulting virtual
        address. Execute faults take precedence when several error bits exist.

    Arguments:

        [IN] Status - Access-violation status to publish.

        [IN] TrapFrame - Trap frame containing the faulting instruction RIP.

        [IN] FaultAddress - Virtual address accessed by the instruction.

        [IN] PageFaultErrorCode - Processor page-fault error code used to
        determine the attempted operation.

        [OUT] ExceptionRecord - Record that receives the access-violation
        parameters.

    Return Values:

        None.

--*/

{
    // Clear every field before constructing the access-violation record.
    kmemset(ExceptionRecord, 0, sizeof(EXCEPTION_RECORD));

    // Publish the access-violation status as the exception code.
    ExceptionRecord->ExceptionCode = Status;

    // Publish the instruction that attempted the invalid memory access.
    ExceptionRecord->ExceptionAddress = (void*)TrapFrame->rip;

    // Advertise the operation and virtual-address information parameters.
    ExceptionRecord->NumberParameters = 2;

    // Store the faulting virtual address in the second information slot.
    ExceptionRecord->ExceptionInformation[1] = FaultAddress;

    // Give an instruction-fetch fault precedence over the other operation bits.
    if (PageFaultErrorCode & (1ULL << 4)) {
        // Encode the access as an execute operation.
        ExceptionRecord->ExceptionInformation[0] = 8; // Execute
    }

    // Otherwise, check whether the processor reported a write operation.
    else if (PageFaultErrorCode & (1ULL << 1)) {
        // Encode the access as a write operation.
        ExceptionRecord->ExceptionInformation[0] = 1; // Write
    }

    // Treat the remaining page-fault operation as a read.
    else {
        // Encode the access as a read operation.
        ExceptionRecord->ExceptionInformation[0] = 0; // Read
    }
}

MTSTATUS
ExpPublishUserException(
    IN const EXCEPTION_RECORD* ExceptionRecord
)


/*++

    Routine description:

        Publishes a fully initialized exception record into the current thread's
        single pending-exception slot. The record is copied before pending state
        is exposed with release ordering. Invalid records are rejected, while an
        active or already-pending exception triggers the initial deterministic
        termination policy.

    Arguments:

        [IN] ExceptionRecord - Fully initialized exception record to publish.

    Return Values:

        MT_SUCCESS if the exception record was copied and published.

        MT_INVALID_PARAM if the record pointer, parameter count, or flags are
        invalid.

        MT_NOT_IMPLEMENTED if the record contains a chained exception.

        An active or already-pending exception terminates the current thread
        with MT_INVALID_STATE under the current no-nesting policy.

--*/

{
    // Reject invalid exception parameters or a null pointer.
    if (!ExceptionRecord || ExceptionRecord->NumberParameters > MT_EXCEPTION_MAXIMUM_PARAMETERS) {
        return MT_INVALID_PARAM;
    }

    // For now reject chained exceptions
    if (ExceptionRecord->ExceptionRecord) {
        return MT_NOT_IMPLEMENTED;
    }

    PETHREAD Thread = PsGetCurrentThread();
    
    // Reject unknown flags
    if ((ExceptionRecord->ExceptionFlags & ~MT_EXCEPTION_VALID_FLAGS) != 0) {
        return MT_INVALID_PARAM;
    }

    bool UserPending = InterlockedLoadAcquire(&Thread->InternalThread.UserExceptionPending);
    bool UserActive = InterlockedLoadAcquire(&Thread->InternalThread.UserExceptionActive);
    bool APCActive = Thread->InternalThread.UserApcActive;

    if (UserActive) {
        // There must not be any nesting exceptions (currently).
        PspExitThread(MT_INVALID_STATE);
    }

    if (UserPending) {
        // Kernel attempted double publish of an exception before actually running it
        PspExitThread(MT_INVALID_STATE);
    }

    if (APCActive) {
        // We cannot publish a pending exception if we are in an APC, meaning probably the APC faulted.
        PspExitThread(MT_APC_ERROR);
    }

    // Copy the current exception to the pending exception
    Thread->InternalThread.PendingExceptionRecord = *ExceptionRecord;

    // Publish the pending exception, so it will be executed on next schedule run.
    InterlockedStoreRelease(&Thread->InternalThread.UserExceptionPending, true);

    return MT_SUCCESS;
}

#ifdef DEBUG
void
ExpTestExceptionRecordConstruction(
    void
)

/*++

    Routine description:

        Verifies the stable EXCEPTION_RECORD construction contract before
        exception delivery is connected to the page-fault path.

    Arguments:

        None.

    Return Values:

        None. An assertion identifies the failed invariant.

--*/

{
    // Begin with a cleared synthetic trap frame for deterministic record input.
    TRAP_FRAME TrapFrame = { 0 };

    // Allocate the exception record reused by every construction test.
    EXCEPTION_RECORD Record;

    // Select a recognizable user virtual address for access-violation tests.
    const uintptr_t FaultAddress = 0x0000000000153000ULL;

    // Select a recognizable user instruction as the reported exception address.
    TrapFrame.rip = 0x0000000000115000ULL;

    // Poison the record so stale fields remain visible after initialization.
    kmemset(&Record, 0xA5, sizeof(Record));

    // Construct a generic exception record from the synthetic trap frame.
    ExpInitializeExceptionRecord(
        MT_INVALID_PARAM,
        &TrapFrame,
        &Record
    );

    // Verify that the generic record preserved its requested exception status.
    assert(Record.ExceptionCode == (uint32_t)MT_INVALID_PARAM,
        "generic exception record lost its status");

    // Verify that the generic record used the interrupted instruction address.
    assert(Record.ExceptionAddress == (void*)TrapFrame.rip,
        "generic exception record used the wrong faulting RIP");

    // Verify that generic exception metadata was cleared rather than left stale.
    assert(Record.ExceptionFlags == 0 &&
        Record.ExceptionRecord == NULL &&
        Record.NumberParameters == 0 &&
        Record.Reserved == 0,
        "generic exception record metadata was not cleared");

    // Inspect every fixed exception-information slot for stale data.
    for (uint32_t Index = 0;
         Index < MT_EXCEPTION_MAXIMUM_PARAMETERS;
         Index++) {
        // Verify that this generic-record information slot was cleared.
        assert(Record.ExceptionInformation[Index] == 0,
            "generic exception record retained stale parameters");
    }

    // Poison the record again before constructing a read access violation.
    kmemset(&Record, 0xA5, sizeof(Record));

    // Construct an access violation without write or execute error bits.
    ExpInitializeAccessViolationRecord(
        MT_ACCESS_VIOLATION,
        &TrapFrame,
        FaultAddress,
        0,
        &Record
    );

    // Verify the read violation's status and interrupted instruction address.
    assert(Record.ExceptionCode == (uint32_t)MT_ACCESS_VIOLATION &&
        Record.ExceptionAddress == (void*)TrapFrame.rip,
        "read access violation lost its status or faulting RIP");

    // Verify that the access violation advertises exactly two parameters.
    assert(Record.NumberParameters == 2,
        "access violation did not publish two parameters");

    // Verify the read-operation code and faulting virtual address parameters.
    assert(Record.ExceptionInformation[0] == 0 &&
        Record.ExceptionInformation[1] == FaultAddress,
        "read access violation parameters are invalid");

    // Reconstruct the record with the processor write bit set.
    ExpInitializeAccessViolationRecord(
        MT_ACCESS_VIOLATION,
        &TrapFrame,
        FaultAddress,
        1ULL << 1,
        &Record
    );

    // Verify that the page-fault write bit produced operation code one.
    assert(Record.ExceptionInformation[0] == 1,
        "write access violation did not publish operation 1");

    // Reconstruct the record with both execute and write bits set.
    ExpInitializeAccessViolationRecord(
        MT_ACCESS_VIOLATION,
        &TrapFrame,
        FaultAddress,
        (1ULL << 4) | (1ULL << 1),
        &Record
    );

    // Verify that execute takes precedence and produces operation code eight.
    assert(Record.ExceptionInformation[0] == 8,
        "execute access violation did not publish operation 8");

    // Announce that every exception-record construction invariant passed.
    gop_printf(COLOR_GREEN, "EXCEPTION RECORD TEST PASS\n");
}

void
ExpTestUserExceptionPublication(
    void
)

/*++

    Routine description:

        Verifies accepted exception flags, rejected record shapes, exact record
        copying, pending-state publication, and failure atomicity for the
        current thread's single pending-exception slot. Fatal active and
        double-pending policies are exercised by disposable stress threads.

    Arguments:

        None.

    Return Values:

        None. An assertion identifies the failed invariant.

--*/

{
    // Select the current thread whose private publication slot is under test.
    PITHREAD Thread = MeGetCurrentThread();

    // Require an idle exception state before borrowing the publication slot.
    assert(!InterlockedLoadAcquire(&Thread->UserExceptionPending) &&
        !InterlockedLoadAcquire(&Thread->UserExceptionActive),
        "exception publication test requires an idle thread");

    // Preserve the thread's existing record bytes for exact cleanup.
    EXCEPTION_RECORD SavedRecord = Thread->PendingExceptionRecord;

    // Construct a recognizable generic record accepted by the publisher.
    EXCEPTION_RECORD Record = { 0 };
    Record.ExceptionCode = (uint32_t)MT_INVALID_PARAM;
    Record.ExceptionAddress = (void*)(uintptr_t)0x115000;
    Record.NumberParameters = 3;
    Record.ExceptionInformation[0] = 0x1111111111111111ULL;
    Record.ExceptionInformation[1] = 0x2222222222222222ULL;
    Record.ExceptionInformation[2] = 0x3333333333333333ULL;

    // Publish a continuable record with no exception flags set.
    MTSTATUS Status = ExpPublishUserException(&Record);

    // Verify success and acquire the record bytes through the pending flag.
    assert(Status == MT_SUCCESS &&
        InterlockedLoadAcquire(&Thread->UserExceptionPending),
        "continuable exception publication failed");

    // Verify that the complete caller record was copied without translation.
    assert(kmemcmp(
        &Thread->PendingExceptionRecord,
        &Record,
        sizeof(Record)
    ) == 0,
        "exception publisher changed the record contents");

    // Return the borrowed slot to idle before testing another valid record.
    InterlockedStoreRelease(&Thread->UserExceptionPending, false);

    // Select the only currently supported nonzero exception flag.
    Record.ExceptionFlags = MT_EXCEPTION_NONCONTINUABLE;

    // Publish a valid noncontinuable record through the same slot.
    Status = ExpPublishUserException(&Record);

    // Verify that the supported noncontinuable flag is accepted and preserved.
    assert(Status == MT_SUCCESS &&
        InterlockedLoadAcquire(&Thread->UserExceptionPending) &&
        Thread->PendingExceptionRecord.ExceptionFlags ==
            MT_EXCEPTION_NONCONTINUABLE,
        "noncontinuable exception publication failed");

    // Clear pending state and restore the saved bytes for failure-atomicity tests.
    InterlockedStoreRelease(&Thread->UserExceptionPending, false);
    Thread->PendingExceptionRecord = SavedRecord;

    // Add an unsupported flag bit that must be rejected before publication.
    Record.ExceptionFlags = 0x80000000U;

    // Attempt to publish the record containing the unsupported flag.
    Status = ExpPublishUserException(&Record);

    // Verify the expected error without consuming or modifying the idle slot.
    assert(Status == MT_INVALID_PARAM &&
        !InterlockedLoadAcquire(&Thread->UserExceptionPending) &&
        kmemcmp(
            &Thread->PendingExceptionRecord,
            &SavedRecord,
            sizeof(SavedRecord)
        ) == 0,
        "unknown exception flags changed publication state");

    // Restore valid flags and exceed the fixed exception-parameter capacity.
    Record.ExceptionFlags = 0;
    Record.NumberParameters = MT_EXCEPTION_MAXIMUM_PARAMETERS + 1U;

    // Attempt to publish the oversized exception record.
    Status = ExpPublishUserException(&Record);

    // Verify that oversized metadata is rejected without publishing anything.
    assert(Status == MT_INVALID_PARAM &&
        !InterlockedLoadAcquire(&Thread->UserExceptionPending) &&
        kmemcmp(
            &Thread->PendingExceptionRecord,
            &SavedRecord,
            sizeof(SavedRecord)
        ) == 0,
        "oversized exception record changed publication state");

    // Restore the parameter count and create a deliberately chained record.
    Record.NumberParameters = 3;
    Record.ExceptionRecord = &Record;

    // Attempt to publish chaining before nested records are implemented.
    Status = ExpPublishUserException(&Record);

    // Verify the explicit unsupported result and unchanged publication slot.
    assert(Status == MT_NOT_IMPLEMENTED &&
        !InterlockedLoadAcquire(&Thread->UserExceptionPending) &&
        kmemcmp(
            &Thread->PendingExceptionRecord,
            &SavedRecord,
            sizeof(SavedRecord)
        ) == 0,
        "chained exception changed publication state");

    // Verify that a missing record is rejected without changing thread state.
    Status = ExpPublishUserException(NULL);
    assert(Status == MT_INVALID_PARAM &&
        !InterlockedLoadAcquire(&Thread->UserExceptionPending),
        "null exception record changed publication state");

    // Restore the exact record bytes borrowed from the current thread.
    Thread->PendingExceptionRecord = SavedRecord;

    // Announce that all nonfatal publication-contract checks passed.
    gop_printf(COLOR_GREEN, "EXCEPTION PUBLICATION TEST PASS\n");
}
#endif

static MTSTATUS
ExppPrepareUserExceptionDispatch(
    IN PITHREAD Thread,
    IN OUT PTRAP_FRAME TrapFrame
)

/*++

    Routine description:

        Private worker that prepares delivery of a specified thread's pending
        user exception. The routine captures the interrupted context,
        constructs the shared exception-dispatch frame on that thread's user
        stack, and redirects the supplied trap frame to MTDLL's
        MeUserExceptionDispatcher entrypoint.

        The pending and active state is published only after the user-stack
        write succeeds, so an unsuccessful preparation leaves the exception
        available to the caller for failure handling.

    Arguments:

        [IN] Thread - Internal thread that owns the pending exception, process,
        and user stack used by this dispatch operation.

        [IN OUT] TrapFrame - Complete CPL3 return frame to capture and redirect
        to the user-mode exception dispatcher.

    Return Values:

        MT_SUCCESS if the user exception frame was prepared successfully.

        MT_INVALID_STATE if no valid user exception dispatch can begin.

        An exception status if writing the dispatch frame to the user stack
        faults.

--*/

{
    // Reject missing state, a non-user return, recursive delivery, or no pending exception.
    if (!Thread || !TrapFrame ||
        (TrapFrame->cs & 0x3) != 3 ||
        InterlockedLoadAcquire(&Thread->UserExceptionActive) == true ||
        InterlockedLoadAcquire(&Thread->UserExceptionPending) == false) {
        // Report that exception delivery cannot begin in the current state.
        return MT_INVALID_STATE;
    }

    // Recover the executive thread that owns the user stack and process image.
    PETHREAD EThread = PsGetEThreadFromIThread(Thread);

    // Reject an internal thread without its required executive-thread owner.
    if (!EThread) {
        // Report that exception delivery cannot resolve its process or user stack.
        return MT_INVALID_STATE;
    }

    // Allocate the complete shared dispatch-frame image on the kernel stack.
    EXCEPTION_DISPATCH_FRAME DispatchFrame;

    // Copy the kernel-owned pending exception record into the dispatch image.
    DispatchFrame.ExceptionRecord = Thread->PendingExceptionRecord;

    // Capture the interrupted user state into the dispatch image's public CONTEXT.
    ExpCaptureContextFromTrapFrame(
        TrapFrame,
        &DispatchFrame.ContextRecord
    );

    // Resolve the private MTDLL exception dispatcher for the current process.
    uint64_t ExceptionDispatcherAddress = (uint64_t)PspFindMtdllEntryAddress(
        EXCEPTION_DISPATCHER_NAME,
        EThread
    );

    // Reject a missing, noncanonical, or non-user MTDLL dispatcher entry.
    if (!ExceptionDispatcherAddress ||
        !MI_IS_CANONICAL_ADDR(ExceptionDispatcherAddress) ||
        ExceptionDispatcherAddress > MmHighestUserAddress) {
        // Report that no valid user exception dispatch target is available.
        return MT_INVALID_STATE;
    }

    // Read the exclusive upper boundary of the thread's allocated user stack.
    uint64_t StackBase = (uint64_t)Thread->StackBase;

    // Read the number of bytes reserved for the thread's allocated user stack.
    uint64_t StackSize = (uint64_t)EThread->UserStackSize;

    // Reject absent or arithmetically impossible user-stack metadata.
    if (!StackBase || !StackSize || StackSize > StackBase) {
        // Report that a trustworthy user-stack range cannot be constructed.
        return MT_INVALID_STATE;
    }

    // Calculate the inclusive lower boundary without allowing subtraction wrap.
    uint64_t StackLimit = StackBase - StackSize;

    // Calculate the final byte below the stack's exclusive upper boundary.
    uint64_t StackLastByte = StackBase - 1;

    // Reject stack metadata that does not describe one canonical user range.
    if (!MI_IS_CANONICAL_ADDR(StackLimit) ||
        !MI_IS_CANONICAL_ADDR(StackLastByte) ||
        StackLimit > MmHighestUserAddress ||
        StackLastByte > MmHighestUserAddress) {
        // Report that the stored user-stack range crosses an invalid boundary.
        return MT_INVALID_STATE;
    }

    // Read the interrupted user stack pointer before performing any arithmetic.
    uint64_t OriginalRsp = TrapFrame->rsp;

    // Reject a noncanonical, non-user, or out-of-stack interrupted stack pointer.
    if (!MI_IS_CANONICAL_ADDR(OriginalRsp) ||
        OriginalRsp > MmHighestUserAddress ||
        OriginalRsp < StackLimit ||
        OriginalRsp > StackBase) {
        // Report that the interrupted context does not own a usable user stack.
        return MT_INVALID_STATE;
    }

    // Prove that preserving the SysV red zone cannot underflow the address.
    if (OriginalRsp < 128) {
        // Report that no address remains below the required red zone.
        return MT_INVALID_STATE;
    }

    // Preserve the 128-byte SysV red zone below the interrupted user RSP.
    uint64_t FrameAddress = OriginalRsp - 128;

    // Prove that reserving the dispatch image cannot underflow the address.
    if (FrameAddress < sizeof(EXCEPTION_DISPATCH_FRAME)) {
        // Report that the dispatch image cannot fit below the red zone.
        return MT_INVALID_STATE;
    }

    // Reserve space below the red zone for the complete dispatch-frame image.
    FrameAddress -= sizeof(EXCEPTION_DISPATCH_FRAME);

    // Align the dispatch-frame address down to a 16-byte boundary.
    FrameAddress &= ~0xFULL;

    // Prove that reserving the synthetic return slot cannot underflow the address.
    if (FrameAddress < sizeof(uint64_t)) {
        // Report that the synthetic return slot cannot fit below the dispatch image.
        return MT_INVALID_STATE;
    }

    // Reserve the synthetic return slot required for SysV function-entry alignment.
    uint64_t NewRsp = FrameAddress - sizeof(uint64_t);

    // Prove that calculating the dispatch image's exclusive end cannot overflow.
    if (sizeof(EXCEPTION_DISPATCH_FRAME) > UINT64_MAX - FrameAddress) {
        // Report that the complete destination range cannot be represented.
        return MT_INVALID_STATE;
    }

    // Calculate the exclusive end of the dispatch image written to user memory.
    uint64_t FrameEnd = FrameAddress + sizeof(EXCEPTION_DISPATCH_FRAME);

    // Validate the complete half-open destination range [NewRsp, FrameEnd).
    if (!MI_IS_CANONICAL_ADDR(NewRsp) ||
        !MI_IS_CANONICAL_ADDR(FrameEnd - 1) ||
        NewRsp < StackLimit ||
        FrameEnd > StackBase ||
        FrameEnd - 1 > MmHighestUserAddress) {
        // Report that the complete exception frame does not fit on the user stack.
        return MT_INVALID_STATE;
    }

    // Prepare a terminating sentinel for the synthetic dispatcher return slot.
    const uint64_t SyntheticReturnAddress = 0;

    // Attempt every user-memory write under the same protected exception scope.
    try {
        // Write the zero return sentinel into the synthetic function-entry slot.
        kmemcpy(
            (void*)NewRsp,
            &SyntheticReturnAddress,
            sizeof(SyntheticReturnAddress)
        );

        // Write the complete dispatch image immediately above the return slot.
        kmemcpy((void*)FrameAddress, &DispatchFrame, sizeof(EXCEPTION_DISPATCH_FRAME));
    }
    except{
        // Leave the original trap frame and pending-exception state unchanged.
        return GetExceptionCode();
    }
    end_try;

    // Preserve the original return frame until all fallible user-memory work succeeds.
    TRAP_FRAME Candidate = *TrapFrame;

    // Redirect the user instruction pointer to MTDLL's exception dispatcher.
    Candidate.rip = ExceptionDispatcherAddress;

    // Publish the synthetic function-entry stack pointer in the candidate frame.
    Candidate.rsp = NewRsp;

    // Pass the user-stack exception-record address as the first SysV argument.
    Candidate.rdi = FrameAddress + FIELD_OFFSET(EXCEPTION_DISPATCH_FRAME, ExceptionRecord);

    // Pass the user-stack context-record address as the second SysV argument.
    Candidate.rsi = FrameAddress + FIELD_OFFSET(EXCEPTION_DISPATCH_FRAME, ContextRecord);

    // Prevent local preemption while the dispatch state and return frame are published.
    bool InterruptsEnabled = MeDisableInterrupts();

    // Consume the pending exception now that its user-stack image exists.
    Thread->UserExceptionPending = false;

    // Mark the thread as executing inside the user exception dispatcher.
    Thread->UserExceptionActive = true;

    // Atomically expose the completed candidate to the return-to-user path.
    *TrapFrame = Candidate;

    // Restore the interrupt-enable state observed on entry to publication.
    MeEnableInterrupts(InterruptsEnabled);

    // Report that exception delivery was prepared successfully.
    return MT_SUCCESS;
}

MTSTATUS
ExpPrepareUserExceptionDispatch(
    IN OUT PTRAP_FRAME TrapFrame
)

/*++

    Routine description:

        Prepares the current thread's pending user exception for delivery. This
        public entrypoint binds the private preparation worker to the current
        thread so production callers cannot prepare another thread's user
        return frame.

    Arguments:

        [IN OUT] TrapFrame - Complete CPL3 return frame to capture and redirect
        to the user-mode exception dispatcher.

    Return Values:

        Status returned by the private exception-dispatch preparation worker.

--*/

{
    // Prepare exception delivery using the thread executing on this processor.
    return ExppPrepareUserExceptionDispatch(
        MeGetCurrentThread(),
        TrapFrame
    );
}

#ifdef DEBUG
void
ExpTestUserExceptionDispatchFrame(
    IN PETHREAD TargetThread
)

/*++

    Routine description:

        Verifies exception-dispatch frame preparation against a real mapped
        user stack without returning to the unfinished MTDLL dispatcher. The
        target must be a stable blocked user thread owned by another thread.

        A local copy of the target's saved CPL3 frame is redirected, leaving
        the scheduler-owned frame unchanged. The routine then inspects the
        bytes written into the target process, verifies the complete ABI
        layout, and exercises an insufficient-stack failure that must not
        consume the pending exception or modify its candidate frame.

    Arguments:

        [IN] TargetThread - Referenced, blocked user thread whose process and
        mapped user stack are used by the test.

    Return Values:

        None. An assertion identifies the failed invariant.

--*/

{
    // Reject an invalid, system, or current-thread test target.
    assert(TargetThread != NULL &&
        !TargetThread->SystemThread &&
        &TargetThread->InternalThread != MeGetCurrentThread(),
        "exception frame test received an invalid user target");

    // Select the internal thread state consumed by the preparation worker.
    PITHREAD Thread = &TargetThread->InternalThread;

    // Require an idle exception state before borrowing it for the test.
    assert(!Thread->UserExceptionPending && !Thread->UserExceptionActive,
        "exception frame test target already owns exception state");

    // Preserve the target's existing pending-record storage for exact cleanup.
    EXCEPTION_RECORD SavedPendingRecord = Thread->PendingExceptionRecord;

    // Copy the scheduler-owned CPL3 continuation instead of modifying it.
    TRAP_FRAME OriginalFrame = Thread->TrapRegisters;

    // Require the blocked syscall to have published a complete user return frame.
    assert((OriginalFrame.cs & 0x3) == 3,
        "exception frame test target has no saved CPL3 continuation");

    // Construct a recognizable pending record from the saved user instruction.
    ExpInitializeExceptionRecord(
        MT_INVALID_PARAM,
        &OriginalFrame,
        &Thread->PendingExceptionRecord
    );

    // Publish the test record before making its pending bit visible.
    Thread->UserExceptionPending = true;

    // Copy the original frame so successful preparation remains non-destructive.
    TRAP_FRAME PreparedFrame = OriginalFrame;

    // Attach the controller to the target process before touching its user stack.
    APC_STATE ApcState;
    MeAttachProcess(&TargetThread->ParentProcess->InternalProcess, &ApcState);

    // Prepare the copied frame and write the dispatch image to mapped user memory.
    MTSTATUS Status = ExppPrepareUserExceptionDispatch(
        Thread,
        &PreparedFrame
    );

    // Require successful preparation against the real target stack and MTDLL.
    assert(Status == MT_SUCCESS,
        "mapped user exception frame preparation failed");

    // Recover the frame address immediately above the synthetic return slot.
    uint64_t FrameAddress = PreparedFrame.rsp + sizeof(uint64_t);

    // Initialize local inspection storage with values that reveal failed copies.
    uint64_t SyntheticReturnAddress = UINT64_MAX;
    EXCEPTION_DISPATCH_FRAME UserDispatchFrame;
    kmemset(&UserDispatchFrame, 0xA5, sizeof(UserDispatchFrame));

    // Begin with a successful inspection status before probing copied bytes.
    MTSTATUS InspectionStatus = MT_SUCCESS;

    // Read both mapped user-stack objects under the kernel exception-table guard.
    try {
        // Copy the synthetic return slot into trusted kernel storage.
        kmemcpy(
            &SyntheticReturnAddress,
            (void*)PreparedFrame.rsp,
            sizeof(SyntheticReturnAddress)
        );

        // Copy the complete user dispatch image into trusted kernel storage.
        kmemcpy(
            &UserDispatchFrame,
            (void*)FrameAddress,
            sizeof(UserDispatchFrame)
        );
    }
    except{
        // Preserve the inspection fault for an assertion after leaving the guard.
        InspectionStatus = GetExceptionCode();
    }
    end_try;

    // Require every mapped user-stack byte to be readable after preparation.
    assert(InspectionStatus == MT_SUCCESS,
        "prepared user exception frame could not be inspected");

    // Verify the SysV function-entry alignment supplied to the dispatcher.
    assert((PreparedFrame.rsp & 0xFULL) == 8,
        "exception dispatcher stack is not SysV function-entry aligned");

    // Verify that an accidental dispatcher return reaches the null sentinel.
    assert(SyntheticReturnAddress == 0,
        "exception dispatcher synthetic return slot was not cleared");

    // Verify that the first dispatcher argument names the copied exception record.
    assert(PreparedFrame.rdi == FrameAddress +
        FIELD_OFFSET(EXCEPTION_DISPATCH_FRAME, ExceptionRecord),
        "exception dispatcher received the wrong exception-record pointer");

    // Verify that the second dispatcher argument names the copied public context.
    assert(PreparedFrame.rsi == FrameAddress +
        FIELD_OFFSET(EXCEPTION_DISPATCH_FRAME, ContextRecord),
        "exception dispatcher received the wrong context-record pointer");

    // Verify that the complete dispatch image resides below the preserved red zone.
    assert(FrameAddress + sizeof(EXCEPTION_DISPATCH_FRAME) <=
        OriginalFrame.rsp - 128,
        "exception dispatch image overlaps the interrupted SysV red zone");

    // Verify that the kernel-owned pending record was copied without alteration.
    assert(kmemcmp(
        &UserDispatchFrame.ExceptionRecord,
        &Thread->PendingExceptionRecord,
        sizeof(EXCEPTION_RECORD)
    ) == 0,
        "exception dispatch image contains the wrong exception record");

    // Construct the context expected from the original saved CPL3 continuation.
    CONTEXT ExpectedContext;
    ExpCaptureContextFromTrapFrame(&OriginalFrame, &ExpectedContext);

    // Verify that the user-stack context exactly describes the interrupted state.
    assert(kmemcmp(
        &UserDispatchFrame.ContextRecord,
        &ExpectedContext,
        sizeof(CONTEXT)
    ) == 0,
        "exception dispatch image contains the wrong public context");

    // Verify that successful preparation consumed pending and published active.
    assert(!Thread->UserExceptionPending && Thread->UserExceptionActive,
        "successful exception preparation published the wrong thread state");

    // Return the borrowed thread state to idle before exercising failure.
    Thread->UserExceptionActive = false;

    // Reconstruct the pending record used by the insufficient-stack test.
    ExpInitializeExceptionRecord(
        MT_INVALID_PARAM,
        &OriginalFrame,
        &Thread->PendingExceptionRecord
    );

    // Republish pending state for the deliberate preparation failure.
    Thread->UserExceptionPending = true;

    // Copy the original frame and place RSP exactly at the stack's lower bound.
    TRAP_FRAME RejectedFrame = OriginalFrame;
    RejectedFrame.rsp = (uint64_t)Thread->StackBase -
        TargetThread->UserStackSize;

    // Preserve the invalid candidate so failure atomicity can be verified.
    TRAP_FRAME UnchangedRejectedFrame = RejectedFrame;

    // Attempt preparation where no red zone or dispatch image can fit.
    Status = ExppPrepareUserExceptionDispatch(Thread, &RejectedFrame);

    // Verify deterministic rejection without partial candidate-frame mutation.
    assert(Status == MT_INVALID_STATE &&
        kmemcmp(
            &RejectedFrame,
            &UnchangedRejectedFrame,
            sizeof(RejectedFrame)
        ) == 0,
        "invalid exception stack partially modified the return frame");

    // Verify that failed preparation left the pending exception available.
    assert(Thread->UserExceptionPending && !Thread->UserExceptionActive,
        "invalid exception stack consumed or activated pending state");

    // Restore the target's original exception-state storage after the test.
    Thread->UserExceptionPending = false;
    Thread->UserExceptionActive = false;
    Thread->PendingExceptionRecord = SavedPendingRecord;

    // Restore the controller's original process address-space attachment.
    MeDetachProcess(&ApcState);

    // Announce that mapped user-stack preparation and failure atomicity passed.
    gop_printf(COLOR_GREEN, "EXCEPTION DISPATCH FRAME TEST PASS\n");
}
#endif
