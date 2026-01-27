/*++

Module Name:

	accessrights.h

Purpose:

	This module contains the macros & definitions that signify the access rights of an handle.

Author:

	slep (Matanel) 2025.

Revision History:

--*/

#pragma once

//
// Thread Access Rights
//
#define MT_THREAD_TERMINATE          0x0001    // Terminate the thread
#define MT_THREAD_SUSPEND_RESUME     0x0002    // Suspend or resume thread execution
#define MT_THREAD_SET_CONTEXT        0x0004    // Modify thread CPU context (registers, e.g RIP/RSP)
#define MT_THREAD_GET_CONTEXT        0x0008    // Read thread CPU context
#define MT_THREAD_QUERY_INFO         0x0010    // Query thread info (state, priority, etc.)
#define MT_THREAD_SET_INFO           0x0020    // Modify thread info (priority, name, affinity)

#define MT_THREAD_ALL_ACCESS         0x003F    // Request all valid thread access rights


//
// Process Access Rights
//
#define MT_PROCESS_TERMINATE          0x0001  // Kill the process
#define MT_PROCESS_CREATE_THREAD      0x0002  // Create a new thread inside process
#define MT_PROCESS_VM_OPERATION       0x0004  // Allocate/Protect/Free process memory
#define MT_PROCESS_VM_READ            0x0008  // Read from process memory
#define MT_PROCESS_VM_WRITE           0x0010  // Write to process memory
#define MT_PROCESS_DUP_HANDLE         0x0020  // Duplicate a handle into this process
#define MT_PROCESS_SET_INFO           0x0040  // Modify process properties/metadata
#define MT_PROCESS_QUERY_INFO         0x0080  // Query process details (PID, exit code, etc.)
#define MT_PROCESS_SUSPEND_RESUME     0x0100  // Suspend / Resume process
#define MT_PROCESS_CREATE_PROCESS     0x0200  // Create a new process.

#define MT_PROCESS_ALL_ACCESS         0x01FF  // Everything above

//
// File Access Rights
//
#define MT_FILE_READ_DATA            0x0001  // file & pipe
#define MT_FILE_LIST_DIRECTORY       0x0001  // directory

#define MT_FILE_WRITE_DATA           0x0002  // file & pipe
#define MT_FILE_ADD_FILE             0x0002  // directory

#define MT_FILE_APPEND_DATA          0x0004  // file
#define MT_FILE_ADD_SUBDIRECTORY     0x0004  // directory
#define MT_FILE_CREATE_PIPE_INSTANCE 0x0004  // named pipe

#define MT_FILE_READ_EA              0x0008  // file & directory
#define MT_FILE_WRITE_EA             0x0010  // file & directory

#define MT_FILE_EXECUTE              0x0020  // file
#define MT_FILE_TRAVERSE             0x0020  // directory

#define MT_FILE_DELETE_CHILD         0x0040  // directory

#define MT_FILE_READ_ATTRIBUTES      0x0080  // all
#define MT_FILE_WRITE_ATTRIBUTES     0x0100  // all
#define MT_FILE_ALL_ACCESS           0x01FF  // everything above

#define MT_FILE_GENERIC_READ  ( MT_FILE_READ_DATA    | MT_FILE_READ_ATTRIBUTES | MT_FILE_READ_EA )
#define MT_FILE_GENERIC_WRITE ( MT_FILE_WRITE_DATA   | MT_FILE_WRITE_ATTRIBUTES | MT_FILE_WRITE_EA | MT_FILE_APPEND_DATA )
#define MT_FILE_GENERIC_EXECUTE ( MT_FILE_READ_ATTRIBUTES | MT_FILE_EXECUTE )