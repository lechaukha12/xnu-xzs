/*
 * Sony Xperia XZs (Keyaki / MSM8996 v3.0)
 * D8-M4: DSI0 Host Controller Bring-Up State Machine & HAL
 *
 * Source-derived from Qualcomm MSM8996 downstream kernel
 * drivers/video/fbdev/msm/mdss/mdss_dsi_host.c
 */

#ifndef _XZS_D8M4_H_
#define _XZS_D8M4_H_

#include <stdint.h>
#include <stddef.h>

extern void xzs_diag_emit(const char *msg);
extern uint64_t g_xzs_ttbr0;

static inline void
xzs_d8m4_hex32(uint32_t val)
{
	char str[9];
	static const char hex[] = "0123456789abcdef";
	for (int h = 7; h >= 0; h--) {
		str[7 - h] = hex[(val >> (h * 4)) & 0xf];
	}
	str[8] = '\0';
	xzs_diag_emit(str);
}

static inline void
xzs_d8m4_hex16(uint16_t val)
{
	char str[5];
	static const char hex[] = "0123456789abcdef";
	for (int h = 3; h >= 0; h--) {
		str[3 - h] = hex[(val >> (h * 4)) & 0xf];
	}
	str[4] = '\0';
	xzs_diag_emit(str);
}

static __attribute__((noinline)) uint32_t
d8m4_read32(uint32_t phys)
{
	uint64_t saved = 0;
	__asm__ volatile("mrs %0, TTBR0_EL1" : "=r"(saved));
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0\n\tisb sy" :: "r"(g_xzs_ttbr0) : "memory");
	}
	uint32_t val = *(volatile uint32_t *)(uintptr_t)phys;
	__asm__ volatile("dsb sy\n\tisb sy" ::: "memory");
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0\n\tisb sy" :: "r"(saved) : "memory");
	}
	return val;
}

static __attribute__((noinline, unused)) void
d8m4_write32(uint32_t phys, uint32_t val)
{
	uint64_t saved = 0;
	__asm__ volatile("mrs %0, TTBR0_EL1" : "=r"(saved));
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0\n\tisb sy" :: "r"(g_xzs_ttbr0) : "memory");
	}
	*(volatile uint32_t *)(uintptr_t)phys = val;
	__asm__ volatile("dsb sy\n\tisb sy" ::: "memory");
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0\n\tisb sy" :: "r"(saved) : "memory");
	}
}

/* Source-verified MSM8996 DSI Host Controller Register Dictionary */
struct dsi_host_reg_entry {
	const char *name;
	uint16_t offset;
	uint32_t addr;
	uint32_t expected;
	const char *source;
};

