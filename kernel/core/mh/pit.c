#include <stdint.h>
#include "../../intrinsics/intrin.h"
#include "../../includes/mh.h"
#include "../../includes/me.h"

/* PIT constants */
#define PIT_FREQ_HZ 1193182U
#define PIT_CMD_PORT 0x43
#define PIT_CH2_PORT 0x42
#define PIT_SPEAKER_PORT 0x61

#define PIT_CMD_CH2_MODE0_LBHB 0xB0
#define PIT_CH2_GATE             0x01
#define PIT_SPEAKER_ENABLE       0x02
#define PIT_CH2_OUTPUT           0x20

// This is only a hardware-failure guard. A normal delay completes many orders
// of magnitude before exhausting this budget, even under emulation.
#define PIT_POLL_BASE             1000000ULL
#define PIT_POLLS_PER_TICK        256ULL

/* Early-boot blocking delay. Uses PIT channel 2 so channel 0 is left alone. */
void pit_sleep_ms(uint32_t ms) {
    if (ms == 0) return;

    uint64_t total_ticks = ((uint64_t)PIT_FREQ_HZ * ms + 999) / 1000;
    bool interrupts_enabled = MeDisableInterrupts();
    uint8_t saved_port_b = __inbyte(PIT_SPEAKER_PORT);

    while (total_ticks > 0) {
        uint32_t chunk = (total_ticks > 0xFFFF) ? 0xFFFF : (uint32_t)total_ticks;

        // Keep the speaker disconnected and lower GATE2 before programming so
        // the following rising edge starts this one-shot deterministically.
        __outbyte(
            PIT_SPEAKER_PORT,
            saved_port_b & (uint8_t)~(PIT_CH2_GATE | PIT_SPEAKER_ENABLE)
        );
        __outbyte(PIT_CMD_PORT, PIT_CMD_CH2_MODE0_LBHB);
        __outbyte(PIT_CH2_PORT, (uint8_t)(chunk & 0xFF));
        __outbyte(PIT_CH2_PORT, (uint8_t)((chunk >> 8) & 0xFF));
        __outbyte(
            PIT_SPEAKER_PORT,
            (saved_port_b & (uint8_t)~PIT_SPEAKER_ENABLE) | PIT_CH2_GATE
        );

        uint64_t polls_remaining = PIT_POLL_BASE +
            (uint64_t)chunk * PIT_POLLS_PER_TICK;
        uint8_t port_b;

        do {
            port_b = __inbyte(PIT_SPEAKER_PORT);
            if (port_b & PIT_CH2_OUTPUT) break;

            if (--polls_remaining == 0) {
                MeBugCheckEx(
                    PIT_TIMER_FAILURE,
                    (void*)(uintptr_t)chunk,
                    (void*)(uintptr_t)port_b,
                    (void*)(uintptr_t)saved_port_b,
                    (void*)pit_sleep_ms
                );
            }

            __asm__ volatile("pause");
        } while (true);

        total_ticks -= chunk;
    }

    __outbyte(PIT_SPEAKER_PORT, saved_port_b);
    MeEnableInterrupts(interrupts_enabled);
}
