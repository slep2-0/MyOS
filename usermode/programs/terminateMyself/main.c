// Include standard header.
#include "../../headers/MatanelOS.h"
#include "../../headers/mtstatus.h"

/// Colors definitions for easier access
#define COLOR_RED        0xFFFF0000
#define COLOR_GREEN      0xFF00FF00
#define COLOR_BLUE       0xFF0000FF
#define COLOR_WHITE      0xFFFFFFFF
#define COLOR_BLACK      0xFF000000
#define COLOR_YELLOW     0xFFFFFF00
#define COLOR_CYAN       0xFF00FFFF
#define COLOR_MAGENTA    0xFFFF00FF
#define COLOR_GRAY       0xFF808080
#define COLOR_DARK_GRAY  0xFF404040
#define COLOR_LIGHT_GRAY 0xFFD3D3D3
#define COLOR_ORANGE     0xFFFFA500
#define COLOR_BROWN      0xFFA52A2A
#define COLOR_PURPLE     0xFF800080
#define COLOR_PINK       0xFFFFC0CB
#define COLOR_LIME       0xFF32CD32
#define COLOR_NAVY       0xFF000080
#define COLOR_TEAL       0xFF008080
#define COLOR_OLIVE      0xFF808000


volatile int GlobalVarData = 1;
volatile int GlobalVarBss;

uint32_t MyThread(void* ThreadParameter) {
    (void)(ThreadParameter);
    printf(COLOR_LIME, "**Hit MyThread**\n");

    for (;;) {
        printf(COLOR_LIME, "In MyThread Sleep loop...\n");
        Sleep(1000);
    }

    return 1;
}

int main(void) {
    printf(COLOR_CYAN, "Main user mode hit.\n");
   
    // Lets attempt to create usermode.txt, write Hello, World! to it, and then read from it into memory allocated (making use of all of the syscalls right now, including MtTerminateProcess in return)
    // If at any point we fail we will terminate the program with the status that failed.
    volatile int counter = 0;
    HANDLE FileHandle = CreateFile("group.txt", MT_FILE_ALL_ACCESS);
    MTSTATUS ExitCode = MT_GENERAL_FAILURE;
    HANDLE ThreadHandle;

    if (FileHandle == MT_INVALID_HANDLE) {
        ExitCode = MT_INVALID_HANDLE;
        goto failure;
    }

    // Write.
    char Hello[] = "ascendz mcdonalds adiravraham ofirs";
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
    ThreadHandle = CreateThread((THREAD_START_ROUTINE)MyThread, NULL);

    // Done, infinite loop.
    goto success;

failure:
    TerminateProcess(MtCurrentProcess(), GetLastError());
success:
    while (true) {
        counter++;
        __asm__ volatile ("pause");

        if (counter == 10000000) {
            printf(COLOR_RED, "**Terminating user mode main thread**\n");
            TerminateThread(MtCurrentThread(), MT_SUCCESS);
        }
    }
    return 0;
}
