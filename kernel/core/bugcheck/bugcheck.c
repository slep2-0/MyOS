/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     GPLv3
 * PURPOSE:		Bugcheck functions implementation.
 */

#include "bugcheck.h"
#include "../../trace.h"
#include "../../intrinsics/intrin.h"
#include "../../intrinsics/atomic.h"
#include "../../cpu/smp/smp.h"

#ifndef NOTHING
#define NOTHING
#endif

#ifndef DEBUG
#define DEBUG
#endif

// We require GOP, so we extern it.
extern GOP_PARAMS gop_local;
extern bool isBugChecking;
extern bool smpInitialized;
extern CPU cpu0;
extern GUARD_PAGE_DB* guard_db_head;

extern uint32_t cursor_x;
extern uint32_t cursor_y;

static inline bool is_canonical_ptr(uint64_t x) {
    // x86_64 canonical: bits 63..48 must be sign-extension of bit 47
    uint64_t hi = x >> 47;
    return (hi == 0 || hi == 0x1FFFF);
}

static bool isInTextSegment(uint64_t* addr) {
    return addr < (uint64_t*) & kernel_end && addr > (uint64_t*) & kernel_start;
}

void MtPrintStackTrace(int depth) {
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    uint64_t* rbp = (uint64_t*)__read_rbp();
    for (int i = 0; rbp != NULL && i < depth; ++i) {
        if (!MtIsAddressValid((void*)rbp)) break;
        if (!MtIsAddressValid((void*)(rbp + 1))) break;
        //if (!isInTextSegment(rbp)) break;
        //if (!isInTextSegment((rbp + 1))) break;

        uint64_t saved_ret = *(rbp + 1);

        /* Validate saved_ret before any symbolization/dereference */
        if (!is_canonical_ptr(saved_ret)) break;
        /* if (!address_in_kernel_text(saved_ret)) break; */

        /* Print raw hex — avoid any logger symbolization that may dereference */
        gop_printf(COLOR_ORANGE, "%p\n",
            (unsigned long long)saved_ret);

        uint64_t next_rbp_val = *rbp;
        if (next_rbp_val == 0) break;
        if (!is_canonical_ptr(next_rbp_val)) break;

        /* SANITY: next RBP should be at a *higher* address (older frame)
           and not unreasonably far away. Enforce both direction and delta. */
        uintptr_t cur = (uintptr_t)rbp;
        uintptr_t next = (uintptr_t)next_rbp_val;
        if (next <= cur) break;
        if (next - cur > (16 * 1024 * 1024)) break; /* 16MB cap */

        /* optional: require alignment */
        if (next & 0xF) break;

        rbp = (uint64_t*)(uintptr_t)next_rbp_val;
    }
}

static void print_stack_trace(int depth) {
    uint64_t* rbp = (uint64_t*)__read_rbp();
    for (int i = 0; rbp != NULL && i < depth; ++i) {
        if (!isInTextSegment(rbp)) break;
        if (!isInTextSegment((rbp + 1))) break;

        uint64_t saved_ret = *(rbp + 1);

        /* Validate saved_ret before any symbolization/dereference */
        if (!is_canonical_ptr(saved_ret)) break;
        /* if (!address_in_kernel_text(saved_ret)) break; */

        /* Print raw hex — avoid any logger symbolization that may dereference */
        gop_printf(COLOR_ORANGE, "%p\n",
            (unsigned long long)saved_ret);

        uint64_t next_rbp_val = *rbp;
        if (next_rbp_val == 0) break;
        if (!is_canonical_ptr(next_rbp_val)) break;

        /* SANITY: next RBP should be at a *higher* address (older frame)
           and not unreasonably far away. Enforce both direction and delta. */
        uintptr_t cur = (uintptr_t)rbp;
        uintptr_t next = (uintptr_t)next_rbp_val;
        if (next <= cur) break;
        if (next - cur > (16 * 1024 * 1024)) break; /* 16MB cap */

        /* optional: require alignment */
        if (next & 0xF) break;

        rbp = (uint64_t*)(uintptr_t)next_rbp_val;
    }
}

static inline void print_lastfunc_chain(uint32_t color) {
    LASTFUNC_HISTORY* lfh = thisCPU()->lastfuncBuffer;
    if (!lfh) return;

    int idx = lfh->current_index;
    int start = (idx + 1) % LASTFUNC_HISTORY_SIZE;
    bool first = true;

    for (int i = 0; i < LASTFUNC_HISTORY_SIZE; i++) {
        idx = (start + i) % LASTFUNC_HISTORY_SIZE;
        char* name = (char*)lfh->names[idx];
        if (!*name) break;

        if (!first) gop_printf(color, " -> ");
        gop_printf(color, "%s", name);
        first = false;
    }
}

