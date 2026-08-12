#include <MatanelOS.h>
#include <mtnative.h>
#include <mtstatus.h>

#include "loader_test.h"

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
    MTSTATUS Status = MtpRunLoaderTests();
    TerminateProcess(MtCurrentProcess(), Status);

    // Self-termination must not return.
    for (;;) {
    }
}
