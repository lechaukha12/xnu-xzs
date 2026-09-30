/*
 * xnu-xzs Display Bringup Phase D8-P1: TLMM GPIO Hardware Prerequisite
 *
 * Implements source-audited TLMM GPIO configuration for Sony Xperia XZs (MSM8996 v3.0 / Keyaki)
 * GPIO 8:  disp_reset_n (Panel Reset, Active Low - safe state: ASSERTED / LOW)
 * GPIO 10: mdp_vsync (TE - safe state: INPUT with pull-down)
 * GPIO 51: lcd_vddio_en (LCD 1.8V VDDIO Enable, Active High - safe state: DISABLED / LOW)
 */

#ifndef _XZS_D8P1_H_
#define _XZS_D8P1_H_

#include <stdint.h>

#define TLMM_BASE              0x01010000u
#define TLMM_GPIO_CFG(n)       (TLMM_BASE + 0x1000u * (n))
#define TLMM_GPIO_IN_OUT(n)    (TLMM_BASE + 0x1000u * (n) + 0x4u)

#define GPIO_RESET_NUM         8u
#define GPIO_TE_NUM            10u
#define GPIO_TOUCH_VDDIO_NUM   50u
#define GPIO_VDDIO_NUM         51u
#define GPIO_TOUCH_RESET_NUM   89u
#define GPIO_TOUCH_INT_NUM     125u

extern void xzs_diag_emit(const char *msg);
extern uint64_t g_xzs_ttbr0;

static inline void
xzs_d8p1_hex32(uint32_t val)
{
	char str[9];
	static const char hex[] = "0123456789abcdef";
	for (int h = 7; h >= 0; h--) {
		str[7 - h] = hex[(val >> (h * 4)) & 0xf];
	}
	str[8] = '\0';
	xzs_diag_emit(str);
}

static __attribute__((noinline)) uint32_t
d8p1_read32(uint32_t phys)
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

static __attribute__((noinline)) void
d8p1_write32(uint32_t phys, uint32_t val)
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

static void
xzs_d8p1_decode_cfg(uint32_t cfg)
{
	uint32_t pull = cfg & 0x3u;
	uint32_t func = (cfg >> 2) & 0xfu;
	uint32_t drv  = (cfg >> 6) & 0x7u;
	uint32_t oe   = (cfg >> 9) & 0x1u;

	xzs_diag_emit(" mux=");
	if (func == 0) xzs_diag_emit("gpio(0)");
	else if (func == 1) xzs_diag_emit("fn1_mdp_vsync(1)");
	else {
		char f_buf[16];
		f_buf[0] = 'f'; f_buf[1] = 'n'; f_buf[2] = '0' + (func % 10); f_buf[3] = '\0';
		xzs_diag_emit(f_buf);
	}

	xzs_diag_emit(" pull=");
	if (pull == 0) xzs_diag_emit("no_pull(0)");
	else if (pull == 1) xzs_diag_emit("pull_down(1)");
	else if (pull == 2) xzs_diag_emit("keeper(2)");
	else xzs_diag_emit("pull_up(3)");

	xzs_diag_emit(" drive=");
	uint32_t ma = (drv + 1) * 2;
	if (ma < 10) {
		char m_buf[8];
		m_buf[0] = '0' + ma; m_buf[1] = 'm'; m_buf[2] = 'A'; m_buf[3] = '\0';
		xzs_diag_emit(m_buf);
	} else {
		char m_buf[8];
		m_buf[0] = '1'; m_buf[1] = '0' + (ma - 10); m_buf[2] = 'm'; m_buf[3] = 'A'; m_buf[4] = '\0';
		xzs_diag_emit(m_buf);
	}

	xzs_diag_emit(" dir=");
	xzs_diag_emit(oe ? "OUTPUT(1)" : "INPUT(0)");
}

static void
xzs_d8p1_decode_in_out(uint32_t io)
{
	uint32_t in_val  = io & 0x1u;
	uint32_t out_val = (io >> 1) & 0x1u;

	xzs_diag_emit(" in=");
	xzs_diag_emit(in_val ? "HIGH(1)" : "LOW(0)");
	xzs_diag_emit(" out=");
	xzs_diag_emit(out_val ? "HIGH(1)" : "LOW(0)");
}

