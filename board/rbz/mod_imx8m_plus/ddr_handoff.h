#ifndef __MOD_IMX8M_PLUS_DDR_HANDOFF_H__
#define __MOD_IMX8M_PLUS_DDR_HANDOFF_H__

/*
 * SPL -> U-Boot proper handoff for the DDR size detected during training.
 *
 * On this board, SPL and U-Boot proper parse two completely unrelated FDT
 * blobs (CONFIG_OF_SEPARATE, no CONFIG_OF_BOARD / CONFIG_HANDOFF): U-Boot
 * proper's gd->fdt_blob comes from the DTB compiled into its own image
 * (see fdt_find_separate() / the "_end" symbol), never from the blob the
 * SPL modifies at runtime. So writing into "/chosen" in SPL and reading it
 * back in board_phys_sdram_size() never works - the property just never
 * appears in the blob U-Boot proper reads.
 *
 * DDR itself, on the other hand, is already trained and physically
 * persists across the SPL -> U-Boot proper jump (same DRAM controller,
 * no reset in between). So a fixed scratch address, below U-Boot
 * proper's own load address (CONFIG_TEXT_BASE), is used instead: SPL
 * writes the detected size there right after training, and
 * board_phys_sdram_size() reads it back.
 *
 * CFG_SYS_SDRAM_BASE is 0x40000000 and U-Boot proper is loaded at
 * CONFIG_TEXT_BASE (0x40200000), so the 2 MiB gap right above the DRAM
 * base is free for this. Pick a small offset into it (not byte 0) to
 * stay clear of anything that might use the very first bytes of DRAM.
 */
#define RBZ_DDR_HANDOFF_ADDR	(CFG_SYS_SDRAM_BASE + 0x1000)
#define RBZ_DDR_HANDOFF_MAGIC	0x52425A31U	/* "RBZ1" */

struct rbz_ddr_handoff {
	u32 size_mb;
	u32 magic;
};

static inline void rbz_ddr_handoff_store(phys_size_t size)
{
	struct rbz_ddr_handoff *ho = (struct rbz_ddr_handoff *)RBZ_DDR_HANDOFF_ADDR;

	ho->size_mb = (u32)(size >> 20);
	ho->magic = RBZ_DDR_HANDOFF_MAGIC;
}

static inline bool rbz_ddr_handoff_load(phys_size_t *size)
{
	struct rbz_ddr_handoff *ho = (struct rbz_ddr_handoff *)RBZ_DDR_HANDOFF_ADDR;

	if (ho->magic != RBZ_DDR_HANDOFF_MAGIC)
		return false;

	*size = (phys_size_t)ho->size_mb << 20;
	return true;
}

#endif /* __MOD_IMX8M_PLUS_DDR_HANDOFF_H__ */
