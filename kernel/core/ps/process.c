/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     GPLv3
 * PURPOSE:     Process Creation Implementation
 */

#include "../../time.h"
#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../includes/mg.h"
#include "../../includes/ms.h"
#include "../../includes/ob.h"
#include "../../assert.h"
#include "../../includes/fs.h"

#define MIN_PID           4u
#define MAX_PID           0xFFFFFFFCUL
#define ALIGN_DELTA       6u
#define MAX_FREE_POOL     1024u

#define PML4_INDEX(addr)  (((addr) >> 39) & 0x1FFULL)
#define KERNEL_PML4_START ((size_t)PML4_INDEX(KernelVaStart))
#define USER_INITIAL_STACK_TOP USER_VA_END
extern EPROCESS SystemProcess;

uintptr_t MmSystemRangeStart = PhysicalMemoryOffset; // Changed to PhysicalMemoryOffset, since thats where actual stuff like hypermap, phys to virt, and more happen.
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
            (void**)&Parent,
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
    gop_printf(COLOR_RED, "Process CR3: %p\n", DirectoryTablePhysical);

    // Per thread stack calculation.
    Process->NextStackHint = USER_INITIAL_STACK_TOP;

    // Creation time.
    Process->CreationTime = MeGetEpoch();

    // Initialize List heads.
    InitializeListHead(&Process->AllThreads);

    // Get the file handle.
    HANDLE FileHandle;
    Status = FsOpenFile(ExecutablePath, MT_FILE_ALL_ACCESS, &FileHandle);
    if (MT_FAILURE(Status)) goto CleanupWithRef;
    PFILE_OBJECT FileObject;
    // Reference the handle, and then close it so only the pointer reference remains (this)
    ObReferenceObjectByHandle(FileHandle, MT_FILE_ALL_ACCESS, FsFileType, (void**)&FileObject, NULL);
    HtClose(FileHandle);
    // TODO ADD ADDRESS TO WORKING SET OF PROCESS!!

    // Create the sections for the process.
    HANDLE SectionHandle;
    Status = MmCreateSection(&SectionHandle, FileObject);
    if (MT_FAILURE(Status)) {
        // If file reference failed it would close the file handle.
        goto CleanupWithRef;
    }

    // Set handle.
    Process->SectionHandle = SectionHandle;

    // Map them into address space.
    void* StartAddress = NULL;
    Status = MmMapViewOfSection(SectionHandle, Process, &StartAddress);
    // MmpDeleteSection closes the file handle.
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Set start address.
    Process->ImageBase = (uint64_t)StartAddress;

    // Create a handle for the process.
    HANDLE hProcess;
    Status = ObCreateHandleForObject(Process, DesiredAccess, &hProcess);
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Create a main thread for the process.
    Process->NextStackHint = USER_INITIAL_STACK_TOP;
    HANDLE MainThreadHandle;
    Status = PsCreateThread(hProcess, &MainThreadHandle, (ThreadEntry)Process->ImageBase, NULL, DEFAULT_TIMESLICE_TICKS);
    if (MT_FAILURE(Status)) {
        // This is a failure, since there is not a handle to the process, we must close it.
        // Destroy the handle.
        HtClose(hProcess);
        goto CleanupWithRef;
    }
    // We are, successful.
    if (ProcessHandle) *ProcessHandle = hProcess;
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

MTSTATUS
PsTerminateProcess(
    IN PEPROCESS Process,
    IN MTSTATUS ExitCode
)

/*++

    Routine description:

        Terminates the process

    Arguments:

        [IN]    PEPROCESS Process - The process to terminate from the system.
        [IN]    MTSTATUS ExitCode - The ExitCode that the process will exit in.

    Return Values:

        None.

--*/

