/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     GPLv3
 * PURPOSE:     Process Creation Implementation
 */

#include "../../time.h"
#include "../../filesystem/vfs/vfs.h"
#include "../../includes/ps.h"
#include "../../includes/mg.h"
#include "../../includes/ms.h"
#include "../../includes/ob.h"
#include "../../assert.h"

#define MIN_PID           4u
#define MAX_PID           0xFFFFFFFCUL
#define ALIGN_DELTA       6u
#define MAX_FREE_POOL     1024u

#define PML4_INDEX(addr)  (((addr) >> 39) & 0x1FFULL)
#define KERNEL_PML4_START ((size_t)PML4_INDEX(KernelVaStart))
#define USER_INITIAL_STACK_TOP 0x00007FFFFFFFFFFF
extern EPROCESS SystemProcess;

uintptr_t MmSystemRangeStart = KernelVaStart;
uintptr_t MmHighestUserAddress = USER_VA_END;
uintptr_t MmUserProbeAddress = 0x00007FFFFFFF0000;



void
PsTerminateProcess(
    IN PEPROCESS Process
)

{
    UNREFERENCED_PARAMETER(Process);
    assert(false, "Unimplemented routine");
    MeBugCheck(MANUALLY_INITIATED_CRASH2);
}