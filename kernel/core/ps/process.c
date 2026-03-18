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
#include "../../includes/exception.h"

#define MIN_PID           4u
#define MAX_PID           0xFFFFFFFCUL
#define ALIGN_DELTA       6u
#define MAX_FREE_POOL     1024u

#define PML4_INDEX(addr)  (((addr) >> 39) & 0x1FFULL)
#define KERNEL_PML4_START ((size_t)PML4_INDEX(KernelVaStart))
#define USER_INITIAL_STACK_TOP USER_VA_END

#define MTDLL_TARGET_ENTRY "LdrInitializeProcess"
#define MAX_EXPORTED_FUNC_NAME 256
extern EPROCESS SystemProcess;

uintptr_t MmSystemRangeStart = PhysicalMemoryOffset; // Changed to PhysicalMemoryOffset, since thats where actual stuff like hypermap, phys to virt, and more happen.
uintptr_t MmHighestUserAddress = USER_VA_END;
uintptr_t MmUserStartAddress = USER_VA_START;
uintptr_t MmUserProbeAddress = 0x00007FFFFFFF0000;

// Define a structure to hold our cached exports
typedef struct _MTDLL_CACHE_ENTRY {
    char RoutineName[MAX_EXPORTED_FUNC_NAME];
    void* RoutineRva;
} MTDLL_CACHE_ENTRY;

// Global cache variables
bool PsMtdllRvasSaved = false;                 // Flag to track if cache is built
size_t PsMtdllExportCount = 0;                 // How many valid exports we actually cached
MTDLL_CACHE_ENTRY* PsMtdllExportCache = NULL;  // Pointer to our dynamic cache array

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

static 
int
ReadStringFromFile(PFILE_OBJECT FileObject, uint64_t off, char* buf, size_t buf_len)
{
    size_t got = 0;
    MTSTATUS st;

    if (buf_len == 0) return -1;

    // Read up to buf - 1 bytes (so we leave room for null term)
    st = FsReadFile(FileObject, off, buf, buf_len - 1, &got);
    if (MT_FAILURE(st)) return -1;

    /* Ensure NUL termination */
    buf[got >= (buf_len - 1) ? (buf_len - 1) : got] = '\0';

    // If null term isnt present, we need a larger buffer.
    if (kmemchr(buf, '\0', got) == NULL) return -1;

    return 0;
}

