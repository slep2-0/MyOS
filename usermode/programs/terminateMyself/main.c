// Include standard header.
#include "../../headers/MatanelOS.h"
#include "../../headers/mtstatus.h"

volatile int GlobalVarData = 1;
volatile int GlobalVarBss;

uint32_t MyThread(void* ThreadParameter) {
    (void)(ThreadParameter);

    // Syscall once, but in a while loop because we are cool.
    bool spammy = false;
    void* wastemem;
    while (true) {
        if (!spammy) {
            spammy = true;
            wastemem = VirtualAlloc(NULL, 4095, PAGE_EXECUTE_READWRITE);
        }

        if (spammy) {
            USER_PROTECTION_TYPE oldprot;
            bool wastemem2 = VirtualProtect((void*)wastemem, 4095, PAGE_READWRITE, &oldprot);
            return 0;
        }
    }

    return 1;
}

int main(void) {
    // Lets attempt to create usermode.txt, write Hello, World! to it, and then read from it into memory allocated (making use of all of the syscalls right now, including MtTerminateProcess in return)
    // If at any point we fail we will terminate the program with the status that failed.
    volatile int counter = 0;
    HANDLE FileHandle = CreateFile("group.txt", MT_FILE_ALL_ACCESS);
    MTSTATUS ExitCode = MT_GENERAL_FAILURE;

    if (FileHandle == MT_INVALID_HANDLE) {
        ExitCode = MT_INVALID_HANDLE;
        goto failure;
    }

    // Write.
    char Hello[] = "ascendz mcdonalds adiravraham ofirs";
    // the sizeof operator shouldnt be used here as it is a text file, and so it would include null termination which is not used.
    // instead, we would need strlen, but my os doesnt have a user standard library yet (i plan to implement it in mtdll)
    bool Worked = WriteFile(FileHandle, 0, Hello, strlen(Hello), NULL);
    if (!Worked) {
        ExitCode = MT_FAT32_INVALID_FILENAME;
        goto failure;
    }

    // Allocate.
    void* BaseAddress = VirtualAlloc(NULL, strlen(Hello), PAGE_EXECUTE_READWRITE);

    // Read
    Worked = ReadFile(FileHandle, 0, BaseAddress, strlen(Hello), NULL);
    if (!Worked) {
        ExitCode = MT_ACCESS_DENIED;
        goto failure;
    }

    // Check if the address is the same.
    MEMORY_BASIC_INFORMATION Information;
    bool ok = VirtualQuery(BaseAddress, &Information);

    if (!ok || Information.Protection != PAGE_EXECUTE_READWRITE) {
        //ExitCode = MT_NO_MEMORY;
        goto failure;
    }

    // Protect it to PAGE_READWRITE only.
    USER_PROTECTION_TYPE OldProtection;
    ok = VirtualProtect(BaseAddress, strlen(Hello), PAGE_READWRITE, &OldProtection);

    if (!ok || OldProtection != PAGE_EXECUTE_READWRITE) {
        ExitCode = MT_DEVICE_ERROR;
        goto failure;
    }

    // Free it.
    ok = VirtualFree(BaseAddress, 0, MEM_RELEASE);

    if (!ok) {
        ExitCode = MT_AHCI_TIMEOUT;
        goto failure;
    }

    // Create thread.
    CreateThread((THREAD_START_ROUTINE)MyThread, NULL);

    // Done, infinite loop.
    goto success;

failure:
    TerminateProcess(MtCurrentProcess(), GetLastError());
success:
    while (true) {
        counter++;
        __asm__ volatile ("pause");
    }
    return 0;
}
