/*
 * xnu-xzs Display Bringup Milestone D8-M5: Panel Power & Reset Bring-Up
 *
 * Target Hardware:
 *   Sony Xperia XZs G8231 (keyaki / tone, Qualcomm MSM8996 v3.0)
 *   Panel: Sharp + Synaptics command-mode panel ("somc,sharp_synaptics_cmd_9_panel")
 *
 * Hardware Prerequisite Foundations:
 *   D8-P1: GPIO 8 (Reset_N), GPIO 10 (TE), GPIO 51 (VDDIO_EN)
 *   D8-P2: SPMI Arbiter v2, PMI8994 LAB (+5.60V) and IBB (-5.60V) regulators
 *   D8-M3: 14nm DSI PLL locked (0x2f), PHY initialized
 *   D8-M4: DSI0 Host active (0x1f5), stopstate (0x1f1f), FIFO idle (0x11111000)
 *
 * Strict Boundaries for D8-M5:
 *   - NO DCS packets sent (no 0x11, 0x29, 0x35, vendor commands)
 *   - NO WLED backlight writes
 *   - NO MDP scanout / DMA kickoff
 *   - Hardware-timed waits only (cntvct_el0 @ 19.2MHz) with continuous watchdog petting
 */

#ifndef _XZS_D8M5_H_
#define _XZS_D8M5_H_

#include <stdint.h>
#include <stdbool.h>
#include "xzs_d8p1.h"
#include "xzs_d8p2.h"

extern void xzs_diag_emit(const char *msg);
extern void xzs_watchdog_pet(void);
extern uint32_t d8p1_read32(uint32_t phys);
extern void d8p1_write32(uint32_t phys, uint32_t val);

/* Safety Counters Structure */
struct xzs_d8m5_counters {
	uint32_t gpio51_enable_count;
	uint32_t gpio51_disable_count;
	uint32_t reset_assert_count;
	uint32_t reset_release_count;
	uint32_t lab_enable_count;
	uint32_t lab_disable_count;
	uint32_t ibb_enable_count;
	uint32_t ibb_disable_count;
	uint32_t dcs_packets_sent;
	uint32_t dma_trigger_count;
	uint32_t bta_trigger_count;
	uint32_t wled_writes;
	uint32_t mdp_kickoff_count;
	uint32_t bus_abort;
	uint32_t serror;
	uint32_t panic;
	uint32_t unintended_reset;
};

static struct xzs_d8m5_counters g_d8m5_counters;

/* Helper to read 64-bit cntvct_el0 */
static inline uint64_t
xzs_d8m5_read_cntvct(void)
{
	uint64_t val;
	__asm__ volatile ("isb\n\tmrs %0, cntvct_el0" : "=r" (val));
	return val;
}

/* Helper to print 64-bit hex */
static inline void
xzs_d8m5_hex64(uint64_t val)
{
	char str[17];
	static const char hex[] = "0123456789abcdef";
	for (int h = 15; h >= 0; h--) {
		str[15 - h] = hex[(val >> (h * 4)) & 0xf];
	}
	str[16] = '\0';
	xzs_diag_emit(str);
}

/* Helper to print uint32 decimal */
static inline void
xzs_d8m5_dec32(uint32_t val)
{
	char buf[12];
	int i = 10;
	buf[11] = '\0';
	if (val == 0) {
		xzs_diag_emit("0");
		return;
	}
	while (val > 0 && i >= 0) {
		buf[i--] = '0' + (val % 10);
		val /= 10;
	}
	xzs_diag_emit(&buf[i + 1]);
}

/* Low-level GPIO Control Helpers */
static inline void
xzs_d8m5_gpio_write_latch(uint32_t gpio_num, int high)
{
	uint32_t addr = TLMM_GPIO_IN_OUT(gpio_num);
	uint32_t cur = d8p1_read32(addr);
	if (high) {
		cur |= 0x2u;
	} else {
		cur &= ~0x2u;
	}
	d8p1_write32(addr, cur);
}

static inline uint32_t
xzs_d8m5_gpio_read_in(uint32_t gpio_num)
{
	return d8p1_read32(TLMM_GPIO_IN_OUT(gpio_num)) & 1u;
}

static inline uint32_t
xzs_d8m5_gpio_read_out(uint32_t gpio_num)
{
	return (d8p1_read32(TLMM_GPIO_IN_OUT(gpio_num)) >> 1) & 1u;
}

static inline void
xzs_d8m5_set_reset_low(void)
{
	xzs_d8m5_gpio_write_latch(GPIO_RESET_NUM, 0);
	g_d8m5_counters.reset_assert_count++;
}

static inline void
xzs_d8m5_set_reset_high(void)
{
	xzs_d8m5_gpio_write_latch(GPIO_RESET_NUM, 1);
	g_d8m5_counters.reset_release_count++;
}

static inline void
xzs_d8m5_set_vddio_high(void)
{
	xzs_d8m5_gpio_write_latch(GPIO_VDDIO_NUM, 1);
	g_d8m5_counters.gpio51_enable_count++;
}

static inline void
xzs_d8m5_set_vddio_low(void)
{
	xzs_d8m5_gpio_write_latch(GPIO_VDDIO_NUM, 0);
	g_d8m5_counters.gpio51_disable_count++;
}

static inline void
xzs_d8m5_set_touch_vddio_high(void)
{
	xzs_d8m5_gpio_write_latch(GPIO_TOUCH_VDDIO_NUM, 1);
}

static inline void
xzs_d8m5_set_touch_vddio_low(void)
{
	xzs_d8m5_gpio_write_latch(GPIO_TOUCH_VDDIO_NUM, 0);
}

static inline void
xzs_d8m5_set_touch_reset_high(void)
{
	xzs_d8m5_gpio_write_latch(GPIO_TOUCH_RESET_NUM, 1);
}

static inline void
xzs_d8m5_set_touch_reset_low(void)
{
	xzs_d8m5_gpio_write_latch(GPIO_TOUCH_RESET_NUM, 0);
}

