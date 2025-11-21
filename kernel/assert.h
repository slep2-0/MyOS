/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		Runtime Assertion Implementation.
 */

#ifndef X86_ASSERT_H
#define X86_ASSERT_H

#ifdef DEBUG
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "includes/me.h"

__attribute__((noreturn))
static void assert_fail(const char* expr, const char* reason, const char* file, const char* func, int line) {
    // Getting here means a runtime assertion has failed (assert())
    (void)(func);
    // It can be versatile, with a reason or not.
    if (reason) {
        MeBugCheckEx(ASSERTION_FAILURE, (void*)expr, (void*)reason, (void*)file, (void*)(uintptr_t)line);
    }
    else {
        reason = "NO_REASON_SPECIFIED";
        MeBugCheckEx(ASSERTION_FAILURE, (void*)expr, (void*)reason, (void*)file, (void*)(uintptr_t)line);
    }

    __builtin_unreachable();
}

// Helper macros for argument counting
#define GET_MACRO(_1,_2,NAME,...) NAME

// Base macros
#define ASSERT1(expr) \
    ((expr) ? (void)0 : assert_fail(#expr, NULL, __FILE__, __func__, __LINE__))

#define ASSERT2(expr, reason) \
    ((expr) ? (void)0 : assert_fail(#expr, reason, __FILE__, __func__, __LINE__))

// assert(expression) OR assert(expression, "expression must be true")
#define assert(...) GET_MACRO(__VA_ARGS__, ASSERT2, ASSERT1)(__VA_ARGS__)

#else
// assert(expression) OR assert(expression, "expression must be true")
#define assert(...) do { } while(0)
#endif

#endif