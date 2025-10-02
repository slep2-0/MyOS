/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     GPLv3
 * PURPOSE:     Process Creation Functions Headers and Prototypes.
 */

#ifndef X86_PROCESS_H
#define X86_PROCESS_H

#include "../../cpu/cpu.h"

/// <summary>
/// The following function creates a user mode process, along with its main thread.
/// </summary>
/// <param name="path">[IN] Full Filesystem path to the executable file.</param>
/// <param name="outProcess">[OUT] Output process structure</param>
/// <param name="outProcess">[IN] Parent Process that requested creation, if NULL, its the SYSTEM process.</param>
/// <returns>MTSTATUS Status Code</returns>
MTSTATUS MtCreateProcess(const char* path, PROCESS** outProcess, PROCESS* ParentProcess);

#endif