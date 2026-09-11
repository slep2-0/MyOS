#include "../../kernel/kernel.h"
#include "stress.h"
#include "ob_namespace.h"

#if MT_STRESS_MODE == MT_STRESS_MODE_NAMESPACE

#if !MT_STRESS_AUTOMATION
#error "The isolated object-namespace test requires MT_STRESS_AUTOMATION"
#endif

#define MT_NAMESPACE_DEBUG_PORT 0x402

typedef enum _STRESS_NAMESPACE_FAILURE {
    StressNamespaceUnexpectedStatus = 1,
    StressNamespaceUnexpectedObject,
    StressNamespaceUnexpectedOutput,
    StressNamespaceReferenceMismatch
} STRESS_NAMESPACE_FAILURE;

static void
StressNamespaceWriteText(
    IN const char* Text
)
{
    while (*Text != '\0') {
        __outbyte(MT_NAMESPACE_DEBUG_PORT, (uint8_t)*Text++);
    }
}

NORETURN
static void
StressNamespaceBugCheck(
    IN STRESS_NAMESPACE_FAILURE Failure,
    IN void* Parameter2,
    IN void* Parameter3,
    IN void* Parameter4
)
{
    StressNamespaceWriteText("MT-NAMESPACE FAIL\n");
    MeBugCheckEx(
        MANUALLY_INITIATED_CRASH2,
        (void*)(uintptr_t)Failure,
        Parameter2,
        Parameter3,
        Parameter4
    );
}

static void
StressNamespaceRequireStatus(
    IN MTSTATUS Actual,
    IN MTSTATUS Expected,
    IN void* Stage
)
{
    if (Actual != Expected) {
        StressNamespaceBugCheck(
            StressNamespaceUnexpectedStatus,
            Stage,
            (void*)(uintptr_t)(uint32_t)Expected,
            (void*)(uintptr_t)(uint32_t)Actual
        );
    }
}

static POBJECT_SYMBOLIC_LINK
StressNamespaceCreateLink(
    IN const char* Name,
    IN size_t NameLength,
    IN const char* Target,
    IN size_t TargetLength,
    IN void* Stage
)
{
    POBJECT_SYMBOLIC_LINK Link = NULL;
    MTSTATUS Status = ObCreateSymbolicLink(
        ObAchtungNamedObjectsDirectory,
        Name,
        NameLength,
        MT_OBJ_CASE_INSENSITIVE,
        Target,
        TargetLength,
        &Link
    );
    StressNamespaceRequireStatus(Status, MT_SUCCESS, Stage);
    if (!Link) {
        StressNamespaceBugCheck(
            StressNamespaceUnexpectedOutput,
            Stage,
            NULL,
            NULL
        );
    }
    return Link;
}

static void
StressNamespaceRequireLookup(
    IN const char* Path,
    IN size_t PathLength,
    IN uint32_t Attributes,
    IN void* ExpectedObject,
    IN void* Stage
)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(ExpectedObject);
    uint64_t ReferencesBefore = InterlockedLoad(
        (volatile uint64_t*)&Header->PointerCount
    );
    void* Object = NULL;
    MTSTATUS Status = ObpLookupObjectPath(
        NULL,
        Path,
        PathLength,
        Attributes,
        NULL,
        &Object
    );
    StressNamespaceRequireStatus(Status, MT_SUCCESS, Stage);
    if (Object != ExpectedObject) {
        StressNamespaceBugCheck(
            StressNamespaceUnexpectedObject,
            Stage,
            Object,
            ExpectedObject
        );
    }

    uint64_t ReferencesWithLookup = InterlockedLoad(
        (volatile uint64_t*)&Header->PointerCount
    );
    if (ReferencesWithLookup != ReferencesBefore + 1) {
        StressNamespaceBugCheck(
            StressNamespaceReferenceMismatch,
            Stage,
            (void*)(uintptr_t)ReferencesBefore,
            (void*)(uintptr_t)ReferencesWithLookup
        );
    }

    ObDereferenceObject(Object);
    uint64_t ReferencesAfter = InterlockedLoad(
        (volatile uint64_t*)&Header->PointerCount
    );
    if (ReferencesAfter != ReferencesBefore) {
        StressNamespaceBugCheck(
            StressNamespaceReferenceMismatch,
            Stage,
            (void*)(uintptr_t)ReferencesBefore,
            (void*)(uintptr_t)ReferencesAfter
        );
    }
}