// This finds the routine inside of the MTDLL Export table, with memory caching.
void*
PspFindMtdllEntry(
    IN PFILE_OBJECT MtdllObject,
    IN const char* RoutineName
)
{
    // If already cached use it.
    if (PsMtdllRvasSaved && PsMtdllExportCache != NULL) {
        for (size_t i = 0; i < PsMtdllExportCount; ++i) {
            if (kstrcmp(PsMtdllExportCache[i].RoutineName, RoutineName) == 0) {
                return PsMtdllExportCache[i].RoutineRva;
            }
        }
        return NULL; // Cache exists, but the requested routine isn't in it
    }

    // No cache yet, file object required.
    if (MtdllObject == NULL) {
        return NULL;
    }

    MTE_HEADER hdr;
    size_t br;
    MTSTATUS st;

    // Read MTE header
    st = FsReadFile(MtdllObject, 0, &hdr, sizeof(hdr), &br);
    if (MT_FAILURE(st) || br < sizeof(hdr)) return NULL;

    if (hdr.Magic[0] != 'M' || hdr.Magic[1] != 'T' || hdr.Magic[2] != 'E' || hdr.Magic[3] != '\0') {
        return NULL;
    }

    if (hdr.exports_rva == 0 || hdr.exports_size < sizeof(MT_EXPORT_ENTRY)) return NULL;

    size_t max_entries = hdr.exports_size / sizeof(MT_EXPORT_ENTRY);
    if (max_entries == 0) return NULL;

    // Allocate memory for the cache based on max_entries
    PsMtdllExportCache = (MTDLL_CACHE_ENTRY*)MmAllocatePoolWithTag(NonPagedPool, max_entries * sizeof(MTDLL_CACHE_ENTRY), 'CACH');
    if (PsMtdllExportCache == NULL) {
        return NULL; // Allocation failed, bail out
    }

    MT_EXPORT_ENTRY entry;
    char namebuf[MAX_EXPORTED_FUNC_NAME];
    void* requested_rva = NULL; // Keep track of the one the user actually asked for

    // Loop through the disk entries and save.
    for (size_t i = 0; i < max_entries; ++i) {
        uint64_t entry_off = hdr.exports_rva + (uint64_t)(i * sizeof(MT_EXPORT_ENTRY));

        st = FsReadFile(MtdllObject, entry_off, &entry, sizeof(entry), &br);
        if (MT_FAILURE(st) || br < sizeof(entry)) {
            break;
        }

        uint64_t name_rva_calculated = entry.name_rva;
        if (name_rva_calculated == 0 || name_rva_calculated >= MtdllObject->FileSize) continue;

        if (ReadStringFromFile(MtdllObject, name_rva_calculated, namebuf, sizeof(namebuf)) != 0) {
            continue;
        }

        // Save the valid entry to our memory cache
        kstrcpy(PsMtdllExportCache[PsMtdllExportCount].RoutineName, namebuf);
        PsMtdllExportCache[PsMtdllExportCount].RoutineRva = (void*)(uintptr_t)(entry.func_rva);

        // Check if this is the routine the caller originally wanted
        if (kstrcmp(namebuf, RoutineName) == 0) {
            requested_rva = PsMtdllExportCache[PsMtdllExportCount].RoutineRva;
        }

        PsMtdllExportCount++;
    }

    // Cached, now set the global.
    PsMtdllRvasSaved = true;

    return requested_rva; // Returns the RVA if found, or NULL if it wasn't in the table
}

