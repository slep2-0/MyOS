/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     GPLv3
 * PURPOSE:		Debugging Functions Implementation.
 */

#include "../../includes/md.h"
#include "../../includes/mh.h"

 /* Find a free debug slot (0..3) or -1 if none */
int find_available_debug_reg(void) {
    for (int i = 0; i < 4; ++i) {
        if (MeGetCurrentProcessor()->DebugEntry[i].Callback == NULL) return i;
    }
    return -1;
}

static inline void write_dr_idx(int idx, uint64_t value) {
    __write_dr(idx, value);
}
static inline uint64_t read_dr7(void) { return __read_dr(7); }
static inline void write_dr7(uint64_t v) { __write_dr(7, v); }
static inline void write_dr6(uint64_t v) { __write_dr(6, v); }

// PUBLIC API
MTSTATUS MdSetHardwareBreakpoint(DebugCallback CallbackFunction, void* BreakpointAddress, DEBUG_ACCESS_MODE AccessMode, DEBUG_LENGTH Length) {
    if (!CallbackFunction || !BreakpointAddress) return MT_INVALID_PARAM;
    if (AccessMode == DEBUG_ACCESS_IO) return MT_NOT_IMPLEMENTED; /* legacy / not handled */

    /* Validate length */
    if (Length != DEBUG_LEN_1 && Length != DEBUG_LEN_2 && Length != DEBUG_LEN_4 && Length != DEBUG_LEN_8)
        return MT_INVALID_PARAM;

    int idx = find_available_debug_reg();
    if (idx == -1) return MT_NO_RESOURCES;

    uint64_t addr = (uint64_t)BreakpointAddress;

    /* Write address into DRx */
    write_dr_idx(idx, addr);

    /* Clear DR6 status before enabling */
    write_dr6(0);

    /* Modify DR7 safely: only change bits for our breakpoint index and the local-enable bit */
    uint64_t dr7 = read_dr7();

    /* set local enable bit Lx (bit 0,2,4,6 for idx 0..3) */
    dr7 |= (1ULL << (idx * 2));

    /* Build the 4-bit RW/LEN field value: low 2 bits = RW, high 2 bits = LEN */
    uint64_t group_val = ((((uint64_t)Length) & 0x3ULL) << 2) | (((uint64_t)AccessMode) & 0x3ULL);

    /* Clear existing 4-bit group and set new one at bits (16 + 4*idx .. 19 + 4*idx) */
    uint64_t mask = 0xFULL << (16 + 4 * idx);
    dr7 &= ~mask;
    dr7 |= (group_val << (16 + 4 * idx));

    write_dr7(dr7);

    // Save in the DEBUG db so the INT1 will handle it.
    MeGetCurrentProcessor()->DebugEntry[idx].Address = BreakpointAddress;
    MeGetCurrentProcessor()->DebugEntry[idx].Callback = CallbackFunction;

    IPI_PARAMS params;
    params.debugRegs.address = addr;
    params.debugRegs.dr7 = dr7;
    params.debugRegs.callback = CallbackFunction;

    MhSendActionToCpusAndWait(CPU_ACTION_WRITE_DEBUG_REGS, params);

    return MT_SUCCESS;
}

MTSTATUS MdClearHardwareBreakpointByIndex(int index) {
    if (index < 0 || index > 3) return MT_INVALID_PARAM;
    if (MeGetCurrentProcessor()->DebugEntry[index].Callback == NULL && MeGetCurrentProcessor()->DebugEntry[index].Address == NULL) return MT_NOT_FOUND;

    /* Clear DRx address */
    write_dr_idx(index, 0);

    /* Clear DR7 bits for this index (local enable and RW/LEN group) */
    uint64_t dr7 = read_dr7();
    /* clear local enable bit */
    dr7 &= ~(1ULL << (index * 2));
    /* clear RW/LEN 4-bit group */
    uint64_t mask = 0xFULL << (16 + 4 * index);
    dr7 &= ~mask;
    write_dr7(dr7);

    /* Clear status DR6 too */
    write_dr6(0);

    IPI_PARAMS params;
    params.debugRegs.address = (uint64_t)MeGetCurrentProcessor()->DebugEntry[index].Address;

    /* Clear table entry */
    MeGetCurrentProcessor()->DebugEntry[index].Callback = NULL;
    MeGetCurrentProcessor()->DebugEntry[index].Address = NULL;

    MhSendActionToCpusAndWait(CPU_ACTION_CLEAR_DEBUG_REGS, params);

    return MT_SUCCESS;
}

MTSTATUS MdClearHardwareBreakpointByAddress(void* BreakpointAddress) {
    if (!BreakpointAddress) return MT_INVALID_PARAM;
    for (int i = 0; i < 4; ++i) {
        if (MeGetCurrentProcessor()->DebugEntry[i].Address == BreakpointAddress) {
            return MdClearHardwareBreakpointByIndex(i);
        }
    }
    return MT_NOT_FOUND;
}