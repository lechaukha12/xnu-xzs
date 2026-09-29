/*
 * xnu-xzs Display Bringup Milestone D8-M6: Keyaki Panel DCS / Vendor Initialization
 *
 * Target Hardware:
 *   Sony Xperia XZs G8231 (keyaki / tone, Qualcomm MSM8996 v3.0, Serial: BH905SX976)
 *   Panel: Sharp + Synaptics command-mode panel ("somc,sharp_synaptics_cmd_9_panel")
 *   Resolution: 1080x1920, DSI0 Command Mode, 4 data lanes, RGB888
 *
 * Strict Boundaries for D8-M6:
 *   - FIRST milestone allowed to transmit DSI command packets to physical panel.
 *   - Strictly ZERO WLED backlight writes (WLED_WRITES=0). Backlight remains OFF.
 *   - Strictly ZERO MDP scanout / DMA kickoff (MDP_KICKOFF_COUNT=0).
 *   - Screen visually black is EXPECTED and normal (backlight is off).
 *   - Fully safe: every active run concludes with graceful OFF sequence + M5 shutdown.
 */

#ifndef _XZS_D8M6_H_
#define _XZS_D8M6_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "xzs_d8m5.h"
#include "xzs_d8m4.h"

extern void xzs_diag_emit(const char *msg);
extern void xzs_watchdog_pet(void);
extern vm_offset_t ml_static_vtop(vm_offset_t va);

/* DSI Controller MMIO Registers (Base: 0x00994000) */
#define D8M6_REG_DSI_CTRL                   0x00994004u
#define D8M6_REG_DSI_STATUS                 0x00994008u
#define D8M6_REG_DSI_FIFO_STATUS            0x0099400cu
#define D8M6_REG_DSI_COMMAND_MODE_DMA_CTRL  0x0099403cu
#define D8M6_REG_DSI_DMA_CMD_OFFSET         0x00994048u
#define D8M6_REG_DSI_DMA_CMD_LENGTH         0x0099404cu
#define D8M6_REG_DSI_DMA_FIFO_CTRL          0x00994050u
#define D8M6_REG_DSI_ACK_ERR_STATUS         0x00994068u
#define D8M6_REG_DSI_RDBK_DATA0             0x0099406cu
#define D8M6_REG_DSI_TRIG_CTRL              0x00994084u
#define D8M6_REG_DSI_CMD_MODE_DMA_SW_TRIGGER 0x00994090u
#define D8M6_REG_DSI_LANE_STATUS            0x009940a8u
#define D8M6_REG_DSI_LANE_CTRL              0x009940acu
#define D8M6_REG_DSI_TIMEOUT_STATUS         0x009940c0u
#define D8M6_REG_DSI_INT_CTRL               0x00994110u
#define D8M6_REG_DSI_SOFT_RESET             0x00994118u
#define D8M6_REG_DSI_CLK_CTRL               0x0099411cu
#define D8M6_REG_DSI_CLK_STATUS             0x00994120u
#define D8M6_REG_DSI_TEST_PATTERN_GEN_CTRL  0x0099415cu
#define D8M6_REG_DSI_TEST_PATTERN_GEN_CMD_DMA_INIT 0x0099417cu
#define D8M6_REG_DSI_TPG_DMA_FIFO_RESET     0x009941ecu

/* Bit definitions for MSM8996 DSI DMA */
#define D8M6_DMA_CTRL_LOW_POWER             (1u << 26)  /* BIT(26): Low Power Escape Mode */
#define D8M6_DMA_CTRL_EMBEDDED_MODE         (1u << 28)  /* BIT(28): Embedded packet header in buffer */
#define D8M6_DMA_SW_TRIGGER_VAL             0x00000001u /* Self-clearing write 1 */
#define D8M6_INT_CTRL_DMA_DONE              (1u << 0)   /* Bit 0: DMA_CMD_DONE status */
#define D8M6_INT_CTRL_DMA_DONE_MASK         (1u << 1)   /* Bit 1: DMA_CMD_DONE interrupt enable mask */

/* DSI Packet Data Types */
#define DSI_DCS_SHORT_WRITE_0_PARAM         0x05u
#define DSI_DCS_SHORT_WRITE_1_PARAM         0x15u
#define DSI_GENERIC_LONG_WRITE              0x29u
#define DSI_DCS_LONG_WRITE                  0x39u

/* Command Descriptor */
struct xzs_d8m6_cmd {
	const char *name;
	uint8_t dtype;          /* DSI Data Type (0x05 or 0x39) */
	uint8_t dlen;           /* Payload length in bytes */
	const uint8_t *payload; /* Command payload bytes */
	uint32_t post_wait_us;  /* Hardware delay after transmission (us) */
};

/* Safety and Audit Counters */
struct xzs_d8m6_counters {
	uint32_t slpout_count;
	uint32_t teon_count;
	uint32_t dispon_count;
	uint32_t dispoff_count;
	uint32_t slpin_count;
	uint32_t dma_triggers;
	uint32_t dma_completions;
	uint32_t dma_timeouts;
	uint32_t ack_errors;
	uint32_t wled_writes;
	uint32_t mdp_kickoffs;
};

static struct xzs_d8m6_counters g_d8m6_counters;

/* DMA Packet Memory Buffer (Aligned to 64 bytes for cache maintenance) */
static uint8_t s_d8m6_dma_buf[1024] __attribute__((aligned(64)));

/* Source-Audited Command Payloads */
static const uint8_t s_payload_slpout[1]  = { 0x11 };
static const uint8_t s_payload_teon[2]    = { 0x35, 0x00 };
static const uint8_t s_payload_dispon[1]  = { 0x29 };
static const uint8_t s_payload_dispoff[1] = { 0x28 };
static const uint8_t s_payload_slpin[1]   = { 0x10 };

/* Authentic Sony Keyaki DT On-Command Payloads (keyaki.dts:1872) */
static const uint8_t s_payload_cmd1_b0_00[2]  = { 0xb0, 0x00 };
static const uint8_t s_payload_cmd2_d6_01[2]  = { 0xd6, 0x01 };
static const uint8_t s_payload_cmd3_c4[3]     = { 0xc4, 0x70, 0x22 };
static const uint8_t s_payload_cmd4_c6[21]    = { 0xc6, 0x53, 0x2e, 0x2e, 0x05, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x04, 0x10, 0x06 };
static const uint8_t s_payload_cmd5_ec[14]    = { 0xec, 0x64, 0xdc, 0xec, 0x3b, 0x52, 0x00, 0x0b, 0x0b, 0x13, 0x15, 0x68, 0x0b, 0xb5 };
static const uint8_t s_payload_cmd6_b0_03[2]  = { 0xb0, 0x03 };
static const uint8_t s_payload_cmd8_madctl[2] = { 0x36, 0x00 };
static const uint8_t s_payload_cmd9_colmod[2] = { 0x3a, 0x77 };
static const uint8_t s_payload_cmd10_caset[5] = { 0x2a, 0x00, 0x00, 0x04, 0x37 };
static const uint8_t s_payload_cmd11_paset[5] = { 0x2b, 0x00, 0x00, 0x07, 0x7f };
static const uint8_t s_payload_cmd12_stesl[3] = { 0x44, 0x00, 0x00 };

