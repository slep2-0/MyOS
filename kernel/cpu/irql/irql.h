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

#ifdef __GNUC__
#define _IRQL_requires_max_(level)
#else
#define _IRQL_requires_max_(level) __attribute__((annotate("IRQL_max_" #level)))
#endif

extern CPU cpu0;

// Functions
void update_pic_mask_for_current_irql(void);

void MtGetCurrentIRQL(IRQL* current_irql);

void MtRaiseIRQL(IRQL new_irql, IRQL* old_irql);

void MtLowerIRQL(IRQL new_irql);

// Use carefully, non careful use could halt machine.
void _MtSetIRQL(IRQL new_irql);

void enforce_max_irql(IRQL max_allowed, void* RIP);

#endif
