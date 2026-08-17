#include "../../includes/exception.h"

bool
ExpIsPrivilegedInstruction(uint8_t* Ip)

/*++

    Routine description:

        Decodes the instruction at an address and determines whether it is a
        privileged x86 instruction.

    Arguments:

        [IN] Ip - The instruction address to inspect.

    Return Values:

        true when the instruction requires kernel privilege, or false when it
        is not recognized as privileged or the instruction probe faults.

--*/

{
    uint32_t i;
    bool IsPrivileged = false;

    try {
        /* Handle prefixes */
        for (i = 0; i < 15; i++)
        {
            /* Check for REX prefix */
            if ((Ip[0] >= 0x40) && (Ip[0] <= 0x4F))
            {
                Ip++;
                continue;
            }

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
            leave;
        }

        switch (Ip[0])
        {
        case 0xF4: // HLT
        case 0xFA: // CLI
        case 0xFB: // STI
            IsPrivileged = true;
            leave;

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
                IsPrivileged = true;
                leave;

            case 0x00:
            {
                /* Check MODRM Reg field */
                switch ((Ip[2] >> 3) & 0x7)
                {
                case 2: // LLDT
                case 3: // LTR
                    IsPrivileged = true;
                    leave;
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
                    IsPrivileged = true;
                    leave;
                }

                /* Check MODRM Reg field */
                switch ((Ip[2] >> 3) & 0x7)
                {
                case 2: // LGDT
                case 3: // LIDT
                case 6: // LMSW
                case 7: // INVLPG / SWAPGS / RDTSCP
                    IsPrivileged = true;
                    leave;
                }
                break;
            }

            case 0x38:
            {
                switch (Ip[2])
                {
                case 0x80: // INVEPT
                case 0x81: // INVVPID
                    IsPrivileged = true;
                    leave;
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
                    IsPrivileged = true;
                    leave;
                }
                break;
            }
            }

            break;
            }
        }
    } except{
            IsPrivileged = false;
    }
    end_try;

    return IsPrivileged;
}
