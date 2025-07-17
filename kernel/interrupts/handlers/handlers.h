/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		ISR Handlers - Handle Interrupts based on number and do something.
 */
#ifndef X86_HANDLER_FUNCTIONS_H
#define X86_HANDLER_FUNCTIONS_H
#include "../../kernel.h"

// Obtained from https://wiki.osdev.org/Interrupts
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// Initialize keyboard left shift and left control to 0.
void init_keyboard();
// Handle keyboard interruptions.
void keyboard_handler();
// Initiate timer with a specified frequency.
void init_timer(unsigned long int frequency);
// Handle Timer Interruptions.
void timer_handle();
#endif