static
MTSTATUS
PspRelocateImage(
    IN void* ImageBase,
    IN MTE_HEADER* Header
)
{
    // Calculate the difference between where it is and where it wants to be
    int64_t delta = (uintptr_t)ImageBase - (uintptr_t)Header->PreferredImageBase;

    // If loaded at preferred address, no work needed
    if (delta == 0) return MT_SUCCESS;

    // null check
    if (Header->reloc_rva == 0 || Header->reloc_size == 0) {
        // Warning: Loaded at wrong address but no relocations found? 
        // Code might crash, but technically not a failure of this function.
        return MT_SUCCESS;
    }

    // Point to the relocation table
    Rela* reloc_table = (Rela*)((uintptr_t)ImageBase + Header->reloc_rva);
    size_t count = Header->reloc_size / sizeof(Rela);

    // Iterate and Fix
    for (size_t i = 0; i < count; i++) {
        Rela* entry = &reloc_table[i];

        // We only care about R_X86_64_RELATIVE (Type 8)
        if ((entry->r_info & 0xFFFFFFFF) == R_X86_64_RELATIVE) {

            // Pointer to the address we need to fix
            uintptr_t* target_ptr = (uintptr_t*)((uintptr_t)ImageBase + entry->r_offset);

            // Apply the fix: NewBase + Addend
            *target_ptr = (uintptr_t)ImageBase + entry->r_addend;
        }
    }

    return MT_SUCCESS;
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

       Creates a user mode process, simple as that.

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

    // Create address space.
    void* DirectoryTablePhysical = NULL;
    Status = MmCreateProcessAddressSpace(&DirectoryTablePhysical);
    if (MT_FAILURE(Status)) goto CleanupWithRef;
    Process->InternalProcess.PageDirectoryPhysical = (uintptr_t)DirectoryTablePhysical;
    gop_printf(COLOR_RED, "Process CR3: %p\n", DirectoryTablePhysical);

    // Create object table.
    PHANDLE_TABLE HandleTable = HtCreateHandleTable(Process);
    if (!HandleTable) goto CleanupWithRef;
    Process->ObjectTable = HandleTable;

    // Open MTDLL for the process. (ALWAYS needed to map it into memory, code below also uses it)
    PFILE_OBJECT MtdllObject = NULL;
    HANDLE MtdllHandle;

    Status = FsCreateFile(MTDLL_PATH, MT_FILE_ALL_ACCESS, &MtdllHandle);
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Reference the handle
    Status = ObReferenceObjectByHandle(MtdllHandle, MT_FILE_ALL_ACCESS, FsFileType, (void**)&MtdllObject, NULL);
    HtClose(MtdllHandle);
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Find MTDLL Entrypoint now. 
    // (PspFindMtdllEntry will safely use the cache and ignore MtdllObject if PsMtdllRvasSaved is true)
    void* MtdllInitializeProcessRva = PspFindMtdllEntry(MtdllObject, MTDLL_TARGET_ENTRY);
    if (!MtdllInitializeProcessRva) {
        ObDereferenceObject(MtdllObject);
        goto CleanupWithRef;
    }

    // Create sections for MTDLL
    HANDLE MtdllSection;
    Status = MmCreateSection(&MtdllSection, MtdllObject);
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Map them into view.
    void* MtdllEntrypoint; // MtdllEntrypoint should be equal to base as mtdll does not have any entrypoints, like normal DLLs.
    void* MtdllBase;
    Status = MmMapViewOfSection(MtdllSection, Process, &MtdllEntrypoint , &MtdllBase);
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Neat assertion.
    assert(MtdllEntrypoint == MtdllBase, "Entrypoint does not match MTDLL Base, mtdll file corruption, or incorrect linking.");

    APC_STATE RelocApcState;
    MeAttachProcess(&Process->InternalProcess, &RelocApcState);

    // Read header from the loaded memory
    MTE_HEADER* LoadedHeader = (MTE_HEADER*)MtdllBase;

    // Perform Relocations
    // We wrap this in a try/except because we are touching user memory
    try {
        // Verify magic in memory just in case
        if (LoadedHeader->Magic[0] == 'M' && LoadedHeader->Magic[1] == 'T' && LoadedHeader->Magic[2] == 'E') {
            // Check to relocate ONLY IF the base address isnt the preferred image base.
            if (LoadedHeader->PreferredImageBase != (uint64_t)MtdllBase) {
                PspRelocateImage(MtdllBase, LoadedHeader);
            }
        }
    } except{
         Status = GetExceptionCode();
    } end_try;

    MeDetachProcess(&RelocApcState);

    if (MT_FAILURE(Status)) goto CleanupWithRef;
    
    // Actual LdrInitializeProcess of MTDLL.
    void* MtdllInitializeProcess = (void*)((uintptr_t)MtdllBase + (uintptr_t)MtdllInitializeProcessRva);

    // Per thread stack calculation.
    Process->NextStackHint = USER_INITIAL_STACK_TOP;

    // Creation time.
    Process->CreationTime = MeGetEpoch();

    // Initialize List heads.
    InitializeListHead(&Process->AllThreads);

    // Get the file handle.
    HANDLE FileHandle;
    Status = FsCreateFile(ExecutablePath, MT_FILE_ALL_ACCESS, &FileHandle);
    if (MT_FAILURE(Status)) goto CleanupWithRef;
    PFILE_OBJECT FileObject;

    // Reference the handle, and then close it so only the pointer reference remains (this)
    Status = ObReferenceObjectByHandle(FileHandle, MT_FILE_ALL_ACCESS, FsFileType, (void**)&FileObject, NULL);
    HtClose(FileHandle);
    if (MT_FAILURE(Status)) goto CleanupWithRef;
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
    // Start address - entry point.
    void* StartAddress = NULL;
    // Executable base address.
    void* ExecutableBaseAddress = NULL;
    Status = MmMapViewOfSection(SectionHandle, Process, &StartAddress, &ExecutableBaseAddress);
    // MmpDeleteSection closes the file handle.
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Create PEB.
    PMTDLL_BASIC_TYPES BasicTypes = NULL;
    Status = MmCreatePeb(Process, (void**)&Process->Peb, (void**)&BasicTypes);

    // Attempt to set the entry point in the PEB.
    // Attach to process first.
    APC_STATE ApcState;
    MeAttachProcess(&Process->InternalProcess, &ApcState);

    // Also create basic MTDLL types.
    try {
        // For now peb is guranteed to be zeroed since allocating a PFN in fault.c is zeroed, but ill still set it to 0
        Process->Peb->BeingDebugged = false;
        Process->Peb->ImageBase = StartAddress;
        BasicTypes->EpochCreation = MeGetEpoch();

        // Init basic MTDLL types as well.
        BasicTypes->PrimaryExecutable.Size = FileObject->FileSize;
        kstrncpy(BasicTypes->PrimaryExecutable.FullPath, ExecutablePath, sizeof(BasicTypes->PrimaryExecutable.FullPath));
        BasicTypes->PrimaryExecutable.Base = ExecutableBaseAddress;

        // Now for MTDLL itself.
        BasicTypes->Mtdll.Base = MtdllBase;
        BasicTypes->Mtdll.Size = MtdllObject->FileSize;
        kstrncpy(BasicTypes->Mtdll.FullPath, MTDLL_PATH, sizeof(BasicTypes->Mtdll.FullPath));

        // Sucessful.
        Status = MT_SUCCESS;
    } except{
        // Bad.
        Status = GetExceptionCode();
    } end_try;

    // Detach.
    MeDetachProcess(&ApcState);

    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Create a handle for the process.
    HANDLE hProcess;
    Status = ObCreateHandleForObject(Process, DesiredAccess, &hProcess);
    if (MT_FAILURE(Status)) goto CleanupWithRef;
    

    // Create a main thread for the process.
    Process->NextStackHint = USER_INITIAL_STACK_TOP;
    HANDLE MainThreadHandle;

#ifdef DEBUG
    gop_printf(COLOR_CYAN, "MTDLL Created for %s at base %p\n", Process->ImageName, MtdllBase);
    gop_printf(COLOR_CYAN, "Process %s created at base %p and entrypoint %p\n", Process->ImageName, ExecutableBaseAddress, StartAddress);
#endif

    Status = PsCreateThread(hProcess, &MainThreadHandle, (ThreadEntry)StartAddress, (THREAD_PARAMETER)BasicTypes, DEFAULT_TIMESLICE_TICKS, MtdllInitializeProcess);
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
        char buf[144];
        ksnprintf(buf, sizeof(buf), "Process creation failure, status: %x, process name: %s", Status, Process->ImageName);
        assert(false, buf);
    }
