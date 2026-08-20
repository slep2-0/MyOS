#include "includes/me.h"
#include "includes/mg.h"
#include "assert.h"

NORETURN void assert_fail(const char* expr, const char* reason, const char* file, const char* func, int line)

/*++

    Routine description:

        Reports a failed kernel assertion and stops execution.

    Arguments:

        [IN] expr - Assertion expression that evaluated to false.
        [IN] reason - Loader notification or failure reason.
        [IN] file - Source file containing the assertion.
        [IN] func - Routine containing the assertion.
        [IN] line - Source line containing the assertion.

    Return Values:

        None.

--*/

{
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