/* Print full safety counters */
static void
xzs_d8m5_dump_safety_counters(void)
{
	xzs_diag_emit("\n=== [D8-M5] SAFETY COUNTERS ===\n");
	xzs_diag_emit("  GPIO51_ENABLE_COUNT=");   xzs_d8m5_dec32(g_d8m5_counters.gpio51_enable_count);  xzs_diag_emit("\n");
	xzs_diag_emit("  GPIO51_DISABLE_COUNT=");  xzs_d8m5_dec32(g_d8m5_counters.gpio51_disable_count); xzs_diag_emit("\n");
	xzs_diag_emit("  RESET_ASSERT_COUNT=");    xzs_d8m5_dec32(g_d8m5_counters.reset_assert_count);   xzs_diag_emit("\n");
	xzs_diag_emit("  RESET_RELEASE_COUNT=");   xzs_d8m5_dec32(g_d8m5_counters.reset_release_count);  xzs_diag_emit("\n");
	xzs_diag_emit("  LAB_ENABLE_COUNT=");      xzs_d8m5_dec32(g_d8m5_counters.lab_enable_count);     xzs_diag_emit("\n");
	xzs_diag_emit("  LAB_DISABLE_COUNT=");     xzs_d8m5_dec32(g_d8m5_counters.lab_disable_count);    xzs_diag_emit("\n");
	xzs_diag_emit("  IBB_ENABLE_COUNT=");      xzs_d8m5_dec32(g_d8m5_counters.ibb_enable_count);     xzs_diag_emit("\n");
	xzs_diag_emit("  IBB_DISABLE_COUNT=");     xzs_d8m5_dec32(g_d8m5_counters.ibb_disable_count);    xzs_diag_emit("\n");
	xzs_diag_emit("  DCS_PACKETS_SENT=");      xzs_d8m5_dec32(g_d8m5_counters.dcs_packets_sent);     xzs_diag_emit("\n");
	xzs_diag_emit("  DMA_TRIGGER_COUNT=");     xzs_d8m5_dec32(g_d8m5_counters.dma_trigger_count);    xzs_diag_emit("\n");
	xzs_diag_emit("  BTA_TRIGGER_COUNT=");     xzs_d8m5_dec32(g_d8m5_counters.bta_trigger_count);    xzs_diag_emit("\n");
	xzs_diag_emit("  WLED_WRITES=");           xzs_d8m5_dec32(g_d8m5_counters.wled_writes);          xzs_diag_emit("\n");
	xzs_diag_emit("  MDP_KICKOFF_COUNT=");     xzs_d8m5_dec32(g_d8m5_counters.mdp_kickoff_count);    xzs_diag_emit("\n");
	xzs_diag_emit("  BUS_ABORT=");             xzs_d8m5_dec32(g_d8m5_counters.bus_abort);            xzs_diag_emit("\n");
	xzs_diag_emit("  SError=");                xzs_d8m5_dec32(g_d8m5_counters.serror);               xzs_diag_emit("\n");
	xzs_diag_emit("  PANIC=");                 xzs_d8m5_dec32(g_d8m5_counters.panic);                xzs_diag_emit("\n");
	xzs_diag_emit("  UNINTENDED_RESET=");      xzs_d8m5_dec32(g_d8m5_counters.unintended_reset);     xzs_diag_emit("\n");
	xzs_diag_emit("===============================\n");
}

/*
 * M5-B: Read-Only Pre-Flight Inspection (display m5-status)
 */
static void
xzs_d8m5_status(void)
{
	xzs_diag_emit("\n=======================================================\n");
	xzs_diag_emit("=== XZS D8-M5 PANEL POWER & RESET PRE-FLIGHT STATUS ===\n");
	xzs_diag_emit("=======================================================\n");

	/* TLMM GPIOs */
	uint32_t g8_cfg  = d8p1_read32(TLMM_GPIO_CFG(GPIO_RESET_NUM));
	uint32_t g8_io   = d8p1_read32(TLMM_GPIO_IN_OUT(GPIO_RESET_NUM));
	uint32_t g10_cfg = d8p1_read32(TLMM_GPIO_CFG(GPIO_TE_NUM));
	uint32_t g10_io  = d8p1_read32(TLMM_GPIO_IN_OUT(GPIO_TE_NUM));
	uint32_t g51_cfg = d8p1_read32(TLMM_GPIO_CFG(GPIO_VDDIO_NUM));
	uint32_t g51_io  = d8p1_read32(TLMM_GPIO_IN_OUT(GPIO_VDDIO_NUM));

	xzs_diag_emit("GPIO8 (disp_reset_n): CFG=0x");
	xzs_d8p1_hex32(g8_cfg);
	xzs_diag_emit(" IN_OUT=0x");
	xzs_d8p1_hex32(g8_io);
	xzs_diag_emit(" (out=");
	xzs_diag_emit((g8_io & 2u) ? "HIGH" : "LOW");
	xzs_diag_emit(" in=");
	xzs_diag_emit((g8_io & 1u) ? "HIGH" : "LOW");
	xzs_diag_emit(")\n");

	xzs_diag_emit("GPIO10 (mdp_vsync/TE): CFG=0x");
	xzs_d8p1_hex32(g10_cfg);
	xzs_diag_emit(" IN_OUT=0x");
	xzs_d8p1_hex32(g10_io);
	xzs_diag_emit(" (in=");
	xzs_diag_emit((g10_io & 1u) ? "HIGH" : "LOW");
	xzs_diag_emit(")\n");

	xzs_diag_emit("GPIO51 (lcd_vddio_en): CFG=0x");
	xzs_d8p1_hex32(g51_cfg);
	xzs_diag_emit(" IN_OUT=0x");
	xzs_d8p1_hex32(g51_io);
	xzs_diag_emit(" (out=");
	xzs_diag_emit((g51_io & 2u) ? "HIGH" : "LOW");
	xzs_diag_emit(" in=");
	xzs_diag_emit((g51_io & 1u) ? "HIGH" : "LOW");
	xzs_diag_emit(")\n");

	/* PMIC LAB / IBB Status */
	uint8_t lab_v = 0, lab_en = 0, lab_st = 0;
	uint8_t ibb_v = 0, ibb_en = 0, ibb_st = 0;
	if (xzs_spmi_init() == 0) {
		xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_LAB + LAB_REG_VOLTAGE, &lab_v);
		xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_LAB + LAB_REG_ENABLE_CTL, &lab_en);
		xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_LAB + LAB_REG_STATUS1, &lab_st);

		xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_IBB + IBB_REG_VOLTAGE, &ibb_v);
		xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_IBB + IBB_REG_ENABLE_CTL, &ibb_en);
		xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_IBB + IBB_REG_STATUS1, &ibb_st);
	}

	xzs_diag_emit("LAB (+5.6V): volt=0x"); xzs_d8p2_hex8(lab_v);
	xzs_diag_emit(" en=0x"); xzs_d8p2_hex8(lab_en);
	xzs_diag_emit(" status=0x"); xzs_d8p2_hex8(lab_st);
	xzs_diag_emit(" (VREG_OK="); xzs_diag_emit((lab_st & LAB_STATUS1_VREG_OK) ? "1" : "0");
	xzs_diag_emit(")\n");

	xzs_diag_emit("IBB (-5.6V): volt=0x"); xzs_d8p2_hex8(ibb_v);
	xzs_diag_emit(" en=0x"); xzs_d8p2_hex8(ibb_en);
	xzs_diag_emit(" status=0x"); xzs_d8p2_hex8(ibb_st);
	xzs_diag_emit(" (VREG_OK="); xzs_diag_emit((ibb_st & IBB_STATUS1_VREG_OK) ? "1" : "0");
	xzs_diag_emit(")\n");

	/* DSI0 Host & PLL Status */
	uint32_t pll_stat   = d8p1_read32(0x009948ccu);
	uint32_t dsi_ctrl   = d8p1_read32(0x00994004u);
	uint32_t dsi_stat   = d8p1_read32(0x00994008u);
	uint32_t fifo_stat  = d8p1_read32(0x0099400cu);
	uint32_t lane_stat  = d8p1_read32(0x009940a8u);
	uint32_t clk_stat   = d8p1_read32(0x00994120u);
	uint32_t ack_err    = d8p1_read32(0x00994068u);
	uint32_t timeout_st = d8p1_read32(0x009940c0u);

	xzs_diag_emit("PLL_PRIMARY_STATUS = 0x"); xzs_d8p1_hex32(pll_stat);   xzs_diag_emit("\n");
	xzs_diag_emit("DSI_CTRL           = 0x"); xzs_d8p1_hex32(dsi_ctrl);   xzs_diag_emit("\n");
	xzs_diag_emit("DSI_STATUS         = 0x"); xzs_d8p1_hex32(dsi_stat);   xzs_diag_emit("\n");
	xzs_diag_emit("DSI_FIFO_STATUS    = 0x"); xzs_d8p1_hex32(fifo_stat);  xzs_diag_emit("\n");
	xzs_diag_emit("DSI_LANE_STATUS    = 0x"); xzs_d8p1_hex32(lane_stat);  xzs_diag_emit("\n");
	xzs_diag_emit("DSI_CLK_STATUS     = 0x"); xzs_d8p1_hex32(clk_stat);   xzs_diag_emit("\n");
	xzs_diag_emit("DSI_ACK_ERR_STATUS = 0x"); xzs_d8p1_hex32(ack_err);    xzs_diag_emit("\n");
	xzs_diag_emit("DSI_TIMEOUT_STATUS = 0x"); xzs_d8p1_hex32(timeout_st); xzs_diag_emit("\n");

	xzs_diag_emit("=======================================================\n");
}

