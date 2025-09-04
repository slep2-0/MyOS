/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      AHCI Driver Implementation.
 */

#include "ahci.h"
#include "../../trace.h"
#include "../../assert.h"

#ifdef REMINDER
_Static_assert(false, "Reminder: AHCI, and other DMA stuff DEAL WITH PHYSICAL ADDRESSES ONLY! not virtual, so supply to them the translated addresses.");
#endif
#ifdef AHCI_DEBUG_PRINT
static void decode_serr(uint32_t serr) {
    if (serr == 0) {
        gop_printf_forced(0xFFFFFF00, "SERR: No errors\n");
        return;
    }

    gop_printf_forced(0xFFFF0000, "SERR: 0x%08x - Errors detected:\n", serr);

    // ERR bits (0-15) - Recoverable and non-recoverable errors
    if (serr & (1 << 0))  gop_printf_forced(0xFFFFFF00, "  [0] ERR.I - Recovered Data Integrity Error\n");
    if (serr & (1 << 1))  gop_printf_forced(0xFFFFFF00, "  [1] ERR.M - Recovered Communications Error\n");
    if (serr & (1 << 8))  gop_printf_forced(0xFFFFFF00, "  [8] ERR.T - Transient Data Integrity Error\n");
    if (serr & (1 << 9))  gop_printf_forced(0xFFFFFF00, "  [9] ERR.C - Persistent Communication/Data Integrity Error\n");
    if (serr & (1 << 10)) gop_printf_forced(0xFFFFFF00, "  [10] ERR.P - Protocol Error\n");
    if (serr & (1 << 11)) gop_printf_forced(0xFFFFFF00, "  [11] ERR.E - Internal Error\n");

    // DIAG bits (16-31) - Diagnostic errors
    if (serr & (1 << 16)) gop_printf_forced(0xFFFFFF00, "  [16] DIAG.N - PhyRdy Change\n");
    if (serr & (1 << 17)) gop_printf_forced(0xFFFFFF00, "  [17] DIAG.I - Phy Internal Error\n");
    if (serr & (1 << 18)) gop_printf_forced(0xFFFFFF00, "  [18] DIAG.W - Comm Wake\n");
    if (serr & (1 << 19)) gop_printf_forced(0xFFFFFF00, "  [19] DIAG.B - 10B to 8B Decode Error\n");
    if (serr & (1 << 20)) gop_printf_forced(0xFFFFFF00, "  [20] DIAG.D - Disparity Error\n");
    if (serr & (1 << 21)) gop_printf_forced(0xFFFFFF00, "  [21] DIAG.C - CRC Error\n");
    if (serr & (1 << 22)) gop_printf_forced(0xFFFFFF00, "  [22] DIAG.H - Handshake Error\n");
    if (serr & (1 << 23)) gop_printf_forced(0xFFFFFF00, "  [23] DIAG.S - Link Sequence Error\n");
    if (serr & (1 << 24)) gop_printf_forced(0xFFFFFF00, "  [24] DIAG.T - Transport State Transition Error\n");
    if (serr & (1 << 25)) gop_printf_forced(0xFFFFFF00, "  [25] DIAG.F - Unknown FIS Type\n");
    if (serr & (1 << 26)) gop_printf_forced(0xFFFFFF00, "  [26] DIAG.X - Exchanged\n");
}
#endif
// Context per initialized port
typedef struct _AHCI_PORT_CTX {
    HBA_PORT* port;             // MMIO base for this port
    HBA_CMD_TBL* cmd_tbl;       // Command table memory
    void* clb;                  // Cmd list buffer
    void* fis;                  // FIS receive buffer
    BLOCK_DEVICE bdev;          // Associated BLOCK_DEVICE interface
} AHCI_PORT_CTX;

static HBA_MEM* hba_mem;
static AHCI_PORT_CTX ports[AHCI_MAX_PORTS];
static int port_count;

