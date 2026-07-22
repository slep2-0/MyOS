#ifndef MATANELOS_SHARED_ANNOTATIONS_H
#define MATANELOS_SHARED_ANNOTATIONS_H

#include <stddef.h>

#define IN
#define OUT
#define _In_Opt
#define _Out_Opt

#ifndef FORCEINLINE
#if defined(__clang__) || defined(__GNUC__)
#define FORCEINLINE static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define FORCEINLINE static __forceinline
#else
#define FORCEINLINE static inline
#endif
#endif

#ifndef FORCEINLINE_NOHEADER
#if defined(__clang__) || defined(__GNUC__)
#define FORCEINLINE_NOHEADER __attribute__((always_inline))
#elif defined(_MSC_VER)
#define FORCEINLINE_NOHEADER __forceinline
#else
#define FORCEINLINE_NOHEADER inline
#endif
#endif

#if defined(__clang__) || defined(__GNUC__)

#define NORETURN __attribute__((noreturn))
#define USED __attribute__((used))
#define MUST_USE_RESULT __attribute__((warn_unused_result))
#define COLD __attribute__((cold))
#define HOT __attribute__((hot))
#define PACKED __attribute__((packed))
#define COMPILE_WARNING(msg) __attribute__((warning(msg)))
#define COMPILE_ERROR(msg) __attribute__((error(msg)))

#if defined(__i386__) || defined(__x86_64__)
#define SYSV_ABI __attribute__((sysv_abi))
#define MS_ABI __attribute__((ms_abi))
#else
#define SYSV_ABI
#define MS_ABI
#endif

#define NOINLINE __attribute__((noinline))
#define UNREACHABLE_CODE() __builtin_unreachable()

#elif defined(_MSC_VER)

#define NORETURN __declspec(noreturn)
#define USED

#if defined(__cplusplus) &&                              \
    ((defined(_MSVC_LANG) && _MSVC_LANG >= 201703L) || \
     (__cplusplus >= 201703L))
#define MUST_USE_RESULT [[nodiscard]]
#elif defined(_Check_return_)
#define MUST_USE_RESULT _Check_return_
#else
#define MUST_USE_RESULT
#endif

#define COLD
#define HOT
#define PACKED
#define COMPILE_WARNING(msg) __declspec(deprecated(msg))
#define COMPILE_ERROR(msg)
#define SYSV_ABI
#define MS_ABI
#define NOINLINE __declspec(noinline)
#define UNREACHABLE_CODE() __assume(0)

#else

#define NORETURN
#define USED
#define MUST_USE_RESULT
#define COLD
#define HOT
#define PACKED
#define COMPILE_WARNING(msg)
#define COMPILE_ERROR(msg)
#define SYSV_ABI
#define MS_ABI
#define NOINLINE
#define UNREACHABLE_CODE() ((void)0)

#endif

#ifdef __cplusplus
#define STATIC_ASSERT(cond, msg) static_assert((cond), msg)
#else
#define STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#endif

#define VALIDATE_SIZE(struc, size) \
    STATIC_ASSERT(sizeof(struc) == (size), "Invalid structure size of " #struc)

#define VALIDATE_OFFSET(struc, member, offset)                        \
    STATIC_ASSERT(                                                   \
        offsetof(struc, member) == (offset),                         \
        "The offset of " #member " in " #struc " is not " #offset \
    )

#endif /* MATANELOS_SHARED_ANNOTATIONS_H */