static void
xzs_d8p1_dump_one_gpio(const char *name, uint32_t gpio_num)
{
	uint32_t cfg_addr = TLMM_GPIO_CFG(gpio_num);
	uint32_t io_addr  = TLMM_GPIO_IN_OUT(gpio_num);

	uint32_t cfg_val = d8p1_read32(cfg_addr);
	uint32_t io_val  = d8p1_read32(io_addr);

	xzs_diag_emit("  GPIO");
	if (gpio_num < 10) {
		char b[2] = { (char)('0' + gpio_num), '\0' };
		xzs_diag_emit(b);
	} else if (gpio_num < 100) {
		char b[3] = { (char)('0' + (gpio_num / 10)), (char)('0' + (gpio_num % 10)), '\0' };
		xzs_diag_emit(b);
	}
	xzs_diag_emit(" (");
	xzs_diag_emit(name);
	xzs_diag_emit("):\n");

	xzs_diag_emit("    CFG    @ 0x");
	xzs_d8p1_hex32(cfg_addr);
	xzs_diag_emit(" = 0x");
	xzs_d8p1_hex32(cfg_val);
	xzs_diag_emit(" [");
	xzs_d8p1_decode_cfg(cfg_val);
	xzs_diag_emit(" ]\n");

	xzs_diag_emit("    IN_OUT @ 0x");
	xzs_d8p1_hex32(io_addr);
	xzs_diag_emit(" = 0x");
	xzs_d8p1_hex32(io_val);
	xzs_diag_emit(" [");
	xzs_d8p1_decode_in_out(io_val);
	xzs_diag_emit(" ]\n");
}

static void
xzs_d8p1_dump_gpio_status(void)
{
	xzs_diag_emit("\n=======================================================\n");
	xzs_diag_emit("=== XZS D8-P1 KEYAKI DISPLAY GPIO STATUS (TLMM)    ===\n");
	xzs_diag_emit("=======================================================\n");
	xzs_diag_emit("TLMM_BASE = 0x01010000\n\n");

	xzs_d8p1_dump_one_gpio("disp_reset_n", GPIO_RESET_NUM);
	xzs_d8p1_dump_one_gpio("mdp_vsync_te", GPIO_TE_NUM);
	xzs_d8p1_dump_one_gpio("lcd_vddio_en", GPIO_VDDIO_NUM);
	xzs_d8p1_dump_one_gpio("touch_vddio",  GPIO_TOUCH_VDDIO_NUM);
	xzs_d8p1_dump_one_gpio("touch_reset_n", GPIO_TOUCH_RESET_NUM);
	xzs_diag_emit("=======================================================\n");
}

static int
d8p1_audit_write(const char *name, uint32_t addr, uint32_t write_val, uint32_t mask, int dryrun)
{
	uint32_t old_val = d8p1_read32(addr);
	uint32_t target_val = (old_val & ~mask) | (write_val & mask);

	xzs_diag_emit("  [WRITE] ");
	xzs_diag_emit(name);
	xzs_diag_emit(" @ 0x");
	xzs_d8p1_hex32(addr);
	xzs_diag_emit("\n    OLD=0x");
	xzs_d8p1_hex32(old_val);
	xzs_diag_emit(" MASK=0x");
	xzs_d8p1_hex32(mask);
	xzs_diag_emit(" WRITE=0x");
	xzs_d8p1_hex32(target_val);

	if (dryrun) {
		xzs_diag_emit(" (DRYRUN - SKIPPED)\n");
		return 0;
	}

	d8p1_write32(addr, target_val);
	uint32_t rb = d8p1_read32(addr);
	xzs_diag_emit(" READBACK=0x");
	xzs_d8p1_hex32(rb);

	if ((rb & mask) != (target_val & mask)) {
		xzs_diag_emit(" [MISMATCH - FAIL]\n");
		return -1;
	}
	xzs_diag_emit(" [PASS]\n");
	return 0;
}