{
    // Declarations
    PETHREAD Thread = NULL;
    UNREFERENCED_PARAMETER(Thread);
    MTSTATUS Status = MT_NOTHING_TO_TERMINATE;
    if (Process->Flags & ProcessBreakOnTermination) {
        // Attempted termination of a process that is critical to system stability,
        // we bugcheck.
        MeBugCheckEx(
            CRITICAL_PROCESS_DIED,
            (void*)(uintptr_t)Process,
            (void*)(uintptr_t)ExitCode,
#ifdef DEBUG
            (void*)(uintptr_t)RETADDR(0),
#else
            NULL,
#endif
            NULL
        );
    }

    // Set the process as terminating in its flags.
    InterlockedOr32((volatile int32_t*)&Process->Flags, ProcessBeingTerminated);
    Process->InternalProcess.ProcessState = PROCESS_TERMINATING;

    // Begin terminating all process threads.
    Thread = PsGetNextProcessThread(Process, Thread);
    while (Thread) {
        // Exterminate the thread from this world (system32)
        PsTerminateThread(Thread, ExitCode);
        // Get the next victim for our massacre.
        Thread = PsGetNextProcessThread(Process, Thread);

        // One got exterminated, so we mark it a successful mission.
        Status = MT_SUCCESS;
    }

    // Return if mission successful.
    return Status;
}

void
PsDeleteProcess(
    IN void* ProcessObject
)

{
    PEPROCESS Process = (PEPROCESS)ProcessObject;
    // Wait for anything to stop holding the rundown protection.
    MsWaitForRundownProtectionRelease(&Process->ProcessRundown);

    // Set flags
    InterlockedOr32((volatile int32_t*)&Process->Flags, ProcessBeingDeleted);

    // Delete section handles.
    if (Process->SectionHandle) {
        HtClose(Process->SectionHandle);
    }

    // TODO (CRITICAL FIXME) (MEMORY LEAK) Working set list delete all active VADs.
    // VADs deletion would also close the FileObject HANDLE!
    
    // Delete its CID.
    PsFreeCid(Process->PID);

    // Delete its handle table.
    if (Process->ObjectTable) {
        // Attach to process so pagedpool inside of it are valid (even though they 100% should be valid now)
        APC_STATE State;
        MeAttachProcess(&Process->InternalProcess, &State);
        HtDeleteHandleTable(Process->ObjectTable);
        MeDetachProcess(&State);
        Process->ObjectTable = NULL;
    }
    // Delete its address space.
    MmDeleteProcessAddressSpace(Process, Process->InternalProcess.PageDirectoryPhysical);

    // EPROCESS Would be deleted after function return.
}

PETHREAD
PsGetNextProcessThread(
    IN PEPROCESS Process,
    _In_Opt PETHREAD LastThread
)

{
    PETHREAD FoundThread = NULL;
    PDOUBLY_LINKED_LIST Entry, ListHead;
    // Acquire thread list lock.
    MsAcquirePushLockShared(&Process->ThreadListLock);

    // Check if we are already starting in another thread list.
    if (LastThread) {
        Entry = LastThread->ThreadListEntry.Flink;
    }
    else {
        // Start at beginnininng
        Entry = Process->AllThreads.Flink;
    }

    // Set the list head and start the loop.
    ListHead = Process->AllThreads.Flink;
    while (ListHead != Entry) {
        // While the pointers arent equal (we arent back the start), we enumerate for the next thread.
        FoundThread = CONTAINING_RECORD(Entry, ETHREAD, ThreadListEntry);
        // First use of MT_SUCCEEDED btw.
        if (MT_SUCCEEDED(ObReferenceObjectByPointer(FoundThread, PsThreadType))) break;
           
        // Nothing found, keep loopin.
        FoundThread = NULL;
        Entry = Entry->Flink;
    }

    // Unlock process.
    MsReleasePushLockShared(&Process->ThreadListLock);
    if (LastThread) {
        // If we had a starting thread we dereference it from the initial reference in the loop
        // The whole point we did the reference is to keep the object alive that we give in the return value.
        ObDereferenceObject(LastThread);
    }

    // Return if we found.
    return FoundThread;
}