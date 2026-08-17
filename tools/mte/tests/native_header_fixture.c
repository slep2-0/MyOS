#include "MatanelOS.h"
#include "mtnative.h"

STATIC_ASSERT(sizeof(HANDLE) == sizeof(int32_t), "HANDLE ABI changed");
STATIC_ASSERT(
    sizeof(DOUBLY_LINKED_LIST) == 16,
    "DOUBLY_LINKED_LIST ABI changed"
);
STATIC_ASSERT(sizeof(MT_MODULE_INFO) == 272, "MT_MODULE_INFO ABI changed");
STATIC_ASSERT(sizeof(MTDLL_BASIC_TYPES) == 552, "MTDLL_BASIC_TYPES ABI changed");
STATIC_ASSERT(
    sizeof(LDR_DATA_TABLE_ENTRY) == 392,
    "LDR_DATA_TABLE_ENTRY ABI changed"
);
STATIC_ASSERT(
    offsetof(LDR_DATA_TABLE_ENTRY, TlsIndex) == 336,
    "LDR_DATA_TABLE_ENTRY.TlsIndex offset changed"
);
STATIC_ASSERT(
    offsetof(LDR_DATA_TABLE_ENTRY, TlsDirectory) == 344,
    "LDR_DATA_TABLE_ENTRY.TlsDirectory offset changed"
);
STATIC_ASSERT(sizeof(PEB_LDR_DATA) == 24, "PEB_LDR_DATA ABI changed");
STATIC_ASSERT(sizeof(PEB) == 56, "PEB ABI changed");
STATIC_ASSERT(offsetof(PEB, ImageBase) == 8, "PEB.ImageBase offset changed");
STATIC_ASSERT(offsetof(PEB, LoaderData) == 16, "PEB.LoaderData offset changed");
STATIC_ASSERT(offsetof(PEB, ProcessHeap) == 40, "PEB.ProcessHeap offset changed");
STATIC_ASSERT(offsetof(PEB, NextTlsIndex) == 48, "PEB.NextTlsIndex offset changed");
STATIC_ASSERT(sizeof(MT_TIB) == 24, "MT_TIB ABI changed");
STATIC_ASSERT(sizeof(TEB) == 88, "TEB ABI changed");
STATIC_ASSERT(offsetof(TEB, MtTib) == 0, "TEB.MtTib offset changed");
STATIC_ASSERT(
    offsetof(TEB, ProcessEnvironmentBlock) == 40,
    "TEB.ProcessEnvironmentBlock offset changed"
);
STATIC_ASSERT(
    offsetof(TEB, LastErrorValue) == 48,
    "TEB.LastErrorValue offset changed"
);
STATIC_ASSERT(
    offsetof(TEB, LastStatusValue) == 52,
    "TEB.LastStatusValue offset changed"
);
STATIC_ASSERT(
    offsetof(TEB, StaticTlsAllocation) == 56,
    "TEB.StaticTlsAllocation offset changed"
);
STATIC_ASSERT(
    offsetof(TEB, ThreadPointer) == 64,
    "TEB.ThreadPointer offset changed"
);
STATIC_ASSERT(offsetof(TEB, TlsSlots) == 72, "TEB.TlsSlots offset changed");
STATIC_ASSERT(
    offsetof(TEB, TlsSlotCount) == 80,
    "TEB.TlsSlotCount offset changed"
);
STATIC_ASSERT(
    ProcessBasicInformation == 0,
    "PROCESSINFOCLASS ABI changed"
);

MTSTATUS
NativeHeaderFixture(
    HANDLE ObjectHandle
)
{
    return MtWaitForSingleObject(ObjectHandle, 0, false);
}

bool
PublicHeaderFixture(
    HANDLE ProcessHandle,
    uint32_t* ExitCode
)
{
    return GetExitCodeProcess(ProcessHandle, ExitCode);
}