// switched to uint64_t and not BUGCHECK_CODES since the custom ones arent in that enum, and compiler throws an error.
static void resolveStopCode(char** s, uint64_t stopcode) {
    switch (stopcode) {
    case DIVIDE_BY_ZERO:
        *s = "DIVIDE_BY_ZERO";
        break;
    case SINGLE_STEP:
        *s = "SINGLE_STEP";
        break;
    case NON_MASKABLE_INTERRUPT:
        *s = "NON_MASKABLE_INTERRUPT";
        break;
    case BREAKPOINT:
        *s = "BREAKPOINT";
        break;
    case OVERFLOW:
        *s = "OVERFLOW";
        break;
    case BOUNDS_CHECK:
        *s = "BOUNDS_CHECK";
        break;
    case INVALID_OPCODE:
        *s = "INVALID_OPCODE";
        break;
    case NO_COPROCESSOR:
        *s = "NO_COPROCESSOR";
        break;
    case DOUBLE_FAULT:
        *s = "DOUBLE_FAULT";
        break;
    case COPROCESSOR_SEGMENT_OVERRUN:
        *s = "COPROCESSOR_SEGMENT_OVERRUN";
        break;
    case INVALID_TSS:
        *s = "INVALID_TSS";
        break;
    case SEGMENT_SELECTOR_NOTPRESENT:
        *s = "SEGMENT_SELECTOR_NOTPRESENT";
        break;
    case STACK_SEGMENT_OVERRUN:
        *s = "STACK_SEGMENT_OVERRUN";
        break;
    case GENERAL_PROTECTION_FAULT:
        *s = "GENERAL_PROTECTION_FAULT";
        break;
    case PAGE_FAULT:
        *s = "PAGE_FAULT";
        break;
    case RESERVED:
        *s = "RESERVED";
        break;
    case FLOATING_POINT_ERROR:
        *s = "FLOATING_POINT_ERROR";
        break;
    case ALIGNMENT_CHECK:
        *s = "ALIGNMENT_CHECK";
        break;
    case SEVERE_MACHINE_CHECK:
        *s = "SEVERE_MACHINE_CHECK";
        break;
    case MEMORY_MAP_SIZE_OVERRUN:
        *s = "MEMORY_MAP_SIZE_OVERRUN";
        break;
    case MANUALLY_INITIATED_CRASH:
        *s = "MANUALLY_INITIATED_CRASH";
        break;
    case BAD_PAGING:
        *s = "BAD_PAGING";
        break;
    case BLOCK_DEVICE_LIMIT_REACHED:
        *s = "BLOCK_DEVICE_LIMIT_REACHED";
        break;
    case NULL_POINTER_DEREFERENCE:
        *s = "NULL_POINTER_DEREFERENCE";
        break;
    case FILESYSTEM_PANIC:
        *s = "FILESYSTEM_PANIC";
        break;
    case UNABLE_TO_INIT_TRACELASTFUNC:
        *s = "UNABLE_TO_INIT_TRACELASTFUNC";
        break;
    case FRAME_LIMIT_REACHED:
        *s = "FRAME_LIMIT_REACHED";
        break;
    case IRQL_NOT_LESS_OR_EQUAL:
        *s = "IRQL_NOT_LESS_OR_EQUAL";
        break;
    case INVALID_IRQL_SUPPLIED:
        *s = "INVALID_IRQL_SUPPLIED";
        break;
    case NULL_CTX_RECEIVED:
        *s = "NULL_CTX_RECEIVED";
        break;
    case THREAD_EXIT_FAILURE:
        *s = "THREAD_EXIT_FAILURE";
        break;
    case BAD_AHCI_COUNT:
        *s = "BAD_AHCI_COUNT";
        break;
    case AHCI_INIT_FAILED:
        *s = "AHCI_INIT_FAILED";
        break;
    case MEMORY_LIMIT_REACHED:
        *s = "MEMORY_LIMIT_REACHED";
        break;
    case HEAP_ALLOCATION_FAILED:
        *s = "HEAP_ALLOCATION_FAILED";
        break;
    case NULL_THREAD:
        *s = "NULL_THREAD";
        break;
    case FATAL_IRQL_CORRUPTION:
        *s = "FATAL_IRQL_CORRUPTION";
        break;
    case THREAD_ID_CREATION_FAILURE:
        *s = "THREAD_ID_CREATION_FAILURE";
        break;
    case ASSERTION_FAILURE:
        *s = "ASSERTION_FAILURE";
        break;
    case FRAME_ALLOCATION_FAILED:
        *s = "FRAME_ALLOCATION_FAILED";
        break;
    case FRAME_BITMAP_CREATION_FAILURE:
        *s = "FRAME_BITMAP_CREATION_FAILURE";
        break;
    case MEMORY_INVALID_FREE:
        *s = "MEMORY_INVALID_FREE";
        break;
    case MEMORY_CORRUPT_HEADER:
        *s = "MEMORY_CORRUPT_HEADER";
        break;
    case MEMORY_DOUBLE_FREE:
        *s = "MEMORY_DOUBLE_FREE";
        break;
    case MEMORY_CORRUPT_FOOTER:
        *s = "MEMORY_CORRUPT_FOOTER";
        break;
    case GUARD_PAGE_DEREFERENCE:
        *s = "GUARD_PAGE_DEREFERENCE";
        break;
    case IRQL_NOT_GREATER_OR_EQUAL:
        *s = "IRQL_NOT_GREATER_OR_EQUAL";
        break;
    case KERNEL_STACK_OVERFLOWN:
        *s = "KERNEL_STACK_OVERFLOWN";
        break;
    default:
        *s = "UNKNOWN_BUGCHECK_CODE";
        break;
    }
}

