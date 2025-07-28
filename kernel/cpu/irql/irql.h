/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      IRQL Header for MatanelOS.
 */
#ifndef X86_IRQL_H
#define X86_IRQL_H

 // Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../trace.h"
#include "../cpu_types.h"

extern CPU cpu;

// Functions
void update_pic_mask_for_current_irql(void);

void GetCurrentIRQL(IRQL* current_irql);

void RaiseIRQL(IRQL new_irql, IRQL* old_irql);

void LowerIRQL(IRQL new_irql);

// Use carefully, non careful use could halt machine.
void _SetIRQL(IRQL new_irql);

void enforce_max_irql(IRQL max_allowed);

#endif
