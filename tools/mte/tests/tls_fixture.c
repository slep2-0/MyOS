#include <stdint.h>

__thread uint64_t TlsInitialized = 0x1122334455667788ULL;
__thread uint64_t TlsZeroFilled;

__attribute__((section(".text.mtapi"), visibility("default"), used))
uint64_t
TlsFixtureRead(void)
{
    return TlsInitialized + TlsZeroFilled;
}

__attribute__((section(".text.mtapi"), visibility("default"), used))
void
TlsFixtureWrite(
    uint64_t Value
)
{
    TlsZeroFilled = Value;
}
