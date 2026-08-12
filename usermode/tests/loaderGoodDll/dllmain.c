#include "loader_fixture.h"

static volatile uint32_t LoaderTestAttachCount;
static const char LoaderTestMessage[] = "loader";
static const char* LoaderTestMessagePointer = LoaderTestMessage;

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
    MtpLoaderTestRecordReason(Reason);

    if (Reason == DLL_PROCESS_ATTACH) {
        if (!ModuleBase || Reserved) {
            return false;
        }

        LoaderTestAttachCount++;
    }

    return true;
}

LOADER_TEST_DLL_API
uint32_t
LoaderTestQueryState(
    void
)

/*++

    Routine description:

        Returns the process-attach count and exported test-message length.

    Arguments:

        None.

    Return Values:

        The encoded attach count and exported message length.

--*/

{
    return (LoaderTestAttachCount << 16) |
        (uint32_t)strlen(LoaderTestMessagePointer);
}
