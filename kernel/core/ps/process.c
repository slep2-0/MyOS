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
#include "../../includes/mt.h"

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
// A zero-initialized static PUSH_LOCK is in the same state produced by
// MsInitializePushLock and is ready before the MTDLL cache is first used.
static PUSH_LOCK PsMtdllCacheLock = { 0 };

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
PspFindMtdllEntryRva(
    IN PFILE_OBJECT MtdllObject,
    IN const char* RoutineName
)
{
    if (!RoutineName) return NULL;

    void* RequestedRva = NULL;
    MsAcquirePushLockExclusive(&PsMtdllCacheLock);

    // If already cached use it.
    if (PsMtdllRvasSaved && PsMtdllExportCache != NULL) {
        for (size_t i = 0; i < PsMtdllExportCount; ++i) {
            if (kstrcmp(PsMtdllExportCache[i].RoutineName, RoutineName) == 0) {
                RequestedRva = PsMtdllExportCache[i].RoutineRva;
                break;
            }
        }
        goto Cleanup;
    }

    // No cache yet, file object required.
    if (MtdllObject == NULL) {
        goto Cleanup;
    }

    MTE_HEADER hdr;
    size_t br;
    MTSTATUS st;

    // Read MTE header
    st = FsReadFile(MtdllObject, 0, &hdr, sizeof(hdr), &br);
    if (MT_FAILURE(st) || br != sizeof(hdr)) goto Cleanup;

    if (hdr.Magic[0] != 'M' || hdr.Magic[1] != 'T' || hdr.Magic[2] != 'E' || hdr.Magic[3] != '\0') {
        goto Cleanup;
    }

    if (hdr.exports_rva == 0 ||
        hdr.exports_rva > MtdllObject->FileSize ||
        hdr.exports_size < sizeof(MT_EXPORT_ENTRY) ||
        hdr.exports_size % sizeof(MT_EXPORT_ENTRY) != 0 ||
        hdr.exports_size > MtdllObject->FileSize - hdr.exports_rva) {
        goto Cleanup;
    }

    size_t max_entries = hdr.exports_size / sizeof(MT_EXPORT_ENTRY);
    if (max_entries == 0 || max_entries > SIZE_MAX / sizeof(MTDLL_CACHE_ENTRY)) {
        goto Cleanup;
    }

    // Allocate memory for the cache based on max_entries
    PsMtdllExportCache = (MTDLL_CACHE_ENTRY*)MmAllocatePoolWithTag(NonPagedPool, max_entries * sizeof(MTDLL_CACHE_ENTRY), 'CACH');
    if (PsMtdllExportCache == NULL) {
        goto Cleanup;
    }
    kmemset(PsMtdllExportCache, 0, max_entries * sizeof(MTDLL_CACHE_ENTRY));
    PsMtdllExportCount = 0;

    MT_EXPORT_ENTRY entry;
    char namebuf[MAX_EXPORTED_FUNC_NAME];

    // Loop through the disk entries and save.
    for (size_t i = 0; i < max_entries; ++i) {
        uint64_t entry_off = hdr.exports_rva + (uint64_t)(i * sizeof(MT_EXPORT_ENTRY));

        st = FsReadFile(MtdllObject, entry_off, &entry, sizeof(entry), &br);
        if (MT_FAILURE(st) || br != sizeof(entry)) {
            break;
        }

        uint64_t name_rva_calculated = entry.name_rva;
        if (name_rva_calculated == 0 ||
            name_rva_calculated >= MtdllObject->FileSize ||
            entry.func_rva >= MtdllObject->FileSize) {
            continue;
        }

        if (ReadStringFromFile(MtdllObject, name_rva_calculated, namebuf, sizeof(namebuf)) != 0) {
            continue;
        }

        // Save the valid entry to our memory cache
        kstrcpy(PsMtdllExportCache[PsMtdllExportCount].RoutineName, namebuf);
        PsMtdllExportCache[PsMtdllExportCount].RoutineRva = (void*)(uintptr_t)(entry.func_rva);

        // Check if this is the routine the caller originally wanted
        if (kstrcmp(namebuf, RoutineName) == 0) {
            RequestedRva = PsMtdllExportCache[PsMtdllExportCount].RoutineRva;
        }

        PsMtdllExportCount++;
    }

    // Cached, now set the global.
    PsMtdllRvasSaved = true;

Cleanup:
    MsReleasePushLockExclusive(&PsMtdllCacheLock);
    return RequestedRva;
}

// MTDLL Entries must be cached for this function to work.
uintptr_t
PspFindMtdllEntryAddress(
    IN const char* RoutineName,
    IN PETHREAD Thread
)

