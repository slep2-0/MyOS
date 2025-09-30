#include "pit.h"
#include <stdint.h>
#include "../../intrinsics/intrin.h"

/* cli/sti helpers */
static inline void disable_interrupts(void) { __asm__ volatile ("cli" ::: "memory"); }
static inline void enable_interrupts(void) { __asm__ volatile ("sti"); }

/* PIT constants */
#define PIT_FREQ_HZ 1193182U
#define PIT_CMD_PORT 0x43
#define PIT_CH0_PORT 0x40

/* Command bytes:
   - 0x34 = channel 0, lobyte/hibyte, mode 2 (rate generator), binary
   - 0x00 = latch command for channel 0 (bits 7..6 = 00, rest 0 = latch)
*/
#define PIT_CMD_MODE2_LBHB 0x34
#define PIT_CMD_LATCH_CH0  0x00

/* Sleep ms implementation (blocking). Uses chunks <= 0xFFFF PIT ticks. */
void pit_sleep_ms(uint32_t ms) {
    if (ms == 0) return;

    uint64_t total_ticks = ((uint64_t)PIT_FREQ_HZ * ms + 999) / 1000;

    while (total_ticks > 0) {
        uint32_t chunk = (total_ticks > 0xFFFF) ? 0xFFFF : (uint32_t)total_ticks;
        if (chunk == 0) chunk = 0xFFFF;

        __outbyte(PIT_CMD_PORT, PIT_CMD_MODE2_LBHB);
        __outbyte(PIT_CH0_PORT, (uint8_t)(chunk & 0xFF));
        __outbyte(PIT_CH0_PORT, (uint8_t)((chunk >> 8) & 0xFF));

        disable_interrupts();

        __outbyte(PIT_CMD_PORT, PIT_CMD_LATCH_CH0);
        // Ensure proper sequencing of port reads
        uint8_t start_lo = __inbyte(PIT_CH0_PORT);
        asm volatile("" ::: "memory");  // compiler barrier
        uint8_t start_hi = __inbyte(PIT_CH0_PORT);
        uint16_t start = start_lo | ((uint16_t)start_hi << 8);

        while (1) {
            __outbyte(PIT_CMD_PORT, PIT_CMD_LATCH_CH0);
            uint8_t curr_lo = __inbyte(PIT_CH0_PORT);
            asm volatile("" ::: "memory");  // compiler barrier
            uint8_t curr_hi = __inbyte(PIT_CH0_PORT);
            uint16_t curr = curr_lo | ((uint16_t)curr_hi << 8);

            uint16_t elapsed = (uint16_t)(start - curr);
            if ((uint32_t)elapsed >= chunk) break;

            asm volatile("pause");
        }

        enable_interrupts();
        total_ticks -= chunk;
    }
}