/* Master Command Table */
static const struct xzs_d8m6_cmd s_cmd_dt_on_1 = {
	.name = "CMD1 (B0 00 Protect Unlock)",
	.dtype = DSI_GENERIC_LONG_WRITE,
	.dlen = 2,
	.payload = s_payload_cmd1_b0_00,
	.post_wait_us = 0,
};
static const struct xzs_d8m6_cmd s_cmd_dt_on_2 = {
	.name = "CMD2 (D6 01 Output Ctrl)",
	.dtype = DSI_GENERIC_LONG_WRITE,
	.dlen = 2,
	.payload = s_payload_cmd2_d6_01,
	.post_wait_us = 0,
};
static const struct xzs_d8m6_cmd s_cmd_dt_on_3 = {
	.name = "CMD3 (C4 70 22 Timing Gen)",
	.dtype = DSI_GENERIC_LONG_WRITE,
	.dlen = 3,
	.payload = s_payload_cmd3_c4,
	.post_wait_us = 0,
};
static const struct xzs_d8m6_cmd s_cmd_dt_on_4 = {
	.name = "CMD4 (C6 Timing Waveforms)",
	.dtype = DSI_GENERIC_LONG_WRITE,
	.dlen = 21,
	.payload = s_payload_cmd4_c6,
	.post_wait_us = 0,
};
static const struct xzs_d8m6_cmd s_cmd_dt_on_5 = {
	.name = "CMD5 (EC Scan Gen Bias)",
	.dtype = DSI_GENERIC_LONG_WRITE,
	.dlen = 14,
	.payload = s_payload_cmd5_ec,
	.post_wait_us = 0,
};
static const struct xzs_d8m6_cmd s_cmd_dt_on_6 = {
	.name = "CMD6 (B0 03 Protect Lock)",
	.dtype = DSI_GENERIC_LONG_WRITE,
	.dlen = 2,
	.payload = s_payload_cmd6_b0_03,
	.post_wait_us = 0,
};
static const struct xzs_d8m6_cmd s_cmd_dt_on_8 = {
	.name = "CMD8 (MADCTL 0x36 0x00)",
	.dtype = DSI_DCS_LONG_WRITE,
	.dlen = 2,
	.payload = s_payload_cmd8_madctl,
	.post_wait_us = 0,
};
static const struct xzs_d8m6_cmd s_cmd_dt_on_9 = {
	.name = "CMD9 (COLMOD 0x3a 0x77 24bpp)",
	.dtype = DSI_DCS_LONG_WRITE,
	.dlen = 2,
	.payload = s_payload_cmd9_colmod,
	.post_wait_us = 0,
};
static const struct xzs_d8m6_cmd s_cmd_dt_on_10 = {
	.name = "CMD10 (CASET 0x2a 0..1079)",
	.dtype = DSI_DCS_LONG_WRITE,
	.dlen = 5,
	.payload = s_payload_cmd10_caset,
	.post_wait_us = 0,
};
static const struct xzs_d8m6_cmd s_cmd_dt_on_11 = {
	.name = "CMD11 (PASET 0x2b 0..1919)",
	.dtype = DSI_DCS_LONG_WRITE,
	.dlen = 5,
	.payload = s_payload_cmd11_paset,
	.post_wait_us = 0,
};
static const struct xzs_d8m6_cmd s_cmd_dt_on_12 = {
	.name = "CMD12 (STESL 0x44 scanline 0)",
	.dtype = DSI_DCS_LONG_WRITE,
	.dlen = 3,
	.payload = s_payload_cmd12_stesl,
	.post_wait_us = 0,
};

/* Master Command Table */
static const struct xzs_d8m6_cmd s_cmd_slpout = {
	.name = "SLPOUT (Sleep Out 0x11)",
	.dtype = DSI_DCS_LONG_WRITE,
	.dlen = 1,
	.payload = s_payload_slpout,
	.post_wait_us = 120000, /* 120 ms */
};

static const struct xzs_d8m6_cmd s_cmd_teon = {
	.name = "TEON (Tear On 0x35 0x00)",
	.dtype = DSI_DCS_LONG_WRITE,
	.dlen = 2,
	.payload = s_payload_teon,
	.post_wait_us = 0,      /* 0 ms */
};

static const struct xzs_d8m6_cmd s_cmd_dispon = {
	.name = "DISPON (Display On 0x29)",
	.dtype = DSI_DCS_LONG_WRITE,
	.dlen = 1,
	.payload = s_payload_dispon,
	.post_wait_us = 0,      /* 0 ms */
};

static const struct xzs_d8m6_cmd s_cmd_dispoff = {
	.name = "DISPOFF (Display Off 0x28)",
	.dtype = DSI_DCS_SHORT_WRITE_0_PARAM,
	.dlen = 1,
	.payload = s_payload_dispoff,
	.post_wait_us = 0,      /* 0 ms */
};

static const struct xzs_d8m6_cmd s_cmd_slpin = {
	.name = "SLPIN (Sleep In 0x10)",
	.dtype = DSI_DCS_SHORT_WRITE_0_PARAM,
	.dlen = 1,
	.payload = s_payload_slpin,
	.post_wait_us = 120000, /* 120 ms */
};

/* Data Cache Maintenance (Clean to Point of Coherency via ARM64 dc cvac) */
static inline void
xzs_d8m6_clean_dcache(void *addr, size_t size)
{
	uintptr_t start = (uintptr_t)addr & ~(uintptr_t)63;
	uintptr_t end = (uintptr_t)addr + size;
	while (start < end) {
		__asm__ volatile("dc cvac, %0" : : "r" (start) : "memory");
		start += 64;
	}
	__asm__ volatile("dsb sy\n\tisb sy" ::: "memory");
}

/*
 * Packet Memory Layout Encoder
 * Encodes command descriptor into Qualcomm MSM8996 DSI DMA buffer format.
 * Returns packet length in bytes (4-byte aligned), or negative on error.
 */
static int
xzs_d8m6_encode_packet(const struct xzs_d8m6_cmd *cmd, uint8_t *buf, size_t buf_size)
{
	if (!cmd || !buf || buf_size < 16) {
		return -1;
	}

	memset(buf, 0, buf_size);

	if (cmd->dtype == DSI_DCS_SHORT_WRITE_0_PARAM || cmd->dtype == DSI_DCS_SHORT_WRITE_1_PARAM) {
		/*
		 * Short Write Packet Format (4 bytes):
		 *   data[0] = cmd payload[0] (e.g. 0x11)
		 *   data[1] = cmd payload[1] (if 1 param) or 0x00
		 *   data[2] = (vc << 6) | dtype = 0x05
		 *   data[3] = BIT(7) (0x80 = Last packet)
		 */
		buf[0] = cmd->payload[0];
		buf[1] = (cmd->dlen > 1) ? cmd->payload[1] : 0x00;
		buf[2] = cmd->dtype & 0x3fu;
		buf[3] = 0x80u; /* Last packet */
		return 4;
	} else if (cmd->dtype == DSI_DCS_LONG_WRITE || cmd->dtype == DSI_GENERIC_LONG_WRITE) {
		/*
		 * Long Write Packet Format (8+ bytes, 4-byte aligned):
		 *   data[0] = wc & 0xFF
		 *   data[1] = (wc >> 8) & 0xFF
		 *   data[2] = (vc << 6) | dtype = 0x39
		 *   data[3] = BIT(7) | BIT(6) (0xC0 = Last packet + Long packet)
		 *   data[4..4+dlen-1] = payload bytes
		 *   data[aligned padding] = 0xFF
		 */
		uint16_t wc = (uint16_t)cmd->dlen;
		buf[0] = (uint8_t)(wc & 0xFFu);
		buf[1] = (uint8_t)((wc >> 8) & 0xFFu);
		buf[2] = cmd->dtype & 0x3fu;
		buf[3] = 0xC0u; /* Last packet (0x80) | Long packet (0x40) */

		for (uint8_t i = 0; i < cmd->dlen; i++) {
			buf[4 + i] = cmd->payload[i];
		}

		/* Total aligned length */
		int unaligned = 4 + cmd->dlen;
		int aligned = (unaligned + 3) & ~3;
		for (int p = unaligned; p < aligned; p++) {
			buf[p] = 0xFFu;
		}
		return aligned;
	}

	return -2;
}

