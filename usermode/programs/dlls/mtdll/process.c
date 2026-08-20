/*++

Module Name:

    process.c

Purpose:

    This translation unit contains the standard library functions involving process creation, termination, and acquirement.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "includes/mtdll.h"
#include "includes/exports.h"
#include "includes/errorhandlingapi.h"

MTDLL_API
HANDLE
OpenProcess(
    IN  ACCESS_MASK DesiredAccess,
    IN  uint32_t ProcessId
)

/*++

    Routine description:

        Opens a handle to a process with the requested access rights.

    Arguments:

        [IN] DesiredAccess - The access mask requested for the process.
        [IN] ProcessId - The client identifier of the process to open.

    Return Values:

        A valid process handle on success, or MT_INVALID_HANDLE on failure.

    Notes:

        The native status and translated last-error value are updated before
        the routine returns.

--*/

{
    // Call kernel.
    HANDLE OutHandle = MT_INVALID_HANDLE;
    MTSTATUS Status = MtOpenProcess(ProcessId, &OutHandle, DesiredAccess);

    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));
    if (MT_FAILURE(Status)) return MT_INVALID_HANDLE;

    // Return handle.
    return OutHandle;
}

MTDLL_API
bool
TerminateProcess(
    IN  HANDLE ProcessHandle,
    IN  uint32_t ExitCode
)

/*++

    Routine description:

        Requests termination of a process through the native API.

    Arguments:

        [IN] ProcessHandle - The handle of the process to terminate.
        [IN] ExitCode - The exit code assigned to the process.

    Return Values:

        true when termination is requested successfully, or false when the
        native operation fails.

    Notes:

        This routine requests termination; it does not wait for the process to
        finish terminating.

--*/

{
    // Since we dont have termination ports for a process (so we can feed the exit code in), we assume exit code is MTSTATUS
    MTSTATUS Status = MtTerminateProcess(ProcessHandle, ExitCode);
    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));

    return MT_SUCCEEDED(Status);
}

MTDLL_API
bool
GetExitCodeProcess(
    IN  HANDLE ProcessHandle,
    OUT uint32_t* ExitCode
)

/*++

    Routine description:

        Retrieves the exit status of a process.

    Arguments:

        [IN] ProcessHandle - The handle of the process to query.
        [OUT] ExitCode - Receives the process exit code.

    Return Values:

        true when the exit code is returned, or false when the query fails.

    Notes:

        The output buffer must be valid when the native query succeeds.

--*/

{
    PROCESS_BASIC_INFORMATION BasicInfo;
    MTSTATUS Status = MtQueryInformationProcess(ProcessHandle, ProcessBasicInformation, &BasicInfo, sizeof(PROCESS_BASIC_INFORMATION), NULL);
    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));

    bool Success = MT_SUCCEEDED(Status);

    if (Success) {
        *ExitCode = BasicInfo.ExitStatus;
    }

    return Success;
}

MTDLL_API bool
CreateProcess(
    IN const char* ImagePath,
    _In_Opt const char* CommandLine,
    _In_Opt const char* CurrentDirectory,
    _In_Opt const char* Environment,
    IN uint64_t EnvironmentSize,
    OUT PPROCESS_INFORMATION ProcessInformation
)

{
    if (!ProcessInformation || !ImagePath) {
        SetLastStatus(MT_INVALID_PARAM);
        SetLastError(MtStatusToLastError(MT_INVALID_PARAM));
        return false;
    }

    // Initialize both out handles to invalid handle values
    ProcessInformation->ProcessHandle = MT_INVALID_HANDLE;
    ProcessInformation->ThreadHandle = MT_INVALID_HANDLE;
    ProcessInformation->ThreadId = 0;
    ProcessInformation->ProcessId = 0;

    // Begin constructing CREATE_PROCESS_PARAMETERS
    // If there is no command line, the default command line is the image path (for argc argv)
    if (!CommandLine) CommandLine = ImagePath;
    
    // If a directory isnt supplied, then inherit from current process
    if (!CurrentDirectory) CurrentDirectory = MtCurrentPeb()->ProcessParameters->CurrentDirectory;

    // If an env isnt supplied, inherit from current process too
    if (!Environment) {
        Environment = MtCurrentPeb()->ProcessParameters->Environment;
        EnvironmentSize = MtCurrentPeb()->ProcessParameters->EnvironmentSize;
    }

    // Create the struct now.
    MT_CREATE_PROCESS_PARAMETERS Parameters = { 0 };
    
    // Kernel must match this
    Parameters.Size = sizeof(MT_CREATE_PROCESS_PARAMETERS);

    // Controls whether to "CREATE_SUSPENDED" or other flags
    // no suport for that currently, so its zero.
    Parameters.Flags = 0;

    // The image path itself.
    Parameters.ImagePath = ImagePath;
    Parameters.ImagePathLength = strlen(ImagePath);

    // Command line and its length
    Parameters.CommandLine = CommandLine;
    Parameters.CommandLineLength = strlen(CommandLine);

    // Environment and its size
    Parameters.Environment = Environment;
    Parameters.EnvironmentSize = EnvironmentSize;

    // Set directory
    Parameters.CurrentDirectory = CurrentDirectory;
    Parameters.CurrentDirectoryLength = strlen(CurrentDirectory);

    // Creator process is us, obviously (unless the user wants to assign a different one, use system call)
    Parameters.ParentProcess = MtCurrentProcess();

    // Desired access is the full process access, since we created it
    Parameters.DesiredAccess = MT_PROCESS_ALL_ACCESS;

    // Call the kernel
    MTSTATUS Status = MtCreateProcess(
        &Parameters,
        ProcessInformation
    );

    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));

    // Thread and Process handles are set by the kernel since we supplied ProcessInformation directly

    return MT_SUCCEEDED(Status);
}