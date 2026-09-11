#include "loader_fixture.h"

LOADER_TEST_DLL_ENTRY
bool
DllMain(
    IN void* ModuleBase,
    IN DLL_REASON Reason,
    IN void* Reserved
)

/*++

    Routine description:

        Handles process attach and detach notifications for the test library.

    Arguments:

        [IN] ModuleBase - Mapped base address of the module.
        [IN] Reason - Loader notification or failure reason.
        [IN] Reserved - Loader-defined context reserved for future notifications.

    Return Values:

        A nonzero value to accept the loader notification, or zero to reject process attachment.

--*/

{
    (void)ModuleBase;
    (void)Reserved;

    // Keep one real MTDLL import so rejected-load rollback releases it.
    (void)GetLastError();
    return Reason != DLL_PROCESS_ATTACH;
}

LOADER_TEST_DLL_API
uint32_t
LoaderTestRejectedImageProbe(
    void
)

/*++

    Routine description:

        Records whether code from a library that should be rejected was executed.

    Arguments:

        None.

    Return Values:

        Zero; reaching the routine itself marks the rejected image as incorrectly loaded.

--*/

{
    return 0;
}
