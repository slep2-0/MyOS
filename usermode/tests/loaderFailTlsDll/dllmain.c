#include "tls_fixture.h"

TLS_TEST_DLL_ENTRY
bool
DllMain(
    IN void* ModuleBase,
    IN DLL_REASON Reason,
    IN void* Reserved
)
{
    if (Reason != DLL_PROCESS_ATTACH) return true;
    if (!ModuleBase || Reserved) return false;

    // Materialize this module's TLS before rejecting the load.
    (void)TlsDllReadInitialized();
    (void)TlsDllReadZeroFilled();
    (void)TlsDllReadAligned();
    return false;
}