// Invalidate cache ranges of the CPU to ensure newest data is fetched from RAM.
static inline void cache_flush_invalidate_range(void* addr, size_t len) {
    uintptr_t p = (uintptr_t)addr & ~(uintptr_t)63;
    uintptr_t end = (uintptr_t)addr + len;
    for (; p < end; p += 64) {
        __asm__ volatile("clflush (%0)" :: "r"((void*)p) : "memory");
    }
    __asm__ volatile("mfence" ::: "memory");
}

static inline void outl_port(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" :: "a"(val), "d"(port));
}
static inline uint32_t inl_port(uint16_t port) {
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "d"(port));
    return val;
}
static uint32_t pci_cfg_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) | (offset & 0xFC);
    outl_port(0xCF8, addr);
    return inl_port(0xCFC);
}
static void pci_cfg_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) | (offset & 0xFC);
    outl_port(0xCF8, addr);
    outl_port(0xCFC, val);
}

// Scans PCI buses and enables Bus Master bit for first AHCI class device found.
// Call this at start of ahci_init() before enable_controller().
static void ensure_ahci_busmaster_enabled(void) {
    for (uint8_t bus = 0; bus < 8; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t d0 = pci_cfg_read32(bus, slot, func, 0x00);
                if ((d0 & 0xFFFF) == 0xFFFF) continue; // no device
                uint32_t cl = pci_cfg_read32(bus, slot, func, 0x08);
                uint8_t base_class = (cl >> 24) & 0xFF;
                uint8_t sub_class = (cl >> 16) & 0xFF;
                uint8_t prog_if = (cl >> 8) & 0xFF;
                if (base_class == 0x01 && sub_class == 0x06 && prog_if == 0x01) {
#ifdef AHCI_DEBUG_PRINT
                    uint32_t hdr = pci_cfg_read32(bus, slot, func, 0x00);
                    uint16_t vendor = hdr & 0xFFFF;
                    uint16_t device = (hdr >> 16) & 0xFFFF;
#endif
                    uint32_t cmd32 = pci_cfg_read32(bus, slot, func, 0x04);
                    uint16_t cmd = cmd32 & 0xFFFF;
#ifdef AHCI_DEBUG_PRINT
                    gop_printf_forced(0xFFFFFF00, "AHCI PCI at %p:%p vendor=%p device=%p\n", bus, slot, func, vendor, device);
                    gop_printf_forced(0xFFFFFF00, "PCI CMD before: %p\n", cmd);
#endif
                    if (!(cmd & (1 << 2))) {
                        cmd |= (1 << 2); // set Bus Master
                        pci_cfg_write32(bus, slot, func, 0x04, (cmd32 & 0xFFFF0000) | (uint32_t)cmd);
#ifdef AHCI_DEBUG_PRINT
                        gop_printf_forced(0xFFFFFF00, "Enabled PCI Bus Master bit for AHCI\n");
#endif
                    }
                    else {
#ifdef AHCI_DEBUG_PRINT
                        gop_printf_forced(0xFFFFFF00, "PCI Bus Master already enabled\n");
#endif
                    }
                    return;
                }
            }
        }
    }
#ifdef AHCI_DEBUG_PRINT
    gop_printf_forced(0xFFFF0000, "AHCI PCI device not found while scanning PCI bus\n");
#endif
}

/// <summary>
/// Locate first zero bit in 32-bit mask.
/// </summary>
/// <param name="mask">The 32-bit mask.</param>
/// <returns>First Zero bit from the argument supplied. | -1 if not found.</returns>
static int find_free_slot(uint32_t mask) {
    tracelast_func("find_free_slot");
    for (int i = 0; i < 32; i++) {
        if (!(mask & (1u << i))) {
            return i;
        }
    }
    return -1;
}

/// <summary>
/// Enable controller and reset
/// </summary>
static void enable_controller(void) {
    tracelast_func("enable_controller");
    hba_mem->ghc |= (1u << 31); // AHCI Enable.
    hba_mem->ghc |= (1u << 0); // Global Reset.
    /// Busy wait.
    while (hba_mem->ghc & (1u << 0));
}