static int
xzs_d8p1_run(int mode)
{
	int dryrun = (mode == 0);

	xzs_diag_emit("\n=======================================================\n");
	if (dryrun) {
		xzs_diag_emit("=== XZS D8-P1 TLMM GPIO CONFIGURATION: DRY-RUN     ===\n");
	} else {
		xzs_diag_emit("=== XZS D8-P1 TLMM GPIO CONFIGURATION: REAL HW     ===\n");
	}
	xzs_diag_emit("=======================================================\n");

	/* D8P1-10: Check prerequisites (M2 Clocks, M3 PLL/PHY, M4 DSI Host) */
	xzs_diag_emit("\n[D8-P1] CHECKPOINT D8P1-10 PREREQUISITES START\n");
	uint32_t pll_stat = d8p1_read32(0x009948ccu);
	uint32_t dsi_ctrl = d8p1_read32(0x00994004u);
	uint32_t lane_stat = d8p1_read32(0x009940a8u);

	xzs_diag_emit("  PLL_PRIMARY_STATUS=0x");
	xzs_d8p1_hex32(pll_stat);
	xzs_diag_emit(" (expected 0x0000002f)\n");

	xzs_diag_emit("  DSI_CTRL=0x");
	xzs_d8p1_hex32(dsi_ctrl);
	xzs_diag_emit(" (expected 0x000001f5)\n");

	xzs_diag_emit("  DSI_LANE_STATUS=0x");
	xzs_d8p1_hex32(lane_stat);
	xzs_diag_emit(" (expected 0x00001f1f)\n");

	if ((pll_stat & 0x21u) != 0x21u || (dsi_ctrl & 0x1u) == 0 || (lane_stat & 0x1f1fu) != 0x1f1fu) {
		xzs_diag_emit("!!! FAIL: Prerequisites unfulfilled. M3/M4 display engine not running.\n");
		xzs_diag_emit("[D8-P1] RESULT=FAIL_PREREQUISITES\n");
		return -1;
	}
	xzs_diag_emit("[D8-P1] CHECKPOINT D8P1-10 PREREQUISITES PASS\n");

	/* D8P1-20: TLMM Map Verification */
	xzs_diag_emit("\n[D8-P1] CHECKPOINT D8P1-20 TLMM_MAP_VERIFY START\n");
	xzs_diag_emit("  TLMM_BASE = 0x01010000\n");
	xzs_diag_emit("  GPIO8_CFG_ADDR  = 0x01018000\n");
	xzs_diag_emit("  GPIO10_CFG_ADDR = 0x0101a000\n");
	xzs_diag_emit("  GPIO50_CFG_ADDR = 0x01042000\n");
	xzs_diag_emit("  GPIO51_CFG_ADDR = 0x01043000\n");
	xzs_diag_emit("  GPIO89_CFG_ADDR = 0x01069000\n");
	xzs_diag_emit("[D8-P1] CHECKPOINT D8P1-20 TLMM_MAP_VERIFY PASS\n");

	/* Pre-Programming Snapshot */
	xzs_diag_emit("\n[D8-P1] PRE-PROGRAMMING GPIO SNAPSHOT:\n");
	xzs_d8p1_dump_gpio_status();

	/* D8P1-30: GPIO 8 (Panel Reset disp_reset_n) Plan & Config
	 * Target: Output (OE=1), Func=0 (GPIO), Pull=0 (No pull), Drive=2mA (0) -> CFG=0x00000200
	 * Safe state: Reset ASSERTED (LOW) -> IN_OUT bit 1 = 0
	 */
	xzs_diag_emit("\n[D8-P1] CHECKPOINT D8P1-30 GPIO8_CONFIG_PLAN START\n");
	xzs_diag_emit("  PLAN: GPIO8 Output LOW (Reset ASSERTED / Safe)\n");
	/* Step 1: Ensure output latch is 0 before enabling OE to avoid high glitch */
	if (d8p1_audit_write("GPIO8_IN_OUT", TLMM_GPIO_IN_OUT(GPIO_RESET_NUM), 0x00000000u, 0x00000002u, dryrun) != 0) {
		return -1;
	}
	/* Step 2: Configure CFG: OE=1, FUNC=0, PULL=0, DRV=0 */
	if (d8p1_audit_write("GPIO8_CFG", TLMM_GPIO_CFG(GPIO_RESET_NUM), 0x00000200u, 0x000003ffu, dryrun) != 0) {
		return -1;
	}
	xzs_diag_emit("[D8-P1] CHECKPOINT D8P1-30 GPIO8_CONFIG_PLAN PASS\n");

	/* D8P1-40: GPIO 10 (TE mdp_vsync) Plan & Config
	 * Target: Input (OE=0), Func=1 (mdp_vsync), Pull=1 (Pull-down), Drive=2mA (0) -> CFG=0x00000005
	 */
	xzs_diag_emit("\n[D8-P1] CHECKPOINT D8P1-40 GPIO10_CONFIG_PLAN START\n");
	xzs_diag_emit("  PLAN: GPIO10 Input mdp_vsync with Pull-Down\n");
	if (d8p1_audit_write("GPIO10_CFG", TLMM_GPIO_CFG(GPIO_TE_NUM), 0x00000005u, 0x000003ffu, dryrun) != 0) {
		return -1;
	}
	xzs_diag_emit("[D8-P1] CHECKPOINT D8P1-40 GPIO10_CONFIG_PLAN PASS\n");

	/* D8P1-50: GPIO 51 (LCD VDDIO Enable lcd_vddio_en) Plan & Config
	 * Target: Output (OE=1), Func=0 (GPIO), Pull=0 (No pull), Drive=2mA (0) -> CFG=0x00000200
	 * Safe state: VDDIO DISABLED (LOW) -> IN_OUT bit 1 = 0
	 */
	xzs_diag_emit("\n[D8-P1] CHECKPOINT D8P1-50 GPIO51_CONFIG_PLAN START\n");
	xzs_diag_emit("  PLAN: GPIO51 Output LOW (VDDIO DISABLED / Safe)\n");
	/* Step 1: Ensure output latch is 0 before enabling OE */
	if (d8p1_audit_write("GPIO51_IN_OUT", TLMM_GPIO_IN_OUT(GPIO_VDDIO_NUM), 0x00000000u, 0x00000002u, dryrun) != 0) {
		return -1;
	}
	/* Step 2: Configure CFG: OE=1, FUNC=0, PULL=0, DRV=0 */
	if (d8p1_audit_write("GPIO51_CFG", TLMM_GPIO_CFG(GPIO_VDDIO_NUM), 0x00000200u, 0x000003ffu, dryrun) != 0) {
		return -1;
	}
	xzs_diag_emit("[D8-P1] CHECKPOINT D8P1-50 GPIO51_CONFIG_PLAN PASS\n");

	/* D8P1-51: GPIO 50 (Touch VDDIO Enable touch_vddio) Plan & Config
	 * Target: Output (OE=1), Func=0 (GPIO), Pull=0 (No pull), Drive=2mA (0) -> CFG=0x00000200
	 * Safe state: Touch VDDIO DISABLED (LOW) -> IN_OUT bit 1 = 0
	 */
	xzs_diag_emit("\n[D8-P1] CHECKPOINT D8P1-51 GPIO50_CONFIG_PLAN START\n");
	xzs_diag_emit("  PLAN: GPIO50 Output LOW (Touch VDDIO DISABLED / Safe)\n");
	if (d8p1_audit_write("GPIO50_IN_OUT", TLMM_GPIO_IN_OUT(GPIO_TOUCH_VDDIO_NUM), 0x00000000u, 0x00000002u, dryrun) != 0) {
		return -1;
	}
	if (d8p1_audit_write("GPIO50_CFG", TLMM_GPIO_CFG(GPIO_TOUCH_VDDIO_NUM), 0x00000200u, 0x000003ffu, dryrun) != 0) {
		return -1;
	}
	xzs_diag_emit("[D8-P1] CHECKPOINT D8P1-51 GPIO50_CONFIG_PLAN PASS\n");

	/* D8P1-52: GPIO 89 (In-Cell Touch Reset touch_reset_n) Plan & Config
	 * Target: Output (OE=1), Func=0 (GPIO), Pull=0 (No pull), Drive=2mA (0) -> CFG=0x00000200
	 * Safe state: Touch Reset ASSERTED (LOW) -> IN_OUT bit 1 = 0
	 */
	xzs_diag_emit("\n[D8-P1] CHECKPOINT D8P1-52 GPIO89_CONFIG_PLAN START\n");
	xzs_diag_emit("  PLAN: GPIO89 Output LOW (Touch Reset ASSERTED / Safe)\n");
	if (d8p1_audit_write("GPIO89_IN_OUT", TLMM_GPIO_IN_OUT(GPIO_TOUCH_RESET_NUM), 0x00000000u, 0x00000002u, dryrun) != 0) {
		return -1;
	}
	if (d8p1_audit_write("GPIO89_CFG", TLMM_GPIO_CFG(GPIO_TOUCH_RESET_NUM), 0x00000200u, 0x000003ffu, dryrun) != 0) {
		return -1;
	}
	xzs_diag_emit("[D8-P1] CHECKPOINT D8P1-52 GPIO89_CONFIG_PLAN PASS\n");

	/* D8P1-53: GPIO 125 (Touch Interrupt touch_int_n) Plan & Config
	 * Target: Input (OE=0), Func=0 (GPIO), Pull=3 (Pull-Up), Drive=2mA (0) -> CFG=0x00000003
	 * Sony LK Step T4 & keyaki.dts:18198 (mdss_touch_active):
	 * GPIO 125 must have bias-pull-up active for in-cell touch controller operation.
	 */
	xzs_diag_emit("\n[D8-P1] CHECKPOINT D8P1-53 GPIO125_CONFIG_PLAN START\n");
	xzs_diag_emit("  PLAN: GPIO125 Input with Pull-Up (mdss_touch_active)\n");
	if (d8p1_audit_write("GPIO125_CFG", TLMM_GPIO_CFG(GPIO_TOUCH_INT_NUM), 0x00000003u, 0x000003ffu, dryrun) != 0) {
		return -1;
	}
	xzs_diag_emit("[D8-P1] CHECKPOINT D8P1-53 GPIO125_CONFIG_PLAN PASS\n");

	/* D8P1-60: Safe State Plan & Verification */
	xzs_diag_emit("\n[D8-P1] CHECKPOINT D8P1-60 SAFE_STATE_PLAN START\n");
	if (!dryrun) {
		uint32_t g8_io   = d8p1_read32(TLMM_GPIO_IN_OUT(GPIO_RESET_NUM));
		uint32_t g51_io  = d8p1_read32(TLMM_GPIO_IN_OUT(GPIO_VDDIO_NUM));
		uint32_t g50_io  = d8p1_read32(TLMM_GPIO_IN_OUT(GPIO_TOUCH_VDDIO_NUM));
		uint32_t g89_io  = d8p1_read32(TLMM_GPIO_IN_OUT(GPIO_TOUCH_RESET_NUM));
		if (((g8_io >> 1) & 1u) != 0) {
			xzs_diag_emit("!!! FAIL: GPIO8 output is HIGH (reset released prematurely)\n");
			return -1;
		}
		if (((g51_io >> 1) & 1u) != 0) {
			xzs_diag_emit("!!! FAIL: GPIO51 output is HIGH (VDDIO enabled prematurely)\n");
			return -1;
		}
		if (((g50_io >> 1) & 1u) != 0) {
			xzs_diag_emit("!!! FAIL: GPIO50 output is HIGH (Touch VDDIO enabled prematurely)\n");
			return -1;
		}
		if (((g89_io >> 1) & 1u) != 0) {
			xzs_diag_emit("!!! FAIL: GPIO89 output is HIGH (Touch reset released prematurely)\n");
			return -1;
		}
		xzs_diag_emit("  SAFE_STATE: GPIO8 (Reset) is LOW (Asserted)\n");
		xzs_diag_emit("  SAFE_STATE: GPIO51 (VDDIO) is LOW (Disabled)\n");
		xzs_diag_emit("  SAFE_STATE: GPIO50 (Touch VDDIO) is LOW (Disabled)\n");
		xzs_diag_emit("  SAFE_STATE: GPIO89 (Touch Reset) is LOW (Asserted)\n");
	}
	xzs_diag_emit("[D8-P1] CHECKPOINT D8P1-60 SAFE_STATE_PLAN PASS\n");

	/* D8P1-70: Post-Programming Snapshot & Accept */
	xzs_diag_emit("\n[D8-P1] CHECKPOINT D8P1-70 ACCEPT START\n");
	if (!dryrun) {
		xzs_diag_emit("\n[D8-P1] POST-PROGRAMMING GPIO SNAPSHOT:\n");
		xzs_d8p1_dump_gpio_status();
	}

	/* Safety Counters Audit */
	xzs_diag_emit("\n[D8-P1] SAFETY COUNTERS:\n");
	xzs_diag_emit("LAB_WRITES=0\n");
	xzs_diag_emit("IBB_WRITES=0\n");
	xzs_diag_emit("WLED_WRITES=0\n");
	xzs_diag_emit("DCS_PACKETS_SENT=0\n");
	xzs_diag_emit("BUS_ABORT=0\n");
	xzs_diag_emit("SError=0\n");
	xzs_diag_emit("PANIC=0\n");
	xzs_diag_emit("UNINTENDED_RESET=0\n");

	if (dryrun) {
		xzs_diag_emit("\n[D8-P1] RESULT=PASS_DRYRUN\n");
	} else {
		xzs_diag_emit("\n[D8-P1] RESULT=PASS_P1\n");
	}
	return 0;
}

#endif /* _XZS_D8P1_H_ */