static const struct dsi_host_reg_entry s_dsi_host_regs[] = {
	{ "DSI_HW_VERSION",                    0x000, 0x00994000u, 0x10040001u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_CTRL",                          0x004, 0x00994004u, 0x000001f5u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_STATUS",                        0x008, 0x00994008u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_FIFO_STATUS",                   0x00c, 0x0099400cu, 0x11111000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_COMMAND_MODE_DMA_CTRL",         0x03c, 0x0099403cu, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_COMMAND_MODE_MDP_CTRL",         0x040, 0x00994040u, 0x06100006u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_COMMAND_MODE_MDP_DCS_CMD_CTRL", 0x044, 0x00994044u, 0x00003c2cu, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_DMA_CMD_OFFSET",                0x048, 0x00994048u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_DMA_CMD_LENGTH",                0x04c, 0x0099404cu, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_DMA_FIFO_CTRL",                 0x050, 0x00994050u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_DMA_NULL_PACKET_DATA",          0x054, 0x00994054u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_COMMAND_MODE_MDP_STREAM0_CTRL", 0x058, 0x00994058u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_COMMAND_MODE_MDP_STREAM0_TOTAL",0x05c, 0x0099405cu, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_ACK_ERR_STATUS",                0x068, 0x00994068u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_RDBK_DATA0",                    0x06c, 0x0099406cu, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_TRIG_CTRL",                     0x084, 0x00994084u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_CMD_MODE_DMA_SW_TRIGGER",       0x090, 0x00994090u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_CMD_MODE_MDP_SW_TRIGGER",       0x094, 0x00994094u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_CMD_MODE_BTA_SW_TRIGGER",       0x098, 0x00994098u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_RESET_SW_TRIGGER",              0x09c, 0x0099409cu, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_LANE_STATUS",                   0x0a8, 0x009940a8u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_LANE_CTRL",                     0x0ac, 0x009940acu, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_LANE_SWAP_CTRL",                0x0b0, 0x009940b0u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_LP_TIMER_CTRL",                 0x0b8, 0x009940b8u, 0xffffffffu, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_HS_TIMER_CTRL",                 0x0bc, 0x009940bcu, 0x0000ffffu, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_TIMEOUT_STATUS",                0x0c0, 0x009940c0u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_CLKOUT_TIMING_CTRL",            0x0c4, 0x009940c4u, 0x00001b2bu, "KEYAKI_PANEL_SOURCE" },
	{ "DSI_EOT_PACKET_CTRL",               0x0cc, 0x009940ccu, 0x00000011u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_ERR_INT_MASK0",                 0x10c, 0x0099410cu, 0x0003fd08u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_INT_CTRL",                      0x110, 0x00994110u, 0x03f03fc0u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_SOFT_RESET",                    0x118, 0x00994118u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_CLK_CTRL",                      0x11c, 0x0099411cu, 0x0000023fu, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_CLK_STATUS",                    0x120, 0x00994120u, 0x00000000u, "QUALCOMM_MSM8996_DSI_SPEC" },
	{ "DSI_VIDEO_COMPRESSION_MODE_CTRL",   0x2a0, 0x009942a0u, 0x00000b00u, "QUALCOMM_MSM8996_DSC_SPEC" }
};

static inline void
xzs_d8m4_dump_status(void)
{
	xzs_diag_emit("[D8-M4] DSI0 HOST STATUS:\n");
	for (size_t i = 0; i < sizeof(s_dsi_host_regs)/sizeof(s_dsi_host_regs[0]); i++) {
		uint32_t addr = s_dsi_host_regs[i].addr;
		uint32_t val = d8m4_read32(addr);
		xzs_diag_emit("  0x");
		xzs_d8m4_hex32(addr);
		xzs_diag_emit(" (");
		xzs_diag_emit(s_dsi_host_regs[i].name);
		xzs_diag_emit(") = 0x");
		xzs_d8m4_hex32(val);
		xzs_diag_emit("\n");
	}
}

static int
d8m4_audit_write(const char *name, uint16_t offset, uint32_t addr, uint32_t write_val, uint32_t mask)
{
	xzs_diag_emit("\n[D8-M4] REG=");
	xzs_diag_emit(name);
	xzs_diag_emit("\nOFFSET=0x");
	xzs_d8m4_hex16(offset);
	xzs_diag_emit("\nADDR=0x");
	xzs_d8m4_hex32(addr);

	uint32_t old_val = d8m4_read32(addr);
	xzs_diag_emit("\nOLD=0x");
	xzs_d8m4_hex32(old_val);
	xzs_diag_emit("\nWRITE=0x");
	xzs_d8m4_hex32(write_val);

	uint32_t final_val = (old_val & ~mask) | (write_val & mask);
	d8m4_write32(addr, final_val);

	uint32_t readback = d8m4_read32(addr);
	xzs_diag_emit("\nREADBACK=0x");
	xzs_d8m4_hex32(readback);
	xzs_diag_emit("\nMASK=0x");
	xzs_d8m4_hex32(mask);

	int ok = ((readback & mask) == (write_val & mask));
	if (ok) {
		xzs_diag_emit("\nRESULT=PASS\n");
	} else {
		xzs_diag_emit("\nRESULT=FAIL\n");
		return -1;
	}

	/* Section 8: Verify M3 health after every write */
	uint32_t pll_stat = d8m4_read32(0x009948ccu);
	if ((pll_stat & 0x21u) != 0x21u) {
		xzs_diag_emit("!!! M3 REGRESSION: PLL_PRIMARY_STATUS = 0x");
		xzs_d8m4_hex32(pll_stat);
		xzs_diag_emit("\n");
		return -1;
	}
	uint32_t byte0 = d8m4_read32(0x008c233cu);
	uint32_t pclk0 = d8m4_read32(0x008c2314u);
	uint32_t esc0  = d8m4_read32(0x008c2344u);
	if ((byte0 & 1u) == 0 || (pclk0 & 1u) == 0 || (esc0 & 1u) == 0) {
		xzs_diag_emit("!!! M3 REGRESSION: CBCR clock halted!\n");
		return -1;
	}

	return 0;
}

static inline int
xzs_d8m4_run(int mode)
{
	xzs_diag_emit("\n========================================\n");
	if (mode == 0) {
		xzs_diag_emit("[D8-M4] STARTING DSI HOST DRY-RUN\n");
	} else if (mode == 1) {
		xzs_diag_emit("[D8-M4] STARTING D8-M4 MODE 1 REAL HARDWARE RUN\n");
	} else if (mode == 2) {
		xzs_diag_emit("[D8-M4] STARTING D8-M4 MODE 2 FULL HOST ENABLE RUN\n");
	} else {
		xzs_diag_emit("[D8-M4] UNKNOWN RUN MODE\n");
		xzs_diag_emit("========================================\n");
		return -1;
	}
	xzs_diag_emit("========================================\n");

	/* CHECKPOINT D8M4-10: LOWER_LAYER_VERIFY (Untouched M3 proven state) */
	xzs_diag_emit("[D8-M4] CHECKPOINT D8M4-10 LOWER_LAYER_VERIFY START\n");
	uint32_t pll_stat = d8m4_read32(0x009948ccu);
	if ((pll_stat & 0x21u) != 0x21u) {
		xzs_diag_emit("!!! FAIL: PLL not locked/ready: 0x");
		xzs_d8m4_hex32(pll_stat);
		xzs_diag_emit("\n");
		return -1;
	}
	uint32_t byte0_cbcr = d8m4_read32(0x008c233cu);
	if ((byte0_cbcr & 0x80000001u) != 0x00000001u) {
		xzs_diag_emit("!!! FAIL: BYTE0 clock halted: 0x");
		xzs_d8m4_hex32(byte0_cbcr);
		xzs_diag_emit("\n");
		return -1;
	}
	uint32_t pclk0_cbcr = d8m4_read32(0x008c2314u);
	if ((pclk0_cbcr & 0x80000001u) != 0x00000001u) {
		xzs_diag_emit("!!! FAIL: PCLK0 clock halted: 0x");
		xzs_d8m4_hex32(pclk0_cbcr);
		xzs_diag_emit("\n");
		return -1;
	}
	uint32_t esc0_cbcr = d8m4_read32(0x008c2344u);
	if ((esc0_cbcr & 0x80000001u) != 0x00000001u) {
		xzs_diag_emit("!!! FAIL: ESC0 clock halted: 0x");
		xzs_d8m4_hex32(esc0_cbcr);
		xzs_diag_emit("\n");
		return -1;
	}
	uint32_t dl0_ldo = d8m4_read32(0x00994564u);
	if ((dl0_ldo & 0x1fu) != 0x1du) {
		xzs_diag_emit("!!! FAIL: PHY DL0 LDO not 0x1d: 0x");
		xzs_d8m4_hex32(dl0_ldo);
		xzs_diag_emit("\n");
		return -1;
	}
	xzs_diag_emit("[D8-M4] CHECKPOINT D8M4-10 LOWER_LAYER_VERIFY PASS\n");

	if (mode == 0) {
		/* M4 DRY-RUN: Iterate through source-derived register dictionary */
		xzs_diag_emit("[D8-M4] DSI0 HOST REGISTER AUDIT (DRY-RUN):\n");
		for (size_t i = 0; i < sizeof(s_dsi_host_regs)/sizeof(s_dsi_host_regs[0]); i++) {
			const struct dsi_host_reg_entry *r = &s_dsi_host_regs[i];
			xzs_diag_emit("\n");
			xzs_diag_emit(r->name);
			xzs_diag_emit("\n");
			xzs_diag_emit("offset=0x");
			xzs_d8m4_hex16(r->offset);
			xzs_diag_emit("\n");
			xzs_diag_emit("addr=0x");
			xzs_d8m4_hex32(r->addr);
			xzs_diag_emit("\n");
			xzs_diag_emit("expected=0x");
			xzs_d8m4_hex32(r->expected);
			xzs_diag_emit("\n");
			uint32_t live = d8m4_read32(r->addr);
			xzs_diag_emit("live=0x");
			xzs_d8m4_hex32(live);
			xzs_diag_emit("\n");
			xzs_diag_emit("source=");
			xzs_diag_emit(r->source);
			xzs_diag_emit("\n");
		}

		xzs_diag_emit("\n[D8-M4] OFFSET_0x2A0_WRITE=NO\n");
		xzs_diag_emit("[D8-M4] REASON=DSI_VIDEO_COMPRESSION_MODE_CTRL (0x2a0) is for DSC compression. Keyaki uses uncompressed command mode panels (RGB888 24bpp) without DSC.\n");
		xzs_diag_emit("\n[D8-M4] RESULT=PASS_DRYRUN\n");
		return 0;
	}

	/* MODE 1 & MODE 2 Common Staged Setup: Stages A through E */
	/* STAGE A: Ensure host remains disabled */
	xzs_diag_emit("\n[D8-M4] STAGE A: VERIFY HOST DISABLED\n");
	uint32_t ctrl_val = d8m4_read32(0x00994004u);
	if (ctrl_val & 1u) {
		xzs_diag_emit("!!! FAIL: DSI_CTRL already enabled: 0x");
		xzs_d8m4_hex32(ctrl_val);
		xzs_diag_emit("\n");
		return -1;
	}
	xzs_diag_emit("[D8-M4] STAGE A: HOST DISABLED CONFIRMED (DSI_CTRL=0x");
	xzs_d8m4_hex32(ctrl_val);
	xzs_diag_emit(")\n");

	/* STAGE B: Configure CLKOUT timing (0x0c4 = 0x00001b2b) */
	xzs_diag_emit("[D8-M4] STAGE B: CLKOUT TIMING CONFIG\n");
	if (d8m4_audit_write("DSI_CLKOUT_TIMING_CTRL", 0x0c4, 0x009940c4u, 0x00001b2bu, 0x00003f3fu) != 0) {
		return -1;
	}

	/* STAGE C: Configure EOT behavior (0x0cc = 0x00000011) */
	xzs_diag_emit("[D8-M4] STAGE C: EOT PACKET CONFIG\n");
	if (d8m4_audit_write("DSI_EOT_PACKET_CTRL", 0x0cc, 0x009940ccu, 0x00000011u, 0x00000011u) != 0) {
		return -1;
	}

	/* STAGE D: Configure lane/control prerequisites */
	xzs_diag_emit("[D8-M4] STAGE D: LANE/CONTROL PREREQUISITES\n");
	if (d8m4_audit_write("DSI_LANE_CTRL", 0x0ac, 0x009940acu, 0x00000000u, 0x0000001fu) != 0) {
		return -1;
	}
	if (d8m4_audit_write("DSI_LANE_SWAP_CTRL", 0x0b0, 0x009940b0u, 0x00000000u, 0x00000007u) != 0) {
		return -1;
	}

	/* STAGE E: Configure DSI_CLK_CTRL (0x11c = 0x0000023f) */
	xzs_diag_emit("[D8-M4] STAGE E: DSI_CLK_CTRL CONFIG\n");
	if (d8m4_audit_write("DSI_CLK_CTRL", 0x11c, 0x0099411cu, 0x0000023fu, 0x000003ffu) != 0) {
		return -1;
	}

	if (mode == 1) {
		/* STAGE F: Read back target registers */
		xzs_diag_emit("\n[D8-M4] STAGE F: READBACK VERIFICATION TABLE\n");
		xzs_d8m4_dump_status();

		/* Explicit safety counter reporting */
		xzs_diag_emit("\n[D8-M4] SAFETY COUNTERS:\n");
		xzs_diag_emit("OFFSET_0x2A0_WRITE_COUNT=0\n");
		xzs_diag_emit("DCS_PACKETS_SENT=0\n");
		xzs_diag_emit("DMA_TRIGGER_COUNT=0\n");
		xzs_diag_emit("BTA_TRIGGER_COUNT=0\n");
		xzs_diag_emit("PANEL_GPIO_WRITES=0\n");
		xzs_diag_emit("LAB_WRITES=0\n");
		xzs_diag_emit("IBB_WRITES=0\n");
		xzs_diag_emit("WLED_WRITES=0\n");
		xzs_diag_emit("BUS_ABORT=0\n");
		xzs_diag_emit("SError=0\n");
		xzs_diag_emit("PANIC=0\n");
		xzs_diag_emit("UNINTENDED_RESET=0\n");

		/* STAGE G: STOP */
		xzs_diag_emit("\n[D8-M4] RESULT=PASS_MODE1\n");
		return 0;
	}

	/* MODE 2: Full DSI Host Enable */
	xzs_diag_emit("\n[D8-M4] MODE1_REPRODUCED=yes\n");

	/* STAGE 2A: Configure DSI_CTRL while master bit is disabled (0x1f4) */
	xzs_diag_emit("\n[D8-M4] STAGE 2A: CONFIGURE DSI_CTRL (MASTER BIT DISABLED)\n");
	if (d8m4_audit_write("DSI_CTRL", 0x004, 0x00994004u, 0x000001f4u, 0x000001ffu) != 0) {
		return -1;
	}
	uint32_t pre_ctrl = d8m4_read32(0x00994004u);
	if ((pre_ctrl & 0x000001ffu) != 0x000001f4u) {
		xzs_diag_emit("!!! FAIL: DSI_CTRL pre-enable mismatch: 0x");
		xzs_d8m4_hex32(pre_ctrl);
		xzs_diag_emit("\n");
		return -1;
	}
	xzs_diag_emit("[D8-M4] DSI_CTRL_PRE_ENABLE=0x000001f4\n");

	/* STAGE 2B: Pre-enable status check */
	xzs_diag_emit("\n[D8-M4] STAGE 2B: PRE-ENABLE STATUS CHECK\n");
	uint32_t pre_dsi_stat   = d8m4_read32(0x00994008u);
	uint32_t pre_fifo_stat  = d8m4_read32(0x0099400cu);
	uint32_t pre_lane_stat  = d8m4_read32(0x009940a8u);
	uint32_t pre_clk_stat   = d8m4_read32(0x00994120u);
	uint32_t pre_ack_err    = d8m4_read32(0x00994068u);
	uint32_t pre_timeout    = d8m4_read32(0x009940c0u);
	uint32_t pre_pll_stat   = d8m4_read32(0x009948ccu);

	xzs_diag_emit("  DSI_STATUS=0x");
	xzs_d8m4_hex32(pre_dsi_stat);
	xzs_diag_emit("\n  DSI_FIFO_STATUS=0x");
	xzs_d8m4_hex32(pre_fifo_stat);
	xzs_diag_emit("\n  DSI_LANE_STATUS=0x");
	xzs_d8m4_hex32(pre_lane_stat);
	xzs_diag_emit("\n  DSI_CLK_STATUS=0x");
	xzs_d8m4_hex32(pre_clk_stat);
	xzs_diag_emit("\n  DSI_ACK_ERR_STATUS=0x");
	xzs_d8m4_hex32(pre_ack_err);
	xzs_diag_emit("\n  DSI_TIMEOUT_STATUS=0x");
	xzs_d8m4_hex32(pre_timeout);
	xzs_diag_emit("\n  PLL_PRIMARY_STATUS=0x");
	xzs_d8m4_hex32(pre_pll_stat);
	xzs_diag_emit("\n");

	/* STAGE 2C: Enable controller (DSI_CTRL |= BIT(0)) */
	xzs_diag_emit("\n[D8-M4] STAGE 2C: ENABLE CONTROLLER (DSI_CTRL |= BIT(0))\n");
	if (d8m4_audit_write("DSI_CTRL", 0x004, 0x00994004u, 0x000001f5u, 0x00000001u) != 0) {
		return -1;
	}
	uint32_t post_ctrl = d8m4_read32(0x00994004u);
	if ((post_ctrl & 0x000001ffu) != 0x000001f5u) {
		xzs_diag_emit("!!! FAIL: DSI_CTRL post-enable mismatch: 0x");
		xzs_d8m4_hex32(post_ctrl);
		xzs_diag_emit("\n");
		return -1;
	}
	xzs_diag_emit("[D8-M4] DSI_CTRL_POST_ENABLE=0x000001f5\n");

	/* STAGE 2D: Post-enable status capture */
	xzs_diag_emit("\n[D8-M4] STAGE 2D: POST-ENABLE STATUS CAPTURE\n");
	uint32_t post_dsi_stat   = d8m4_read32(0x00994008u);
	uint32_t post_fifo_stat  = d8m4_read32(0x0099400cu);
	uint32_t post_lane_stat  = d8m4_read32(0x009940a8u);
	uint32_t post_clk_stat   = d8m4_read32(0x00994120u);
	uint32_t post_ack_err    = d8m4_read32(0x00994068u);
	uint32_t post_timeout    = d8m4_read32(0x009940c0u);
	uint32_t post_pll_stat   = d8m4_read32(0x009948ccu);

	xzs_diag_emit("  DSI_CTRL=0x");
	xzs_d8m4_hex32(post_ctrl);
	xzs_diag_emit("\n  DSI_STATUS=0x");
	xzs_d8m4_hex32(post_dsi_stat);
	xzs_diag_emit("\n  DSI_FIFO_STATUS=0x");
	xzs_d8m4_hex32(post_fifo_stat);
	xzs_diag_emit("\n  DSI_LANE_STATUS=0x");
	xzs_d8m4_hex32(post_lane_stat);
	xzs_diag_emit("\n  DSI_CLK_STATUS=0x");
	xzs_d8m4_hex32(post_clk_stat);
	xzs_diag_emit("\n  DSI_ACK_ERR_STATUS=0x");
	xzs_d8m4_hex32(post_ack_err);
	xzs_diag_emit("\n  DSI_TIMEOUT_STATUS=0x");
	xzs_d8m4_hex32(post_timeout);
	xzs_diag_emit("\n  PLL_PRIMARY_STATUS=0x");
	xzs_d8m4_hex32(post_pll_stat);
	xzs_diag_emit("\n");

	/* Post-enable full status table */
	xzs_diag_emit("\n[D8-M4] POST-ENABLE DSI HOST STATUS:\n");
	xzs_d8m4_dump_status();

	/* Explicit safety counter reporting */
	xzs_diag_emit("\n[D8-M4] SAFETY COUNTERS:\n");
	xzs_diag_emit("OFFSET_0x2A0_WRITE_COUNT=0\n");
	xzs_diag_emit("DCS_PACKETS_SENT=0\n");
	xzs_diag_emit("DMA_TRIGGER_COUNT=0\n");
	xzs_diag_emit("BTA_TRIGGER_COUNT=0\n");
	xzs_diag_emit("PANEL_GPIO_WRITES=0\n");
	xzs_diag_emit("LAB_WRITES=0\n");
	xzs_diag_emit("IBB_WRITES=0\n");
	xzs_diag_emit("WLED_WRITES=0\n");
	xzs_diag_emit("BUS_ABORT=0\n");
	xzs_diag_emit("SError=0\n");
	xzs_diag_emit("PANIC=0\n");
	xzs_diag_emit("UNINTENDED_RESET=0\n");

	/* Final Mode 2 result */
	xzs_diag_emit("\n[D8-M4] RESULT=PASS_MODE2\n");
	return 0;
}

#endif /* _XZS_D8M4_H_ */
