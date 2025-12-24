// Include standard header.
#include "../../headers/MatanelOS.h"

int main(void) {
    // Lets attempt to create usermode.txt, write Hello, World! to it, and then read from it into memory allocated (making use of all of the syscalls right now, including MtTerminateProcess in return)
    // If at any point we fail we will terminate the program with the status that failed.
    volatile int counter = 0;
    HANDLE FileHandle;
    MTSTATUS Status = MtCreateFile("akflame.txt", MT_FILE_ALL_ACCESS, &FileHandle);
    if (MT_FAILURE(Status)) {
        goto failure;
    }

    // Write.
    char Hello[] = "ascendz mcdonalds adiravraham ofirs";
    Status = MtWriteFile(FileHandle, 0, Hello, sizeof(Hello), NULL);
    if (MT_FAILURE(Status)) {
        goto failure;
    }

    // Allocate.
    void* BaseAddress = NULL;
    Status = MtAllocateVirtualMemory(MtCurrentProcess(), &BaseAddress, sizeof(Hello), PAGE_READWRITE);
    if (MT_FAILURE(Status)) {
        goto failure;
    }

    // Read
    Status = MtReadFile(FileHandle, 0, BaseAddress, sizeof(Hello), NULL);
    if (MT_FAILURE(Status)) {
        goto failure;
    }

    // Done, infinite loop.
    goto success;

failure:
    MtTerminateProcess(MtCurrentProcess(), Status);
success:
    while (true) {
        counter++;
        __asm__ volatile ("pause");
    }
    return 0;
}
