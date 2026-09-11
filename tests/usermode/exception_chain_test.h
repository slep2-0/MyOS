#ifndef MATANELOS_USERMODE_EXCEPTION_CHAIN_TEST_H
#define MATANELOS_USERMODE_EXCEPTION_CHAIN_TEST_H

#include "MatanelOS.h"
#include "mtstatus.h"

/* Test-only MTDLL entrypoint. It is absent from ordinary MTDLL builds. */
MTDLL_API MTSTATUS
MtpRunExceptionChainTests(
    void
);

#endif /* MATANELOS_USERMODE_EXCEPTION_CHAIN_TEST_H */