/* Dump safety counters */
static void
xzs_d8m6_dump_safety_counters(void)
{
	xzs_diag_emit("\n=== [D8-M6] SAFETY COUNTERS ===\n");
	xzs_diag_emit("  SLPOUT_COUNT=");    xzs_d8m5_dec32(g_d8m6_counters.slpout_count);    xzs_diag_emit("\n");
	xzs_diag_emit("  TEON_COUNT=");      xzs_d8m5_dec32(g_d8m6_counters.teon_count);      xzs_diag_emit("\n");
	xzs_diag_emit("  DISPON_COUNT=");    xzs_d8m5_dec32(g_d8m6_counters.dispon_count);    xzs_diag_emit("\n");
	xzs_diag_emit("  DISPOFF_COUNT=");   xzs_d8m5_dec32(g_d8m6_counters.dispoff_count);   xzs_diag_emit("\n");
	xzs_diag_emit("  SLPIN_COUNT=");     xzs_d8m5_dec32(g_d8m6_counters.slpin_count);     xzs_diag_emit("\n");
	xzs_diag_emit("  DMA_TRIGGERS=");    xzs_d8m5_dec32(g_d8m6_counters.dma_triggers);    xzs_diag_emit("\n");
	xzs_diag_emit("  DMA_COMPLETIONS="); xzs_d8m5_dec32(g_d8m6_counters.dma_completions); xzs_diag_emit("\n");
	xzs_diag_emit("  DMA_TIMEOUTS=");    xzs_d8m5_dec32(g_d8m6_counters.dma_timeouts);    xzs_diag_emit("\n");
	xzs_diag_emit("  ACK_ERRORS=");      xzs_d8m5_dec32(g_d8m6_counters.ack_errors);      xzs_diag_emit("\n");
	xzs_diag_emit("  WLED_WRITES=");     xzs_d8m5_dec32(g_d8m6_counters.wled_writes);     xzs_diag_emit(" (EXP: 0)\n");
	xzs_diag_emit("  MDP_KICKOFFS=");    xzs_d8m5_dec32(g_d8m6_counters.mdp_kickoffs);    xzs_diag_emit(" (EXP: 0)\n");
	xzs_diag_emit("===============================\n");
}

/*
 * Transmit a single DSI command packet via MSM8996 DMA Engine
 * Handles encoding, cache flushing, DMA configuration, triggering, completion polling,
 * error validation, and exact hardware post-wait delay.
 */