/// <summary>
/// Initialize individual port at index.
/// </summary>
/// <param name="idx">Index to initialize ports in</param>
/// <returns>True or False based on succession</returns>
static bool init_one_port(int idx) {
    HBA_PORT* p = (HBA_PORT*)((uint8_t*)hba_mem + 0x100 + idx * 0x80);
    uint32_t status = p->ssts;
    if ((status & 0x0F) != 3) return false; // no device present

    // Stop the port before configuration
    p->cmd &= ~(1u << 0); // Clear ST (START)
    p->cmd &= ~(1u << 4); // Clear PRE (FIS Receive Enable)

    // Wait until port is idle
    while ((p->cmd & (1u << 15)) || (p->cmd & (1u << 14))) {
        __pause();
    }

    // Allocate and zero CLB (1 KiB)
    void* clb = MtAllocateVirtualMemoryEx(1024, 1024, PAGE_PRESENT | PAGE_RW | PAGE_PCD | PAGE_PWT);
    if (!clb) return false;
    kmemset(clb, 0, 1024);
    // pass the PHYSICAL address.
    uintptr_t clb_phys = MtTranslateVirtualToPhysical(clb);
    assert(((uintptr_t)clb_phys & 0x3FF) == 0, "CLB must be 1KiB-aligned (1024 byte multiple)");
#ifdef AHCI_DEBUG_PRINT
    gop_printf(COLOR_BLUE, "In INIT_ONE_PORT, clb_phys: %p | virt: %p\n", clb_phys, clb);
#endif
    p->clb = (uint32_t)(uintptr_t)clb_phys;
    p->clbu = (uint32_t)((uintptr_t)clb_phys >> 32);

    // Allocate and zero FIS receive buffer (256 B)
    void* fis_buf = MtAllocateVirtualMemoryEx(256, 256, PAGE_PRESENT | PAGE_RW | PAGE_PCD | PAGE_PWT);
    if (!fis_buf) return false;
    kmemset(fis_buf, 0, 256);
    // Again, pass the PHYSICAL.
    uintptr_t fis_buf_phys = MtTranslateVirtualToPhysical(fis_buf);
#ifdef AHCI_DEBUG_PRINT
    gop_printf(COLOR_BLUE, "In INIT_ONE_PORT, fis_buf_phys: %p | virt: %p\n", fis_buf_phys, fis_buf);
#endif
    p->fb = (uint32_t)(uintptr_t)fis_buf_phys;
    p->fbu = (uint32_t)((uintptr_t)fis_buf_phys >> 32);

    // Allocate and zero Command Table buffers: 256 B × 32 slots
    size_t tbl_size = 256 * 32;
    void* cmd_tbl = MtAllocateVirtualMemoryEx(tbl_size, 256, PAGE_PRESENT | PAGE_RW | PAGE_PCD | PAGE_PWT);
    if (!cmd_tbl) return false;
    kmemset(cmd_tbl, 0, tbl_size);
    uintptr_t cmd_tbl_phys = MtTranslateVirtualToPhysical(cmd_tbl);
    assert(((uintptr_t)cmd_tbl_phys & 0xFF) == 0, "Command table block must be 256-byte aligned");
#ifdef AHCI_DEBUG_PRINT
    gop_printf(COLOR_BLUE, "In INIT_ONE_PORT, cmd_tbl_phys: %p | virt: %p\n", cmd_tbl_phys, cmd_tbl);
#endif

    // Point each command header to its table
    for (int slot = 0; slot < 32; slot++) {
        // Header at clb + slot*32 bytes
        HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)((uint8_t*)clb + slot * sizeof(HBA_CMD_HEADER));
        uintptr_t tbl_pa_phys = (uintptr_t)cmd_tbl_phys + slot * 256;
        hdr->ctba = (uint32_t)(tbl_pa_phys & 0xFFFFFFFF);
        hdr->ctbau = (uint32_t)(tbl_pa_phys >> 32);
        hba_cmd_hdr_set_prdtl(hdr, 1); // one PRDT entry
    }

    // Clear any old errors and start the port
    p->serr = ~0U; // Clear all SERROR bits by writing 1 to them.
    p->cmd |= (1u << 4); // Set FRE
    p->cmd |= (1u << 0); // Set ST

    // Add this assertion to ensure the port actually starts
    assert((p->cmd & 1) != 0, "Port ST bit failed to set!");

    // Save context
    AHCI_PORT_CTX* ctx = &ports[port_count];
    ctx->port = p;
    ctx->clb = clb;
    ctx->fis = fis_buf;
    ctx->cmd_tbl = cmd_tbl;
    ctx->bdev.read_sector = ahci_read_sector;
    ctx->bdev.write_sector = ahci_write_sector;
    ctx->bdev.dev_data = ctx;

    /* CAP and slot counts */
