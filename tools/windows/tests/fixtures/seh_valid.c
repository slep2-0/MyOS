#include "MatanelOS.h"

int
SehValidFixture(
    int Value
)
{
    __try {
        while (Value < 2) {
            Value++;
            if (Value == 1) continue;
        }

        __try {
            Value += 3;
        }
        __except (
            GetExceptionCode() == 0x1234
                ? MT_EXCEPTION_EXECUTE_HANDLER
                : MT_EXCEPTION_CONTINUE_SEARCH
        ) {
            Value = (int)GetExceptionInformation()->ExceptionRecord->ExceptionCode;
        }
    }
    __except (MT_EXCEPTION_EXECUTE_HANDLER) {
        PEXCEPTION_POINTERS Information = GetExceptionInformation();
        return Information ? Value : -1;
    }

    return Value;
}
