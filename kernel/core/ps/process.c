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
static SPINLOCK g_pid_lock = { 0 };

uintptr_t MmSystemRangeStart = KernelVaStart;
uintptr_t MmHighestUserAddress = USER_VA_END;
uintptr_t MmUserProbeAddress = 0x00007FFFFFFF0000;

///
// Call with freedPid == 0 ? allocate a new PID (returns 0 on failure)
// Call with freedPid  > 0 ? release that PID back into the pool (always returns 0)
///
static uint32_t ManagePID(uint32_t freedPid)
{
    IRQL oldIrql;
    MsAcquireSpinlock(&g_pid_lock, &oldIrql);
    static uint32_t nextPID = MIN_PID;
    static uint32_t freePool[MAX_FREE_POOL];
    static uint32_t freeCount = 0;
    uint32_t result = 0;

    if (freedPid) {
        // Release path: push into free pool if aligned & room
        if ((freedPid % ALIGN_DELTA) == 0 && freeCount < MAX_FREE_POOL) {
            freePool[freeCount++] = freedPid;
        }
    }
    else {
        // Allocate path:
        if (freeCount > 0) {
            // Reuse most-recently freed
            result = freePool[--freeCount];
        }
        else {
            // Hand out next aligned TID
            result = nextPID;
            nextPID += ALIGN_DELTA;

            // Wrap/overflow check
            if (nextPID < ALIGN_DELTA || result > MAX_PID) {
                // Exhausted all TIDs
                result = 0;
            }
        }
    }
    MsReleaseSpinlock(&g_pid_lock, oldIrql);
    return result;
}

void
PsTerminateProcess(
    IN PEPROCESS Process
)

{
    UNREFERENCED_PARAMETER(Process);
    assert(false, "Unimplemented routine");
    MeBugCheck(MANUALLY_INITIATED_CRASH2);
}