#ifdef AHCI_DEBUG_PRINT
    uint32_t cap = (uint32_t)hba_mem->cap;
    uint32_t ncs = (cap >> 8) & 0x1Fu;
    assert(((ncs + 1) >= 1) && ((ncs + 1) <= 32), "CAP.NCS invalid (command slots out of range)");

    bool s64a = !!((cap >> 31) & 1u);

    /* Alignment checks */
    assert(((uintptr_t)clb_phys & 0x3FF) == 0, "PxCLB must be 1KiB-aligned (1024 bytes)");
    assert(((uintptr_t)fis_buf_phys & 0xFF) == 0, "PxFB (FIS) must be 256-byte aligned");
    assert(((uintptr_t)cmd_tbl_phys & 0xFF) == 0, "Command table region must start at 256-byte boundary");

    /* 64-bit addressing: if device doesn't advertise S64A, upper bits must be zero */
    if (!s64a) {
        assert(((uintptr_t)clb_phys >> 32) == 0, "CLB high dword must be zero when CAP.S64A==0");
        assert(((uintptr_t)cmd_tbl_phys >> 32) == 0, "CMD_TBL high dword must be zero when CAP.S64A==0");
        assert(((uintptr_t)fis_buf_phys >> 32) == 0, "FIS high dword must be zero when CAP.S64A==0");
    }
#endif
    /* Per-header CTBA programmed correctly (for the number of slots the HBA advertises) */
#ifdef AHCI_DEBUG_PRINT
    for (unsigned sl = 0; sl <= ncs; ++sl) {
        HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)((uint8_t*)clb + sl * sizeof(HBA_CMD_HEADER));
        uintptr_t expected = (uintptr_t)cmd_tbl_phys + sl * 256;
        assert(hdr->ctba == (uint32_t)(expected & 0xFFFFFFFFu), "Header CTBA low doesn't match expected CTBA");
        if (s64a) {
            assert(hdr->ctbau == (uint32_t)(expected >> 32), "Header CTBAU mismatch (S64A advertised)");
        }
        else {
            assert(hdr->ctbau == 0, "Header CTBAU must be zero when S64A==0");
        }
    }
#endif

    port_count++;
    return true;
}

extern BOOT_INFO boot_info_local;
extern GOP_PARAMS gop_local;

bool ahci_initialized = false;

