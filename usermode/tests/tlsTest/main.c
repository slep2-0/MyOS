#include <MatanelOS.h>
#include <mtstatus.h>

#include "tls_fixture.h"
#include "tls_test.h"

#define TLS_INITIAL_VALUE      0x1122334455667788ULL
#define TLS_MAIN_VALUE         0xA1A2A3A4A5A6A7A8ULL
#define TLS_MAIN_ZERO_VALUE    0xB1B2B3B4B5B6B7B8ULL
#define TLS_WORKER_VALUE       0xC1C2C3C4C5C6C7C8ULL
#define TLS_WORKER_ZERO_VALUE  0xD1D2D3D4D5D6D7D8ULL
#define TLS_DLL_INITIAL_VALUE  0x2132435465768798ULL
#define TLS_DLL_MAIN_VALUE     0x31425364758697A8ULL
#define TLS_DLL_MAIN_ZERO      0x415263748596A7B8ULL
#define TLS_DLL_WORKER_VALUE   0x5162738495A6B7C8ULL
#define TLS_DLL_WORKER_ZERO    0x61728394A5B6C7D8ULL
#define TLS_DYNAMIC_MAIN_VALUE 0x718293A4B5C6D7E8ULL
#define TLS_DYNAMIC_MAIN_ZERO  0x8192A3B4C5D6E7F8ULL
#define TLS_DYNAMIC_WORKER_VALUE 0x91A2B3C4D5E6F708ULL
#define TLS_DYNAMIC_WORKER_ZERO 0xA1B2C3D4E5F60718ULL
#define TLS_SWITCH_ITERATIONS  64u

typedef uint64_t (*PTLS_DLL_READ_U64)(void);
typedef uint8_t (*PTLS_DLL_READ_U8)(void);
typedef uintptr_t (*PTLS_DLL_ADDRESS)(void);
typedef void (*PTLS_DLL_WRITE)(uint64_t, uint64_t, uint8_t);

static PTLS_DLL_READ_U64 DynamicReadInitialized;
static PTLS_DLL_READ_U64 DynamicReadZeroFilled;
static PTLS_DLL_READ_U8 DynamicReadAligned;
static PTLS_DLL_ADDRESS DynamicAlignedAddress;
static PTLS_DLL_WRITE DynamicWrite;

static __thread volatile uint64_t TlsInitialized = TLS_INITIAL_VALUE;
static __thread volatile uint64_t TlsZeroFilled;
static __thread __attribute__((aligned(64))) volatile uint8_t TlsAligned;