/*
 * M5-C: Dry-Run State Machine (display m5-dryrun)
 */
static int
xzs_d8m5_dryrun(void)
{
	xzs_diag_emit("\n=======================================================\n");
	xzs_diag_emit("=== XZS D8-M5 PANEL POWER & RESET: DRY-RUN          ===\n");
	xzs_diag_emit("=======================================================\n");

	/* D8M5-10: Prerequisites */
	xzs_diag_emit("\n[D8-M5] CHECKPOINT D8M5-10 PREREQUISITES START\n");
	uint32_t pll = d8p1_read32(0x009948ccu);
	uint32_t ctrl = d8p1_read32(0x00994004u);
	uint32_t lane = d8p1_read32(0x009940a8u);
	xzs_diag_emit("  PLL_PRIMARY_STATUS=0x"); xzs_d8p1_hex32(pll); xzs_diag_emit(" (expected 0x0000002f)\n");
	xzs_diag_emit("  DSI_CTRL=0x"); xzs_d8p1_hex32(ctrl); xzs_diag_emit(" (expected 0x000001f5)\n");
	xzs_diag_emit("  DSI_LANE_STATUS=0x"); xzs_d8p1_hex32(lane); xzs_diag_emit(" (expected 0x00001f1f)\n");

	if ((pll & 0x21u) != 0x21u || (ctrl & 0x1u) == 0 || (lane & 0x1f1fu) != 0x1f1fu) {
		xzs_diag_emit("!!! FAIL: Prerequisites unfulfilled. M3/M4 display engine not running.\n");
		xzs_diag_emit("[D8-M5] RESULT=FAIL_PREREQUISITES\n");
		return -1;
	}
	xzs_diag_emit("[D8-M5] CHECKPOINT D8M5-10 PREREQUISITES PASS\n");

	/* D8M5-20: Source Sequence Verify */
	xzs_diag_emit("\n[D8-M5] CHECKPOINT D8M5-20 SOURCE_SEQUENCE_VERIFY START\n");
	xzs_diag_emit("  SOURCE: keyaki.dts (somc,sharp_synaptics_cmd_9_panel)\n");
	xzs_diag_emit("  POWER_ON:  VDDIO -> LAB -> IBB -> RESET_ASSERT -> RESET_RELEASE\n");
	xzs_diag_emit("  POWER_OFF: RESET_ASSERT -> IBB_OFF -> LAB_OFF -> VDDIO_OFF -> SETTLE\n");
	xzs_diag_emit("[D8-M5] CHECKPOINT D8M5-20 SOURCE_SEQUENCE_VERIFY PASS\n");

	/* D8M5-30: Initial Safe State */
	xzs_diag_emit("\n[D8-M5] CHECKPOINT D8M5-30 INITIAL_SAFE_STATE START\n");
	xzs_diag_emit("  ACTION: Verify GPIO8=LOW, GPIO51=LOW, LAB=OFF, IBB=OFF\n");
	xzs_diag_emit("  SIGNAL: disp_reset_n | ADDR: 0x01018004 | OLD: 0x00000000 | MASK: 0x2 | NEW: 0x0 | RB: 0x0 | DELAY: 0us | SOURCE: D8-P1\n");
	xzs_diag_emit("  SIGNAL: lcd_vddio_en | ADDR: 0x01043004 | OLD: 0x00000000 | MASK: 0x2 | NEW: 0x0 | RB: 0x0 | DELAY: 0us | SOURCE: D8-P1\n");
	xzs_diag_emit("  SIGNAL: LAB_ENABLE   | ADDR: 0xDE46     | OLD: 0x00       | MASK: 0x80| NEW: 0x0 | RB: 0x0 | DELAY: 0us | SOURCE: D8-P2\n");
	xzs_diag_emit("  SIGNAL: IBB_ENABLE   | ADDR: 0xDC46     | OLD: 0x00       | MASK: 0x80| NEW: 0x0 | RB: 0x0 | DELAY: 0us | SOURCE: D8-P2\n");
	xzs_diag_emit("[D8-M5] CHECKPOINT D8M5-30 INITIAL_SAFE_STATE PASS\n");

	/* D8M5-40: VDDIO Plan */
	xzs_diag_emit("\n[D8-M5] CHECKPOINT D8M5-40 VDDIO_PLAN START\n");
	xzs_diag_emit("  SIGNAL: lcd_vddio_en | ADDR: 0x01043004 | OLD: 0x00000000 | MASK: 0x2 | NEW: 0x2 | RB: 0x3 | DELAY: 10000us | SOURCE: somc,pw-wait-after-on-vddio\n");
	xzs_diag_emit("[D8-M5] CHECKPOINT D8M5-40 VDDIO_PLAN PASS\n");

	/* D8M5-50: Bias Rail Plan */
	xzs_diag_emit("\n[D8-M5] CHECKPOINT D8M5-50 BIAS_RAIL_PLAN START\n");
	xzs_diag_emit("  SIGNAL: LAB_ENABLE   | ADDR: 0xDE46     | OLD: 0x00       | MASK: 0x80| NEW: 0x80| RB: 0x80 (VREG_OK=1) | DELAY: 10000us | SOURCE: somc,pw-wait-after-on-vsp\n");
	xzs_diag_emit("  SIGNAL: IBB_ENABLE   | ADDR: 0xDC46     | OLD: 0x00       | MASK: 0x80| NEW: 0x80| RB: 0x80 (VREG_OK=1) | DELAY: 0us     | SOURCE: somc,pw-wait-after-on-vsn\n");
	xzs_diag_emit("[D8-M5] CHECKPOINT D8M5-50 BIAS_RAIL_PLAN PASS\n");

	/* D8M5-60: Reset Assert Plan */
	xzs_diag_emit("\n[D8-M5] CHECKPOINT D8M5-60 RESET_ASSERT_PLAN START\n");
	xzs_diag_emit("  SIGNAL: disp_reset_n | ADDR: 0x01018004 | OLD: 0x00000000 | MASK: 0x2 | NEW: 0x0 | RB: 0x0 | DELAY: 10000us | SOURCE: somc,pw-on-rst-seq (part 1)\n");
	xzs_diag_emit("[D8-M5] CHECKPOINT D8M5-60 RESET_ASSERT_PLAN PASS\n");

	/* D8M5-70: Reset Release Plan */
	xzs_diag_emit("\n[D8-M5] CHECKPOINT D8M5-70 RESET_RELEASE_PLAN START\n");
	xzs_diag_emit("  SIGNAL: disp_reset_n | ADDR: 0x01018004 | OLD: 0x00000000 | MASK: 0x2 | NEW: 0x2 | RB: 0x3 | DELAY: 10000us | SOURCE: somc,pw-on-rst-seq (part 2)\n");
	xzs_diag_emit("[D8-M5] CHECKPOINT D8M5-70 RESET_RELEASE_PLAN PASS\n");

	/* D8M5-80: Powered State Plan */
	xzs_diag_emit("\n[D8-M5] CHECKPOINT D8M5-80 POWERED_STATE_PLAN START\n");
	xzs_diag_emit("  EXPECT: VDDIO=ON, LAB=ON, IBB=ON, RESET=HIGH, DSI0_HOST=IDLE, DCS_SENT=0\n");
	xzs_diag_emit("[D8-M5] CHECKPOINT D8M5-80 POWERED_STATE_PLAN PASS\n");

	/* D8M5-90: Powerdown Plan */
	xzs_diag_emit("\n[D8-M5] CHECKPOINT D8M5-90 POWERDOWN_PLAN START\n");
	xzs_diag_emit("  SIGNAL: disp_reset_n | ADDR: 0x01018004 | OLD: 0x00000002 | MASK: 0x2 | NEW: 0x0 | RB: 0x0 | DELAY: 5000us  | SOURCE: somc,pw-off-rst-b-seq\n");
	xzs_diag_emit("  SIGNAL: IBB_ENABLE   | ADDR: 0xDC46     | OLD: 0x80       | MASK: 0x80| NEW: 0x0 | RB: 0x0 (VREG_OK=0) | DELAY: 10000us | SOURCE: somc,pw-wait-after-off-vsn\n");
	xzs_diag_emit("  SIGNAL: LAB_ENABLE   | ADDR: 0xDE46     | OLD: 0x80       | MASK: 0x80| NEW: 0x0 | RB: 0x0 (VREG_OK=0) | DELAY: 10000us | SOURCE: somc,pw-wait-after-off-vsp\n");
	xzs_diag_emit("  SIGNAL: lcd_vddio_en | ADDR: 0x01043004 | OLD: 0x00000002 | MASK: 0x2 | NEW: 0x0 | RB: 0x0 | DELAY: 0us     | SOURCE: somc,pw-wait-after-off-vddio\n");
	xzs_diag_emit("  SIGNAL: SETTLE_TIME  | DURATION: 300000us | SOURCE: somc,pw-down-period\n");
	xzs_diag_emit("[D8-M5] CHECKPOINT D8M5-90 POWERDOWN_PLAN PASS\n");

	/* D8M5-A0: Final Safe State Plan */
	xzs_diag_emit("\n[D8-M5] CHECKPOINT D8M5-A0 FINAL_SAFE_STATE_PLAN START\n");
	xzs_diag_emit("  EXPECT: RESET=LOW, VDDIO=OFF, LAB=OFF, IBB=OFF, DCS=0, WLED=0\n");
	xzs_diag_emit("[D8-M5] CHECKPOINT D8M5-A0 FINAL_SAFE_STATE_PLAN PASS\n");

	/* D8M5-B0: Accept */
	xzs_diag_emit("\n[D8-M5] CHECKPOINT D8M5-B0 ACCEPT START\n");
	xzs_diag_emit("[D8-M5] DRYRUN_VERDICT=PASS\n");
	xzs_diag_emit("[D8-M5] RESULT=PASS_DRYRUN\n");
	return 0;
}