{
    if (!RoutineName || !Thread || !Thread->ParentProcess) {
        return 0;
    }

    void* FunctionRva = PspFindMtdllEntryRva(NULL, RoutineName);

    if (!FunctionRva) {
        return 0;
    }

    PEPROCESS Process = Thread->ParentProcess;
    uintptr_t MtdllBase = (uintptr_t)Process->MtdllBase;
    uintptr_t FunctionOffset = (uintptr_t)FunctionRva;

    // The PEB loader list belongs to user mode and cannot be trusted to choose
    // a privileged dispatch target. Resolve the kernel-cached RVA against the
    // base recorded when this process's MTDLL section was mapped.
    if (!MtdllBase || MtdllBase > MmHighestUserAddress ||
        FunctionOffset > MmHighestUserAddress - MtdllBase) {
        return 0;
    }

    return MtdllBase + FunctionOffset;
}

static
MTSTATUS
PspRelocateImage(
    IN void* ImageBase,
    IN MTE_HEADER* Header,
    IN size_t ImageSize
)
{
    if (!ImageBase || !Header || ImageSize < sizeof(*Header)) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    if (Header->reloc_size == 0) {
        return MT_SUCCESS;
    }

    if (Header->reloc_size % sizeof(MTE_RELOCATION) != 0 ||
        Header->reloc_rva > ImageSize ||
        Header->reloc_size > ImageSize - Header->reloc_rva) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Point to the relocation table
    MTE_RELOCATION* reloc_table =
        (MTE_RELOCATION*)((uintptr_t)ImageBase + Header->reloc_rva);
    size_t count = Header->reloc_size / sizeof(MTE_RELOCATION);

    // Iterate and Fix
    for (size_t i = 0; i < count; i++) {
        MTE_RELOCATION* entry = &reloc_table[i];

        // The MTE packer emits only normalized image-relative relocations.
        if ((entry->r_info & 0xFFFFFFFF) !=
            MTE_RELOCATION_X86_64_RELATIVE ||
            (entry->r_info >> 32) != 0 ||
            entry->r_offset > ImageSize - sizeof(uintptr_t) ||
            entry->r_addend < 0 ||
            (uint64_t)entry->r_addend >= ImageSize) {
            return MT_INVALID_IMAGE_FORMAT;
        }

        uintptr_t* target_ptr =
            (uintptr_t*)((uintptr_t)ImageBase + entry->r_offset);
        *target_ptr = (uintptr_t)ImageBase + (uintptr_t)entry->r_addend;
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
    if (!ExecutablePath || !ProcessHandle) return MT_INVALID_PARAM;
    *ProcessHandle = MT_INVALID_HANDLE;

    MTSTATUS Status = MT_GENERAL_FAILURE;
    PEPROCESS Process = NULL;
    PEPROCESS Parent = NULL;
    PFILE_OBJECT MtdllObject = NULL;
    PFILE_OBJECT FileObject = NULL;
    HANDLE hProcess = MT_INVALID_HANDLE;
    bool ProcessHandleCreated = false;
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
    Status = ObCreateObject(PsProcessType, sizeof(EPROCESS), (void**)&Process);
    if (MT_FAILURE(Status)) goto Cleanup;

    // Initialize the push locks first
    MsInitializePushLock(&Process->ProcessLock);
    MsInitializePushLock(&Process->ThreadListLock);
    MsInitializePushLock(&Process->AddressSpaceLock);
    MsInitializePushLock(&Process->VadLock);

    // No MTDLL mapping is trusted until its image has been mapped, validated,
    // and relocated successfully below.
    Process->MtdllSection = NULL;
    Process->MtdllBase = NULL;

    // CleanupWithRef from now on.
    // Assume failure status.
    Status = MT_GENERAL_FAILURE;
    // Setup the process now, create its PID.
    Process->PID = PsAllocateProcessId(Process);
    if (Process->PID == MT_INVALID_HANDLE) {
        Status = MT_NO_RESOURCES;
        goto CleanupWithRef;
    }

    // Initialize Dispatcher Header
    MsInitializeDispatcherHeader(&Process->InternalProcess.Header, 0, DispatcherProcess);
    Process->ExitStatus = MT_PENDING;

    // Set its parent process handle.
    Process->ParentProcess = ParentProcess;

    // Set its image name.
    char filename[24];
    bool mtexeok = GetBaseName(ExecutablePath, filename, sizeof(filename));
    if (!mtexeok || filename[0] == '\0') {
        Status = MT_INVALID_IMAGE_FORMAT;
        goto CleanupWithRef;
    }
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
    if (!HandleTable) {
        Status = MT_NO_MEMORY;
        goto CleanupWithRef;
    }
    Process->ObjectTable = HandleTable;

    // Open MTDLL for the process. (ALWAYS needed to map it into memory, code below also uses it)
    HANDLE MtdllHandle;

    Status = FsCreateFile(MTDLL_PATH, MT_FILE_ALL_ACCESS, FILE_OPEN_EXISTING, &MtdllHandle);
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Reference the handle
    Status = ObReferenceObjectByHandle(MtdllHandle, MT_FILE_ALL_ACCESS, FsFileType, (void**)&MtdllObject, NULL);
    HtClose(MtdllHandle);
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Find MTDLL Entrypoint now. 
    // (PspFindMtdllEntry will safely use the cache and ignore MtdllObject if PsMtdllRvasSaved is true)
    void* MtdllInitializeProcessRva = PspFindMtdllEntryRva(MtdllObject, MTDLL_TARGET_ENTRY);
    if (!MtdllInitializeProcessRva) {
        Status = MT_INVALID_IMAGE_FORMAT;
        goto CleanupWithRef;
    }

    // Create sections for MTDLL
    void* MtdllSection;
    Status = MmCreateSection(&MtdllSection, MtdllObject);
    if (MT_FAILURE(Status)) goto CleanupWithRef;
    
    // Set in process.
    Process->MtdllSection = MtdllSection;

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
        if (LoadedHeader->Magic[0] == 'M' && LoadedHeader->Magic[1] == 'T' &&
            LoadedHeader->Magic[2] == 'E' && LoadedHeader->Magic[3] == '\0') {
            Status = PspRelocateImage(
                MtdllBase,
                LoadedHeader,
                ((PMM_SECTION)MtdllSection)->ImageSize
            );
        }
        else {
            Status = MT_INVALID_IMAGE_FORMAT;
        }
    } except{
         Status = GetExceptionCode();
    } end_try;

    MeDetachProcess(&RelocApcState);

    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Publish the trusted mapping base only after image validation and
    // relocation have completed. Kernel dispatch paths never consult the
    // user-writable PEB loader list for this address.
    Process->MtdllBase = MtdllBase;
    
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
    Status = FsCreateFile(ExecutablePath, MT_FILE_ALL_ACCESS, FILE_OPEN_EXISTING, &FileHandle);
    if (MT_FAILURE(Status)) goto CleanupWithRef;
    // Reference the handle, and then close it so only the pointer reference remains (this)
    Status = ObReferenceObjectByHandle(FileHandle, MT_FILE_ALL_ACCESS, FsFileType, (void**)&FileObject, NULL);
    HtClose(FileHandle);
    if (MT_FAILURE(Status)) goto CleanupWithRef;
    // TODO ADD ADDRESS TO WORKING SET OF PROCESS!!

    // Create the sections for the process.
    void* SectionObject;
    Status = MmCreateSection(&SectionObject, FileObject);
    if (MT_FAILURE(Status)) {
        // If file reference failed it would close the file handle.
        goto CleanupWithRef;
    }

    // Set process section.
    Process->SectionObject = SectionObject;

    // Map them into address space.
    // Start address - entry point.
    void* StartAddress = NULL;
    // Executable base address.
    void* ExecutableBaseAddress = NULL;
    Status = MmMapViewOfSection(SectionObject, Process, &StartAddress, &ExecutableBaseAddress);
    // MmpDeleteSection closes the file handle.
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // RELA entries are zero-backed in the file and must be applied even when
    // the image lands at its preferred base. The MTE packer has already
    // converted ELF load-bias addends into image-relative addends.
    APC_STATE ExecutableRelocApcState;
    MeAttachProcess(&Process->InternalProcess, &ExecutableRelocApcState);
    try {
        MTE_HEADER* ExecutableHeader = (MTE_HEADER*)ExecutableBaseAddress;
        if (ExecutableHeader->Magic[0] == 'M' &&
            ExecutableHeader->Magic[1] == 'T' &&
            ExecutableHeader->Magic[2] == 'E' &&
            ExecutableHeader->Magic[3] == '\0') {
            Status = PspRelocateImage(
                ExecutableBaseAddress,
                ExecutableHeader,
                ((PMM_SECTION)SectionObject)->ImageSize
            );
        }
        else {
            Status = MT_INVALID_IMAGE_FORMAT;
        }
    } except {
        Status = GetExceptionCode();
    } end_try;
    MeDetachProcess(&ExecutableRelocApcState);
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Create PEB.
    PMTDLL_BASIC_TYPES BasicTypes = NULL;
    Status = MmCreatePeb(Process, (void**)&Process->Peb, (void**)&BasicTypes);
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // Attempt to set the entry point in the PEB.
    // Attach to process first.
    APC_STATE ApcState;
    MeAttachProcess(&Process->InternalProcess, &ApcState);

    // Also create basic MTDLL types.
    try {
        // For now peb is guranteed to be zeroed since allocating a PFN in fault.c is zeroed, but ill still set it to 0
        Process->Peb->BeingDebugged = false;
        Process->Peb->ImageBase = ExecutableBaseAddress;
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
    Status = ObCreateHandleForObject(Process, DesiredAccess, &hProcess);
    if (MT_FAILURE(Status)) goto CleanupWithRef;
    ProcessHandleCreated = true;
    

    // Create a main thread for the process.
    Process->NextStackHint = USER_INITIAL_STACK_TOP;
    HANDLE MainThreadHandle;

#ifdef DEBUG
    gop_printf(COLOR_CYAN, "MTDLL Created for %s at base %p\n", Process->ImageName, MtdllBase);
    gop_printf(COLOR_CYAN, "Process %s created at base %p and entrypoint %p\n", Process->ImageName, ExecutableBaseAddress, StartAddress);
#endif

    Status = PsCreateThread(Process, &MainThreadHandle, (THREAD_START_ROUTINE)StartAddress, (THREAD_PARAMETER)BasicTypes, DEFAULT_TIMESLICE_TICKS, (ThreadEntry)MtdllInitializeProcess);
    if (MT_FAILURE(Status)) goto CleanupWithRef;

    // We are, successful.
    *ProcessHandle = hProcess;
    Status = MT_SUCCESS;

CleanupWithRef:
    if (MT_FAILURE(Status) && ProcessHandleCreated) {
        HtClose(hProcess);
        ProcessHandleCreated = false;
    }
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

    // Sections and mapped VADs hold their own references. These are only the
    // process-creation routine's temporary references.
    if (FileObject) ObDereferenceObject(FileObject);
    if (MtdllObject) ObDereferenceObject(MtdllObject);
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
    gop_printf(COLOR_MAGENTA, "**PsTerminateProcess called on process %p with name %s, ExitCode is %x (MTSTATUS)**\n", Process, Process->ImageName, ExitCode);
#endif
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
    int32_t PreviousFlags = InterlockedOr32(
        (volatile int32_t*) & Process->Flags,
        ProcessBeingTerminated
    );

    if (PreviousFlags & ProcessBeingTerminated) {
        return MT_PROCESS_IS_TERMINATING;
    }

    IRQL oldIrql;
    MsAcquireSpinlock(&Process->InternalProcess.Header.Lock, &oldIrql);

    if (Process->InternalProcess.ProcessState != PROCESS_TERMINATED) {
        Process->InternalProcess.ProcessState = PROCESS_TERMINATING;
        Process->ExitStatus = ExitCode;
    }

    MsReleaseSpinlock(&Process->InternalProcess.Header.Lock, oldIrql);

    // PsGetNextProcessThread transfers a safe reference from the previous
    // cursor to the next one while holding ThreadListLock. Thread list entries
    // remain linked until object deletion, so a referenced cursor cannot be
    // self-linked by concurrent thread exit.
    PETHREAD Thread = PsGetNextProcessThread(Process, NULL);
    while (Thread) {
        if (Thread == current) {
            SeenOurselves = true;
        }
        else {
            MTSTATUS ThreadStatus = PsTerminateThread(Thread, ExitCode);
            if (MT_FAILURE(ThreadStatus)) {
                Status = ThreadStatus;
            }
            else if (Status == MT_NOTHING_TO_TERMINATE) {
                Status = MT_SUCCESS;
            }
        }

        // This call dereferences Thread after it has referenced the next
        // object under the process thread-list lock.
        Thread = PsGetNextProcessThread(Process, Thread);
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
    // future me - i have no idea what the fuck i meant back then, but ok..

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
    if (Process->SectionObject) {
        ObDereferenceObject(Process->SectionObject);
        Process->SectionObject = NULL;
    }

    if (Process->MtdllSection) {
        ObDereferenceObject(Process->MtdllSection);
        Process->MtdllSection = NULL;
    }

    // Delete all VADs owned by process.
    MiTerminateVadsProcess(Process);
    
    // Delete its CID if construction reached CID allocation.
    if (Process->PID > 0 && Process->PID != MT_INVALID_HANDLE) {
        PsFreeCid(Process->PID);
    }

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

    // Delete its address space if construction reached page-table creation.
    if (Process->InternalProcess.PageDirectoryPhysical) {
        MmDeleteProcessAddressSpace(Process, Process->InternalProcess.PageDirectoryPhysical);
    }

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
    }
    else {
        // Start at beginnininng -- that shit made me laugh (29/01/2026 5:00:04 PM)
        Entry = ListHead->Flink;
    }

    // Set the list head and start the loop.
    while (ListHead != Entry) {
        // While the pointers arent equal (we arent back the start), we enumerate for the next thread.
        FoundThread = CONTAINING_RECORD(Entry, ETHREAD, ThreadListEntry);
        if (ObReferenceObject(FoundThread)) break;
           
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
