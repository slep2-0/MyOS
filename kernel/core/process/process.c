/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     GPLv3
 * PURPOSE:     Process Creation Implementation
 */

#include "process.h"
#include "../../time.h"
#include "../../filesystem/vfs/vfs.h"
#include "../../core/bugcheck/bugcheck.h"

#define MIN_PID           4u
#define MAX_PID           0xFFFFFFFCu
#define ALIGN_DELTA       6u
#define MAX_FREE_POOL     1024u

static SPINLOCK g_pid_lock = { 0 };
extern PROCESS SystemProcess;
///
// Call with freedPid == 0 ? allocate a new PID (returns 0 on failure)
// Call with freedPid  > 0 ? release that PID back into the pool (always returns 0)
///
static uint32_t ManagePID(uint32_t freedPid)
{
    IRQL oldIrql;
    MtAcquireSpinlock(&g_pid_lock, &oldIrql);
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
    MtReleaseSpinlock(&g_pid_lock, oldIrql);
    return result;
}

static bool GetBaseName(const char* fullpath, char* out, size_t outsz) {
    const char* ext = ".mtexe";
    size_t ext_len = kstrlen(ext);
    if (!fullpath || !out || outsz == 0) return false;

    size_t len = kstrlen(fullpath);
    const char* p = fullpath + len;
    while (p > fullpath && *(p - 1) != '/') --p;

    size_t name_len = kstrlen(p);
    if (name_len < ext_len || kstrcmp(p + name_len - ext_len, ext) != 0) return false;

    if (name_len + 1 > outsz) return false; // too small
    kstrncpy(out, p, name_len + 1);
    return true;
}

