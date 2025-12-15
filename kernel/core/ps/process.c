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

static 
bool 
GetBaseName(const char* fullpath, char* out, size_t outsz) {
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

MTSTATUS
PsCreateProcess(
    IN const char* ExecutablePath,
    OUT PHANDLE ProcessHandle,
    IN ACCESS_MASK DesiredAccess,
    _In_Opt HANDLE ParentProcess
)

/*++

    Routine description:

       Creates a process, simple as that.

    Arguments:

        [IN]    const char* ExecutablePath - The process's main executable file.
        [OUT]   PHANDLE ProcessHandle - Pointer to store the the process's created handle.
        [IN]    ACCESS_MASK DesiredAccess - The maximum access the process should originally have.
        [IN OPTIONAL]   HANDLE ParentProcess - Optionally supply a handle to the parent of this process.

    Return Values:

        Various MTSTATUS Status codes.

--*/

{
    MTSTATUS Status;
    PEPROCESS Process, Parent;
    // If we have a parent process, attempt to see if the parent process has the access to create another process.
    if (ParentProcess) {
        Status = ObReferenceObjectByHandle(
            ParentProcess,
            MT_PROCESS_CREATE_PROCESS,
            PsProcessType,
            (void*)&Parent,
            NULL
        );

        if (MT_FAILURE(Status)) {
            return Status;
        }
    }
    else {
        // We have no parent process.
        Parent = NULL;
    }

    // Create the EPROCESS Object.
    Status = ObCreateObject(PsProcessType, sizeof(EPROCESS), (void*)&Process);
    if (MT_FAILURE(Status)) goto Cleanup;

    // CleanupWithRef from now on.
    // Assume failure status.
    Status = MT_GENERAL_FAILURE;
    // Setup the process now, create its PID.
    Process->PID = PsAllocateProcessId(Process);

    // Set its parent process handle.
    Process->ParentProcess = ParentProcess;

    // Set its image name.
    char filename[24];
    GetBaseName(ExecutablePath, filename, sizeof(filename));
    if (filename[0] == '\0') goto CleanupWithRef;
    kstrncpy(Process->ImageName, filename, sizeof(Process->ImageName));

    // Set initial state
    Process->InternalProcess.ProcessState |= PROCESS_READY;

    // Create object table.
    PHANDLE_TABLE HandleTable = HtCreateHandleTable(Process);
    if (!HandleTable) goto CleanupWithRef;
    Process->ObjectTable = HandleTable;

    // Create address space.
    void* DirectoryTablePhysical = NULL;
    Status = MmCreateProcessAddressSpace(&DirectoryTablePhysical);
    if (MT_FAILURE(Status)) goto CleanupWithRef;    
    Process->InternalProcess.PageDirectoryPhysical = (uintptr_t)DirectoryTablePhysical;

    // Per thread stack calculation.
    Process->NextStackTop = USER_INITIAL_STACK_TOP;

    // Creation time.
    Process->CreationTime = MeGetEpoch();

    // Initialize List heads.
    InitializeListHead(&Process->AllThreads);

    // Load file into memory (TODO Section objects)
    void* file_buffer = NULL;
    uint32_t FileSize = 0;
    Status = vfs_read(ExecutablePath, &FileSize, &file_buffer);
    if (MT_FAILURE(Status)) goto CleanupWithRef;
    Process->FileBuffer = file_buffer;
    Process->ImageBase = USER_VA_START; // Dummy VA, FIXME Headers.

    // Create VADs for the process to load.
    void* BaseAddress = (void*)USER_VA_START;
    Status = MmAllocateVirtualMemory(Process, &BaseAddress, FileSize, VAD_FLAG_MAPPED_FILE);
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Calculate the number of pages needed to map the entire file in.
    size_t num_pages = (FileSize + VirtualPageSize - 1) / VirtualPageSize;

    // Attach to the process.
    APC_STATE State;
    kmemset(&State, 0, sizeof(APC_STATE));
    MeAttachProcess(&Process->InternalProcess, &State);

    // Prepare for the copy loop
    uintptr_t CurrentVA = Process->ImageBase;
    uint8_t* SourcePtr = (uint8_t*)file_buffer; // Pointer to the data we read from disk
    size_t BytesRemaining = FileSize;

    for (size_t i = 0; i < num_pages; i++) {

        // Calculate how many bytes to copy for this specific iteration.
        // It will be 4096 for every page except potentially the last one.
        size_t BytesToCopy = (BytesRemaining > VirtualPageSize) ? VirtualPageSize : BytesRemaining;

        // Copy the data.
        kmemcpy((void*)CurrentVA, SourcePtr, BytesToCopy);

        // Advance pointers and decrement counters
        CurrentVA += VirtualPageSize;
        SourcePtr += VirtualPageSize;
        BytesRemaining -= BytesToCopy;
    }

    // Detach.
    MeDetachProcess(&State);

    // Create a handle for the process.
    HANDLE hProcess;
    Status = ObCreateHandleForObject(Process, DesiredAccess, &hProcess);
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Create a main thread for the process.
    HANDLE MainThreadHandle;
    Status = PsCreateThread(hProcess, &MainThreadHandle, (ThreadEntry)Process->ImageBase, NULL, DEFAULT_TIMESLICE_TICKS);
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Insert main thread to processor queue.
    MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, Process->MainThread);

    // We are, successful.
    *ProcessHandle = hProcess;
    Status = MT_SUCCESS;

CleanupWithRef:
#ifdef DEBUG
    if (MT_FAILURE(Status)) {
        assert(false, "Something went wrong.");
    }
#endif
    // If all went smoothly, this should cancel out the reference made by ObCreateHandleForObject. (so we only have 1 reference left by ObCreateObject)
    // If not, it would reach reference 0, and PspDeleteProcessd would execute.
    ObDereferenceObject(Process);
    // [[fallthrough]]
Cleanup:
    if (Parent) ObDereferenceObject(Parent);
    return Status;
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