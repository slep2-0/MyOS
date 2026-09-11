/*
 * PROJECT:      MatanelOS
 * LICENSE:      GPLv3
 * PURPOSE:      Access masks shared by kernel and user mode.
 */

#ifndef MATANELOS_SHARED_ACCESSRIGHTS_H
#define MATANELOS_SHARED_ACCESSRIGHTS_H

/* Standard rights */
#define MT_SYNCHRONIZE 0x00100000

/* Synchronization Objects rights */
#define MT_EVENT_QUERY_STATE        0x0001
#define MT_EVENT_MODIFY_STATE       0x0002
#define MT_EVENT_ALL_ACCESS \
    (MT_SYNCHRONIZE | MT_EVENT_MODIFY_STATE | MT_EVENT_QUERY_STATE)

#define MT_SEMAPHORE_QUERY_STATE    0x0001
#define MT_SEMAPHORE_MODIFY_STATE   0x0002
#define MT_SEMAPHORE_ALL_ACCESS \
    (MT_SYNCHRONIZE | MT_SEMAPHORE_MODIFY_STATE | MT_SEMAPHORE_QUERY_STATE)

#define MT_MUTEX_QUERY_STATE        0x0001
#define MT_MUTEX_ALL_ACCESS \
    (MT_SYNCHRONIZE | MT_MUTEX_QUERY_STATE)

/* Thread rights */
#define MT_THREAD_TERMINATE      0x0001
#define MT_THREAD_SUSPEND_RESUME 0x0002
#define MT_THREAD_SET_CONTEXT    0x0004
#define MT_THREAD_GET_CONTEXT    0x0008
#define MT_THREAD_QUERY_INFO     0x0010
#define MT_THREAD_SET_INFO       0x0020
#define MT_THREAD_ALL_ACCESS     (MT_SYNCHRONIZE | 0x003F)

/* Process rights */
#define MT_PROCESS_TERMINATE      0x0001
#define MT_PROCESS_CREATE_THREAD  0x0002
#define MT_PROCESS_VM_OPERATION   0x0004
#define MT_PROCESS_VM_READ        0x0008
#define MT_PROCESS_VM_WRITE       0x0010
#define MT_PROCESS_DUP_HANDLE     0x0020
#define MT_PROCESS_SET_INFO       0x0040
#define MT_PROCESS_QUERY_INFO     0x0080
#define MT_PROCESS_SUSPEND_RESUME 0x0100
#define MT_PROCESS_CREATE_PROCESS 0x0200
#define MT_PROCESS_ALL_ACCESS     (MT_SYNCHRONIZE | 0x03FF)

/* Section Rights */
#define MT_SECTION_QUERY             0x0001  // Query section info (size, attributes)
#define MT_SECTION_MAP_WRITE         0x0002  // Map section with write permissions
#define MT_SECTION_MAP_READ          0x0004  // Map section with read permissions
#define MT_SECTION_MAP_EXECUTE       0x0008  // Map section with execute permissions
#define MT_SECTION_EXTEND_SIZE       0x0010  // Extend section size (file-backed sections)
#define MT_SECTION_MAP_EXECUTE_EXPL  0x0020  // Explicit executable mapping (DEP / NX override)

// All valid section rights
#define MT_SECTION_ALL_ACCESS        0x003F

/* Object directory rights */
#define MT_DIRECTORY_ALL_ACCESS      0x000F

/* Symbolic-link rights */
#define MT_SYMBOLIC_LINK_QUERY       0x0001
#define MT_SYMBOLIC_LINK_ALL_ACCESS  MT_SYMBOLIC_LINK_QUERY

/* File and directory rights */
#define MT_FILE_READ_DATA            0x0001
#define MT_FILE_LIST_DIRECTORY       0x0001
#define MT_FILE_WRITE_DATA           0x0002
#define MT_FILE_ADD_FILE             0x0002
#define MT_FILE_APPEND_DATA          0x0004
#define MT_FILE_ADD_SUBDIRECTORY     0x0004
#define MT_FILE_CREATE_PIPE_INSTANCE 0x0004
#define MT_FILE_READ_EA              0x0008
#define MT_FILE_WRITE_EA             0x0010
#define MT_FILE_EXECUTE              0x0020
#define MT_FILE_TRAVERSE             0x0020
#define MT_FILE_DELETE_CHILD         0x0040
#define MT_FILE_READ_ATTRIBUTES      0x0080
#define MT_FILE_WRITE_ATTRIBUTES     0x0100
#define MT_FILE_ALL_ACCESS           0x01FF

#define MT_FILE_GENERIC_READ \
    (MT_FILE_READ_DATA | MT_FILE_READ_ATTRIBUTES | MT_FILE_READ_EA)
#define MT_FILE_GENERIC_WRITE \
    (MT_FILE_WRITE_DATA | MT_FILE_WRITE_ATTRIBUTES | MT_FILE_WRITE_EA | \
     MT_FILE_APPEND_DATA)
#define MT_FILE_GENERIC_EXECUTE \
    (MT_FILE_READ_ATTRIBUTES | MT_FILE_EXECUTE)

#endif /* MATANELOS_SHARED_ACCESSRIGHTS_H */
