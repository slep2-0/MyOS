/*++

Module Name:

    ht.h

Purpose:

    This module contains the header files & prototypes required for the Handle Table implementation of MatanelOS.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#ifndef X86_MATANEL_HT_H
#define X86_MATANEL_HT_H

#include "core.h"
#include "ms.h"



typedef struct _HANDLE_TABLE {
    DOUBLY_LINKED_LIST TableList;
    SPINLOCK TableLock;

} HANDLE_TABLE, *PHANDLE_TABLE;

#endif