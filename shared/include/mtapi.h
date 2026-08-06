/*
 * PROJECT:      MatanelOS
 * LICENSE:      GPLv3
 * PURPOSE:      MTDLL public API import and export annotation.
 */

#ifndef MATANELOS_SHARED_MTAPI_H
#define MATANELOS_SHARED_MTAPI_H

/*
 * MTDLL and its consumers compile against the same declarations.
 *
 * MTDLL definitions are published through .text.mtapi while applications see
 * ordinary external declarations. The MTE packer turns referenced undefined
 * symbols into imports from the dependency that exports them.
 */
#if defined(MATANELOS_BUILDING_MTDLL) && \
    (defined(__clang__) || defined(__GNUC__))
#define MTDLL_API \
    __attribute__((visibility("default"), section(".text.mtapi")))
#else
#define MTDLL_API
#endif

#endif /* MATANELOS_SHARED_MTAPI_H */
