/*
 * MTDLL_API gives an exported MTDLL routine a normal C function type.
 *
 * While MTDLL itself is compiled, the section attribute is retained in the
 * intermediate ELF. The MTE packer discovers that section and builds the
 * custom export directory. Applications see an ordinary extern declaration;
 * LLD creates a PLT/GOT import only when the function is actually referenced.
 */
#ifndef MATANELOS_USER_MTAPI_H
#define MATANELOS_USER_MTAPI_H

#if defined(MTDLL_BUILD) && (defined(__clang__) || defined(__GNUC__))
#define MTDLL_API \
    __attribute__((visibility("default"), section(".text.mtapi")))
#else
#define MTDLL_API
#endif

#endif /* MATANELOS_USER_MTAPI_H */
