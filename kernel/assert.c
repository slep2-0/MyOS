#include "assert.h"

__attribute__((noreturn))
void assert_fail(const char* expr, const char* reason, const char* file, const char* func, int line) {
    // Getting here means a runtime assertion has failed (assert())
    (void)(func);

    // Check if expr is 0 or 1 (only) to make it true/false for readability.
    if (!kstrcmp(expr, "0")) {
        expr = "false";
    }
    if (!kstrcmp(expr, "1")) {
        expr = "true";
    }

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