#endif
    // If all went smoothly, this should cancel out the reference made by ObCreateHandleForObject. (so we only have 1 reference left by ObCreateObject)
    // If not, it would reach reference 0, and PspDeleteProcess would execute.
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

        Terminates the process, kills its threads.

    Arguments:

        [IN]    PEPROCESS Process - The process to terminate from the system.
        [IN]    MTSTATUS ExitCode - The ExitCode that the process will exit in.

    Return Values:

        MTSTATUS Status code representing if the process has terminated successfully.
        Or a NORETURN if this is the current process.

--*/

{
    // Declarations
#ifdef DEBUG
    gop_printf(COLOR_MAGENTA, "PsTerminateProcess called on process %p with name %s, ExitCode is %x (MTSTATUS)", Process, Process->ImageName, ExitCode);
#endif
    PETHREAD Thread = NULL;
    MTSTATUS Status = MT_NOTHING_TO_TERMINATE;
    bool SeenOurselves = false;
    PETHREAD current = PsGetCurrentThread();
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

    // Acquire last process rundown.
    MsWaitForRundownProtectionRelease(&Process->ProcessRundown);
    
    // Set the process as terminating in its flags.
    PROCESS_FLAGS FlagBefore = InterlockedOr32((volatile int32_t*)&Process->Flags, ProcessBeingTerminated);
    if (FlagBefore & ProcessBeingTerminated) return MT_PROCESS_IS_TERMINATING;

    Process->InternalProcess.ProcessState = PROCESS_TERMINATING;

    // Begin terminating all process threads.
    Thread = PsGetNextProcessThread(Process, Thread);
    while (Thread) {
        if (Thread == current) {
            SeenOurselves = true;
            Thread = PsGetNextProcessThread(Process, Thread);
            continue;
        }

        // Exterminate the thread from this world (system32)
        PsTerminateThread(Thread, ExitCode);
        // Get the next victim for our massacre.
        Thread = PsGetNextProcessThread(Process, Thread);

        // One got exterminated, so we mark it a successful mission.
        Status = MT_SUCCESS;
    }

    if (SeenOurselves) {
        // noreturn
        PspExitThread(ExitCode);
        assert(false, "No return, returned? (possible memory corruption, or malware)");
    }

    // Should I create a PspExitProcess function as well? I mean it should only dereference stuff, check the ReactOS PspExitProcess
    // So I dont think its REALLY needed, unless the pointers MUST NOT be dereferenced by other processes
    // and I can imagine a case where thats needed, so TODO PspExitProcess for self term. (check comment below before taking action)
    // PspDeleteProcess takes care of the actual dereference stuff too, so idk to be honest.

    // Return if mission successful.
    return Status;
}

