// Include standard header.
#include "../../headers/MatanelOS.h"

int main(void) {
    void* BaseAddress = NULL;
    MTSTATUS Status = MtAllocateVirtualMemory(MtCurrentProcess(), (void**)0x1000, 512, PAGE_EXECUTE_READWRITE);
    if (MT_FAILURE(Status)) {
        // what??? failure??? I DO NOT accept failure.. not in my book.
        // time to justify the name of my program!
        __asm__ volatile ("hlt");
    }

    // hooray
    uint8_t* ptr = (uint8_t*)BaseAddress;
    for (int i = 0; i < 512; i++) {
        ptr[i] = 0xA;
    }

    return 0;
}