static int
xzs_d8m6_transmit_cmd(const struct xzs_d8m6_cmd *cmd, int is_dryrun)
{
	if (!cmd) {
		return -1;
	}

	int pkt_len = xzs_d8m6_encode_packet(cmd, s_d8m6_dma_buf, sizeof(s_d8m6_dma_buf));
	if (pkt_len <= 0) {
		xzs_diag_emit("  [D8-M6] ERROR: Packet encoding failed for ");
		xzs_diag_emit(cmd->name);
		xzs_diag_emit("\n");
		return -2;
	}

	uint32_t dma_paddr = (uint32_t)ml_static_vtop((vm_offset_t)&s_d8m6_dma_buf[0]);

	xzs_diag_emit("  [TX-PACKET] ");
	xzs_diag_emit(cmd->name);
	xzs_diag_emit(": len=");
	xzs_d8m5_dec32((uint32_t)pkt_len);
	xzs_diag_emit(" paddr=0x");
	xzs_d8p1_hex32(dma_paddr);
	xzs_diag_emit(" bytes=[ ");
	for (int b = 0; b < pkt_len; b++) {
		xzs_d8p2_hex8(s_d8m6_dma_buf[b]);
		xzs_diag_emit(" ");
	}
	xzs_diag_emit("]\n");

	if (is_dryrun) {
		return 0;
	}

	/* 1. Ensure DSI trigger controls select Software Trigger (dma_cmd_trigger = TRIGGER_SW = 0x4) */
	d8m4_write32(D8M6_REG_DSI_TRIG_CTRL, 0x00000004u);

	/* 2. Ensure dynamic force-on clock bits are enabled in DSI_CLK_CTRL */
	uint32_t orig_clk = d8m4_read32(D8M6_REG_DSI_CLK_CTRL);
	d8m4_write32(D8M6_REG_DSI_CLK_CTRL, orig_clk | (1u << 8) | (1u << 9) | (1u << 11) | (1u << 21));

	/* 3. Reset TPG DMA FIFO before loading */
	d8m4_write32(D8M6_REG_DSI_TEST_PATTERN_GEN_CTRL, 0);
	d8m4_write32(D8M6_REG_DSI_TPG_DMA_FIFO_RESET, 1);
	xzs_d8p2_delay_us(5);
	d8m4_write32(D8M6_REG_DSI_TPG_DMA_FIFO_RESET, 0);

	/* 4. Set CMD_DMA_TPG_EN, TPG_DMA_FIFO_MODE and custom pattern: (BIT(1) | BIT(2) | (0x3 << 16)) */
	d8m4_write32(D8M6_REG_DSI_TEST_PATTERN_GEN_CTRL, (1u << 1) | (1u << 2) | (0x3u << 16));

	/* 5. Load command DWORDs into TPG DMA FIFO */
	const uint32_t *pdw = (const uint32_t *)&s_d8m6_dma_buf[0];
	for (int i = 0; i < pkt_len; i += 4) {
		d8m4_write32(D8M6_REG_DSI_TEST_PATTERN_GEN_CMD_DMA_INIT, *pdw++);
	}
	if (((pkt_len / 4) & 1) != 0) {
		d8m4_write32(D8M6_REG_DSI_TEST_PATTERN_GEN_CMD_DMA_INIT, 0);
	}

	/* 6. Program DMA Controller: Low Power Mode + Embedded Mode */
	uint32_t dma_ctrl_val = D8M6_DMA_CTRL_EMBEDDED_MODE | D8M6_DMA_CTRL_LOW_POWER;
	d8m4_write32(D8M6_REG_DSI_COMMAND_MODE_DMA_CTRL, dma_ctrl_val);

	/* 7. Program DMA Length */
	d8m4_write32(D8M6_REG_DSI_DMA_CMD_LENGTH, (uint32_t)pkt_len & 0x00FFFFFFu);

	/* 8. Ensure DMA_DONE interrupt mask is enabled (bit 1) and previous done is cleared (bit 0 W1C) */
	uint32_t int_ctrl = d8m4_read32(D8M6_REG_DSI_INT_CTRL);
	int_ctrl |= D8M6_INT_CTRL_DMA_DONE_MASK;
	int_ctrl |= D8M6_INT_CTRL_DMA_DONE;
	d8m4_write32(D8M6_REG_DSI_INT_CTRL, int_ctrl);

	/* 9. Memory barrier before trigger */
	__asm__ volatile("dsb sy; isb" ::: "memory");

	/* 10. Trigger DMA Transmission */
	g_d8m6_counters.dma_triggers++;
	uint64_t t_start = xzs_d8m5_read_cntvct();
	d8m4_write32(D8M6_REG_DSI_CMD_MODE_DMA_SW_TRIGGER, D8M6_DMA_SW_TRIGGER_VAL);

	/*
	 * Bounded completion poll loop:
	 * Polls DSI_INT_CTRL bit 0 (DMA_CMD_DONE) with 200 ms timeout.
	 * 200 ms = 3,840,000 cntvct ticks @ 19.2 MHz.
	 */
	uint64_t timeout_ticks = ((uint64_t)200000 * 192ULL) / 10ULL;
	bool completed = false;
	uint32_t isr_status = 0;

	while (1) {
		xzs_watchdog_pet();
		isr_status = d8m4_read32(D8M6_REG_DSI_INT_CTRL);
		if (isr_status & D8M6_INT_CTRL_DMA_DONE) {
			completed = true;
			break;
		}
		uint64_t cur = xzs_d8m5_read_cntvct();
		if ((cur - t_start) >= timeout_ticks) {
			break;
		}
	}

	uint64_t t_end = xzs_d8m5_read_cntvct();
	uint32_t elapsed_us = (uint32_t)(((t_end - t_start) * 10ULL) / 192ULL);

	/* Reset TPG FIFO */
	d8m4_write32(D8M6_REG_DSI_TEST_PATTERN_GEN_CTRL, 0);
	d8m4_write32(D8M6_REG_DSI_TPG_DMA_FIFO_RESET, 1);
	xzs_d8p2_delay_us(5);
	d8m4_write32(D8M6_REG_DSI_TPG_DMA_FIFO_RESET, 0);

	/* Restore clock control */
	d8m4_write32(D8M6_REG_DSI_CLK_CTRL, orig_clk);

	/* Acknowledge/clear completion interrupt (W1C) */
	d8m4_write32(D8M6_REG_DSI_INT_CTRL, d8m4_read32(D8M6_REG_DSI_INT_CTRL) | D8M6_INT_CTRL_DMA_DONE);

	if (!completed) {
		g_d8m6_counters.dma_timeouts++;
		xzs_diag_emit("  !!! [D8-M6] ERROR: DMA Timeout waiting for DMA_CMD_DONE! isr=0x");
		xzs_d8p1_hex32(isr_status);
		xzs_diag_emit(" elapsed_us=");
		xzs_d8m5_dec32(elapsed_us);
		xzs_diag_emit("\n");

		/* Diagnostic register dump */
		uint32_t d_status = d8m4_read32(D8M6_REG_DSI_STATUS);
		uint32_t d_fifo   = d8m4_read32(D8M6_REG_DSI_FIFO_STATUS);
		uint32_t d_lane   = d8m4_read32(D8M6_REG_DSI_LANE_STATUS);
		uint32_t d_clk    = d8m4_read32(0x00994120u); /* DSI_CLK_STATUS */
		uint32_t d_trig   = d8m4_read32(D8M6_REG_DSI_TRIG_CTRL);
		xzs_diag_emit("  [DIAG-DUMP] DSI_STATUS=0x"); xzs_d8p1_hex32(d_status);
		xzs_diag_emit(" FIFO=0x"); xzs_d8p1_hex32(d_fifo);
		xzs_diag_emit(" LANE=0x"); xzs_d8p1_hex32(d_lane);
		xzs_diag_emit(" CLK_ST=0x"); xzs_d8p1_hex32(d_clk);
		xzs_diag_emit(" TRIG=0x"); xzs_d8p1_hex32(d_trig);
		xzs_diag_emit("\n");
		return -3;
	}

	g_d8m6_counters.dma_completions++;

	/* Check error status registers */
	uint32_t ack_err = d8m4_read32(D8M6_REG_DSI_ACK_ERR_STATUS);
	uint32_t to_stat = d8m4_read32(D8M6_REG_DSI_TIMEOUT_STATUS);

	xzs_diag_emit("  [TX-DONE] elapsed_us=");
	xzs_d8m5_dec32(elapsed_us);
	xzs_diag_emit(" ACK_ERR=0x");
	xzs_d8p1_hex32(ack_err);
	xzs_diag_emit(" TIMEOUT=0x");
	xzs_d8p1_hex32(to_stat);
	xzs_diag_emit("\n");

	if (ack_err != 0) {
		g_d8m6_counters.ack_errors++;
		xzs_diag_emit("  !!! [D8-M6] WARNING: DSI ACK Error reported: 0x");
		xzs_d8p1_hex32(ack_err);
		xzs_diag_emit("\n");
	}

	/* Enforce hardware-timed post-wait delay */
	if (cmd->post_wait_us > 0) {
		xzs_diag_emit("  [POST-WAIT] Waiting ");
		xzs_d8m5_dec32(cmd->post_wait_us / 1000);
		xzs_diag_emit(" ms (");
		xzs_d8m5_dec32(cmd->post_wait_us);
		xzs_diag_emit(" us)...\n");
		xzs_d8p2_delay_us(cmd->post_wait_us);
	}

	return 0;
}

/*
 * Helper: Power up panel to idle state using D8-M5 sequence
 * Steps: VDDIO -> LAB -> IBB -> Reset Low 10ms -> Reset High 10ms.
 */
