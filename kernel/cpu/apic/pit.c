#include "pit.h"
#include <stdint.h>
#include "../../intrin/intrin.h"

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

    /* total ticks needed (rounded up) */
    uint64_t total_ticks = ((uint64_t)PIT_FREQ_HZ * ms + 999) / 1000;

    while (total_ticks > 0) {
        /* chunk <= 0xFFFF for PIT 16-bit counter (0 means 65536) */
        uint32_t chunk = (total_ticks > 0xFFFF) ? 0xFFFF : (uint32_t)total_ticks;
        if (chunk == 0) chunk = 0xFFFF;

        /* Program PIT channel 0 (mode 2, lobyte/hibyte) with 'chunk' reload */
        __outbyte(PIT_CMD_PORT, PIT_CMD_MODE2_LBHB);
        __outbyte(PIT_CH0_PORT, (uint8_t)(chunk & 0xFF));         // low byte
        __outbyte(PIT_CH0_PORT, (uint8_t)((chunk >> 8) & 0xFF));  // high byte

        /* Read a latched current count and then poll until elapsed >= chunk */
        disable_interrupts();

        /* latch current count into internal latch register */
        __outbyte(PIT_CMD_PORT, PIT_CMD_LATCH_CH0);

        /* read latched low/high bytes */
        uint16_t start = (uint16_t)__inbyte(PIT_CH0_PORT) | ((uint16_t)__inbyte(PIT_CH0_PORT) << 8);

        while (1) {
            /* latch and read current count again */
            __outbyte(PIT_CMD_PORT, PIT_CMD_LATCH_CH0);
            uint16_t curr = (uint16_t)__inbyte(PIT_CH0_PORT) | ((uint16_t)__inbyte(PIT_CH0_PORT) << 8);

            /* elapsed = (start - curr) modulo 65536 */
            uint16_t elapsed = (uint16_t)(start - curr);

            if ((uint32_t)elapsed >= chunk) break;
            /* small CPU-friendly pause */
            __asm__ volatile("pause");
        }

        enable_interrupts();

        total_ticks -= chunk;
    }
}
