#include "tls_fixture.h"

#define TLS_DLL_INITIAL_VALUE 0x2132435465768798ULL

TLS_TEST_DLL_ENTRY
bool
DllMain(
    IN void* ModuleBase,
    IN DLL_REASON Reason,
    IN void* Reserved
)
{
    if (Reason == DLL_PROCESS_DETACH) {
        // TLS must remain available until DllMain returns.
        (void)TlsDllReadInitialized();
        (void)TlsDllReadZeroFilled();
        (void)TlsDllReadAligned();
        (void)TlsDllAlignedAddress();
        return true;
    }

    if (Reason != DLL_PROCESS_ATTACH) {
        return true;
    }

    if (!ModuleBase || Reserved) {
        return false;
    }

    return TlsDllReadInitialized() == TLS_DLL_INITIAL_VALUE &&
        TlsDllReadZeroFilled() == 0 &&
        TlsDllReadAligned() == 0 &&
        (TlsDllAlignedAddress() & 63u) == 0;
}
