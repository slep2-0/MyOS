#ifndef MATANELOS_INTELLISENSE_STDDEF_H
#define MATANELOS_INTELLISENSE_STDDEF_H

/* Model the kernel's x86-64 target rather than the Windows host ABI. */
typedef unsigned long long size_t;
typedef signed long long ptrdiff_t;

#ifndef NULL
#define NULL 0
#endif

#ifndef offsetof
#define offsetof(type, member) ((size_t)&(((type*)0)->member))
#endif

#ifndef SIZE_MAX
#define SIZE_MAX 0xffffffffffffffffULL
#endif

#endif
