#include "mtapi.h"

int FixtureValue = 0x12345678;
int* FixturePointer = &FixtureValue;

MTDLL_API int
FixtureExport(void)
{
    return *FixturePointer;
}
