#include "loader_fixture.h"

LOADER_TEST_DLL_ENTRY
bool
DllMain(
    IN void* ModuleBase,
    IN DLL_REASON Reason,
    IN void* Reserved
)
{
    (void)ModuleBase;
    (void)Reserved;
    return Reason != DLL_PROCESS_ATTACH;
}

LOADER_TEST_DLL_API
uint32_t
LoaderTestRejectedImageProbe(
    void
)
{
    return 0;
}
