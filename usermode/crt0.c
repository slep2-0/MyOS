#include "../shared/include/MatanelOS.h"

extern int main(int argc, char** argv);

// Replaces the crt0.S, since, its easier
// why did I even choose GAS tbh..
NORETURN
void
_start(
    PMT_PROCESS_PARAMETERS Parameters
)
{
    // Call main with ARGC and ARGV
    // If it is void or int main()
    // i.e, not taking argc or argv, then
    // the function will just override these registers anyway
    int Result = main(
        Parameters->ArgumentCount,
        Parameters->ArgumentVector
    );

    TerminateProcess(
        MtCurrentProcess(),
        Result == 0 ? MT_SUCCESS : MT_GENERAL_FAILURE
    );

    for (;;) {
    }
}