/// <summary>
/// Checks if a given virtual address falls within any registered guard page.
/// </summary>
/// <param name="address">The virtual address to check.</param>
/// <returns>True if the address is within a guard page, otherwise false.</returns>
static bool isInGuardDB(void* address) {
    // A NULL address can't be in a guard page.
    if (!address) {
        return false;
    }
    uintptr_t check_addr = (uintptr_t)address;

    // Traverse the linked list from the head.
    for (GUARD_PAGE_DB* current = guard_db_head; current != NULL; current = current->next) {
        uintptr_t guard_start = (uintptr_t)current->address;
        uintptr_t guard_end = guard_start + current->pageSize;
        // Check if the address is within the half-open interval [start, end).
        if (check_addr >= guard_start && check_addr < guard_end) {
            return true;
        }
    }

    // If we finish the loop, no match was found.
    return false;
}
__attribute__((noreturn))
void MtBugcheck(CTX_FRAME* context, INT_FRAME* int_frame, BUGCHECK_CODES err_code, uint64_t additional, bool isAdditionals) {
    // Critical system error, instead of triple faulting, we hang the system with specified error codes.
    // Disable interrupts if they werent disabled before.
    __cli();
    if (smpInitialized) {
        // If all other cores are online, we obviously want to stop them.
        MtSendActionToCpusAndWait(CPU_ACTION_STOP, 0);
    }
    // atomically set isBugChecking
    InterlockedExchangeBool(&isBugChecking, 1);
#ifdef DEBUG
    IRQL recordedIrql = thisCPU()->currentIrql;
#endif
    // Force to be redrawn from the top, instead of last place.
    cursor_x = 0;
    cursor_y = 0;
    _MtSetIRQL(HIGH_LEVEL); // SET the irql to high level (not raise) (we could raise, but this takes less cycles and so is faster) (previous comment was setting an IRQL to each CPU Core, instead, we send an IPI up top)

	// Clear the screen to blue (bsod windows style)
	gop_clear_screen(&gop_local, 0xFF0035b8);
    if (err_code == PAGE_FAULT && isAdditionals) {
        // check if nullptr deref.
        if (additional == 0) {
            err_code = NULL_POINTER_DEREFERENCE;
        }
        // check if guard page
        if (isInGuardDB((void*)additional)) {
            // The PAGE FAULT is because a guard page was touched.
            err_code = GUARD_PAGE_DEREFERENCE;
        }
    }
	// Write some debugging and an error message
	gop_printf(0xFFFFFFFF, "FATAL ERROR: Your system has encountered a fatal error.\n\n");
	gop_printf(0xFFFFFFFF, "Your system has been stopped for safety.\n\n");
    char* stopCodeToStr = ""; // empty at first.
    resolveStopCode(&stopCodeToStr, err_code);
    uint64_t rspIfExist = (uint64_t)-1;
    if (context) {
        if (int_frame) {
            rspIfExist = int_frame->rsp;
        }
        else {
            if (context->rsp) rspIfExist = context->rsp;
        }
    }
	gop_printf(0xFFFFFFFF, "**STOP CODE: ");
	gop_printf(0xFF8B0000, "%s", stopCodeToStr);
    gop_printf(0xFF00FF00, " (numerical: %d)**", err_code);
    if (context) {
        gop_printf(0xFFFFFFFF,
            "\n\nRegisters:\n\n"
            "RAX: %p RBX: %p RCX: %p RDX: %p\n\n"
            "RSI: %p RDI: %p RBP: %p RSP: %p\n\n"
            "R8 : %p R9 : %p R10: %p R11: %p \n\n"
            "R12: %p R13: %p R14: %p R15: %p ISR RSP (current top): %p\n\n\n",
            context->rax,
            context->rbx,
            context->rcx,
            context->rdx,
            context->rsi,
            context->rdi,
            context->rbp,
            rspIfExist,
            context->r8,
            context->r9,
            context->r10,
            context->r11,
            context->r12,
            context->r13,
            context->r14,
            context->r15,
            __read_rsp()
        );
    }
	else {
        gop_printf(0xFFFF0000, "\n\n\n**ERROR: NO REGISTERS.**\n");
	}
    // don't alert if there is no interrupt frame, the user shouldn't care and know. - i should do an IFDEF here for debug, but I could not remember that I didn't define, i'd rather keep it like this for now.
    if (int_frame) {
        gop_printf((uint32_t)-1,
            "Exceptions:\n\n"
            "Vector Number: %d Error Code: %d\n\n"
            "RIP: %p CS: %p RFLAGS: %b\n",
            int_frame->vector,
            int_frame->error_code,
            int_frame->rip,
            int_frame->cs,
            int_frame->rflags
        );
    }
    gop_printf(0xFFFFA500, "**Last IRQL: %d**\n", recordedIrql);
	if (isAdditionals) {
		if (err_code == PAGE_FAULT) {
            gop_printf(0xFFFFA500, "\n\n**FAULTY ADDRESS: %p (tip, place a breakpoint on it)**\n", additional);
		}
		else {
            gop_printf(0xFFBF40BF, "\n\n**ADDITIONALS: %p**\n", additional);
		}
	}
    if (smpInitialized) {
        gop_printf(COLOR_LIME, "Sent IPI To all CPUs to HALT.\n");
        gop_printf(COLOR_LIME, "Current Executing CPU: %d\n", thisCPU()->lapic_ID);
    }
    int32_t currTid = (thisCPU()->currentThread) ? thisCPU()->currentThread->TID : (uint32_t)-1;
    gop_printf(0xFFFFFF00, "Current Thread ID: %d\n", currTid);
#ifdef DEBUG
    if (thisCPU()->lastfuncBuffer) {
        if (thisCPU()->lastfuncBuffer->names[thisCPU()->lastfuncBuffer->current_index][0] != '\0') {
            gop_printf(0xFFBF40BF, "\n**FUNCTION TRACE (oldest to newest, on this CPU): ");
            print_lastfunc_chain(0xFFBF40BF);
            gop_printf(0xFFBF40BF, "**");
        }
    }
    // call stack trace
    gop_printf(COLOR_GREEN, "\n\nCall Stack Trace:\n");
    print_stack_trace(10); // 10 function calls;
#endif
	//test
    __cli();
    // spin the thisCPU()->
    while (1) {
        NOTHING;
        __pause();
    }
}


