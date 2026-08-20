#include "exception_chain_test.h"
#include "mtnative.h"

#define MTP_TRANSLATED_APP_EXCEPTION 0xE8060300u
#define MTP_TRANSLATED_APP_FAILURE   ((MTSTATUS)0xC8060301)
#define MTP_TRANSLATED_APP_ARGUMENT_BASE ((uintptr_t)0x80603000u)

static int
MtpTranslatedApplicationFilter(
    PEXCEPTION_POINTERS Information,
    volatile uint32_t* FilterCount,
    const uintptr_t* ExpectedArguments
)

/*++

    Routine description:

        Evaluates the translated application exception filter.

    Arguments:

        [IN] Information - Exception information supplied to the filter.
        [IN] FilterCount - Number of filter entries.
        [IN] ExpectedArguments - Expected arguments.

    Return Values:

        The exception-filter action selected for the exception.

--*/

{
    (*FilterCount)++;
    if (!Information || !Information->ExceptionRecord ||
        !Information->ContextRecord ||
        Information->ExceptionRecord->ExceptionCode !=
            MTP_TRANSLATED_APP_EXCEPTION ||
        Information->ExceptionRecord->ExceptionFlags != 0 ||
        Information->ExceptionRecord->ExceptionRecord != NULL ||
        !Information->ExceptionRecord->ExceptionAddress ||
        !MT_IS_CANONICAL_USER_ADDRESS(
            Information->ExceptionRecord->ExceptionAddress
        ) ||
        Information->ExceptionRecord->NumberParameters !=
            MT_EXCEPTION_MAXIMUM_PARAMETERS) {
        return MT_EXCEPTION_EXECUTE_HANDLER;
    }

    for (uint32_t Index = 0;
         Index < MT_EXCEPTION_MAXIMUM_PARAMETERS;
         Index++) {
        if (Information->ExceptionRecord->ExceptionInformation[Index] !=
            ExpectedArguments[Index]) {
            return MT_EXCEPTION_EXECUTE_HANDLER;
        }
    }

    return MT_EXCEPTION_CONTINUE_EXECUTION;
}

int
main(
    void
)

/*++

    Routine description:

        Runs the program test scenario and returns its result to the loader.

    Arguments:

        None.

    Return Values:

        Zero on successful completion, or a nonzero program failure code.

--*/

{
    volatile uint32_t FilterCount = 0;
    uintptr_t Arguments[MT_EXCEPTION_MAXIMUM_PARAMETERS + 2];

    for (uint32_t Index = 0;
         Index < MT_EXCEPTION_MAXIMUM_PARAMETERS + 2;
         Index++) {
        Arguments[Index] = MTP_TRANSLATED_APP_ARGUMENT_BASE + Index;
    }

    __try {
        RaiseException(
            MTP_TRANSLATED_APP_EXCEPTION,
            0x80000000u,
            MT_EXCEPTION_MAXIMUM_PARAMETERS + 2,
            Arguments
        );
    }
    __except (
        MtpTranslatedApplicationFilter(
            GetExceptionInformation(),
            &FilterCount,
            Arguments
        )
    ) {
        MtTerminateProcess(MtCurrentProcess(), MTP_TRANSLATED_APP_FAILURE);
        UNREACHABLE_CODE();
    }

    if (FilterCount != 1) {
        MtTerminateProcess(MtCurrentProcess(), MTP_TRANSLATED_APP_FAILURE);
        UNREACHABLE_CODE();
    }

    MTSTATUS Status = MtpRunExceptionChainTests();
    if (MT_FAILURE(Status)) {
        MtTerminateProcess(MtCurrentProcess(), Status);
        for (;;) {
            __asm__ volatile ("pause");
        }
    }

    return 0;
}