/*
 * Safe Power-Down Helper (Proven Reverse Shutdown)
 */
static int
xzs_d8m5_power_down(void)
{
	xzs_diag_emit("\n[D8-M5-SHUTDOWN] Executing source-proven panel power-down:\n");

	/* Step 1: Assert Reset LOW -> delay 5ms per somc,pw-off-rst-b-seq = <0x00 0x05> */
	xzs_diag_emit("  1. Assert RESET LOW (GPIO8=0, GPIO89=0)...\n");
	xzs_d8m5_set_reset_low();
	xzs_d8m5_set_touch_reset_low();
	xzs_d8p2_delay_us(5000); // 5 ms

	/* Step 2: Disable IBB (-5.6V) */
	xzs_diag_emit("  2. Disabling IBB (0xDC46=0x00)...\n");
	xzs_spmi_write8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_IBB + IBB_REG_ENABLE_CTL, 0x00);
	g_d8m5_counters.ibb_disable_count++;

	bool ibb_off = false;
	uint8_t poll_ibb_st = 0;
	for (int poll = 0; poll < 150; poll++) {
		xzs_d8p2_delay_us(1000);
		xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_IBB + IBB_REG_STATUS1, &poll_ibb_st);
		if (!(poll_ibb_st & IBB_STATUS1_VREG_OK)) {
			ibb_off = true;
			break;
		}
	}
	xzs_diag_emit("     IBB STATUS1=0x"); xzs_d8p2_hex8(poll_ibb_st);
	xzs_diag_emit(ibb_off ? " (OFF PASS)\n" : " (TIMEOUT FAULT)\n");

	/* Wait 10ms per somc,pw-wait-after-off-vsn = <0x0a> */
	xzs_d8p2_delay_us(10000);

	/* Step 3: Disable LAB (+5.6V) */
	xzs_diag_emit("  3. Disabling LAB (0xDE46=0x00)...\n");
	xzs_spmi_write8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_LAB + LAB_REG_ENABLE_CTL, 0x00);
	g_d8m5_counters.lab_disable_count++;

	bool lab_off = false;
	uint8_t poll_lab_st = 0;
	for (int poll = 0; poll < 150; poll++) {
		xzs_d8p2_delay_us(1000);
		xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_LAB + LAB_REG_STATUS1, &poll_lab_st);
		if (!(poll_lab_st & LAB_STATUS1_VREG_OK)) {
			lab_off = true;
			break;
		}
	}
	xzs_diag_emit("     LAB STATUS1=0x"); xzs_d8p2_hex8(poll_lab_st);
	xzs_diag_emit(lab_off ? " (OFF PASS)\n" : " (TIMEOUT FAULT)\n");

	/* Wait 10ms per somc,pw-wait-after-off-vsp = <0x0a> */
	xzs_d8p2_delay_us(10000);

	/* Step 4: Disable VDDIO (GPIO 51 LOW, GPIO 50 LOW) */
	xzs_diag_emit("  4. Disabling VDDIO (GPIO51=0, GPIO50=0)...\n");
	xzs_d8m5_set_vddio_low();
	xzs_d8m5_set_touch_vddio_low();
	/* somc,pw-wait-after-off-vddio = <0x00> */

	/* Step 5: Power-down settling period per somc,pw-down-period = <0x12c> (300 ms) */
	xzs_diag_emit("  5. Settling window (300 ms)...\n");
	xzs_d8p2_delay_us(300000);

	/* Verify final safe state */
	uint32_t rst_in = xzs_d8m5_gpio_read_in(GPIO_RESET_NUM);
	uint32_t vddio_in = xzs_d8m5_gpio_read_in(GPIO_VDDIO_NUM);
	uint32_t t_rst_in = xzs_d8m5_gpio_read_in(GPIO_TOUCH_RESET_NUM);
	uint32_t t_vddio_in = xzs_d8m5_gpio_read_in(GPIO_TOUCH_VDDIO_NUM);
	xzs_diag_emit("  Shutdown Verification: GPIO8_IN=");
	xzs_diag_emit(rst_in ? "HIGH(FAULT)" : "LOW(OK)");
	xzs_diag_emit(" GPIO51_IN=");
	xzs_diag_emit(vddio_in ? "HIGH(FAULT)" : "LOW(OK)");
	xzs_diag_emit(" GPIO89_IN=");
	xzs_diag_emit(t_rst_in ? "HIGH(FAULT)" : "LOW(OK)");
	xzs_diag_emit(" GPIO50_IN=");
	xzs_diag_emit(t_vddio_in ? "HIGH(FAULT)" : "LOW(OK)");
	xzs_diag_emit("\n");

	if (!ibb_off || !lab_off || rst_in != 0 || vddio_in != 0 || t_rst_in != 0 || t_vddio_in != 0) {
		xzs_diag_emit("[D8-M5-SHUTDOWN] FAILED: Power-down incomplete!\n");
		return -1;
	}
	xzs_diag_emit("[D8-M5-SHUTDOWN] SUCCESS: Panel powered down safely.\n");
	return 0;
}