static int
xzs_d8m6_panel_power_up_to_idle(void)
{
	xzs_diag_emit("\n[D8-M6-POWERUP] Powering panel to idle state via M5 sequence:\n");

	/* Prerequisites check */
	uint32_t pll = d8p1_read32(0x009948ccu);
	uint32_t ctrl = d8p1_read32(0x00994004u);
	uint32_t lane = d8p1_read32(0x009940a8u);
	if ((pll & 0x21u) != 0x21u || (ctrl & 0x1u) == 0 || (lane & 0x1f1fu) != 0x1f1fu) {
		xzs_diag_emit("!!! FAIL: Prerequisites unfulfilled. M3/M4 display engine not running.\n");
		return -1;
	}

	if (xzs_spmi_init() != 0) {
		xzs_diag_emit("!!! FAIL: SPMI init failed!\n");
		return -2;
	}
	xzs_d8p2_run(1);

	/* 1. Reset held LOW */
	xzs_diag_emit("  1. Assert RESET LOW (GPIO8=0, GPIO89=0)...\n");
	xzs_d8m5_set_reset_low();
	xzs_d8m5_set_touch_reset_low();
	if (xzs_d8m5_gpio_read_in(GPIO_RESET_NUM) != 0) {
		xzs_diag_emit("!!! FAIL: GPIO8 is HIGH while expecting LOW!\n");
		return -3;
	}

	/* 2. VDDIO ON -> wait 10 ms */
	xzs_diag_emit("  2. Enable VDDIO (GPIO51=1, GPIO50=1)...\n");
	xzs_d8m5_set_vddio_high();
	xzs_d8m5_set_touch_vddio_high();
	xzs_d8p2_delay_us(10000);
	if (xzs_d8m5_gpio_read_in(GPIO_VDDIO_NUM) == 0 || xzs_d8m5_gpio_read_in(GPIO_TOUCH_VDDIO_NUM) == 0) {
		xzs_diag_emit("!!! FAIL: VDDIO enable failed!\n");
		xzs_d8m5_power_down();
		return -4;
	}

	/* 3. LAB ON (+5.6V) -> poll VREG_OK -> wait 10 ms */
	xzs_diag_emit("  3. Enable LAB rail (+5.6V)...\n");
	xzs_spmi_write8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_LAB + LAB_REG_ENABLE_CTL, LAB_ENABLE_CTL_EN);
	g_d8m5_counters.lab_enable_count++;

	bool lab_ok = false;
	uint8_t poll_lab_st = 0;
	for (int poll = 0; poll < 150; poll++) {
		xzs_d8p2_delay_us(1000);
		xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_LAB + LAB_REG_STATUS1, &poll_lab_st);
		if (poll_lab_st & LAB_STATUS1_VREG_OK) {
			lab_ok = true;
			break;
		}
	}
	xzs_diag_emit("  LAB STATUS1=0x"); xzs_d8p2_hex8(poll_lab_st);
	if (!lab_ok) {
		xzs_diag_emit(" -> FAILED: LAB timed out waiting for VREG_OK!\n");
		xzs_d8m5_power_down();
		return -5;
	}
	xzs_diag_emit(" (OK)\n");
	xzs_d8p2_delay_us(10000);

	/* 4. IBB ON (-5.6V) -> poll VREG_OK -> wait 0 ms */
	xzs_diag_emit("  4. Enable IBB rail (-5.6V)...\n");
	xzs_spmi_write8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_IBB + IBB_REG_ENABLE_CTL, IBB_ENABLE_CTL_MODULE_EN);
	g_d8m5_counters.ibb_enable_count++;

	bool ibb_ok = false;
	uint8_t poll_ibb_st = 0;
	for (int poll = 0; poll < 150; poll++) {
		xzs_d8p2_delay_us(1000);
		xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_IBB + IBB_REG_STATUS1, &poll_ibb_st);
		if (poll_ibb_st & IBB_STATUS1_VREG_OK) {
			ibb_ok = true;
			break;
		}
	}
	xzs_diag_emit("  IBB STATUS1=0x"); xzs_d8p2_hex8(poll_ibb_st);
	if (!ibb_ok) {
		xzs_diag_emit(" -> FAILED: IBB timed out waiting for VREG_OK!\n");
		xzs_d8m5_power_down();
		return -6;
	}
	xzs_diag_emit(" (OK)\n");
	xzs_d8p2_delay_us(10000);

	/*
	 * F11 Architectural Parity: qcom,mdss-dsi-lp11-init Lifecycle.
	 * In authentic Sony LK (aboot.img:0xaa03ef4c & 0xaa01fe9c/0xaa01fecc), when lp11-init is set:
	 * 1. panel_power_on() keeps RESET held LOW while rails power up.
	 * 2. mdss_dsi_host_init() actively drives all clock and data lanes into LP-11.
	 * 3. pre_init_func() pulses Panel Reset (GPIO8) and Touch Reset (GPIO89) WHILE IN LP-11!
	 */
	xzs_diag_emit("  5. Actively establishing DSI LP-11 state before reset release...\n");
	d8m4_write32(D8M6_REG_DSI_SOFT_RESET, 1u);
	xzs_d8p2_delay_us(10);
	d8m4_write32(D8M6_REG_DSI_SOFT_RESET, 0u);
	xzs_d8p2_delay_us(10);
	d8m4_write32(D8M6_REG_DSI_CLK_CTRL, 0x0000003fu);
	d8m4_write32(D8M6_REG_DSI_TRIG_CTRL, 0x00000004u);
	d8m4_write32(D8M6_REG_DSI_CTRL, 0x000001f5u);
	/* Sony LK mdss_dsi_panel_initialize: DSI_LANE_CTRL bit 28 = 1 (force_clk_lane_hs) */
	d8m4_write32(0x009940acu, 0x10000000u);
	xzs_d8p2_delay_us(5000); /* 5 ms LP-11 stabilization window */
	xzs_diag_emit("  F11_LP11_ESTABLISHED=YES\n");

	/* 6. Panel Reset Sequence WHILE IN LP-11: Low 10 ms -> High 10 ms */
	xzs_diag_emit("  6. Releasing Panel Reset in LP-11 state (Low 10ms -> High 10ms)...\n");
	xzs_d8m5_set_reset_low();
	xzs_d8p2_delay_us(10000);
	xzs_d8m5_set_reset_high();
	xzs_d8p2_delay_us(10000);

	if (xzs_d8m5_gpio_read_in(GPIO_RESET_NUM) == 0) {
		xzs_diag_emit("!!! FAIL: GPIO8 failed to release HIGH!\n");
		xzs_d8m5_power_down();
		return -7;
	}
	xzs_diag_emit("  RESET_RELEASE_RELATIVE_TO_LP11=AFTER_LP11_ESTABLISHED\n");
	xzs_diag_emit("  F11_RESET_RELEASED_IN_LP11=YES\n");

	/* 7. In-Cell Touch Reset Sequence per somc,ewu-rst-seq = <0 2 1 5> -> LOW 2ms, HIGH 5ms */
	xzs_diag_emit("  7. In-Cell Touch Reset Pulse in LP-11 (Low 2ms -> High 5ms -> settling 40ms)...\n");
	xzs_d8m5_set_touch_reset_low();
	xzs_d8p2_delay_us(2000); // 2 ms
	xzs_d8m5_set_touch_reset_high();
	xzs_d8p2_delay_us(5000); // 5 ms
	/* somc,ewu-wait-after-touch-reset = <0x28> (40 ms) */
	xzs_d8p2_delay_us(40000); // 40 ms

	if (xzs_d8m5_gpio_read_in(GPIO_TOUCH_RESET_NUM) == 0) {
		xzs_diag_emit("!!! FAIL: GPIO89 failed to release HIGH!\n");
		xzs_d8m5_power_down();
		return -8;
	}

	xzs_diag_emit("[D8-M6-POWERUP] SUCCESS: Panel and in-cell touch at powered-idle state (Reset=HIGH, TouchReset=HIGH, LAB=+5.6V, IBB=-5.6V, VDDIO=1.8V, LP11=ACTIVE).\n");
	return 0;
}

/*
 * Helper: Graceful Panel Shutdown (OFF Commands + M5 Physical Shutdown)
 * Sends Display Off (0x28) -> Sleep In (0x10) + 120ms -> M5 physical shutdown.
 */
