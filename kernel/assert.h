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
#include "bugcheck/bugcheck.h"
__attribute__((noreturn))
static void assert_fail(const char* expr, const char* reason, const char* file, const char* func, int line) {
    // Getting here means a runtime assertion has failed (assert())
    BUGCHECK_ADDITIONALS addt = { 0 };

    // It can be versatile, with a reason or not.
    if (reason) {
        ksnprintf(addt.str, sizeof(addt.str), "An assertion has failed (%s)\nReason: %s\nLocation: %s:%d, function %s", expr, reason, file, line, func);
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        MtBugcheckEx(&ctx, NULL, ASSERTION_FAILURE, &addt, true);
    }
    else {
        ksnprintf(addt.str, sizeof(addt.str), "An assertion has failed (%s)\nLocation: %s:%d, function %s", expr, file, line, func);
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        MtBugcheckEx(&ctx, NULL, ASSERTION_FAILURE, &addt, true);
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

// assert(expression) OR assert(expression, "expression must be ...")
#define assert(...) GET_MACRO(__VA_ARGS__, ASSERT2, ASSERT1)(__VA_ARGS__)

#else
// Make intellisense be quiet.

// assert(expression) OR assert(expression, "expression must be ...")
#define assert(...) do { } while(0)
#endif

#endif