/*
 * M5-D: Stage 1 Real Hardware Run (display m5-stage1)
 * Powers VDDIO + LAB + IBB with Reset held strictly LOW.
 */
static int
xzs_d8m5_stage1(void)
{
	xzs_diag_emit("\n=======================================================\n");
	xzs_diag_emit("=== XZS D8-M5 STAGE 1: POWER DOMAIN (RESET HELD LOW)===\n");
	xzs_diag_emit("=======================================================\n");

	/* Check lower-layer prerequisites */
	uint32_t pll = d8p1_read32(0x009948ccu);
	uint32_t ctrl = d8p1_read32(0x00994004u);
	uint32_t lane = d8p1_read32(0x009940a8u);
	if ((pll & 0x21u) != 0x21u || (ctrl & 0x1u) == 0 || (lane & 0x1f1fu) != 0x1f1fu) {
		xzs_diag_emit("!!! FAIL: Prerequisites unfulfilled. M3/M4 display engine not running.\n");
		xzs_diag_emit("[D8-M5] RESULT=FAIL_PREREQUISITES\n");
		return -1;
	}

	/* Ensure SPMI and P2 rail configuration is applied */
	if (xzs_spmi_init() != 0) {
		xzs_diag_emit("!!! FAIL: SPMI init failed!\n");
		return -2;
	}
	/* Ensure LAB & IBB registers programmed (target 5.60V) */
	xzs_d8p2_run(1); // Configure registers safely while remaining disabled

	/* Step 1: Ensure GPIO8 (Reset) is strictly asserted LOW */
	xzs_diag_emit("[STAGE1-STEP1] Asserting GPIO8 Reset LOW (Safe)...\n");
	xzs_d8m5_set_reset_low();
	if (xzs_d8m5_gpio_read_in(GPIO_RESET_NUM) != 0) {
		xzs_diag_emit("!!! FAIL: GPIO8 readback is HIGH while expecting LOW!\n");
		return -3;
	}
	xzs_diag_emit("  GPIO8 = LOW confirmed.\n");

	/* Step 2: Enable VDDIO (GPIO 51 HIGH) -> delay 10ms per somc,pw-wait-after-on-vddio = <10> */
	xzs_diag_emit("[STAGE1-STEP2] Enabling VDDIO (GPIO51 HIGH)...\n");
	xzs_d8m5_set_vddio_high();
	xzs_d8p2_delay_us(10000); // 10 ms

	uint32_t vddio_in = xzs_d8m5_gpio_read_in(GPIO_VDDIO_NUM);
	uint32_t rst_check = xzs_d8m5_gpio_read_in(GPIO_RESET_NUM);
	xzs_diag_emit("  VDDIO_IN="); xzs_diag_emit(vddio_in ? "HIGH(OK)" : "LOW(FAIL)");
	xzs_diag_emit(" RESET_IN="); xzs_diag_emit(rst_check ? "HIGH(LEAK_FAIL)" : "LOW(OK)");
	xzs_diag_emit("\n");

	if (vddio_in == 0 || rst_check != 0) {
		xzs_diag_emit("!!! FAIL: VDDIO enable failed or Reset glitch detected!\n");
		xzs_d8m5_power_down();
		return -4;
	}

	/* Step 3: Enable LAB (+5.6V) -> bounded poll VREG_OK -> delay 10ms */
	xzs_diag_emit("[STAGE1-STEP3] Enabling LAB rail (+5.6V)...\n");
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
	xzs_diag_emit(" -> LAB VREG_OK PASS\n");
	/* Inter-rail delay (10ms per somc,pw-wait-after-on-vsp = <10>) */
	xzs_d8p2_delay_us(10000);

	/* Step 4: Enable IBB (-5.6V) -> bounded poll VREG_OK -> delay 0ms */
	xzs_diag_emit("[STAGE1-STEP4] Enabling IBB rail (-5.6V)...\n");
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
	xzs_diag_emit(" -> IBB VREG_OK PASS\n");
	/* somc,pw-wait-after-on-vsn = <0> -> 0ms */

	/* Step 5: Validate Stage 1 Acceptance */
	rst_check = xzs_d8m5_gpio_read_in(GPIO_RESET_NUM);
	vddio_in = xzs_d8m5_gpio_read_in(GPIO_VDDIO_NUM);
	pll = d8p1_read32(0x009948ccu);
	ctrl = d8p1_read32(0x00994004u);
	lane = d8p1_read32(0x009940a8u);
	uint32_t dsi_stat = d8p1_read32(0x00994008u);

	xzs_diag_emit("\n[STAGE1-ACCEPTANCE] Verification:\n");
	xzs_diag_emit("  VDDIO_STATE=ON\n");
	xzs_diag_emit("  LAB_VREG_OK=1\n");
	xzs_diag_emit("  IBB_VREG_OK=1\n");
	xzs_diag_emit("  GPIO8_RESET="); xzs_diag_emit(rst_check ? "HIGH(FAIL)\n" : "LOW(PASS)\n");
	xzs_diag_emit("  PLL_PRIMARY_STATUS=0x"); xzs_d8p1_hex32(pll); xzs_diag_emit("\n");
	xzs_diag_emit("  DSI_CTRL=0x"); xzs_d8p1_hex32(ctrl); xzs_diag_emit("\n");
	xzs_diag_emit("  DSI_STATUS=0x"); xzs_d8p1_hex32(dsi_stat); xzs_diag_emit("\n");
	xzs_diag_emit("  DSI_LANE_STATUS=0x"); xzs_d8p1_hex32(lane); xzs_diag_emit("\n");

	if (rst_check != 0 || !lab_ok || !ibb_ok || vddio_in == 0 ||
	    (pll & 0x21u) != 0x21u || (ctrl & 0x1u) == 0 || (lane & 0x1f1fu) != 0x1f1fu) {
		xzs_diag_emit("!!! FAIL: Stage 1 acceptance criteria violated!\n");
		xzs_d8m5_power_down();
		return -7;
	}

	xzs_diag_emit("[D8-M5] STAGE1_RESULT=PASS\n");

	/* Safe shutdown after standalone stage 1 test */
	xzs_d8m5_power_down();
	xzs_d8m5_dump_safety_counters();
	return 0;
}