static int
xzs_d8m6_panel_shutdown(void)
{
	xzs_diag_emit("\n[D8-M6-SHUTDOWN] Executing graceful panel shutdown:\n");

	/* Correct payload for dispoff */
	struct xzs_d8m6_cmd cmd_off = s_cmd_dispoff;
	cmd_off.payload = s_payload_dispoff;

	/* Step 1: Send DCS Display Off (0x28) */
	xzs_diag_emit("  1. Sending DCS Display Off (0x28)...\n");
	int ret1 = xzs_d8m6_transmit_cmd(&cmd_off, 0);
	g_d8m6_counters.dispoff_count++;

	/* Step 2: Send DCS Sleep In (0x10) + 120 ms wait */
	xzs_diag_emit("  2. Sending DCS Sleep In (0x10)...\n");
	int ret2 = xzs_d8m6_transmit_cmd(&s_cmd_slpin, 0);
	g_d8m6_counters.slpin_count++;

	/* Step 3: Proven M5 physical shutdown sequence */
	xzs_diag_emit("  3. Initiating M5 physical shutdown sequence...\n");
	int ret3 = xzs_d8m5_power_down();

	if (ret1 != 0 || ret2 != 0 || ret3 != 0) {
		xzs_diag_emit("[D8-M6-SHUTDOWN] FAILED: Graceful shutdown encountered errors!\n");
		return -1;
	}

	xzs_diag_emit("[D8-M6-SHUTDOWN] SUCCESS: Panel gracefully shut down and settled.\n");
	return 0;
}

/*
 * Sample TE Pin (GPIO10) over ~33 ms (2 frame periods @ 60Hz)
 * Reports transition count and sampled states.
 */
static void
xzs_d8m6_sample_te_pin(void)
{
	xzs_diag_emit("  [TE-MONITOR] Sampling GPIO10 (TE/mdp_vsync) over ~33 ms (2 frames @ 60Hz)...\n");
	uint64_t sample_ticks = ((uint64_t)33333 * 192ULL) / 10ULL;
	uint64_t start = xzs_d8m5_read_cntvct();
	uint32_t transitions = 0;
	uint32_t prev = xzs_d8m5_gpio_read_in(GPIO_TE_NUM);
	uint32_t high_samples = (prev == 1) ? 1 : 0;
	uint32_t total_samples = 1;

	xzs_watchdog_pet();
	while (1) {
		uint32_t cur_val = xzs_d8m5_gpio_read_in(GPIO_TE_NUM);
		total_samples++;
		if (cur_val == 1) {
			high_samples++;
		}
		if (cur_val != prev) {
			transitions++;
			prev = cur_val;
		}
		uint64_t now = xzs_d8m5_read_cntvct();
		if ((now - start) >= sample_ticks) {
			break;
		}
	}

	xzs_diag_emit("  [TE-MONITOR] TRANSITIONS=");
	xzs_d8m5_dec32(transitions);
	xzs_diag_emit(" HIGH_SAMPLES=");
	xzs_d8m5_dec32(high_samples);
	xzs_diag_emit(" TOTAL_SAMPLES=");
	xzs_d8m5_dec32(total_samples);
	xzs_diag_emit(" FINAL_STATE=");
	xzs_diag_emit(prev ? "HIGH" : "LOW");
	xzs_diag_emit("\n");
}

/*
 * M6-A & M6-B: Dryrun Entry Point (display m6-dryrun)
 * Validates memory packet encoder, byte exactness, physical addressing,
 * lower-layer health without performing any MMIO writes.
 */
static int
xzs_d8m6_dryrun(void)
{
	xzs_diag_emit("\n=======================================================\n");
	xzs_diag_emit("=== XZS D8-M6 PANEL VENDOR/DCS DRYRUN VALIDATION    ===\n");
	xzs_diag_emit("=======================================================\n");

	uint8_t test_buf[32] __attribute__((aligned(4)));

	/* Test 1: Sleep Out (0x11, short write) */
	int len1 = xzs_d8m6_encode_packet(&s_cmd_slpout, test_buf, sizeof(test_buf));
	uint32_t w0_1 = *(uint32_t *)test_buf;
	xzs_diag_emit("Command 1 (SLPOUT 0x11): len=");
	xzs_d8m5_dec32((uint32_t)len1);
	xzs_diag_emit(" Word0=0x");
	xzs_d8p1_hex32(w0_1);
	xzs_diag_emit((w0_1 == 0x80050011u && len1 == 4) ? " [EXACT MATCH]\n" : " [MISMATCH!]\n");

	/* Test 2: Tear On (0x35 0x00, long write) */
	int len2 = xzs_d8m6_encode_packet(&s_cmd_teon, test_buf, sizeof(test_buf));
	uint32_t w0_2 = *(uint32_t *)test_buf;
	uint32_t w1_2 = *(uint32_t *)(test_buf + 4);
	xzs_diag_emit("Command 2 (TEON 0x35 0x00): len=");
	xzs_d8m5_dec32((uint32_t)len2);
	xzs_diag_emit(" Word0=0x");
	xzs_d8p1_hex32(w0_2);
	xzs_diag_emit(" Word1=0x");
	xzs_d8p1_hex32(w1_2);
	xzs_diag_emit((w0_2 == 0xC0390002u && w1_2 == 0xFFFF0035u && len2 == 8) ? " [EXACT MATCH]\n" : " [MISMATCH!]\n");

	/* Test 3: Display On (0x29, short write) */
	int len3 = xzs_d8m6_encode_packet(&s_cmd_dispon, test_buf, sizeof(test_buf));
	uint32_t w0_3 = *(uint32_t *)test_buf;
	xzs_diag_emit("Command 3 (DISPON 0x29): len=");
	xzs_d8m5_dec32((uint32_t)len3);
	xzs_diag_emit(" Word0=0x");
	xzs_d8p1_hex32(w0_3);
	xzs_diag_emit((w0_3 == 0x80050029u && len3 == 4) ? " [EXACT MATCH]\n" : " [MISMATCH!]\n");

	/* Test 4: Display Off (0x28, short write) */
	struct xzs_d8m6_cmd cmd_off = s_cmd_dispoff;
	cmd_off.payload = s_payload_dispoff;
	int len4 = xzs_d8m6_encode_packet(&cmd_off, test_buf, sizeof(test_buf));
	uint32_t w0_4 = *(uint32_t *)test_buf;
	xzs_diag_emit("Command 4 (DISPOFF 0x28): len=");
	xzs_d8m5_dec32((uint32_t)len4);
	xzs_diag_emit(" Word0=0x");
	xzs_d8p1_hex32(w0_4);
	xzs_diag_emit((w0_4 == 0x80050028u && len4 == 4) ? " [EXACT MATCH]\n" : " [MISMATCH!]\n");

	/* Test 5: Sleep In (0x10, short write) */
	int len5 = xzs_d8m6_encode_packet(&s_cmd_slpin, test_buf, sizeof(test_buf));
	uint32_t w0_5 = *(uint32_t *)test_buf;
	xzs_diag_emit("Command 5 (SLPIN 0x10): len=");
	xzs_d8m5_dec32((uint32_t)len5);
	xzs_diag_emit(" Word0=0x");
	xzs_d8p1_hex32(w0_5);
	xzs_diag_emit((w0_5 == 0x80050010u && len5 == 4) ? " [EXACT MATCH]\n" : " [MISMATCH!]\n");

	/* Physical address mapping check */
	uint32_t dma_pa = (uint32_t)ml_static_vtop((vm_offset_t)&s_d8m6_dma_buf[0]);
	xzs_diag_emit("\nDMA Buffer Physical Address: 0x");
	xzs_d8p1_hex32(dma_pa);
	xzs_diag_emit(" (Aligned to 64 bytes: ");
	xzs_diag_emit(((dma_pa & 63) == 0) ? "PASS)\n" : "FAIL!)\n");

	/* Lower-layer prerequisites check */
	uint32_t pll  = d8p1_read32(0x009948ccu);
	uint32_t ctrl = d8p1_read32(0x00994004u);
	uint32_t lane = d8p1_read32(0x009940a8u);
	uint32_t fifo = d8p1_read32(0x0099400cu);

	xzs_diag_emit("Prerequisites Check:\n");
	xzs_diag_emit("  PLL_STATUS=0x");  xzs_d8p1_hex32(pll);  xzs_diag_emit(" (exp 0x0000002f)\n");
	xzs_diag_emit("  DSI_CTRL=0x");    xzs_d8p1_hex32(ctrl); xzs_diag_emit(" (exp 0x000001f5)\n");
	xzs_diag_emit("  DSI_LANE=0x");    xzs_d8p1_hex32(lane); xzs_diag_emit(" (exp 0x00001f1f)\n");
	xzs_diag_emit("  DSI_FIFO=0x");    xzs_d8p1_hex32(fifo); xzs_diag_emit(" (exp 0x11111000)\n");

	bool exact = (w0_1 == 0x80050011u && len1 == 4) &&
	             (w0_2 == 0xC0390002u && w1_2 == 0xFFFF0035u && len2 == 8) &&
	             (w0_3 == 0x80050029u && len3 == 4) &&
	             (w0_4 == 0x80050028u && len4 == 4) &&
	             (w0_5 == 0x80050010u && len5 == 4) &&
	             ((dma_pa & 63) == 0);

	xzs_diag_emit("\n[D8-M6] DRYRUN_VERDICT=");
	xzs_diag_emit(exact ? "PASS\n" : "FAIL\n");
	xzs_diag_emit(exact ? "[D8-M6] RESULT=PASS_DRYRUN\n" : "[D8-M6] RESULT=FAIL_DRYRUN\n");
	return exact ? 0 : -1;
}

