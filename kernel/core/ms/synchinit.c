#include "../../includes/ms.h"
#include "../../includes/ob.h"
#include "../../includes/ps.h"

POBJECT_TYPE MsEventType;
POBJECT_TYPE MsMutexType;

MTSTATUS
MsInitializeSynchronization(
	void
)

{
    // Define how each mutex & events be created.
    MTSTATUS status;
    OBJECT_TYPE_INITIALIZER ObjectTypeInitializer;
    kmemset(&ObjectTypeInitializer, 0, sizeof(OBJECT_TYPE_INITIALIZER));

    // Mutexes
    char* Name = "Mutex";
    ObjectTypeInitializer.PoolType = NonPagedPool;
#ifdef DEBUG
    ObjectTypeInitializer.DumpProcedure = NULL; // TODO DUMP PROC!
#else
    ObjectTypeInitializer.DumpProcedure = NULL;
#endif
    ObjectTypeInitializer.DeleteProcedure = NULL;
    ObjectTypeInitializer.ValidAccessRights = MT_SYNCHRONIZE;
    status = ObCreateObjectType(Name, &ObjectTypeInitializer, &MsMutexType);
    if (MT_FAILURE(status)) return status;

    // Events
    Name = "Event";
    ObjectTypeInitializer.PoolType = NonPagedPool;
#ifdef DEBUG
    ObjectTypeInitializer.DumpProcedure = NULL; // TODO DUMP PROC!
#else
    ObjectTypeInitializer.DumpProcedure = NULL;
#endif
    ObjectTypeInitializer.DeleteProcedure = NULL;
    ObjectTypeInitializer.ValidAccessRights = MT_SYNCHRONIZE;
    status = ObCreateObjectType(Name, &ObjectTypeInitializer, &MsEventType);
    if (MT_FAILURE(status)) return status;

    // Initialize the timer list head.
    InitializeListHead(&MsTimerQueue);

    return status;
}