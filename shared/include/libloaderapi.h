#pragma once
#include "mtstatus.h"
#include <stdbool.h>
#include "mtapi.h"

typedef void* HMODULE;

typedef enum _DLL_REASON {
    DLL_PROCESS_DETACH = 0,
    DLL_PROCESS_ATTACH = 1,
    DLL_THREAD_ATTACH = 2,
    DLL_THREAD_DETACH = 3
} DLL_REASON;

typedef bool (*PDLL_ENTRY_POINT)(
    void* ModuleBase,
    DLL_REASON Reason,
    void* Reserved
    );

MTDLL_API HMODULE
LoadLibrary(
    IN const char* DllPath
);

MTDLL_API HMODULE
GetModuleHandle(
    IN const char* ModuleName
);

MTDLL_API void*
GetProcAddress(
    IN HMODULE Module,
    IN const char* FunctionName
);

MTDLL_API bool
FreeLibrary(
    IN HMODULE Module
);