void
PsDeleteProcess(
    IN void* ProcessObject
)

{
    PEPROCESS Process = (PEPROCESS)ProcessObject;

    // Set flags
    InterlockedOr32((volatile int32_t*)&Process->Flags, ProcessBeingDeleted);

    // Delete section handles.
    if (Process->SectionHandle) {
        HtClose(Process->SectionHandle);
    }

    // TODO Is this needed?????
    if (Process->MtdllHandle) {
        HtClose(Process->MtdllHandle);
    }

    // TODO (CRITICAL FIXME) (MEMORY LEAK) Working set list delete all active VADs.
    // VADs deletion would also close the FileObject HANDLE!
    
    // Delete its CID.
    PsFreeCid(Process->PID);

    // Delete its handle table, this if statement should only pass if the process has failed creation.
    // The other place where the process handle table is deleted, is in the last thread termination in PspExitThread.
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
    PDOUBLY_LINKED_LIST Entry;
    PDOUBLY_LINKED_LIST ListHead = &Process->AllThreads;
    // Acquire thread list lock.
    MsAcquirePushLockShared(&Process->ThreadListLock);

    // Check if we are already starting in another thread list.
    if (LastThread) {
        Entry = LastThread->ThreadListEntry.Flink;
        if (Entry == &LastThread->ThreadListEntry) {
            // If the thread points to itself (it was removed) (even though this shouldnt happen as we acquire a shared push lock)
            // We will set entry to NULL, which will go to cleanup.
            Entry = NULL;
        }
    }
    else {
        // Start at beginnininng -- that shit made me laugh (29/01/2026 5:00:04 PM)
        Entry = ListHead->Flink;
    }

    if (Entry == NULL) {
        goto Cleanup;
    }

    // Set the list head and start the loop.
    while (ListHead != Entry) {
        // While the pointers arent equal (we arent back the start), we enumerate for the next thread.
        FoundThread = CONTAINING_RECORD(Entry, ETHREAD, ThreadListEntry);
        // First use of MT_SUCCEEDED btw.
        if (MT_SUCCEEDED(ObReferenceObjectByPointer(FoundThread, PsThreadType))) break;
           
        // Nothing found, keep loopin.
        FoundThread = NULL;
        Entry = Entry->Flink;
    }

Cleanup:
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