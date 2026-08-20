#ifndef MATANELOS_USERMODE_LOADER_FIXTURE_H
#define MATANELOS_USERMODE_LOADER_FIXTURE_H

#include <MatanelOS.h>

#if defined(MATANELOS_BUILDING_LOADER_TEST_DLL)
#define LOADER_TEST_DLL_API \
    __attribute__((section(".text.mtapi"), used, visibility("default")))
#else
#define LOADER_TEST_DLL_API
#endif

#define LOADER_TEST_DLL_ENTRY \
    __attribute__((used, visibility("default")))

#define LOADER_TEST_QUERY_RESULT 0x00010006u

MTDLL_API void
MtpLoaderTestRecordReason(
    IN DLL_REASON Reason
);

LOADER_TEST_DLL_API uint32_t
LoaderTestQueryState(
    void
);

LOADER_TEST_DLL_API uint32_t
LoaderNoEntryQueryState(
    void
);

#endif /* MATANELOS_USERMODE_LOADER_FIXTURE_H */
