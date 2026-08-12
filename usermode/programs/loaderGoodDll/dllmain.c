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
{
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
{
    return (LoaderTestAttachCount << 16) |
        (uint32_t)strlen(LoaderTestMessagePointer);
}