static void
StressNamespaceRequireLookupFailure(
    IN const char* Path,
    IN size_t PathLength,
    IN uint32_t Attributes,
    IN MTSTATUS ExpectedStatus,
    IN void* Stage
)
{
    void* Object = (void*)(uintptr_t)1;
    MTSTATUS Status = ObpLookupObjectPath(
        NULL,
        Path,
        PathLength,
        Attributes,
        NULL,
        &Object
    );
    StressNamespaceRequireStatus(Status, ExpectedStatus, Stage);
    if (Object != NULL) {
        StressNamespaceBugCheck(
            StressNamespaceUnexpectedOutput,
            Stage,
            Object,
            NULL
        );
    }
}

void
StressNamespaceController(
    void
)
{
    static const char DeviceAliasName[] = "ObNsDeviceAlias";
    static const char DeviceAliasPath[] =
        "\\AchtungNamedObjects\\ObNsDeviceAlias";
    static const char DeviceAliasLowerPath[] =
        "\\achtungnamedobjects\\obnsdevicealias";
    static const char DeviceTarget[] = "\\Device";

    static const char RootAliasName[] = "ObNsRootAlias";
    static const char RootAliasDevicePath[] =
        "\\AchtungNamedObjects\\ObNsRootAlias\\Device";
    static const char RootTarget[] = "\\";

    static const char NamespaceAliasName[] = "ObNsNamespaceAlias";
    static const char NamespaceAliasDevicePath[] =
        "\\AchtungNamedObjects\\ObNsNamespaceAlias\\ObNsDeviceAlias";
    static const char NamespaceTarget[] = "\\AchtungNamedObjects";

    static const char MissingAliasName[] = "ObNsMissingAlias";
    static const char MissingAliasPath[] =
        "\\AchtungNamedObjects\\ObNsMissingAlias";
    static const char MissingTarget[] = "\\ObNsMissingTarget";

    static const char SelfAliasName[] = "ObNsSelfAlias";
    static const char SelfAliasPath[] =
        "\\AchtungNamedObjects\\ObNsSelfAlias";

    static const char CycleAName[] = "ObNsCycleA";
    static const char CycleAPath[] =
        "\\AchtungNamedObjects\\ObNsCycleA";
    static const char CycleBName[] = "ObNsCycleB";
    static const char CycleBPath[] =
        "\\AchtungNamedObjects\\ObNsCycleB";

    StressNamespaceWriteText("MT-NAMESPACE START\n");

    POBJECT_SYMBOLIC_LINK DeviceAlias = StressNamespaceCreateLink(
        DeviceAliasName,
        sizeof(DeviceAliasName) - 1,
        DeviceTarget,
        sizeof(DeviceTarget) - 1,
        (void*)0xD001
    );

    POBJECT_SYMBOLIC_LINK Duplicate = (POBJECT_SYMBOLIC_LINK)(uintptr_t)1;
    MTSTATUS Status = ObCreateSymbolicLink(
        ObAchtungNamedObjectsDirectory,
        "obnsdevicealias",
        sizeof("obnsdevicealias") - 1,
        MT_OBJ_CASE_INSENSITIVE,
        DeviceTarget,
        sizeof(DeviceTarget) - 1,
        &Duplicate
    );
    StressNamespaceRequireStatus(
        Status,
        MT_ALREADY_EXISTS,
        (void*)0xD002
    );
    if (Duplicate != NULL) {
        StressNamespaceBugCheck(
            StressNamespaceUnexpectedOutput,
            (void*)0xD002,
            Duplicate,
            NULL
        );
    }
    StressNamespaceWriteText("MT-NAMESPACE CREATE PASS\n");

    StressNamespaceRequireLookup(
        DeviceAliasPath,
        sizeof(DeviceAliasPath) - 1,
        MT_OBJ_CASE_INSENSITIVE,
        ObDeviceDirectory,
        (void*)0xD010
    );
    StressNamespaceRequireLookup(
        DeviceAliasPath,
        sizeof(DeviceAliasPath) - 1,
        MT_OBJ_CASE_INSENSITIVE | MT_OBJ_OPEN_LINK,
        DeviceAlias,
        (void*)0xD011
    );
    StressNamespaceRequireLookup(
        DeviceAliasLowerPath,
        sizeof(DeviceAliasLowerPath) - 1,
        MT_OBJ_CASE_INSENSITIVE,
        ObDeviceDirectory,
        (void*)0xD012
    );
    StressNamespaceRequireLookupFailure(
        DeviceAliasLowerPath,
        sizeof(DeviceAliasLowerPath) - 1,
        0,
        MT_NOT_FOUND,
        (void*)0xD013
    );
    StressNamespaceWriteText("MT-NAMESPACE LOOKUP PASS\n");

    POBJECT_SYMBOLIC_LINK RootAlias = StressNamespaceCreateLink(
        RootAliasName,
        sizeof(RootAliasName) - 1,
        RootTarget,
        sizeof(RootTarget) - 1,
        (void*)0xD020
    );
    StressNamespaceRequireLookup(
        RootAliasDevicePath,
        sizeof(RootAliasDevicePath) - 1,
        MT_OBJ_CASE_INSENSITIVE | MT_OBJ_OPEN_LINK,
        ObDeviceDirectory,
        (void*)0xD021
    );

    POBJECT_SYMBOLIC_LINK NamespaceAlias = StressNamespaceCreateLink(
        NamespaceAliasName,
        sizeof(NamespaceAliasName) - 1,
        NamespaceTarget,
        sizeof(NamespaceTarget) - 1,
        (void*)0xD022
    );
    StressNamespaceRequireLookup(
        NamespaceAliasDevicePath,
        sizeof(NamespaceAliasDevicePath) - 1,
        MT_OBJ_CASE_INSENSITIVE,
        ObDeviceDirectory,
        (void*)0xD023
    );
    StressNamespaceWriteText("MT-NAMESPACE REPARSE PASS\n");

    POBJECT_SYMBOLIC_LINK MissingAlias = StressNamespaceCreateLink(
        MissingAliasName,
        sizeof(MissingAliasName) - 1,
        MissingTarget,
        sizeof(MissingTarget) - 1,
        (void*)0xD030
    );
    StressNamespaceRequireLookupFailure(
        MissingAliasPath,
        sizeof(MissingAliasPath) - 1,
        MT_OBJ_CASE_INSENSITIVE,
        MT_NOT_FOUND,
        (void*)0xD031
    );

    POBJECT_SYMBOLIC_LINK SelfAlias = StressNamespaceCreateLink(
        SelfAliasName,
        sizeof(SelfAliasName) - 1,
        SelfAliasPath,
        sizeof(SelfAliasPath) - 1,
        (void*)0xD032
    );
    StressNamespaceRequireLookupFailure(
        SelfAliasPath,
        sizeof(SelfAliasPath) - 1,
        MT_OBJ_CASE_INSENSITIVE,
        MT_REPARSE_LIMIT_EXCEEDED,
        (void*)0xD033
    );

    POBJECT_SYMBOLIC_LINK CycleA = StressNamespaceCreateLink(
        CycleAName,
        sizeof(CycleAName) - 1,
        CycleBPath,
        sizeof(CycleBPath) - 1,
        (void*)0xD034
    );
    POBJECT_SYMBOLIC_LINK CycleB = StressNamespaceCreateLink(
        CycleBName,
        sizeof(CycleBName) - 1,
        CycleAPath,
        sizeof(CycleAPath) - 1,
        (void*)0xD035
    );
    StressNamespaceRequireLookupFailure(
        CycleAPath,
        sizeof(CycleAPath) - 1,
        MT_OBJ_CASE_INSENSITIVE,
        MT_REPARSE_LIMIT_EXCEEDED,
        (void*)0xD036
    );
    StressNamespaceWriteText("MT-NAMESPACE FAILURE PASS\n");

    ObDereferenceObject(CycleB);
    ObDereferenceObject(CycleA);
    ObDereferenceObject(SelfAlias);
    ObDereferenceObject(MissingAlias);
    ObDereferenceObject(NamespaceAlias);
    ObDereferenceObject(RootAlias);
    ObDereferenceObject(DeviceAlias);

    StressNamespaceWriteText("MT-NAMESPACE RUNTIME PASS\n");
}

#endif