MTSTATUS ahci_init(void) {
    if (ahci_initialized) { gop_printf(COLOR_RED, "AHCI Initialization got called again when alreaedy init.\n"); return MT_SUCCESS; }
    tracelast_func("ahci_init");
    // Use BootInfo PCI BARs.
    for (size_t i = 0; i < boot_info_local.AhciCount; i++) {
        uint64_t base = boot_info_local.AhciBarBases[i];
        void* virt = (void*)(base + PHYS_MEM_OFFSET);
        map_page(virt, base, PAGE_PRESENT | PAGE_RW | PAGE_PCD | PAGE_PWT); // MMIO flags
#ifdef AHCI_DEBUG_PRINT
        gop_printf(COLOR_ORANGE, "Address of AHCI BAR %u (%p) is: %s\n", i, virt, MtIsAddressValid(virt) ? "Valid" : "Invalid");
#endif
        // Now change the values in the struct
        boot_info_local.AhciBarBases[i] = (uint64_t)virt;
    }

    uint64_t bar = boot_info_local.AhciBarBases[0];
    hba_mem = (HBA_MEM*)(uintptr_t)bar;
#ifdef AHCI_DEBUG_PRINT
    gop_printf(0xFF00FFFF, "About to touch AHCI %u at %p | It's %s\n",0, hba_mem, MtIsAddressValid((void*)bar) ? "Valid" : "Invalid");
    //_cli(); __hlt();
#endif
    ensure_ahci_busmaster_enabled();
    enable_controller();
    port_count = 0; // Start from 0.
    uint32_t pi = hba_mem->pi;

    for (int idx = 0; idx < AHCI_MAX_PORTS; idx++) {
        if (pi & (1u << idx)) {
            init_one_port(idx);
        }
    }

    // Register ALL block devices.
    for (int i = 0; i < port_count; i++) {
        register_block_device(&ports[i].bdev);
    }
    ahci_initialized = true;
    return port_count > 0 ? MT_SUCCESS : MT_AHCI_PORT_FAILURE; // If it could register a port, it will return true, if it couldn't, it will return false (bugcheck)
}

MTSTATUS ahci_read_sector(BLOCK_DEVICE* dev, uint32_t lba, void* buf) {
    tracelast_func("ahci_read_sector");
    AHCI_PORT_CTX* ctx = (AHCI_PORT_CTX*)dev->dev_data;
    HBA_PORT* p = ctx->port;
    // Clear pending interrupt bits.
    p->is = (uint32_t)-1;
    int slot = find_free_slot(p->sact | p->ci);
    if (slot < 0) return MT_AHCI_PORT_FAILURE;
    uint32_t spin = 0;
    const uint32_t TIMEOUT = 100000000;
    while (p->ci & (1u << slot)) {
        if (++spin >= TIMEOUT) break;
    }


    /* Command table for this slot (256 bytes per slot) */
    HBA_CMD_TBL* cmd = (HBA_CMD_TBL*)((uint8_t*)ctx->cmd_tbl + slot * 256);
    kmemset(cmd, 0, 256);


    /* Command header in CLB for this slot */
    HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)((uint8_t*)ctx->clb + slot * sizeof(HBA_CMD_HEADER));
    hba_cmd_hdr_set_cfl(hdr, (sizeof(FIS_REG_H2D) + 3) / 4); /* CFIS length in DWORDs (20 bytes -> 5) */
    hba_cmd_hdr_set_w(hdr, 0);       /* read */
    hdr->prdbc = 0;   /* clear transferred byte count (DW1) */
    hba_cmd_hdr_set_prdtl(hdr, 1);   /* one PRDT entry */


    /* Build CFIS */
    FIS_REG_H2D* fis = (FIS_REG_H2D*)(&cmd->cfis);
    kmemset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = 0x25; /* READ DMA EXT */
    fis->device = 1 << 6;
    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = 0;
    fis->lba5 = 0;
    fis->countl = 1;
    fis->counth = 0;

    /* PRDT (one entry) */
    HBA_PRDT_ENTRY* prdt = &cmd->prdt_entry[0];
    uintptr_t buf_phys = MtTranslateVirtualToPhysical(buf);
#ifdef AHCI_DEBUG_PRINT

    gop_printf(COLOR_BLUE, "In ahci_read_sector: buf_phys: %p | virt: %p\n", (void*)buf_phys, buf);

