/*++

Module Name:

    psmgr.c

Purpose:

    This translation unit contains the initialization routines of the Process & Thread subsystem.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/ps.h"
#include "../../includes/ob.h"

// Explanation for future me or anything going over my kernel.
// Instead of creating processes and deleting them when exiting, we use an object manager
// To automatically do this for us when the reference count for the required thread ends.
// It defines how should the process / thread be created (With what pool, what access rights)
// It supplements the core functionality of security for the process & threads life, and access.

// Reference count reaches 0 -> Dump Routine -> Deletion Routine (depends on kind of thread)

POBJECT_TYPE PsProcessType;
POBJECT_TYPE PsThreadType;

static
MTSTATUS
PsInitializeProcessThreadManager(
    void
)

/*++

    Routine description:

       Initializes the process & thread subsystem.

    Arguments:

        None.

    Return Values:

        MTSTATUS Status codes representing if succeeded or not.
        If we didn't succeed, system should bugcheck with status code.

--*/

{
    // Define how each thread & process be created and deleted.
    MTSTATUS status;
    OBJECT_TYPE_INITIALIZER ObjectTypeInitializer;
    kmemset(&ObjectTypeInitializer, 0, sizeof(OBJECT_TYPE_INITIALIZER));

    // Processes
    char* Name = "Process";
    ObjectTypeInitializer.PoolType = NonPagedPool;
#ifdef DEBUG
    ObjectTypeInitializer.DumpProcedure = NULL; // TODO DUMP PROC!
#else
    ObjectTypeInitializer.DumpProcedure = NULL;
#endif
    ObjectTypeInitializer.DeleteProcedure = &PsDeleteProcess;
    ObjectTypeInitializer.ValidAccessRights = MT_PROCESS_ALL_ACCESS;
    status = ObCreateObjectType(Name, &ObjectTypeInitializer, &PsProcessType);
    if (MT_FAILURE(status)) return status;

    // Threads
    Name = "Thread";
    ObjectTypeInitializer.PoolType = NonPagedPool;
#ifdef DEBUG
    ObjectTypeInitializer.DumpProcedure = NULL; // TODO DUMP PROC!
#else
    ObjectTypeInitializer.DumpProcedure = NULL;
#endif
    ObjectTypeInitializer.DeleteProcedure = &PsDeleteThread;
    ObjectTypeInitializer.ValidAccessRights = MT_THREAD_ALL_ACCESS;
    status = ObCreateObjectType(Name, &ObjectTypeInitializer, &PsThreadType);
    if (MT_FAILURE(status)) return status;

    return MT_SUCCESS;
}


MTSTATUS
PsInitializeSystem(
    IN  enum _PS_PHASE_ROUTINE Phase
)

{
    if (Phase == PS_PHASE_INITIALIZE_SYSTEM) {
        // Initialize the PS Subsystem.
        // Initialize the CID Table.
        PsInitializeCidTable();

        // Initialize the process & thread subsystem.
        MTSTATUS st = PsInitializeProcessThreadManager();
        return st;
    }
    else if (Phase == PS_PHASE_INITIALIZE_WORKER_THREADS) {
        PsInitializeWorkerThreads();
        return MT_SUCCESS;
    }
    else {
        MeBugCheck(INVALID_INITIALIZATION_PHASE);
    }
}