MTSTATUS MtCreateProcess(const char* path, PROCESS** outProcess, PROCESS* ParentProcess) {
	// First, we must allocate the PROCESS structure, this is a kernel mode structure, so it is NOT allocated with PAGE_USER flags.
	PROCESS* process = MtAllocateVirtualMemory(sizeof(PROCESS), _Alignof(PROCESS));
    if (!process) return MT_NO_MEMORY;

    // Obtain a PID (Process Identifier), return no resources if we cannot obtain one from the pool.
    uint32_t pid = ManagePID(0);
    if (!pid) {
        // Free the allocated process.
        MtFreeVirtualMemory((void*)process);
        return MT_NO_RESOURCES;
    }
    process->PID = pid;

    // Set its parent process, if NULL, the parent process must be the system process.
    if (!ParentProcess) process->ParentProcess = &SystemProcess;
    else process->ParentProcess = ParentProcess;

    // Set it's image name, TODO PARSE HEADERS, for now, we use its executable name
    char filename[256];
    GetBaseName(path, filename, sizeof(filename));
    if (filename[0] == '\0') {
        // Free the PID and PROCESS.
        MtFreeVirtualMemory((void*)process);
        ManagePID(pid);
        return MT_INVALID_PARAM;
    }
    // This gurantees null termination.
    kstrncpy(process->ImageName, filename, sizeof(process->ImageName));
    gop_printf(COLOR_RED, "Filename: %s\n", filename);
    // Set it's initial state.
    process->ProcessState |= PROCESS_READY;

    // PRIORITY TODO

    // Setup the PML4 Of the process, and its whole virtual memory.
    uint64_t* pml4 = MtAllocateVirtualMemory(4096, 4096);
    if (!pml4) {
        // Free all previous ones
        MtFreeVirtualMemory((void*)process);
        ManagePID(pid);
        return MT_NO_MEMORY;
    }
    // Allocate PDPT,PD,PT
    uint64_t* pdpt = MtAllocateVirtualMemory(4096, 4096);
    if (!pdpt) {
        MtFreeVirtualMemory((void*)process);
        MtFreeVirtualMemory((void*)pml4);
        ManagePID(pid);
        return MT_NO_MEMORY;
    }
    uint64_t* pd = MtAllocateVirtualMemory(4096, 4096);
    if (!pd) {
        MtFreeVirtualMemory((void*)process);
        MtFreeVirtualMemory((void*)pml4);
        MtFreeVirtualMemory((void*)pdpt);
        ManagePID(pid);
        return MT_NO_MEMORY;
    }
    uint64_t* pt = MtAllocateVirtualMemory(4096, 4096);
    if (!pt) {
        MtFreeVirtualMemory((void*)process);
        MtFreeVirtualMemory((void*)pml4);
        MtFreeVirtualMemory((void*)pdpt);
        MtFreeVirtualMemory((void*)pd);
        ManagePID(pid);
        return MT_NO_MEMORY;
    }

    // Finally, after all of that repetition, we setup its basic mapping, translate to physical, and continue with the final setup of the process.
    uint64_t* cur_pml4 = pml4_from_recursive(); // Our kernel PML4
    for (size_t i = KERNEL_PML4_START; i < 512; i++) {
        // set the higher half.
        pml4[i] = cur_pml4[i];
    }

    // Install recursive entry for the process PML4.
    uintptr_t phys_pml4 = MtTranslateVirtualToPhysical((void*)pml4);
    pml4[RECURSIVE_INDEX] = phys_pml4 | PAGE_PRESENT | PAGE_RW; // This is kernel mode only, we cant let the user mode change their own mapping.

    // Write the physical address with the appropriate flags.
    uintptr_t phys_pdpt = MtTranslateVirtualToPhysical((void*)pdpt);
    uintptr_t phys_pd = MtTranslateVirtualToPhysical((void*)pd);
    uintptr_t phys_pt = MtTranslateVirtualToPhysical((void*)pt);
    
    pml4[0] = phys_pdpt | PAGE_PRESENT | PAGE_RW | PAGE_USER; // allow user translations.
    pdpt[0] = phys_pd | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    pd[0] = phys_pt | PAGE_PRESENT | PAGE_RW | PAGE_USER;

    process->PageDirectoryVirtual = pml4;
    process->PageDirectoryPhysical = phys_pml4;

    // Initializing the per process stack arithmetic and number of threads.
    process->NextStackTop = USER_INITIAL_STACK_TOP;
    process->NumThreads = 0;

    // Creation time, it is the epoch.
    uint64_t timestamp = MtGetEpoch();
    process->CreationTime = timestamp;

    // SID TODO

    // Setup its image base, which means, we finally load the file from disk, TODO parsing its headers when we load it.
    void* file_buffer = NULL;
    uint32_t file_size = 0;
    MTSTATUS status = vfs_read(path, &file_size, &file_buffer);
    if (MT_FAILURE(status)) {
        // Looks like we hit a failure, erase everything, unfortunately.
        MtFreeVirtualMemory((void*)process);
        MtFreeVirtualMemory((void*)pml4);
        MtFreeVirtualMemory((void*)pdpt);
        MtFreeVirtualMemory((void*)pd);
        MtFreeVirtualMemory((void*)pt);
        ManagePID(pid);
        return status;
    }
    if (file_size == 0) {
        // Yeah, we are not going to load files that arent even 1 byte, seriously?
        MtFreeVirtualMemory((void*)process);
        MtFreeVirtualMemory((void*)pml4);
        MtFreeVirtualMemory((void*)pdpt);
        MtFreeVirtualMemory((void*)pd);
        MtFreeVirtualMemory((void*)pt);
        MtFreeVirtualMemory((void*)file_buffer);
        ManagePID(pid);
        return status;
    }
    
    // Store the pointer for future freeing.
    process->FileBuffer = file_buffer;

    // Finally, map the buffer into the users PML4.
    uint64_t imageBase = 0x00401000; // FIXME Dummy VA.

    // Calculate the number of pages required to map the entire file.
    // This also protects against files that are also below 4KB, which is what I had, so it underflowed...
    size_t num_pages = (file_size + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K;

    for (size_t i = 0; i < num_pages; i++) {
        uintptr_t file_offset = i * PAGE_SIZE_4K;
        uintptr_t virtualaddr = (uintptr_t)imageBase + file_offset;
        uintptr_t buf_va = (uintptr_t)file_buffer + file_offset;
        uintptr_t buf_phys = MtTranslateVirtualToPhysical((void*)buf_va);
        if (!buf_phys) MtBugcheck(NULL, NULL, MANUALLY_INITIATED_CRASH, 0, false);
        status = MtMapPageInAddressSpace(pml4, (void*)virtualaddr, buf_phys, PAGE_PRESENT | PAGE_RW | PAGE_USER);

        if (MT_FAILURE(status)) {
            // Free.
            MtFreeVirtualMemory((void*)process);
            MtFreeVirtualMemory((void*)pml4);
            MtFreeVirtualMemory((void*)pdpt);
            MtFreeVirtualMemory((void*)pd);
            MtFreeVirtualMemory((void*)pt);
            MtFreeVirtualMemory((void*)file_buffer);
            ManagePID(pid);
            return status;
        }
    }

    process->ImageBase = imageBase;

    // End of the line, now setup its threads, and begin.
    Thread* MainThread = NULL;
    status = MtCreateThread(process, &MainThread, (ThreadEntry)process->ImageBase, NULL, DEFAULT_TIMESLICE_TICKS);
    if (MT_FAILURE(status)) {
        // Looks like a thread creation failed, we free, everything.
        MtFreeVirtualMemory((void*)process);
        MtFreeVirtualMemory((void*)pml4);
        MtFreeVirtualMemory((void*)pdpt);
        MtFreeVirtualMemory((void*)pd);
        MtFreeVirtualMemory((void*)pt);
        MtFreeVirtualMemory((void*)file_buffer);
        ManagePID(pid);
        return status;
    }

    if (outProcess) *outProcess = process;
    // Thread has been created yet not enqueued, we enqueue it now, and return success. (we dont put it in the struct, it already has been put by the createThread function)
    MtEnqueueThreadWithLock(&thisCPU()->readyQueue, MainThread);
    return MT_SUCCESS;
}