#endif
    uint32_t bytes = 512;
    prdt->dba = (uint32_t)(uintptr_t)buf_phys;
    prdt->dbau = (uint32_t)(((uintptr_t)buf_phys) >> 32);
    prdt->dbc = bytes - 1;
    prdt->i |= 1;

    /* Ensure controller sees CLB/CMD/FIS and data buffer in memory in correct order. */

    /* Flush/invalidate command structures first, then the data buffer, then strong fences. */
    cache_flush_invalidate_range(ctx->clb, 1024);            // CLB is 1 KiB
    cache_flush_invalidate_range(cmd, 256);
    cache_flush_invalidate_range(ctx->fis, 256);             // FIS receive buffer
    /* Buffer sanity */
#ifdef AHCI_DEBUG_PRINT
    assert(buf != NULL, "read buffer is NULL");
    assert(MtIsAddressValid(buf), "read buffer virtual address invalid");
    assert((buf_phys & 0x1) == 0, "Buffer not word-aligned.");
    assert(buf_phys != 0, "read buffer physical translation returned 0");
    assert((buf_phys & 0x1FF) == 0, "read buffer physical address must be 512-byte aligned");
    /* hdr / cmd table bounds sanity */
    assert(((uintptr_t)hdr >= (uintptr_t)ctx->clb) && ((uintptr_t)hdr < ((uintptr_t)ctx->clb + 1024)),
        "HBA_CMD_HEADER pointer not inside CLB region");
    uintptr_t cmd_tbl_region_start = (uintptr_t)ctx->cmd_tbl;
    uintptr_t cmd_tbl_region_end = cmd_tbl_region_start + (256 * 32);
    assert(((uintptr_t)cmd >= cmd_tbl_region_start) && ((uintptr_t)cmd < cmd_tbl_region_end),
        "Command table pointer out of allocated command-table region");
    /* PRDT correctness: DBA/DBAU match the buffer physical address and dbc encodes bytes-1 and IOC set */
    uintptr_t prdt_dba_full = (((uintptr_t)prdt->dbau) << 32) | (uintptr_t)prdt->dba;
    assert(prdt_dba_full == buf_phys, "PRDT DBA != buffer physical address");
    uint32_t prdt_bytes = prdt->dbc & 0x003FFFFFu;  // low 22 bits = byte_count - 1
    uint32_t prdt_ioc = (prdt->i & 0x1);
    assert(prdt_ioc == 1, "PRDT IOC (interrupt-on-completion) not set");
    assert(prdt_bytes == (uint32_t)(bytes - 1), "PRDT dbc byte count not equal to bytes-1");
    /* CFIS/header sanity */
#endif
#ifdef HBA_CMD_HDR_CFL_MASK
    assert(hba_cmd_hdr_get_cfl(hdr) >= 2 && hba_cmd_hdr_get_cfl(hdr) <= 16, "CFIS length out of range");

#else

    assert(((hdr->dw0 & 0x1F) >= 2) && ((hdr->dw0 & 0x1F) <= 16), "CFIS length out of range");

#endif
    /* Flush/invalidate the data buffer as well (prepare CPU cache for DMA result) */
    cache_flush_invalidate_range(buf, 512);
    /* Strong ordering so device definitely sees the command and PRDT */
    __asm__ volatile("sfence; mfence" ::: "memory");

    /* Clear pending interrupts for the port (read/write to acknowledge) - explicit read/write */
    uint32_t port_is_before = p->is;
    p->is = port_is_before;

#ifdef AHCI_DEBUG_PRINT
    // Validate the Command FIS:
    gop_printf_forced(0xFFFFFF00, "Command FIS Validation:\n");
    gop_printf_forced(0xFFFFFF00, "  FIS Type: %p (should be 0x27)\n", fis->fis_type);
    gop_printf_forced(0xFFFFFF00, "  C bit: %u (should be 1)\n", fis->c);
    gop_printf_forced(0xFFFFFF00, "  Command: %p (READ DMA EXT)\n", fis->command);
    gop_printf_forced(0xFFFFFF00, "  Device: %p (should be 0x40)\n", fis->device);
    // Verify the complete FIS is exactly 20 bytes and properly terminated

    uint8_t cfl = hba_cmd_hdr_get_cfl(hdr);
    gop_printf_forced(0xFFFFFF00, "  FIS size check: CFL=%u DWORDs = %u bytes (should be 20)\n",
        cfl, cfl * 4);
