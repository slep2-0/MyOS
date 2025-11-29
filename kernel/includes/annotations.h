#ifndef X86_ANNOTATIONS_H
#define X86_ANNOTATIONS_H

// Annotations (and macros) for documentation, and potential future analyzing.

// Parameter Annotations
#define IN // Takes REQUIRED INPUT
#define OUT // Supplies REQUIRED OUTPUT
#define _In_Opt // Takes OPTIONAL INPUT if given.
#define _Out_Opt // OPTIONALLY Supplies OUTPUT if given.

// Function will not return.
#define NORETURN __attribute__((noreturn))

// Function will be forcefully inlined by the compiler.
#ifndef FORCEINLINE
#if defined(__clang__) || defined(__GNUC__)
#define FORCEINLINE static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define FORCEINLINE static __forceinline
#else
#define FORCEINLINE static inline
#endif
#endif

// Function will be forcefully inlined by the compiler. (between translation files)
#ifndef FORCEINLINE_NOHEADER
#if defined(__clang__) || defined(__GNUC__)
#define FORCEINLINE_NOHEADER inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define FORCEINLINE_NOHEADER __forceinline
#else
#define FORCEINLINE_NOHEADER inline
#endif
#endif

// Function / Object is signaled as used, even though it is not used in any translation unit.
#define USED __attribute__((used))

#endif