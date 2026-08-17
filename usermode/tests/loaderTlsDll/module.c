#include "tls_fixture.h"

#define TLS_DLL_INITIAL_VALUE 0x2132435465768798ULL

static __thread volatile uint64_t TlsDllInitialized = TLS_DLL_INITIAL_VALUE;
static __thread volatile uint64_t TlsDllZeroFilled;
static __thread __attribute__((aligned(64))) volatile uint8_t TlsDllAligned;

TLS_TEST_DLL_API
uint64_t
TlsDllReadInitialized(
    void
)
{
    return TlsDllInitialized;
}

TLS_TEST_DLL_API
uint64_t
TlsDllReadZeroFilled(
    void
)
{
    return TlsDllZeroFilled;
}

TLS_TEST_DLL_API
uint8_t
TlsDllReadAligned(
    void
)
{
    return TlsDllAligned;
}

TLS_TEST_DLL_API
uintptr_t
TlsDllAlignedAddress(
    void
)
{
    return (uintptr_t)&TlsDllAligned;
}

TLS_TEST_DLL_API
void
TlsDllWrite(
    IN uint64_t Initialized,
    IN uint64_t ZeroFilled,
    IN uint8_t Aligned
)
{
    TlsDllInitialized = Initialized;
    TlsDllZeroFilled = ZeroFilled;
    TlsDllAligned = Aligned;
}