#endif


    /* Issue command */
    p->ci = (1u << slot);

    /* Wait for completion with timeout */

    spin = 0;
    while (p->ci & (1u << slot)) {
        if (++spin >= TIMEOUT) break;
    }
    if (spin >= TIMEOUT) {

#ifdef AHCI_DEBUG_PRINT

        gop_printf_forced(0xFFFF0000, "AHCI read timeout\n");

        gop_printf_forced(0xFFFFFF00,

            "tfd=%p serr=%p is=%p\n",

            (void*)(uintptr_t)p->tfd,

            (void*)(uintptr_t)p->serr,

            (void*)(uintptr_t)p->is);

        gop_printf_forced(0xFFFFFF00,

            "hdr.ctba=%p hdr.prdtl=%u hdr.prdbc=%u\n",

            (void*)(uintptr_t)hdr->ctba,

            hba_cmd_hdr_get_prdtl(hdr),

            hdr->prdbc);

        gop_printf_forced(0xFFFFFF00,

            "ctx=%p ctx->cmd_tbl=%p ctx->clb=%p port=%p\n",

            (void*)ctx,

            (void*)ctx->cmd_tbl,

            (void*)ctx->clb,

            (void*)p);

#endif
        return MT_AHCI_TIMEOUT;

    }

    // IMPORTANT: Even if it didn't time out, check for errors from the device
    // p->tfd bit 7 is BSY, bit 0 is ERR
    if (p->tfd & ((1 << 7) | (1 << 0))) {
#ifdef AHCI_DEBUG_PRINT

        gop_printf_forced(0xFFFF0000, "AHCI read error!\n");

        gop_printf_forced(0xFFFFFF00, "Port TFD: %p, SERR: %p\n", (void*)(uintptr_t)p->tfd, (void*)(uintptr_t)p->serr);

#endif
        return MT_AHCI_READ_FAILURE;
    }

#ifdef AHCI_DEBUG_PRINT
    /* Command issue cleared */
    assert(((p->ci & (1u << slot)) == 0), "PxCI slot bit still set after completion");

    /* Task file and error checks */
    assert(((p->tfd & (1u << 0)) == 0), "PxTFD ERR bit set after transfer (device reported error)");
    /* optional: ensure not BSY */
    assert(((p->tfd & (1u << 7)) == 0), "PxTFD BSY still set after transfer");

    assert((p->serr) == 0, "Serr isn't equal 0");
    uint8_t status = (p->tfd >> 8) & 0xFF;  // ATA status register
    assert((status & 0x01) == 0, "ERR Bit in status set");
#endif
    /* Byte count reported by header (DW1) matches requested bytes (512) */
    /* Read prdbc after we ensure device completed and after an mfence to be safe */
    __asm__ volatile("mfence" ::: "memory");
    cache_flush_invalidate_range(hdr, sizeof(HBA_CMD_HEADER));
    __asm__ volatile("mfence" ::: "memory");

    // Now read prdbc
#ifdef AHCI_DEBUG_PRINT
    uint32_t actual_bytes = hdr->prdbc;
    gop_printf_forced(0xFFFFFF00, "Expected: %u, Actual prdbc: %u\n", bytes, actual_bytes);
    assert(hdr->prdbc == (uint32_t)bytes, "hdr.prdbc != bytes transferred");

    /* Make sure CPU cache invalidation can see device writes (simple check: buf_phys non-zero and mapped) */
    assert(MtIsAddressValid(buf), "buffer invalid after transfer (mapping vanished)");
#endif
    // If we have gotten here, ahci read is successfull.
    cache_flush_invalidate_range(buf, 512);
    __asm__ volatile("mfence" ::: "memory");

    /* Acknowledge/clear interrupts (read and write back) */
    uint32_t port_is_after = p->is;
    p->is = port_is_after;

    return MT_SUCCESS;
}