void MtBugcheckEx(CTX_FRAME* context, INT_FRAME* int_frame, BUGCHECK_CODES err_code, BUGCHECK_ADDITIONALS* additional, bool isAdditionals) {
    // Critical system error, instead of triple faulting, we hang the system with specified error codes.
    // Disable interrupts if they werent disabled before.
    __cli();
    if (smpInitialized) {
        // If all other cores are online, we obviously want to stop them.
        MtSendActionToCpusAndWait(CPU_ACTION_STOP, 0);
    }
    // atomically set isBugChecking
    InterlockedExchangeBool(&isBugChecking, 1);
#ifdef DEBUG
    IRQL recordedIrql = thisCPU()->currentIrql;
#endif
    // Force to be redrawn from the top, instead of last place.
    cursor_x = 0;
    cursor_y = 0;
    _MtSetIRQL(HIGH_LEVEL); // SET the irql to high level (not raise) (we could raise, but this takes less cycles and so is faster)

    // Clear the screen to blue (bsod windows style)
    gop_clear_screen(&gop_local, 0xFF0035b8);
    // Write some debugging and an error message
    gop_printf(0xFFFFFFFF, "FATAL ERROR: Your system has encountered a fatal error.\n\n");
    gop_printf(0xFFFFFFFF, "Your system has been stopped for safety.\n\n");
    char* stopCodeToStr = ""; // empty at first.
    resolveStopCode(&stopCodeToStr, err_code);
    uint64_t rspIfExist;
    if (context) {
        rspIfExist = (context->rsp) ? context->rsp : (uint64_t)(-1);
    }
    gop_printf(0xFFFFFFFF, "**STOP CODE: ");
    gop_printf(0xFF8B0000, "%s", stopCodeToStr);
    gop_printf(0xFF00FF00, " (numerical: %d)**", err_code);
    if (context) {
        gop_printf(0xFFFFFFFF,
            "\n\nRegisters:\n\n"
            "RAX: %p RBX: %p RCX: %p RDX: %p\n\n"
            "RSI: %p RDI: %p RBP: %p RSP: %p\n\n"
            "R8 : %p R9 : %p R10: %p R11: %p \n\n"
            "R12: %p R13: %p R14: %p R15: %p\n\n\n",
            context->rax,
            context->rbx,
            context->rcx,
            context->rdx,
            context->rsi,
            context->rdi,
            context->rbp,
            rspIfExist,
            context->r8,
            context->r9,
            context->r10,
            context->r11,
            context->r12,
            context->r13,
            context->r14,
            context->r15
        );
    }
    else {
        gop_printf(0xFFFF0000, "\n\n\n**ERROR: NO REGISTERS.**\n");
    }
    // don't alert if there is no interrupt frame, the user shouldn't care and know. - i should do an IFDEF here for debug, but I could not remember that I didn't define, i'd rather keep it like this for now.
    if (int_frame) {
        gop_printf((uint32_t)-1,
            "Exceptions:\n\n"
            "Vector Number: %d Error Code: %p\n\n"
            "RIP: %p CS: %p RFLAGS: %b\n",
            int_frame->vector,
            int_frame->error_code,
            int_frame->rip,
            int_frame->cs,
            int_frame->rflags
        );
    }
    gop_printf(0xFFFFA500, "**Last IRQL: %d**\n", recordedIrql);
    int32_t currTid = (thisCPU()->currentThread) ? thisCPU()->currentThread->TID : (uint32_t)-1;
    gop_printf(0xFFFFFF00, "Current Thread ID: %d\n", currTid);
    if (isAdditionals) {
        if (additional->boolean) {
            gop_printf(COLOR_RED, "**BOOLEAN ADDITIONAL: %d**\n", additional->boolean);
        }
        if (additional->num) {
            gop_printf(COLOR_RED, "**UNSIGNED NUMBER ADDITIONAL: %d**\n", additional->num);
        }
        if (additional->ptr) {
            gop_printf(COLOR_RED, "**POINTER ADDITIONAL: %p**\n", additional->ptr);
        }
        if (additional->signednum) {
            gop_printf(COLOR_RED, "**SIGNED NUMBER ADDITIONAL: %d**\n", additional->signednum);
        }
        if (additional->str) {
            gop_printf(COLOR_RED, "**STRING ADDITIONAL: %s**\n", additional->str);
        }
    }
    if (smpInitialized) {
        gop_printf(COLOR_LIME, "Sent IPI To all CPUs to HALT.\n");
        gop_printf(COLOR_LIME, "Current Executing CPU: %d\n", thisCPU()->lapic_ID);
    }
#ifdef DEBUG
    if (thisCPU()->lastfuncBuffer) {
        if (thisCPU()->lastfuncBuffer->names[thisCPU()->lastfuncBuffer->current_index][0] != '\0') {
            gop_printf(0xFFBF40BF, "\n**FUNCTION TRACE (oldest to newest, on this CPU): ");
            print_lastfunc_chain(0xFFBF40BF);
            gop_printf(0xFFBF40BF, "**");
        }
    }


    // call stack trace
    gop_printf(COLOR_GREEN, "\n\nCall Stack Trace:\n");
    print_stack_trace(10); // 10 function calls;
#endif
    //test
    __cli();
    // spin the thisCPU()->
    while (1) {
        NOTHING;
        __pause();
    }
}