static uint32_t
TlsTestWorker(
    IN void* Parameter
)
{
    (void)Parameter;

    if (TlsInitialized != TLS_INITIAL_VALUE) {
        return (uint32_t)MT_TLS_TEST_WORKER_INITIALIZED;
    }
    if (TlsZeroFilled != 0 || TlsAligned != 0) {
        return (uint32_t)MT_TLS_TEST_WORKER_ZERO_FILLED;
    }
    if (((uintptr_t)&TlsAligned & 63u) != 0) {
        return (uint32_t)MT_TLS_TEST_WORKER_ALIGNMENT;
    }
    if (TlsDllReadInitialized() != TLS_DLL_INITIAL_VALUE) {
        return (uint32_t)MT_TLS_TEST_DLL_WORKER_INITIALIZED;
    }
    if (TlsDllReadZeroFilled() != 0 || TlsDllReadAligned() != 0) {
        return (uint32_t)MT_TLS_TEST_DLL_WORKER_ZERO_FILLED;
    }
    if ((TlsDllAlignedAddress() & 63u) != 0) {
        return (uint32_t)MT_TLS_TEST_DLL_WORKER_ALIGNMENT;
    }
    if (DynamicReadInitialized() != TLS_DLL_INITIAL_VALUE) {
        return (uint32_t)MT_TLS_TEST_DYNAMIC_WORKER_INITIALIZED;
    }
    if (DynamicReadZeroFilled() != 0 || DynamicReadAligned() != 0) {
        return (uint32_t)MT_TLS_TEST_DYNAMIC_WORKER_ZERO_FILLED;
    }
    if ((DynamicAlignedAddress() & 63u) != 0) {
        return (uint32_t)MT_TLS_TEST_DYNAMIC_WORKER_ALIGNMENT;
    }

    TlsInitialized = TLS_WORKER_VALUE;
    TlsZeroFilled = TLS_WORKER_ZERO_VALUE;
    TlsAligned = 0x5Au;
    TlsDllWrite(TLS_DLL_WORKER_VALUE, TLS_DLL_WORKER_ZERO, 0x6Bu);
    DynamicWrite(
        TLS_DYNAMIC_WORKER_VALUE,
        TLS_DYNAMIC_WORKER_ZERO,
        0x7Cu
    );

    for (uint32_t Index = 0; Index < TLS_SWITCH_ITERATIONS; Index++) {
        Sleep(1);
        if (TlsInitialized != TLS_WORKER_VALUE ||
            TlsZeroFilled != TLS_WORKER_ZERO_VALUE ||
            TlsAligned != 0x5Au) {
            return (uint32_t)MT_TLS_TEST_WORKER_PRESERVATION;
        }
        if (TlsDllReadInitialized() != TLS_DLL_WORKER_VALUE ||
            TlsDllReadZeroFilled() != TLS_DLL_WORKER_ZERO ||
            TlsDllReadAligned() != 0x6Bu) {
            return (uint32_t)MT_TLS_TEST_DLL_WORKER_PRESERVATION;
        }
        if (DynamicReadInitialized() != TLS_DYNAMIC_WORKER_VALUE ||
            DynamicReadZeroFilled() != TLS_DYNAMIC_WORKER_ZERO ||
            DynamicReadAligned() != 0x7Cu) {
            return (uint32_t)MT_TLS_TEST_DYNAMIC_WORKER_PRESERVATION;
        }
    }

    return (uint32_t)MT_SUCCESS;
}