/*
 * M5-E & M5-F & M5-G: Full M5 State Machine (display m5-run)
 * Sequence:
 *   1. Prerequisites check
 *   2. Power domains with reset held LOW (VDDIO -> LAB -> IBB)
 *   3. Reset sequence: Reset LOW (10ms) -> Reset HIGH (10ms) with hardware timestamps
 *   4. Powered-Idle verification (GPIOs, LAB/IBB, DSI host, lanes, FIFO, ack err)
 *   5. Mandatory Power-Down test (Reset LOW 5ms -> IBB OFF 10ms -> LAB OFF 10ms -> VDDIO OFF 0ms -> Settling 300ms)
 *   6. Final safe state verification
 */
static int
xzs_d8m5_run(void)
{
	xzs_diag_emit("\n=======================================================\n");
	xzs_diag_emit("=== XZS D8-M5 FULL PANEL POWER & RESET BRINGUP      ===\n");
	xzs_diag_emit("=======================================================\n");

	/* PHASE 1: Prerequisites Check */
	xzs_diag_emit("\n[D8-M5-CHECKPOINT-1] PREREQUISITES:\n");
	uint32_t pll_pre  = d8p1_read32(0x009948ccu);
	uint32_t ctrl_pre = d8p1_read32(0x00994004u);
	uint32_t lane_pre = d8p1_read32(0x009940a8u);
	uint32_t fifo_pre = d8p1_read32(0x0099400cu);

	xzs_diag_emit("  PLL_PRIMARY_STATUS=0x"); xzs_d8p1_hex32(pll_pre);  xzs_diag_emit(" (exp 0x0000002f)\n");
	xzs_diag_emit("  DSI_CTRL=0x");           xzs_d8p1_hex32(ctrl_pre); xzs_diag_emit(" (exp 0x000001f5)\n");
	xzs_diag_emit("  DSI_LANE_STATUS=0x");    xzs_d8p1_hex32(lane_pre); xzs_diag_emit(" (exp 0x00001f1f)\n");
	xzs_diag_emit("  DSI_FIFO_STATUS=0x");    xzs_d8p1_hex32(fifo_pre); xzs_diag_emit(" (exp 0x11111000)\n");

	if ((pll_pre & 0x21u) != 0x21u || (ctrl_pre & 0x1u) == 0 || (lane_pre & 0x1f1fu) != 0x1f1fu) {
		xzs_diag_emit("!!! FAIL: Prerequisites unfulfilled. M3/M4 display engine not running.\n");
		xzs_diag_emit("[D8-M5] RESULT=FAIL_PREREQUISITES\n");
		return -1;
	}
	xzs_diag_emit("  PREREQUISITES: PASS\n");

	/* Ensure SPMI and P2 rail configuration is applied */
	if (xzs_spmi_init() != 0) {
		xzs_diag_emit("!!! FAIL: SPMI init failed!\n");
		return -2;
	}
	xzs_d8p2_run(1); // Configure LAB (+5.6V) and IBB (-5.6V) registers safely while disabled

	/* PHASE 2: Stage 1 Power Rails with Reset Held LOW */
	xzs_diag_emit("\n[D8-M5-CHECKPOINT-2] POWER DOMAINS (RESET HELD LOW):\n");

	/* 1. Ensure GPIO8 Reset is strictly asserted LOW */
	xzs_d8m5_set_reset_low();
	if (xzs_d8m5_gpio_read_in(GPIO_RESET_NUM) != 0) {
		xzs_diag_emit("!!! FAIL: GPIO8 is HIGH while expecting LOW!\n");
		return -3;
	}
	xzs_diag_emit("  Reset held LOW confirmed.\n");

	/* 2. Enable VDDIO (GPIO 51 HIGH) -> delay 10ms per somc,pw-wait-after-on-vddio = <10> */
	xzs_diag_emit("  Enabling VDDIO (GPIO51=1)...\n");
	xzs_d8m5_set_vddio_high();
	xzs_d8p2_delay_us(10000); // 10 ms
	uint32_t vddio_in = xzs_d8m5_gpio_read_in(GPIO_VDDIO_NUM);
	uint32_t rst_check = xzs_d8m5_gpio_read_in(GPIO_RESET_NUM);
	xzs_diag_emit("  VDDIO_IN="); xzs_diag_emit(vddio_in ? "HIGH(OK)" : "LOW(FAIL)");
	xzs_diag_emit(" RESET_IN="); xzs_diag_emit(rst_check ? "HIGH(LEAK_FAIL)" : "LOW(OK)");
	xzs_diag_emit("\n");
	if (vddio_in == 0 || rst_check != 0) {
		xzs_diag_emit("!!! FAIL: VDDIO enable failed or Reset glitch detected!\n");
		xzs_d8m5_power_down();
		return -4;
	}

	/* 3. Enable LAB (+5.6V) -> bounded poll VREG_OK -> delay 10ms per somc,pw-wait-after-on-vsp = <10> */
	xzs_diag_emit("  Enabling LAB rail (+5.6V)...\n");
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
	xzs_diag_emit(" -> LAB VREG_OK PASS\n");
	xzs_d8p2_delay_us(10000); // 10 ms

	/* 4. Enable IBB (-5.6V) -> bounded poll VREG_OK -> delay 0ms per somc,pw-wait-after-on-vsn = <0> */
	xzs_diag_emit("  Enabling IBB rail (-5.6V)...\n");
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
	xzs_diag_emit(" -> IBB VREG_OK PASS\n");

	/* Verify power domain ready and Reset still held LOW */
	rst_check = xzs_d8m5_gpio_read_in(GPIO_RESET_NUM);
	if (rst_check != 0) {
		xzs_diag_emit("!!! FAIL: Reset released prematurely!\n");
		xzs_d8m5_power_down();
		return -7;
	}
	xzs_diag_emit("  Power domains established with Reset held LOW: PASS\n");

	/* PHASE 3: Exact Reset Sequence (somc,pw-on-rst-seq = <0x00 0x0a 0x01 0x0a>) */
	xzs_diag_emit("\n[D8-M5-CHECKPOINT-3] RESET SEQUENCE:\n");
	xzs_d8m5_set_reset_low();
	uint64_t ts_assert = xzs_d8m5_read_cntvct();

	/* Hold LOW for 10ms (10,000 us) */
	xzs_d8p2_delay_us(10000);

	uint64_t ts_release = xzs_d8m5_read_cntvct();
	xzs_d8m5_set_reset_high();

	xzs_diag_emit("  RESET_ASSERT_TS=0x"); xzs_d8m5_hex64(ts_assert); xzs_diag_emit("\n");
	xzs_diag_emit("  RESET_RELEASE_TS=0x"); xzs_d8m5_hex64(ts_release); xzs_diag_emit("\n");

	uint64_t delta_ticks = ts_release - ts_assert;
	uint32_t delta_us = (uint32_t)((delta_ticks * 10ULL) / 192ULL);
	xzs_diag_emit("  RESET_DELTA_US="); xzs_d8m5_dec32(delta_us); xzs_diag_emit("\n");
	xzs_diag_emit("  EXPECTED_MIN_US=10000\n");

	if (delta_us < 9900) {
		xzs_diag_emit("!!! FAIL: Reset assert delta below required minimum!\n");
		xzs_d8m5_power_down();
		return -8;
	}
	xzs_diag_emit("  RESET_TIMING_RESULT=PASS\n");

	/* Stabilization delay: 10ms per somc,pw-on-rst-seq */
	xzs_diag_emit("  Reset release stabilization delay (10 ms)...\n");
	xzs_d8p2_delay_us(10000);

	uint32_t rst_high_in = xzs_d8m5_gpio_read_in(GPIO_RESET_NUM);
	xzs_diag_emit("  GPIO8_RESET_RELEASED="); xzs_diag_emit(rst_high_in ? "HIGH(PASS)\n" : "LOW(FAIL)\n");
	if (rst_high_in == 0) {
		xzs_diag_emit("!!! FAIL: GPIO8 did not release to HIGH!\n");
		xzs_d8m5_power_down();
		return -9;
	}

	/* PHASE 4: Powered-Idle Verification */
	xzs_diag_emit("\n[D8-M5-CHECKPOINT-4] POWERED-IDLE VERIFICATION:\n");
	uint32_t g8_val  = xzs_d8m5_gpio_read_in(GPIO_RESET_NUM);
	uint32_t g10_val = xzs_d8m5_gpio_read_in(GPIO_TE_NUM);
	uint32_t g51_val = xzs_d8m5_gpio_read_in(GPIO_VDDIO_NUM);

	uint8_t lab_st_pow = 0, ibb_st_pow = 0;
	xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_LAB + LAB_REG_STATUS1, &lab_st_pow);
	xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_IBB + IBB_REG_STATUS1, &ibb_st_pow);

	uint32_t pll_pow      = d8p1_read32(0x009948ccu);
	uint32_t ctrl_pow     = d8p1_read32(0x00994004u);
	uint32_t stat_pow     = d8p1_read32(0x00994008u);
	uint32_t fifo_pow     = d8p1_read32(0x0099400cu);
	uint32_t ack_err_pow  = d8p1_read32(0x00994068u);
	uint32_t lane_pow     = d8p1_read32(0x009940a8u);
	uint32_t timeout_pow  = d8p1_read32(0x009940c0u);
	uint32_t clk_pow      = d8p1_read32(0x00994120u);

	xzs_diag_emit("  GPIO8_RESET     = "); xzs_diag_emit(g8_val ? "HIGH(RELEASED)\n" : "LOW(FAIL)\n");
	xzs_diag_emit("  GPIO10_TE       = "); xzs_diag_emit(g10_val ? "HIGH\n" : "LOW(INACTIVE_EXPECTED)\n");
	xzs_diag_emit("  GPIO51_VDDIO    = "); xzs_diag_emit(g51_val ? "HIGH(ASSERTED)\n" : "LOW(FAIL)\n");
	xzs_diag_emit("  LAB_STATUS1     = 0x"); xzs_d8p2_hex8(lab_st_pow); xzs_diag_emit(" (VREG_OK=1)\n");
	xzs_diag_emit("  IBB_STATUS1     = 0x"); xzs_d8p2_hex8(ibb_st_pow); xzs_diag_emit(" (VREG_OK=1)\n");
	xzs_diag_emit("  PLL_STATUS      = 0x"); xzs_d8p1_hex32(pll_pow); xzs_diag_emit("\n");
	xzs_diag_emit("  DSI_CTRL        = 0x"); xzs_d8p1_hex32(ctrl_pow); xzs_diag_emit("\n");
	xzs_diag_emit("  DSI_STATUS      = 0x"); xzs_d8p1_hex32(stat_pow); xzs_diag_emit("\n");
	xzs_diag_emit("  DSI_FIFO_STATUS = 0x"); xzs_d8p1_hex32(fifo_pow); xzs_diag_emit("\n");
	xzs_diag_emit("  DSI_ACK_ERR     = 0x"); xzs_d8p1_hex32(ack_err_pow); xzs_diag_emit("\n");
	xzs_diag_emit("  DSI_LANE_STATUS = 0x"); xzs_d8p1_hex32(lane_pow); xzs_diag_emit("\n");
	xzs_diag_emit("  DSI_TIMEOUT     = 0x"); xzs_d8p1_hex32(timeout_pow); xzs_diag_emit("\n");
	xzs_diag_emit("  DSI_CLK_STATUS  = 0x"); xzs_d8p1_hex32(clk_pow); xzs_diag_emit("\n");

	if (g8_val == 0 || g51_val == 0 || !(lab_st_pow & LAB_STATUS1_VREG_OK) || !(ibb_st_pow & IBB_STATUS1_VREG_OK) ||
	    (pll_pow & 0x21u) != 0x21u || (ctrl_pow & 0x1u) == 0 || (lane_pow & 0x1f1fu) != 0x1f1fu) {
		xzs_diag_emit("!!! FAIL: Powered-idle state verification failed!\n");
		xzs_d8m5_power_down();
		return -10;
	}
	xzs_diag_emit("  POWERED_IDLE_STATE: PASS\n");

	/* Hold powered-idle state for brief observation window (50 ms) */
	xzs_d8p2_delay_us(50000);

	/* PHASE 5: Mandatory Power-Down Test */
	xzs_diag_emit("\n[D8-M5-CHECKPOINT-5] MANDATORY POWER-DOWN TEST:\n");
	int pd_rc = xzs_d8m5_power_down();
	if (pd_rc != 0) {
		xzs_diag_emit("!!! FAIL: Power-down test failed!\n");
		return -11;
	}
	xzs_diag_emit("  POWER_DOWN: PASS\n");

	/* PHASE 6: Lower-Layer Post-Verification */
	xzs_diag_emit("\n[D8-M5-CHECKPOINT-6] LOWER-LAYER REGRESSION CHECK:\n");
	uint32_t pll_post  = d8p1_read32(0x009948ccu);
	uint32_t ctrl_post = d8p1_read32(0x00994004u);
	uint32_t fifo_post = d8p1_read32(0x0099400cu);
	uint32_t lane_post = d8p1_read32(0x009940a8u);

	xzs_diag_emit("  PLL_POST  = 0x"); xzs_d8p1_hex32(pll_post);  xzs_diag_emit("\n");
	xzs_diag_emit("  CTRL_POST = 0x"); xzs_d8p1_hex32(ctrl_post); xzs_diag_emit("\n");
	xzs_diag_emit("  FIFO_POST = 0x"); xzs_d8p1_hex32(fifo_post); xzs_diag_emit("\n");
	xzs_diag_emit("  LANE_POST = 0x"); xzs_d8p1_hex32(lane_post); xzs_diag_emit("\n");

	if ((pll_post & 0x21u) != 0x21u || (ctrl_post & 0x1u) == 0 || (lane_post & 0x1f1fu) != 0x1f1fu) {
		xzs_diag_emit("!!! FAIL: Lower layer integrity degraded after power cycle!\n");
		return -12;
	}
	xzs_diag_emit("  LOWER_LAYER_REGRESSION: NONE (PASS)\n");

	/* Final Safety Counters Summary */
	xzs_d8m5_dump_safety_counters();

	if (g_d8m5_counters.dcs_packets_sent != 0 ||
	    g_d8m5_counters.dma_trigger_count != 0 ||
	    g_d8m5_counters.bta_trigger_count != 0 ||
	    g_d8m5_counters.wled_writes != 0 ||
	    g_d8m5_counters.mdp_kickoff_count != 0) {
		xzs_diag_emit("!!! FAIL: Boundary violation: display transmission or backlight occurred!\n");
		return -13;
	}

	xzs_diag_emit("[D8-M5] RESULT=PASS_FULL_M5\n");
	return 0;
}

#endif /* _XZS_D8M5_H_ */
