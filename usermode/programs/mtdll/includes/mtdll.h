#pragma once
// mtdll.h


// Internal MTDLL support. Applications use MatanelOS.h for the public API and
// explicitly include mtnative.h when they need the unstable native API.

// Stable public declarations and unstable native service declarations.
#include "MatanelOS.h"
#include "mtnative.h"

// Kernel/MTDLL ABI layouts are declared once by shared/include/mttypes.h,
// which MatanelOS.h includes above.

/*++

    Routine description:

        Links a language exception frame into the current thread's exception
        chain.

    Arguments:

        [IN OUT] Frame - The frame to link.
        [IN] Handler - The routine invoked during exception search.

    Return Values:

        None.

--*/
void
MtpPushExceptionFrame(
    PEXCEPTION_REGISTRATION_RECORD Frame,
    PEXCEPTION_ROUTINE Handler
);

/*++

    Routine description:

        Removes a language exception frame from the current thread's chain.

    Arguments:

        [IN OUT] Frame - The frame to unlink.

    Return Values:

        None.

--*/
void
MtpPopExceptionFrame(
    PEXCEPTION_REGISTRATION_RECORD Frame
);

/*++

    Routine description:

        Walks the current language exception chain and dispatches an exception.

    Arguments:

        [IN] ExceptionRecord - The exception record to dispatch.
        [IN OUT] ContextRecord - The register context associated with it.

    Return Values:

        true when a handler accepts the exception, or false when the chain is
        exhausted or no handler accepts it.

--*/
bool
MtpDispatchException(
    PEXCEPTION_RECORD ExceptionRecord,
    PCONTEXT ContextRecord
);

/*++

    Routine description:

        Receives a prepared user exception and enters the user-mode dispatch
        path.

    Arguments:

        [IN] ExceptionRecord - The exception record.
        [IN OUT] ContextRecord - The user register context.

    Return Values:

        Does not return.

--*/
NORETURN
void
MtpUserExceptionDispatcher(
    PEXCEPTION_RECORD ExceptionRecord,
    PCONTEXT ContextRecord
);

/*++

    Routine description:

        Restores a saved language context and returns with the supplied value.

    Arguments:

        [IN] Context - The saved context.
        [IN] ReturnValue - The value returned by the restored context.

    Return Values:

        Does not return to the caller.

--*/
NORETURN
void
MtpRestoreLanguageContext(
    PMT_LANGUAGE_CONTEXT Context,
    int ReturnValue
);

/*++

    Routine description:

        Restores a saved language context for filter continuation.

    Arguments:

        [IN] Context - The saved context.
        [IN] ReturnValue - The filter return value.

    Return Values:

        Does not return to the caller.

--*/
NORETURN
void
MtpRestoreLanguageContextForFilter(
    PMT_LANGUAGE_CONTEXT Context,
    int ReturnValue
);

/// This example is using the legacy kernel structures.
/// Usage: CONTAINING_RECORD(ptr, struct, ptr_member)
/// Example: 
/// CTX_FRAME* ctxframeptr = 0x1234; // Hypothetical address of the pointer.
/// Thread* threadAssociated = CONTAINING_RECORD(ctxframeptr, Thread, ctx); // Note that ctx is the member name for CTX_FRAME in the Thread struct.
#ifndef CONTAINING_RECORD
#define CONTAINING_RECORD(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

static
inline
void
InitializeListHead(
    PDOUBLY_LINKED_LIST Head
)

{
    Head->Flink = Head;
    Head->Blink = Head;
}

// ->>>> CRASHES IN THESE FUNCTIONS USUALLY BECAUSE INITIALIZELISTHEAD WASNT USED ON THE DOUBLY LINKED LIST !!!!!!!

static
inline
void
InsertTailList(
    PDOUBLY_LINKED_LIST Head,
    PDOUBLY_LINKED_LIST Entry
)

{
    PDOUBLY_LINKED_LIST Blink;
    // The last element is the one before Head (circular list style)
    Blink = Head->Blink;
    Entry->Flink = Head;  // New entry points forward to Head
    Entry->Blink = Blink; // New entry points back to old last node
    Blink->Flink = Entry; // Old last node points forward to new entry
    Head->Blink = Entry;  // Head points back to new entry
}

static
inline
void
InsertHeadList(
    PDOUBLY_LINKED_LIST Head,
    PDOUBLY_LINKED_LIST Entry
)
{
    PDOUBLY_LINKED_LIST First;

    // The first element is the one after Head (circular list)
    First = Head->Flink;

    Entry->Flink = First; // Entry -> next = old first
    Entry->Blink = Head;  // Entry -> prev = head

    First->Blink = Entry; // old first -> prev = entry
    Head->Flink = Entry;  // head -> next = entry
}

static
inline
PDOUBLY_LINKED_LIST
RemoveHeadList(
    PDOUBLY_LINKED_LIST Head
)

{
    PDOUBLY_LINKED_LIST Entry;
    PDOUBLY_LINKED_LIST Flink;

    Entry = Head->Flink;
    if (Entry == Head) {
        // List is empty
        return NULL;
    }

    Flink = Entry->Flink;
    Head->Flink = Flink;
    Flink->Blink = Head;

    // Clear links
    Entry->Flink = Entry->Blink = NULL;
    return Entry;
}

static
inline
void
RemoveEntryList(
    PDOUBLY_LINKED_LIST Entry
)
{
    PDOUBLY_LINKED_LIST Flink;
    PDOUBLY_LINKED_LIST Blink;

    Flink = Entry->Flink;
    Blink = Entry->Blink;

    /* Normal (minimal) unlink - identical to Windows' RemoveEntryList */
    Blink->Flink = Flink;
    Flink->Blink = Blink;

    // Sanitize the removed entry so it doesn't look valid
    Entry->Flink = Entry;
    Entry->Blink = Entry;
}


FORCEINLINE
PTEB
MtCurrentTeb(
    void
)

{
    void* teb;
    __asm__ volatile (
        "rdgsbase %0"
        : "=r"(teb)
        );
    return (PTEB)teb;
}

#define MtCurrentPeb() (MtCurrentTeb()->ProcessEnvironmentBlock)

PLDR_DATA_TABLE_ENTRY
LdrFindEntryForModule(
    IN const char* ModuleName,
    IN PPEB Peb,
    IN bool UseFullPath
);

MTSTATUS
LdrLoadDll(
    IN const char* DllPath,
    OUT PLDR_DATA_TABLE_ENTRY* DllEntry
);

MTSTATUS
LdrpProcessImports(
    IN PLDR_DATA_TABLE_ENTRY ExecutableEntry,
    IN PPEB PebPointer
);

MTSTATUS
LdrpGetProcedureAddress(
    IN PLDR_DATA_TABLE_ENTRY DllEntry,
    IN const char* FunctionName,
    OUT void** ProcdureAddress
);