MTSTATUS ahci_write_sector(BLOCK_DEVICE* dev, uint32_t lba, const void* buf) {
    tracelast_func("ahci_write_sector");
    AHCI_PORT_CTX* ctx = (AHCI_PORT_CTX*)dev->dev_data;
    HBA_PORT* p = ctx->port;

    int slot = find_free_slot(p->sact | p->ci);
    if (slot < 0) return MT_AHCI_GENERAL_FAILURE;

    /* Command table for this slot */
    HBA_CMD_TBL* cmd = (HBA_CMD_TBL*)((uint8_t*)ctx->cmd_tbl + slot * 256);
    kmemset(cmd, 0, 256);

    /* Command header */
    HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)((uint8_t*)ctx->clb + slot * sizeof(HBA_CMD_HEADER));
    hba_cmd_hdr_set_cfl(hdr, (sizeof(FIS_REG_H2D) + 3) / 4);
    hba_cmd_hdr_set_w(hdr, 1);       /* write */
    hdr->prdbc = 0;
    hba_cmd_hdr_set_prdtl(hdr, 1);

    /* Build CFIS */
    FIS_REG_H2D* fis = (FIS_REG_H2D*)(&cmd->cfis);
    kmemset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = 0x35; /* WRITE DMA EXT */
    fis->device = 1 << 6;
    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = 0;
    fis->lba5 = 0;
    fis->countl = 1;
    fis->counth = 0;

    /* PRDT */
    HBA_PRDT_ENTRY* prdt = &cmd->prdt_entry[0];
    // this removes constness from buf, but it's fine since the translation DOES NOT write to the buffer, only makes translations.
    uintptr_t buf_phys = MtTranslateVirtualToPhysical((void*)buf);
#ifdef AHCI_DEBUG_PRINT
    gop_printf(COLOR_BLUE, "In AHCI_WRITE_SECTOR, buf_phys: %p | buf: %p\n", buf_phys, buf);
#endif
    uint32_t bytes = 512;
    prdt->dba = (uint32_t)(uintptr_t)buf_phys;
    prdt->dbau = (uint32_t)(((uintptr_t)buf_phys) >> 32);
    prdt->dbc = bytes - 1;
    prdt->i |= 1;

    cache_flush_invalidate_range((void*)buf, 512);
    cache_flush_invalidate_range((void*)buf, 512);
    cache_flush_invalidate_range(ctx->clb, 1024);            // CLB is 1 KiB
    cache_flush_invalidate_range(cmd, 256);
    cache_flush_invalidate_range(ctx->fis, 256);             // FIS receive buffer
    __asm__ volatile("mfence" ::: "memory");

    /* Clear pending interrupts */
    p->is = p->is;

    /* Issue */
    p->ci = (1u << slot);

    /* Wait */
    uint32_t spin = 0;
    const uint32_t TIMEOUT = 100000000;
    while (p->ci & (1u << slot)) {
        if (++spin >= TIMEOUT) break;
    }

    if (spin >= TIMEOUT) {
#ifdef AHCI_DEBUG_PRINT
        gop_printf(COLOR_RED, "AHCI TIMEOUT ahci_read_sector\n");
#endif
        return MT_AHCI_TIMEOUT;
    }

    // IMPORTANT: Check for errors from the device
    if (p->tfd & ((1 << 7) | (1 << 0))) {
#ifdef AHCI_DEBUG_PRINT
        gop_printf_forced(0xFFFF0000, "AHCI write error!\n");
        gop_printf_forced(0xFFFFFF00, "Port TFD: %p, SERR: %p\n", (void*)(uintptr_t)p->tfd, (void*)(uintptr_t)p->serr);
#endif
        return MT_AHCI_WRITE_FAILURE;
    }

    // clear int
    p->is = p->is;

    return MT_SUCCESS;
}


BLOCK_DEVICE* ahci_get_block_device(int index) {
    tracelast_func("ahci_get_block_device");
    return get_block_device(index);
}
