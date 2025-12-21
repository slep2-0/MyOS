#include "../../includes/exception.h"

bool
ExpIsPrivilegedInstruction(uint8_t* Ip /*, bool Wow64*/)

// Desc: This will check if the instruction ran in the instruction pointer from user mode (or from anywhere really)
// Is a privileged instruction or not (meaning, it could only be executed in KernelMode (CPL == 0)

// This is taken DIRECTLY from ReactOS, why? Because I dont really want to make my own parser right now.
// And they are already brilliant people so I trust them

// Link to code: https://github.com/reactos/reactos/blob/5047e62e3dde76635a46516b289968b951348f74/ntoskrnl/ke/amd64/except.c#L446
// Thanks Timo Kreuzer and Alex Ionescu

{
    uint32_t i;

    /* Handle prefixes */
    for (i = 0; i < 15; i++)
    {
        /*
        if (!Wow64)
        */
        //{
        /* Check for REX prefix */
        if ((Ip[0] >= 0x40) && (Ip[0] <= 0x4F))
        {
            Ip++;
            continue;
        }
        //}

        switch (Ip[0])
        {
            /* Check prefixes */
        case 0x26: // ES
        case 0x2E: // CS / null
        case 0x36: // SS
        case 0x3E: // DS
        case 0x64: // FS
        case 0x65: // GS
        case 0x66: // OP
        case 0x67: // ADDR
        case 0xF0: // LOCK
        case 0xF2: // REP
        case 0xF3: // REP INS/OUTS
            Ip++;
            continue;
        }

        break;
    }

    if (i == 15)
    {
        /* Too many prefixes. Should only happen, when the code was concurrently modified. */
        return false;
    }

    switch (Ip[0])
    {
    case 0xF4: // HLT
    case 0xFA: // CLI
    case 0xFB: // STI
        return true;

    case 0x0F:
    {
        switch (Ip[1])
        {
        case 0x06: // CLTS
        case 0x07: // SYSRET
        case 0x08: // INVD
        case 0x09: // WBINVD
        case 0x20: // MOV CR, XXX
        case 0x21: // MOV DR, XXX
        case 0x22: // MOV XXX, CR
        case 0x23: // MOV YYY, DR
        case 0x30: // WRMSR
        case 0x32: // RDMSR
        case 0x33: // RDPMC
        case 0x35: // SYSEXIT
        case 0x78: // VMREAD
        case 0x79: // VMWRITE
            return true;

        case 0x00:
        {
            /* Check MODRM Reg field */
            switch ((Ip[2] >> 3) & 0x7)
            {
            case 2: // LLDT
            case 3: // LTR
                return true;
            }
            break;
        }

        case 0x01:
        {
            switch (Ip[2])
            {
            case 0xC1: // VMCALL
            case 0xC2: // VMLAUNCH
            case 0xC3: // VMRESUME
            case 0xC4: // VMXOFF
            case 0xC8: // MONITOR
            case 0xC9: // MWAIT
            case 0xD1: // XSETBV
            case 0xF8: // SWAPGS
                return true;
            }

            /* Check MODRM Reg field */
            switch ((Ip[2] >> 3) & 0x7)
            {
            case 2: // LGDT
            case 3: // LIDT
            case 6: // LMSW
            case 7: // INVLPG / SWAPGS / RDTSCP
                return true;
            }
            break;
        }

        case 0x38:
        {
            switch (Ip[2])
            {
            case 0x80: // INVEPT
            case 0x81: // INVVPID
                return true;
            }
            break;
        }

        case 0xC7:
        {
            /* Check MODRM Reg field */
            switch ((Ip[2] >> 3) & 0x7)
            {
            case 0x06: // VMPTRLD, VMCLEAR, VMXON
            case 0x07: // VMPTRST
                return true;
            }
            break;
        }
        }

        break;
    }
    }

    return false;
}