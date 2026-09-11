#ifndef MATANELOS_TLS_FIXTURE_H
#define MATANELOS_TLS_FIXTURE_H

#include <MatanelOS.h>

#if defined(MATANELOS_BUILDING_TLS_TEST_DLL)
#define TLS_TEST_DLL_API \
    __attribute__((section(".text.mtapi"), used, visibility("default")))
#else
#define TLS_TEST_DLL_API
#endif

#define TLS_TEST_DLL_ENTRY \
    __attribute__((used, visibility("default")))

TLS_TEST_DLL_API
uint64_t
TlsDllReadInitialized(
    void
);

TLS_TEST_DLL_API
uint64_t
TlsDllReadZeroFilled(
    void
);

TLS_TEST_DLL_API
uint8_t
TlsDllReadAligned(
    void
);

TLS_TEST_DLL_API
uintptr_t
TlsDllAlignedAddress(
    void
);

TLS_TEST_DLL_API
void
TlsDllWrite(
    IN uint64_t Initialized,
    IN uint64_t ZeroFilled,
    IN uint8_t Aligned
);

#endif /* MATANELOS_TLS_FIXTURE_H */