/*
 * M6-D: Stage 1 Single Command Test (display m6-stage1)
 * Powers panel to idle -> Transmits Sleep Out (0x11) + 120ms -> Graceful shutdown.
 */
static int
xzs_d8m6_stage1(void)
{
	xzs_diag_emit("\n=======================================================\n");
	xzs_diag_emit("=== XZS D8-M6 STAGE 1: SINGLE COMMAND (SLPOUT 0x11) ===\n");
	xzs_diag_emit("=======================================================\n");

	/* Step 1: Power up panel to idle state */
	if (xzs_d8m6_panel_power_up_to_idle() != 0) {
		xzs_diag_emit("!!! [D8-M6] STAGE1 FAILED at panel power-up!\n");
		return -1;
	}

	/* Step 2: Transmit Sleep Out (0x11) */
	xzs_diag_emit("\n[D8-M6-STAGE1] Transmitting DCS Sleep Out (0x11) + 120 ms delay...\n");
	int tx_res = xzs_d8m6_transmit_cmd(&s_cmd_slpout, 0);
	g_d8m6_counters.slpout_count++;

	if (tx_res != 0) {
		xzs_diag_emit("!!! [D8-M6] STAGE1 FAILED: Sleep Out transmission failed!\n");
		xzs_d8m6_panel_shutdown();
		return -2;
	}

	/* Step 3: Verify DSI host state after Sleep Out */
	uint32_t ctrl = d8m4_read32(D8M6_REG_DSI_CTRL);
	uint32_t lane = d8m4_read32(D8M6_REG_DSI_LANE_STATUS);
	uint32_t fifo = d8m4_read32(D8M6_REG_DSI_FIFO_STATUS);
	uint32_t ack  = d8m4_read32(D8M6_REG_DSI_ACK_ERR_STATUS);
	uint32_t to   = d8m4_read32(D8M6_REG_DSI_TIMEOUT_STATUS);

	xzs_diag_emit("\n[STAGE1-VERIFY] DSI Controller State After SLPOUT:\n");
	xzs_diag_emit("  DSI_CTRL=0x");        xzs_d8p1_hex32(ctrl); xzs_diag_emit(" (exp 0x000001f5)\n");
	xzs_diag_emit("  DSI_LANE_STATUS=0x"); xzs_d8p1_hex32(lane); xzs_diag_emit(" (exp 0x00001f1f)\n");
	xzs_diag_emit("  DSI_FIFO_STATUS=0x"); xzs_d8p1_hex32(fifo); xzs_diag_emit(" (exp 0x11111000)\n");
	xzs_diag_emit("  ACK_ERR_STATUS=0x");  xzs_d8p1_hex32(ack);  xzs_diag_emit(" (exp 0x00000000)\n");
	xzs_diag_emit("  TIMEOUT_STATUS=0x");  xzs_d8p1_hex32(to);   xzs_diag_emit(" (exp 0x00000000)\n");

	/* Step 4: Graceful shutdown */
	xzs_d8m6_panel_shutdown();
	xzs_d8m6_dump_safety_counters();

	bool pass = (tx_res == 0 && (ctrl & 1u) != 0 && (lane & 0x1f1fu) == 0x1f1fu && to == 0);
	xzs_diag_emit("\n[D8-M6] STAGE1_VERDICT=");
	xzs_diag_emit(pass ? "PASS\n" : "FAIL\n");
	xzs_diag_emit(pass ? "[D8-M6] RESULT=PASS_STAGE1\n" : "[D8-M6] RESULT=FAIL_STAGE1\n");
	return pass ? 0 : -3;
}

/*
 * M6-E: Stage 2 Prefix Test (display m6-stage2)
 * Powers panel to idle -> Transmits Sleep Out (0x11) + Tear On (0x35 0x00) -> Graceful shutdown.
 */
static int
xzs_d8m6_stage2(void)
{
	xzs_diag_emit("\n=======================================================\n");
	xzs_diag_emit("=== XZS D8-M6 STAGE 2: PREFIX (SLPOUT + TEON)       ===\n");
	xzs_diag_emit("=======================================================\n");

	/* Step 1: Power up panel to idle state */
	if (xzs_d8m6_panel_power_up_to_idle() != 0) {
		xzs_diag_emit("!!! [D8-M6] STAGE2 FAILED at panel power-up!\n");
		return -1;
	}

	/* Step 2: Transmit Sleep Out (0x11) + 120 ms */
	xzs_diag_emit("\n[D8-M6-STAGE2] 1. Transmitting DCS Sleep Out (0x11) + 120 ms...\n");
	int ret1 = xzs_d8m6_transmit_cmd(&s_cmd_slpout, 0);
	g_d8m6_counters.slpout_count++;
	if (ret1 != 0) {
		xzs_diag_emit("!!! [D8-M6] STAGE2 FAILED: SLPOUT failed!\n");
		xzs_d8m6_panel_shutdown();
		return -2;
	}

	/* Step 3: Transmit Tear On (0x35 0x00) */
	xzs_diag_emit("\n[D8-M6-STAGE2] 2. Transmitting DCS Tear On (0x35 0x00)...\n");
	int ret2 = xzs_d8m6_transmit_cmd(&s_cmd_teon, 0);
	g_d8m6_counters.teon_count++;
	if (ret2 != 0) {
		xzs_diag_emit("!!! [D8-M6] STAGE2 FAILED: TEON failed!\n");
		xzs_d8m6_panel_shutdown();
		return -3;
	}

	/* Step 4: Sample TE Pin (GPIO10) */
	xzs_d8m6_sample_te_pin();

	/* Step 5: Graceful shutdown */
	xzs_d8m6_panel_shutdown();
	xzs_d8m6_dump_safety_counters();

	bool pass = (ret1 == 0 && ret2 == 0);
	xzs_diag_emit("\n[D8-M6] STAGE2_VERDICT=");
	xzs_diag_emit(pass ? "PASS\n" : "FAIL\n");
	xzs_diag_emit(pass ? "[D8-M6] RESULT=PASS_STAGE2\n" : "[D8-M6] RESULT=FAIL_STAGE2\n");
	return pass ? 0 : -4;
}

