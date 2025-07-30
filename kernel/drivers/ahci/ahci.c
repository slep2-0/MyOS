/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      AHCI Driver Implementation.
 */

#include "ahci.h"
#include "../../trace.h"

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

    // Allocate and zero CLB (1 KiB)
    void* clb = kmalloc(1024, 1024);
    if (!clb) return false;
    kmemset(clb, 0, 1024);
    p->clb = (uint32_t)(uintptr_t)clb;
    p->clbu = (uint32_t)((uintptr_t)clb >> 32);

    // Allocate and zero FIS receive buffer (256 B)
    void* fis_buf = kmalloc(256, 256);
    if (!fis_buf) return false;
    kmemset(fis_buf, 0, 256);
    p->fb = (uint32_t)(uintptr_t)fis_buf;
    p->fbu = (uint32_t)((uintptr_t)fis_buf >> 32);

    // Allocate and zero Command Table buffers: 256 B × 32 slots
    size_t tbl_size = 256 * 32;
    void* cmd_tbl = kmalloc(tbl_size, 256);
    if (!cmd_tbl) return false;
    kmemset(cmd_tbl, 0, tbl_size);

    // Point each command header to its table
    for (int slot = 0; slot < 32; slot++) {
        // Header at clb + slot*32 bytes
        HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)((uint8_t*)clb + slot * sizeof(HBA_CMD_HEADER));
        uintptr_t tbl_pa = (uintptr_t)cmd_tbl + slot * 256;
        hdr->ctba = (uint32_t)(tbl_pa & 0xFFFFFFFF);
        hdr->ctbau = (uint32_t)(tbl_pa >> 32);
        hdr->prdtl = 1; // one PRDT entry
    }

    // Start FIS RX and command engine
    p->cmd |= (1u << 4); // FRE
    p->cmd |= (1u << 0); // ST

    // Save context
    AHCI_PORT_CTX* ctx = &ports[port_count];
    ctx->port = p;
    ctx->clb = clb;
    ctx->fis = fis_buf;
    ctx->cmd_tbl = cmd_tbl;
    ctx->bdev.read_sector = ahci_read_sector;
    ctx->bdev.write_sector = ahci_write_sector;
    ctx->bdev.dev_data = ctx;

    port_count++;
    return true;
}

extern BOOT_INFO boot_info_local;
extern GOP_PARAMS gop_local;

bool ahci_init(void) {
    tracelast_func("ahci_init");
    // Use BootInfo PCI BARs.
    uint64_t bar = boot_info_local.AhciBarBases[0];
    hba_mem = (HBA_MEM*)(uintptr_t)bar;
#ifdef DEBUG
    gop_printf(&gop_local, 0xFF00FFFF, "About to touch AHCI at %p\n", hba_mem);
#endif
    enable_controller();
    /*
    gop_printf(&gop_local, 0xFF00FF00, "Reached after enable controller!");
    __cli();
    __hlt();
    */
    port_count = 0; // Start from 0.
    uint32_t pi = hba_mem->pi;
    /*
    gop_printf(&gop_local, 0xFF00FF00, "Reached after pi hba mem!");
    __cli();
    __hlt();
    */
    // Initialize ports.
    /// TEST TODO THAT KMALLOC DYNAMIC MEM WORKS!

    void* testbuffer = kmalloc(4096, 128);
    gop_printf(&gop_local, 0xFF00FF00, "KMalloc Works! Address: %p", testbuffer);
    __cli();
    __hlt();
    for (int idx = 0; idx < AHCI_MAX_PORTS; idx++) {
        if (pi & (1u << idx)) {
            init_one_port(idx);
        }
    }
    gop_printf(&gop_local, 0xFF00FF00, "Reached after init_one_port!");
    __cli();
    __hlt();
    // Register ALL block devices.
    for (int i = 0; i < port_count; i++) {
        register_block_device(&ports[i].bdev);
    }
    gop_printf(&gop_local, 0xFF00FF00, "Reached after register block device!");
    __cli();
    __hlt();
    return port_count > 0; // If it could register a port, it will return true, if it couldn't, it will return false (bugcheck)
}

bool ahci_read_sector(BLOCK_DEVICE* dev, uint32_t lba, void* buf) {
    tracelast_func("ahci_read_sector");
    // Our port context is the dev data
    AHCI_PORT_CTX* ctx = (AHCI_PORT_CTX*)dev->dev_data;
    // Port HBA.
    HBA_PORT* p = ctx->port;

    int slot = find_free_slot(p->sact | p->ci);
    if (slot < 0) return false; // Didn't find a free slot to read from.

    // Command table per slot: 256B CFIS + 16*6B overhead + 1 PRDT
    //uint32_t cmd_offset = slot * 1024 / 32; // each header is 32 bytes.
    uint8_t* clb_mem = (uint8_t*)ctx->clb;
    HBA_CMD_TBL* cmd = (HBA_CMD_TBL*)(clb_mem + slot * 256);
    kmemset(cmd, 0, sizeof(FIS_REG_H2D) + sizeof(HBA_PRDT_ENTRY));

    // Prepare Register - Host to Device FIS.
    FIS_REG_H2D* fis = &cmd->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D; // FIS Type of REG_H2D buffer.
    fis->c = 1; // Command 
    fis->command = 0x25; // READ_DMA_EXT
    fis->lba0 = (uint8_t)lba;
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->countl = 1;

    // Setup PRDT for one 512-byte buffer.
    HBA_PRDT_ENTRY* prdt = (HBA_PRDT_ENTRY*)((uint8_t*)cmd + sizeof(FIS_REG_H2D) + 16 + 48);
    prdt->dba = (uint32_t)(uintptr_t)buf;
    prdt->dbau = (uint32_t)((uintptr_t)buf >> 32);
    prdt->dbc = 512 - 1;
    prdt->i = 1;

    // Issue the command.
    // The port listens in the command issue for new data.
    p->ci |= (1u << slot);

    // Wait for completion
    while (p->ci & (1u << slot));
    // Clear interrupt flags
    p->is = p->is;

    return true;
}

bool ahci_write_sector(BLOCK_DEVICE* dev, uint32_t lba, const void* buf) {
    tracelast_func("ahci_write_sector");
    AHCI_PORT_CTX* ctx = (AHCI_PORT_CTX*)dev->dev_data;
    HBA_PORT* p = ctx->port;

    int slot = find_free_slot(p->sact | p->ci);
    if (slot < 0) return false;

    uint8_t* clb_mem = (uint8_t*)ctx->clb;
    HBA_CMD_TBL* cmd = (HBA_CMD_TBL*)(clb_mem + slot * 256);
    kmemset(cmd, 0, sizeof(FIS_REG_H2D) + sizeof(HBA_PRDT_ENTRY));

    FIS_REG_H2D* fis = &cmd->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = 0x35; // WRITE_DMA_EXT
    fis->lba0 = (uint8_t)lba;
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->countl = 1;

    HBA_PRDT_ENTRY* prdt = (HBA_PRDT_ENTRY*)((uint8_t*)cmd + sizeof(FIS_REG_H2D) + 16 + 48);
    prdt->dba = (uint32_t)(uintptr_t)buf;
    prdt->dbau = (uint32_t)((uintptr_t)buf >> 32);
    prdt->dbc = 512 - 1;
    prdt->i = 1;

    p->ci |= (1u << slot);
    // busy wait
    while (p->ci & (1u << slot));
    p->is = p->is;

    return true;
}

BLOCK_DEVICE* ahci_get_block_device(int index) {
    tracelast_func("ahci_get_block_device");
    return get_block_device(index);
}