int
main(
    void
)
{
    if (TlsInitialized != TLS_INITIAL_VALUE) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_MAIN_INITIALIZED);
    }
    if (TlsZeroFilled != 0 || TlsAligned != 0) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_MAIN_ZERO_FILLED);
    }
    if (((uintptr_t)&TlsAligned & 63u) != 0) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_MAIN_ALIGNMENT);
    }
    if (TlsDllReadInitialized() != TLS_DLL_INITIAL_VALUE) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_DLL_MAIN_INITIALIZED);
    }
    if (TlsDllReadZeroFilled() != 0 || TlsDllReadAligned() != 0) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_DLL_MAIN_ZERO_FILLED);
    }
    if ((TlsDllAlignedAddress() & 63u) != 0) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_DLL_MAIN_ALIGNMENT);
    }

    TlsInitialized = TLS_MAIN_VALUE;
    TlsZeroFilled = TLS_MAIN_ZERO_VALUE;
    TlsAligned = 0xA5u;
    TlsDllWrite(TLS_DLL_MAIN_VALUE, TLS_DLL_MAIN_ZERO, 0xB6u);

    for (uint32_t Index = 0; Index < 16; Index++) {
        HMODULE FailedModule = LoadLibrary("FAILTLS.MTE");
        if (FailedModule) {
            FreeLibrary(FailedModule);
            TerminateProcess(
                MtCurrentProcess(),
                MT_TLS_TEST_FAILED_LOAD_ACCEPTED
            );
        }

        if (GetLastError() != ERROR_GEN_FAILURE) {
            TerminateProcess(
                MtCurrentProcess(),
                MT_TLS_TEST_FAILED_LOAD_STATUS
            );
        }
    }

    HMODULE DynamicModule = LoadLibrary("dynamicTls.mtdll");
    if (!DynamicModule) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_DYNAMIC_LOAD);
    }

    DynamicReadInitialized = (PTLS_DLL_READ_U64)GetProcAddress(
        DynamicModule,
        "TlsDllReadInitialized"
    );
    DynamicReadZeroFilled = (PTLS_DLL_READ_U64)GetProcAddress(
        DynamicModule,
        "TlsDllReadZeroFilled"
    );
    DynamicReadAligned = (PTLS_DLL_READ_U8)GetProcAddress(
        DynamicModule,
        "TlsDllReadAligned"
    );
    DynamicAlignedAddress = (PTLS_DLL_ADDRESS)GetProcAddress(
        DynamicModule,
        "TlsDllAlignedAddress"
    );
    DynamicWrite = (PTLS_DLL_WRITE)GetProcAddress(
        DynamicModule,
        "TlsDllWrite"
    );

    if (!DynamicReadInitialized ||
        !DynamicReadZeroFilled ||
        !DynamicReadAligned ||
        !DynamicAlignedAddress ||
        !DynamicWrite) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_DYNAMIC_EXPORT);
    }

    if (DynamicReadInitialized() != TLS_DLL_INITIAL_VALUE) {
        TerminateProcess(
            MtCurrentProcess(),
            MT_TLS_TEST_DYNAMIC_MAIN_INITIALIZED
        );
    }
    if (DynamicReadZeroFilled() != 0 || DynamicReadAligned() != 0) {
        TerminateProcess(
            MtCurrentProcess(),
            MT_TLS_TEST_DYNAMIC_MAIN_ZERO_FILLED
        );
    }
    if ((DynamicAlignedAddress() & 63u) != 0) {
        TerminateProcess(
            MtCurrentProcess(),
            MT_TLS_TEST_DYNAMIC_MAIN_ALIGNMENT
        );
    }

    DynamicWrite(TLS_DYNAMIC_MAIN_VALUE, TLS_DYNAMIC_MAIN_ZERO, 0xC7u);

    HANDLE Thread = CreateThread(TlsTestWorker, NULL);
    if (Thread == MT_INVALID_HANDLE) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_THREAD_CREATE);
    }

    if (WaitForSingleObject(Thread, MT_INFINITE) != WAIT_OBJECT_0) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_THREAD_WAIT);
    }

    uint32_t ExitCode = (uint32_t)MT_PENDING;
    if (!GetExitCodeThread(Thread, &ExitCode)) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_THREAD_QUERY);
    }
    if (!CloseHandle(Thread)) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_THREAD_CLOSE);
    }
    if (ExitCode != (uint32_t)MT_SUCCESS) {
        TerminateProcess(MtCurrentProcess(), (MTSTATUS)ExitCode);
    }

    if (TlsInitialized != TLS_MAIN_VALUE ||
        TlsZeroFilled != TLS_MAIN_ZERO_VALUE ||
        TlsAligned != 0xA5u) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_MAIN_ISOLATION);
    }
    if (TlsDllReadInitialized() != TLS_DLL_MAIN_VALUE ||
        TlsDllReadZeroFilled() != TLS_DLL_MAIN_ZERO ||
        TlsDllReadAligned() != 0xB6u) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_DLL_MAIN_ISOLATION);
    }
    if (DynamicReadInitialized() != TLS_DYNAMIC_MAIN_VALUE ||
        DynamicReadZeroFilled() != TLS_DYNAMIC_MAIN_ZERO ||
        DynamicReadAligned() != 0xC7u) {
        TerminateProcess(
            MtCurrentProcess(),
            MT_TLS_TEST_DYNAMIC_MAIN_ISOLATION
        );
    }

    if (!FreeLibrary(DynamicModule)) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_DYNAMIC_FREE);
    }

    DynamicModule = LoadLibrary("dynamicTls.mtdll");
    if (!DynamicModule) {
        TerminateProcess(MtCurrentProcess(), MT_TLS_TEST_DYNAMIC_RELOAD);
    }

    DynamicReadInitialized = (PTLS_DLL_READ_U64)GetProcAddress(
        DynamicModule,
        "TlsDllReadInitialized"
    );
    if (!DynamicReadInitialized) {
        TerminateProcess(
            MtCurrentProcess(),
            MT_TLS_TEST_DYNAMIC_RELOAD_EXPORT
        );
    }

    if (DynamicReadInitialized() != TLS_DLL_INITIAL_VALUE) {
        TerminateProcess(
            MtCurrentProcess(),
            MT_TLS_TEST_DYNAMIC_RELOAD_INITIALIZED
        );
    }

    if (!FreeLibrary(DynamicModule)) {
        TerminateProcess(
            MtCurrentProcess(),
            MT_TLS_TEST_DYNAMIC_RELOAD_FREE
        );
    }

    TerminateProcess(MtCurrentProcess(), MT_SUCCESS);
    for (;;) {
        __asm__ volatile ("pause");
    }
}