/*
 * M6-F & M6-G: Full Milestone Execution (display m6-run)
 * Complete Sequence:
 *   1. Panel Power-Up & Reset to Idle (M5 sequence)
 *   2. ON Sequence:
 *      - SLPOUT (0x11) + 120 ms
 *      - TEON (0x35 0x00) + 0 ms
 *      - DISPON (0x29) + 0 ms
 *   3. Powered-Active Verification & TE Pin Sampling
 *   4. OFF Sequence:
 *      - DISPOFF (0x28) + 0 ms
 *      - SLPIN (0x10) + 120 ms
 *   5. Physical Shutdown & 300 ms Settling Window (M5 shutdown)
 *   6. Final Safe State & Safety Counter Verification
 */
static int
xzs_d8m6_run(void)
{
	xzs_diag_emit("\n=======================================================\n");
	xzs_diag_emit("=== XZS D8-M6 FULL DCS INITIALIZATION RUN           ===\n");
	xzs_diag_emit("=======================================================\n");

	/* PHASE 1: Panel Power-Up & Reset to Idle */
	xzs_diag_emit("\n[D8-M6-PHASE1] Panel Power-Up & Reset:\n");
	if (xzs_d8m6_panel_power_up_to_idle() != 0) {
		xzs_diag_emit("!!! [D8-M6] RUN FAILED at panel power-up!\n");
		return -1;
	}

	/* PHASE 2: Complete ON Sequence */
	xzs_diag_emit("\n[D8-M6-PHASE2] Transmitting Complete Panel ON Sequence:\n");

	/* 1. Sleep Out (0x11) + 120 ms */
	xzs_diag_emit("  [CMD 1/3] Transmitting DCS Sleep Out (0x11) + 120 ms...\n");
	int r1 = xzs_d8m6_transmit_cmd(&s_cmd_slpout, 0);
	g_d8m6_counters.slpout_count++;
	if (r1 != 0) {
		xzs_diag_emit("!!! [D8-M6] FAILED: SLPOUT transmission failed!\n");
		xzs_d8m6_panel_shutdown();
		return -2;
	}

	/* 2. Tear On (0x35 0x00) */
	xzs_diag_emit("  [CMD 2/3] Transmitting DCS Tear On (0x35 0x00)...\n");
	int r2 = xzs_d8m6_transmit_cmd(&s_cmd_teon, 0);
	g_d8m6_counters.teon_count++;
	if (r2 != 0) {
		xzs_diag_emit("!!! [D8-M6] FAILED: TEON transmission failed!\n");
		xzs_d8m6_panel_shutdown();
		return -3;
	}

	/* 3. Display On (0x29) */
	xzs_diag_emit("  [CMD 3/3] Transmitting DCS Display On (0x29)...\n");
	int r3 = xzs_d8m6_transmit_cmd(&s_cmd_dispon, 0);
	g_d8m6_counters.dispon_count++;
	if (r3 != 0) {
		xzs_diag_emit("!!! [D8-M6] FAILED: DISPON transmission failed!\n");
		xzs_d8m6_panel_shutdown();
		return -4;
	}

	xzs_diag_emit("[D8-M6-PHASE2] SUCCESS: Panel ON sequence completed.\n");

	/* PHASE 3: Powered-Active Verification */
	xzs_diag_emit("\n[D8-M6-PHASE3] Powered-Active Verification:\n");
	uint32_t pll  = d8p1_read32(0x009948ccu);
	uint32_t ctrl = d8p1_read32(D8M6_REG_DSI_CTRL);
	uint32_t lane = d8p1_read32(D8M6_REG_DSI_LANE_STATUS);
	uint32_t fifo = d8p1_read32(D8M6_REG_DSI_FIFO_STATUS);
	uint32_t ack  = d8m4_read32(D8M6_REG_DSI_ACK_ERR_STATUS);
	uint32_t to   = d8m4_read32(D8M6_REG_DSI_TIMEOUT_STATUS);

	xzs_diag_emit("  PLL_STATUS=0x");      xzs_d8p1_hex32(pll);  xzs_diag_emit(" (exp 0x0000002f)\n");
	xzs_diag_emit("  DSI_CTRL=0x");        xzs_d8p1_hex32(ctrl); xzs_diag_emit(" (exp 0x000001f5)\n");
	xzs_diag_emit("  DSI_LANE_STATUS=0x"); xzs_d8p1_hex32(lane); xzs_diag_emit(" (exp 0x00001f1f)\n");
	xzs_diag_emit("  DSI_FIFO_STATUS=0x"); xzs_d8p1_hex32(fifo); xzs_diag_emit(" (exp 0x11111000)\n");
	xzs_diag_emit("  ACK_ERR_STATUS=0x");  xzs_d8p1_hex32(ack);  xzs_diag_emit(" (exp 0x00000000)\n");
	xzs_diag_emit("  TIMEOUT_STATUS=0x");  xzs_d8p1_hex32(to);   xzs_diag_emit(" (exp 0x00000000)\n");

	/* Sample TE Pin (GPIO10) in active state */
	xzs_d8m6_sample_te_pin();

	/* PHASE 4: Graceful Shutdown (OFF Commands + Physical Shutdown) */
	xzs_diag_emit("\n[D8-M6-PHASE4] Graceful Panel Shutdown:\n");
	int r_shut = xzs_d8m6_panel_shutdown();

	/* PHASE 5: Safety Counter & Verdict */
	xzs_d8m6_dump_safety_counters();

	bool pass = (r1 == 0 && r2 == 0 && r3 == 0 && r_shut == 0 &&
	             (pll & 0x21u) == 0x21u && (ctrl & 1u) != 0 && (lane & 0x1f1fu) == 0x1f1fu &&
	             to == 0 && g_d8m6_counters.wled_writes == 0 && g_d8m6_counters.mdp_kickoffs == 0);

	xzs_diag_emit("\n[D8-M6] ACCEPTANCE_VERDICT=");
	xzs_diag_emit(pass ? "PASS\n" : "FAIL\n");
	xzs_diag_emit(pass ? "[D8-M6] RESULT=PASS_ACCEPTANCE\n" : "[D8-M6] RESULT=FAIL_ACCEPTANCE\n");
	return pass ? 0 : -5;
}

#endif /* _XZS_D8M6_H_ */
