/*++

Module Name:

    mminit.c

Purpose:

    This translation unit contains the implementation of memory initialization routines.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/mm.h"
#include "../../includes/me.h"
#include "../../includes/mg.h"
#include "../../assert.h"

#define IA32_PAT 0x277

static
bool
MiIsPATAvailable(void)

{
    uint32_t eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    return (edx & (1 << 16)) != 0;
}

static
void 
MiInitializePAT(void) 

{
    uint64_t pat =
        0x00 |                   // 0 = WB
        (0x01ULL << 8) |         // 1 = WT
        (0x02ULL << 16) |        // 2 = UC-
        (0x03ULL << 24) |        // 3 = UC
        (0x00ULL << 32) |        // 4 = WB
        (0x01ULL << 40) |        // 5 = WC
        (0x02ULL << 48) |        // 6 = UC-
        (0x03ULL << 56);         // 7 = UC

    __writemsr(IA32_PAT, pat);
}

bool
MmInitSystem(
    IN uint8_t Phase,
    IN PBOOT_INFO BootInformation
)

/*++

    Routine description:

        Initializes the memory manager of the system.

    Arguments:

        [IN]    uint8_t Phase - Specifies the phase of the system for correct init routines.
        [IN]    PBOOT_INFO BootInformation - The boot information supplied by the UEFI Bootloader (can be NULL in later phases, look below)

    Phase Demands:

        1 - BootInformation

    Phase Does:
           
        1 - Initializes the core memory managment routines. (PFN Database, Virtual Address bitmap, PAT, PTE Database, etc.)

    Return Values:

        True or false if the phase given has succeeded initilization.

--*/

{
    // Currently we only support the first and only phase.
    if (Phase == 1) {

        // Initialize PAT (Page Attribute Table)
        bool PatAvailable = MiIsPATAvailable();
        assert(PatAvailable == true);
        if (PatAvailable) {
            MiInitializePAT();
        }

        // Initialize all memory managment routines (PFN Database, VA Space, Pools, MMIO, PTE Database)
        // If we fail init of one of them, we bugcheck, since they are mandatory for operation.
        MTSTATUS st = MiInitializePfnDatabase(BootInformation);
        if (MT_FAILURE(st)) {
            MeBugCheckEx(
                PFN_DATABASE_INIT_FAILURE,
                (void*)(uintptr_t)st,
                NULL,
                NULL,
                NULL
            );
        }

        if (!MiInitializePoolVaSpace()) {
            MeBugCheck(VA_SPACE_INIT_FAILURE);
        }

        st = MiInitializePoolSystem();
        if (MT_FAILURE(st)) {
            MeBugCheckEx(
                POOL_INIT_FAILURE,
                (void*)(uintptr_t)st,
                NULL,
                NULL,
                NULL
            );
        }

        // Phase 1 Done.
        return true;
    }
    else {
        // Only phase 1 is supported currently.
        MeBugCheck(INVALID_INITIALIZATION_PHASE);
    }
}