#include <MatanelOS.h>
#include <mtstatus.h>

#include "process_test.h"

static const char ExpectedCommandLine[] =
    "processChild.mtexe alpha \"two words\" \"\"";
static const char ExpectedEnvironment[] =
    "PROCESS_TEST=child\0"
    "SECOND=two\0";

static
PTEB
ProcessTestCurrentTeb(
    void
)

/*++

    Routine description:

        Returns the TEB published in the current thread's user GS base.

    Arguments:

        None.

    Return Values:

        The current thread's TEB pointer.

--*/

{
    void* Teb;
    __asm__ volatile ("rdgsbase %0" : "=r"(Teb));
    return (PTEB)Teb;
}

NORETURN
static void
ProcessTestExit(
    IN MTSTATUS Status
)

/*++

    Routine description:

        Terminates the process child with the supplied test status.

    Arguments:

        [IN] Status - Status returned to the process-test parent.

    Return Values:

        None.

--*/

{
    TerminateProcess(MtCurrentProcess(), (uint32_t)Status);
    for (;;) {
    }
}

int
main(
    IN int argc,
    IN char** argv
)

/*++

    Routine description:

        Validates captured child parameters or runs the delayed orphan-child
        lifetime case.

    Arguments:

        [IN] argc - Number of parsed command-line arguments.
        [IN] argv - Parsed command-line argument vector.

    Return Values:

        This routine terminates the process with its test result.

--*/

{
    if (argc == 2 && argv && argv[2] == NULL &&
        strcmp(argv[0], "processChild.mtexe") == 0 &&
        strcmp(argv[1], "--orphan") == 0) {
        // Keep this process alive while its parent exits and destroys its
        // handle table. The kernel controller adopts only a test reference.
        Sleep(2000);
        ProcessTestExit(MT_SUCCESS);
    }

    if (argc != 4 || !argv || argv[4] != NULL ||
        strcmp(argv[0], "processChild.mtexe") != 0 ||
        strcmp(argv[1], "alpha") != 0 ||
        strcmp(argv[2], "two words") != 0 ||
        strcmp(argv[3], "") != 0) {
        ProcessTestExit(MT_PROCESS_TEST_CHILD_ARGUMENTS);
    }

    PTEB Teb = ProcessTestCurrentTeb();
    PMT_PROCESS_PARAMETERS Parameters = Teb && Teb->ProcessEnvironmentBlock
        ? Teb->ProcessEnvironmentBlock->ProcessParameters
        : NULL;
    if (!Parameters ||
        strcmp(Parameters->ImagePath, "processChild.mtexe") != 0 ||
        strcmp(Parameters->CommandLine, ExpectedCommandLine) != 0 ||
        strcmp(Parameters->CurrentDirectory, "process-test-dir") != 0) {
        ProcessTestExit(MT_PROCESS_TEST_CHILD_PARAMETERS);
    }

    if (Parameters->EnvironmentSize != sizeof(ExpectedEnvironment)) {
        ProcessTestExit(MT_PROCESS_TEST_CHILD_ENVIRONMENT);
    }
    for (size_t Index = 0; Index < sizeof(ExpectedEnvironment); Index++) {
        if (Parameters->Environment[Index] != ExpectedEnvironment[Index]) {
            ProcessTestExit(MT_PROCESS_TEST_CHILD_ENVIRONMENT);
        }
    }

    ProcessTestExit(MT_SUCCESS);
}
