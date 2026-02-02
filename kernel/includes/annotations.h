#ifndef X86_ANNOTATIONS_H
#define X86_ANNOTATIONS_H

// Annotations (and macros) for documentation, and potential future analyzing.

// Parameter Annotations
#define IN // Takes REQUIRED INPUT
#define OUT // Supplies REQUIRED OUTPUT
#define _In_Opt // Optional input (NULL allowed)
#define _Out_Opt // Optional output (NULL allowed)

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
#define FORCEINLINE_NOHEADER __attribute__((always_inline))
#elif defined(_MSC_VER)
#define FORCEINLINE_NOHEADER __forceinline
#else
#define FORCEINLINE_NOHEADER inline
#endif
#endif

// Function / Object is signaled as used, even though it is not used in any translation unit.
#define USED __attribute__((used))

// Caller MUST use return value (e.g., pool allocation, status codes)
#define MUST_USE_RESULT __attribute__((warn_unused_result))

// Function is cold (unlikely to be executed)
#define COLD __attribute__((cold))

// Function is hot (frequently executed)
#define HOT __attribute__((hot))

// Object or type is packed (no padding)
#define PACKED __attribute__((packed))

// Emit compile-time error if condition is false
#define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)

// Force a compile-time warning
#define COMPILE_WARNING(msg) __attribute__((warning(msg)))

// Force a compile-time error
#define COMPILE_ERROR(msg) __attribute__((error(msg)))

// SysV ABI (explicit) (always used in this kernel)
#define SYSV_ABI __attribute__((sysv_abi))

// MS ABI (for interop)
#define MS_ABI __attribute__((ms_abi))

// Prevent function from being inlined
#define NOINLINE __attribute__((noinline))

// Execution must never reach this point, reaching it is undefined behavior.
#define UNREACHABLE_CODE() __builtin_unreachable()

// Ensures the size of struct 'struc' must be 'size' size in bytes.
#define VALIDATE_SIZE(struc, size) static_assert(sizeof(struc) == size, "Invalid structure size of " #struc)

// Ensures the offset of 'member' field in the struct 'struc', must be 'offset' bytes from the start.
#define VALIDATE_OFFSET(struc, member, offset) static_assert(offsetof(struc, member) == offset, "The offset of " #member " in " #struc " is not " #offset "...")

#endif