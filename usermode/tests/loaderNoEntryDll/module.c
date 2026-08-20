#include "loader_fixture.h"

LOADER_TEST_DLL_API
uint32_t
LoaderNoEntryQueryState(
    void
)

/*++

    Routine description:

        Returns the fixed state exported by the library that has no entry point.

    Arguments:

        None.

    Return Values:

        The fixed state value exported by the no-entry test library.

--*/

{
    return LOADER_TEST_QUERY_RESULT;
}
