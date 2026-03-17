// Include standard header.
#include "../../headers/MatanelOS.h"

int main(void) {
    // Lets attempt to create usermode.txt, write Hello, World! to it, and then read from it into memory allocated (making use of all of the syscalls right now, including MtTerminateProcess in return)
    // If at any point we fail we will terminate the program with the status that failed.
    volatile int counter = 0;
    HANDLE FileHandle = CreateFile("group.txt", MT_FILE_ALL_ACCESS);
    if (FileHandle == MT_INVALID_HANDLE) {
        goto failure;
    }

    // Write.
    char Hello[] = "ascendz mcdonalds adiravraham ofirs";
    // the sizeof operator shouldnt be used here as it is a text file, and so it would include null termination which is not used.
    // instead, we would need strlen, but my os doesnt have a user standard library yet (i plan to implement it in mtdll)
    bool Worked = WriteFile(FileHandle, 0, Hello, strlen(Hello), NULL);
    if (!Worked) {
        goto failure;
    }

    // Allocate.
    void* BaseAddress = VirtualAlloc(NULL, sizeof(Hello), PAGE_EXECUTE_READWRITE);

    // Read
    Worked = ReadFile(FileHandle, 0, BaseAddress, sizeof(Hello), NULL);
    if (!Worked) {
        goto failure;
    }

    // Done, infinite loop.
    goto success;

failure:
    // todo GetLastError
    TerminateProcess(MtCurrentProcess(), MT_GENERAL_FAILURE);
success:
    while (true) {
        counter++;
        __asm__ volatile ("pause");
    }
    return 0;
}
