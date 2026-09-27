/*
 * One diagnostic backend for the bring-up shell.
 * Each line is emitted once. xzs_bringup_console_write mirrors that
 * line to the USB console and to the existing pstore path.
 * Display, DSI, PHY, and clock blocks are not read here: the reference
 * tree marks MDSS disabled behind a power domain, and this port has no
 * proof that those registers are safe to touch.
 */

#include <stdint.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm64/boot.h>

extern void xzs_bringup_console_write(const void *buf, int len);
extern volatile uint32_t g_xzs_usb_irq_count;
extern volatile uint32_t g_xzs_uart_rx_irq_count;

#define XZS_DIAG_RETAIN (8 * 1024)

static char s_retain[XZS_DIAG_RETAIN];
static uint32_t s_retain_len;

static void xzs_display_dump_state(void);
static void xzs_dsi_dump_state(void);
static void xzs_clock_dump_state(void);
static void xzs_irq_dump_state(void);
static void xzs_fb_dump_state(void);
static void xzs_memory_dump_state(void);
void xzs_diag_dispatch(uint64_t which, uint64_t arg1, uint64_t arg2, uint64_t arg3);

static void
xzs_diag_emit(const char *s)
{
	int n = 0;

	if (s == NULL) {
		return;
	}
	while (s[n] != '\0') {
		s_retain[s_retain_len % XZS_DIAG_RETAIN] = s[n];
		s_retain_len++;
		n++;
	}
	xzs_bringup_console_write(s, n);
}

static void
xzs_diag_hex_line(const char *label, uint64_t value)
{
	char line[80];
	static const char hex[] = "0123456789abcdef";
	int i = 0;
	int h;

	while (label[i] != '\0' && i < 48) {
		line[i] = label[i];
		i++;
	}
	line[i++] = '0';
	line[i++] = 'x';
	for (h = 15; h >= 0; h--) {
		line[i++] = hex[(value >> (h * 4)) & 0xf];
	}
	line[i++] = '\n';
	line[i] = '\0';
	xzs_diag_emit(line);
}

static const boot_args *
xzs_diag_boot_args(void)
{
	if (PE_state.bootArgs == NULL) {
		return NULL;
	}
	return (const boot_args *)PE_state.bootArgs;
}

/*
 * MMCC is at 0x008c0000, inside the bootstrap device window
 * 0x00000000-0x01ffffff. These registers are the clock controller,
 * not the MDSS slave behind MDSS_GDSC.
 */
#define XZS_MMCC_BASE 0x008c0000UL
#define XZS_MMCC_MDSS_GDSC 0x2304u
#define XZS_MMCC_MMAGIC_MDSS_GDSC 0x247cu
#define XZS_MMCC_MDSS_AHB 0x2308u
#define XZS_MMCC_MDSS_AXI 0x2310u
#define XZS_MMCC_MDSS_PCLK0 0x2314u
#define XZS_MMCC_MDSS_MDP 0x231cu
#define XZS_MMCC_MDSS_BYTE0 0x233cu
#define XZS_MMCC_MDSS_ESC0 0x2344u
#define XZS_MMCC_CFG_AHB 0x5054u
#define XZS_MMCC_MMAGIC_AHB 0x5024u
#define XZS_MMCC_MMAGIC_MDSS_NOC 0x2478u
#define XZS_MMCC_AHB_CMD 0x5000u
#define XZS_MMCC_AHB_CFG 0x5004u
#define XZS_MMCC_AXI_CMD 0x5040u
#define XZS_MMCC_AXI_CFG 0x5044u
#define XZS_MMCC_MMAGIC_MDSS_AXI 0x2474u
#define XZS_MMCC_MMAGIC_HW_CTRL 0x2480u
#define XZS_GCC_BASE 0x00300000u
#define XZS_GCC_MMSS_NOC_CFG_AHB 0x9008u
#define XZS_GCC_GPLL0_MODE 0x00000u
#define XZS_GCC_GPLL0_VOTE 0x52000u
#define XZS_MMCC_MDSS_BCR 0x2300u
#define XZS_MMCC_MMAGIC_MDSS_BCR 0x2470u
#define XZS_MMCC_MMAGIC_AHB_BCR 0x5020u
#define XZS_MMCC_MMAGIC_CFG_BCR 0x5050u

#define XZS_GDSC_PWR_ON (1u << 31)
#define XZS_GDSC_HW_CONTROL (1u << 1)
#define XZS_GDSC_SW_COLLAPSE (1u << 0)
#define XZS_CBCR_CLK_OFF (1u << 31)
#define XZS_CBCR_ENABLE (1u << 0)
#define XZS_CBCR_RETAIN ((1u << 14) | (1u << 13))

extern uint64_t g_xzs_ttbr0;
extern void delay(int usec);

static __attribute__((noinline)) uint32_t
xzs_mmcc_read32(uint32_t offset)
{
	uint64_t saved = 0;
	uint32_t value;

	__asm__ volatile("mrs %0, TTBR0_EL1" : "=r"(saved));
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0\n\tisb sy" :: "r"(g_xzs_ttbr0) : "memory");
	}
	value = *(volatile uint32_t *)(XZS_MMCC_BASE + offset);
	__asm__ volatile("dsb sy\n\tisb sy" ::: "memory");
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0\n\tisb sy" :: "r"(saved) : "memory");
	}
	return value;
}

static __attribute__((noinline)) uint32_t
xzs_phys_read32(uint32_t phys)
{
	uint64_t saved = 0;
	uint32_t value;

	if (phys >= 0x02000000u || (phys & 3u) != 0) {
		return 0xffffffffu;
	}
	__asm__ volatile("mrs %0, TTBR0_EL1" : "=r"(saved));
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0\n\tisb sy" :: "r"(g_xzs_ttbr0) : "memory");
	}
	value = *(volatile uint32_t *)(uintptr_t)phys;
	__asm__ volatile("dsb sy\n\tisb sy" ::: "memory");
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0\n\tisb sy" :: "r"(saved) : "memory");
	}
	return value;
}

static void
xzs_mmcc_reg(const char *name, uint32_t offset)
{
	uint32_t value;

	xzs_diag_emit("[XZS-D8M1] MMCC_READ_PRE ");
	xzs_diag_emit(name);
	xzs_diag_emit("\n");
	value = xzs_mmcc_read32(offset);
	xzs_diag_emit("[XZS-D8M1] MMCC_READ_POST ");
	xzs_diag_emit(name);
	xzs_diag_emit("\n");
	xzs_diag_hex_line("[XZS-D8M1] mmio ", value);
	xzs_diag_emit("[XZS-D8M1] source=mmio reg=");
	xzs_diag_emit(name);
	xzs_diag_emit(" bit0=");
	xzs_diag_emit((value & 1u) ? "1" : "0");
	xzs_diag_emit(" bit31=");
	xzs_diag_emit((value & 0x80000000u) ? "1\n" : "0\n");
}

static void
xzs_display_dump_state(void)
{
	xzs_diag_emit("[XZS-D8M1] DISPLAY_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] mdss_base=0x00900000 source=reference\n");
	xzs_diag_emit("[XZS-D8M1] mdp_base=0x00901000 source=reference\n");
	xzs_diag_emit("[XZS-D8M1] mdss_regs=not_read class=REQUIRES_POWER_DOMAIN\n");
	xzs_mmcc_reg("mmagic_mdss_gdscr", XZS_MMCC_MMAGIC_MDSS_GDSC);
	xzs_mmcc_reg("mdss_gdscr", XZS_MMCC_MDSS_GDSC);
	xzs_diag_emit("[XZS-D8M1] gdsc_bit31=pwr_on bit0=sw_collapse source=mmio\n");
	xzs_diag_emit("[XZS-D8M1] panel=UNKNOWN backlight=UNKNOWN fb_handoff=NONE\n");
	xzs_diag_emit("[XZS-D8M1] DISPLAY_AUDIT_DONE\n");
}

static void
xzs_dsi_dump_state(void)
{
	xzs_diag_emit("[XZS-D8M1] DSI_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] dsi0_base=0x00994000 source=reference class=REQUIRES_POWER_DOMAIN\n");
	xzs_diag_emit("[XZS-D8M1] phy_base=0x00994400 lane=0x00994500 pll=0x00994800 source=reference\n");
	xzs_diag_emit("[XZS-D8M1] dsi_phy_pll_regs=not_read\n");
	xzs_mmcc_reg("mdss_byte0_cbcr", XZS_MMCC_MDSS_BYTE0);
	xzs_mmcc_reg("mdss_pclk0_cbcr", XZS_MMCC_MDSS_PCLK0);
	xzs_mmcc_reg("mdss_esc0_cbcr", XZS_MMCC_MDSS_ESC0);
	xzs_diag_emit("[XZS-D8M1] branch_bit0=enable source=mmio\n");
	xzs_diag_emit("[XZS-D8M1] DSI_AUDIT_DONE\n");
}

static void
xzs_clock_dump_state(void)
{
	xzs_diag_emit("[XZS-D8M1] CLOCK_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] mmcc_base=0x008c0000 source=reference\n");
	xzs_mmcc_reg("mmss_mmagic_cfg_ahb", XZS_MMCC_CFG_AHB);
	xzs_mmcc_reg("mdss_ahb_cbcr", XZS_MMCC_MDSS_AHB);
	xzs_mmcc_reg("mdss_axi_cbcr", XZS_MMCC_MDSS_AXI);
	xzs_mmcc_reg("mdss_mdp_cbcr", XZS_MMCC_MDSS_MDP);
	xzs_diag_emit("[XZS-D8M1] parents=XO GPLL0 MMPLL0 MMPLL5 DSI0PLL source=reference\n");
	xzs_diag_emit("[XZS-D8M1] CLOCK_AUDIT_DONE\n");
}

static void
xzs_irq_dump_state(void)
{
	xzs_diag_emit("[XZS-D8M1] IRQ_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] usb_irq_class=TARGET\n");
	xzs_diag_hex_line("[XZS-D8M1] usb_irq_count=", (uint64_t)g_xzs_usb_irq_count);
	xzs_diag_emit("[XZS-D8M1] uart_irq_class=TARGET\n");
	xzs_diag_hex_line("[XZS-D8M1] uart_irq_count=", (uint64_t)g_xzs_uart_rx_irq_count);
	xzs_diag_emit("[XZS-D8M1] ref_mdss_spi=83 ref_intid=115 class=CODE_AUDIT\n");
	xzs_diag_emit("[XZS-D8M1] gic_ispend_read=no\n");
	xzs_diag_emit("[XZS-D8M1] IRQ_AUDIT_DONE\n");
}

static void
xzs_fb_dump_state(void)
{
	xzs_diag_emit("[XZS-D8M1] FB_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] handoff_class=TARGET\n");
	xzs_diag_hex_line("[XZS-D8M1] handoff_base=", PE_state.video.v_baseAddr);
	xzs_diag_hex_line("[XZS-D8M1] handoff_rowbytes=", PE_state.video.v_rowBytes);
	xzs_diag_hex_line("[XZS-D8M1] handoff_width=", PE_state.video.v_width);
	xzs_diag_hex_line("[XZS-D8M1] handoff_height=", PE_state.video.v_height);
	xzs_diag_hex_line("[XZS-D8M1] handoff_depth=", PE_state.video.v_depth);
	xzs_diag_hex_line("[XZS-D8M1] handoff_display=", PE_state.video.v_display);
	xzs_diag_emit("[XZS-D8M1] pixel_read=no\n");
	xzs_diag_emit("[XZS-D8M1] FB_AUDIT_DONE\n");
}

static void
xzs_memory_dump_state(void)
{
	const boot_args *args = xzs_diag_boot_args();

	xzs_diag_emit("[XZS-D8M1] MEM_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] class=TARGET\n");
	if (args == NULL) {
		xzs_diag_emit("[XZS-D8M1] boot_args=missing\n");
	} else {
		xzs_diag_hex_line("[XZS-D8M1] phys_base=", args->physBase);
		xzs_diag_hex_line("[XZS-D8M1] virt_base=", args->virtBase);
		xzs_diag_hex_line("[XZS-D8M1] mem_size=", args->memSize);
		xzs_diag_hex_line("[XZS-D8M1] mem_size_actual=", args->memSizeActual);
	}
	xzs_diag_hex_line("[XZS-D8M1] diag_retain_bytes=", (uint64_t)s_retain_len);
	xzs_diag_emit("[XZS-D8M1] MEM_AUDIT_DONE\n");
}

static void
xzs_d8m2_line(const char *s)
{
	xzs_diag_emit("[D8-M2] ");
	xzs_diag_emit(s);
}

static void
xzs_d8m2_u32(const char *label, uint32_t value)
{
	char line[64];
	static const char hex[] = "0123456789abcdef";
	int i = 0;
	int h;

	while (label[i] != '\0' && i < 40) {
		line[i] = label[i];
		i++;
	}
	line[i++] = '0';
	line[i++] = 'x';
	for (h = 7; h >= 0; h--) {
		line[i++] = hex[(value >> (h * 4)) & 0xf];
	}
	line[i++] = '\n';
	line[i] = '\0';
	xzs_diag_emit(line);
}

static __attribute__((noinline)) void
xzs_mmcc_map_begin(uint64_t *saved)
{
	__asm__ volatile("mrs %0, TTBR0_EL1" : "=r"(*saved));
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0\n\tisb sy" :: "r"(g_xzs_ttbr0) : "memory");
	}
}

static __attribute__((noinline)) void
xzs_mmcc_map_end(uint64_t saved)
{
	__asm__ volatile("dsb sy\n\tisb sy" ::: "memory");
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0\n\tisb sy" :: "r"(saved) : "memory");
	}
}

static void
xzs_mmcc_write32(uint32_t offset, uint32_t value)
{
	uint64_t saved = 0;

	xzs_mmcc_map_begin(&saved);
	*(volatile uint32_t *)(XZS_MMCC_BASE + offset) = value;
	xzs_mmcc_map_end(saved);
}

static uint32_t
xzs_mmcc_rmw(uint32_t offset, uint32_t mask, uint32_t value)
{
	uint32_t next = (xzs_mmcc_read32(offset) & ~mask) | (value & mask);

	xzs_mmcc_write32(offset, next);
	return next;
}

static int
xzs_gdsc_already_on(uint32_t value)
{
	return (value & XZS_GDSC_PWR_ON) != 0 && (value & XZS_GDSC_SW_COLLAPSE) == 0;
}

static int
xzs_branch_running(uint32_t value)
{
	uint32_t fsm = (value >> 28) & 7u;

	return (value & XZS_CBCR_ENABLE) != 0 &&
	    ((value & XZS_CBCR_CLK_OFF) == 0 || fsm == 2u);
}

/*
 * Poll matches Linux gdsc_poll_status: STATUS_POLL_TIMEOUT_US is 2000.
 * delay() is microseconds. The loop always returns.
 */
static int
xzs_poll_bit31(uint32_t offset, int want_set, uint32_t *last)
{
	int i;

	for (i = 0; i < 2000; i++) {
		uint32_t value = xzs_mmcc_read32(offset);

		*last = value;
		if (want_set) {
			if ((value & XZS_GDSC_PWR_ON) != 0) {
				return 1;
			}
		} else if (xzs_branch_running(value)) {
			return 1;
		}
		delay(1);
	}
	*last = xzs_mmcc_read32(offset);
	if (want_set) {
		return (*last & XZS_GDSC_PWR_ON) != 0;
	}
	return xzs_branch_running(*last);
}

static void
xzs_d8m2_finish(const char *result)
{
	xzs_d8m2_line("RESULT=");
	xzs_diag_emit(result);
	xzs_diag_emit("\n[D8-M2] POST\n");
}

static void
xzs_d8m2_power_status(void)
{
	uint32_t mmagic;
	uint32_t hw;
	uint32_t mdss;

	xzs_d8m2_line("ACTION=PWR-STATUS-001\n");
	xzs_d8m2_line("PRE\n");
	mmagic = xzs_mmcc_read32(XZS_MMCC_MMAGIC_MDSS_GDSC);
	hw = xzs_mmcc_read32(XZS_MMCC_MMAGIC_HW_CTRL);
	mdss = xzs_mmcc_read32(XZS_MMCC_MDSS_GDSC);
	xzs_d8m2_u32("[D8-M2] mmagic=", mmagic);
	xzs_d8m2_u32("[D8-M2] mmagic_hw=", hw);
	xzs_d8m2_u32("[D8-M2] mdss=", mdss);
	xzs_d8m2_line("APPLY\n");
	xzs_d8m2_line("write=none\n");
	xzs_d8m2_finish("PASS");
}

static void
xzs_d8m2_mmagic_on(void)
{
	uint32_t old;
	uint32_t hw;
	uint32_t wrote;
	uint32_t readback;

	xzs_d8m2_line("ACTION=PWR-MMAGIC-001\n");
	xzs_d8m2_line("PRE\n");
	old = xzs_mmcc_read32(XZS_MMCC_MMAGIC_MDSS_GDSC);
	hw = xzs_mmcc_read32(XZS_MMCC_MMAGIC_HW_CTRL);
	xzs_d8m2_u32("[D8-M2] old=", old);
	xzs_d8m2_u32("[D8-M2] hw_old=", hw);
	xzs_d8m2_line("APPLY\n");
	if (xzs_gdsc_already_on(old)) {
		xzs_d8m2_line("write=none\n");
		xzs_d8m2_u32("[D8-M2] new=", old);
		xzs_d8m2_finish("ALREADY_ON");
		return;
	}
	/*
	 * Linux gdsc_enable for mmagic_mdss: clear SW_COLLAPSE, udelay(1)
	 * because gds_hw_ctrl is set, poll PWR_ON on 0x2480, udelay(1),
	 * then set HW_CONTROL. No resets, clamps, or cxc retain bits.
	 */
	wrote = xzs_mmcc_rmw(XZS_MMCC_MMAGIC_MDSS_GDSC, XZS_GDSC_SW_COLLAPSE, 0);
	xzs_d8m2_u32("[D8-M2] wrote=", wrote);
	delay(1);
	if (!xzs_poll_bit31(XZS_MMCC_MMAGIC_HW_CTRL, 1, &readback)) {
		xzs_d8m2_u32("[D8-M2] readback=", readback);
		xzs_d8m2_finish("TIMEOUT");
		return;
	}
	delay(1);
	wrote = xzs_mmcc_rmw(XZS_MMCC_MMAGIC_MDSS_GDSC, XZS_GDSC_HW_CONTROL, XZS_GDSC_HW_CONTROL);
	delay(1);
	readback = xzs_mmcc_read32(XZS_MMCC_MMAGIC_HW_CTRL);
	xzs_d8m2_u32("[D8-M2] wrote=", wrote);
	xzs_d8m2_u32("[D8-M2] new=", xzs_mmcc_read32(XZS_MMCC_MMAGIC_MDSS_GDSC));
	xzs_d8m2_u32("[D8-M2] readback=", readback);
	if ((readback & XZS_GDSC_PWR_ON) == 0) {
		xzs_d8m2_finish("FAIL");
		return;
	}
	xzs_d8m2_finish("PASS");
}

static void
xzs_d8m2_mdss_on(void)
{
	uint32_t parent;
	uint32_t old;
	uint32_t wrote;
	uint32_t readback;
	uint32_t axi;
	uint32_t mdp;

	xzs_d8m2_line("ACTION=PWR-MDSS-001\n");
	xzs_d8m2_line("PRE\n");
	parent = xzs_mmcc_read32(XZS_MMCC_MMAGIC_MDSS_GDSC);
	old = xzs_mmcc_read32(XZS_MMCC_MDSS_GDSC);
	xzs_d8m2_u32("[D8-M2] parent=", parent);
	xzs_d8m2_u32("[D8-M2] old=", old);
	xzs_d8m2_line("APPLY\n");
	if (!xzs_gdsc_already_on(parent)) {
		xzs_d8m2_line("write=none\n");
		xzs_d8m2_finish("PARENT_OFF");
		return;
	}
	if (xzs_gdsc_already_on(old)) {
		xzs_d8m2_line("write=none\n");
		xzs_d8m2_u32("[D8-M2] new=", old);
		xzs_d8m2_finish("ALREADY_ON");
		return;
	}
	/*
	 * Linux gdsc_enable for mdss: no SW_RESET, no CLAMP_IO, no HW_CTRL.
	 * Clear SW_COLLAPSE and poll PWR_ON on the gdscr. pwrsts includes
	 * OFF, so then set RETAIN_MEM|RETAIN_PERIPH on cxcs 0x2310 and
	 * 0x231c. Those bits are not the branch enable. udelay(1) after.
	 */
	wrote = xzs_mmcc_rmw(XZS_MMCC_MDSS_GDSC, XZS_GDSC_SW_COLLAPSE, 0);
	xzs_d8m2_u32("[D8-M2] wrote=", wrote);
	if (!xzs_poll_bit31(XZS_MMCC_MDSS_GDSC, 1, &readback)) {
		xzs_d8m2_u32("[D8-M2] readback=", readback);
		xzs_d8m2_finish("TIMEOUT");
		return;
	}
	(void)xzs_mmcc_rmw(XZS_MMCC_MDSS_AXI, XZS_CBCR_RETAIN, XZS_CBCR_RETAIN);
	(void)xzs_mmcc_rmw(XZS_MMCC_MDSS_MDP, XZS_CBCR_RETAIN, XZS_CBCR_RETAIN);
	delay(1);
	axi = xzs_mmcc_read32(XZS_MMCC_MDSS_AXI);
	mdp = xzs_mmcc_read32(XZS_MMCC_MDSS_MDP);
	readback = xzs_mmcc_read32(XZS_MMCC_MDSS_GDSC);
	xzs_d8m2_u32("[D8-M2] retain_axi=", axi);
	xzs_d8m2_u32("[D8-M2] retain_mdp=", mdp);
	xzs_d8m2_u32("[D8-M2] new=", readback);
	xzs_d8m2_u32("[D8-M2] readback=", readback);
	if (!xzs_gdsc_already_on(readback)) {
		xzs_d8m2_finish("FAIL");
		return;
	}
	xzs_d8m2_finish("PASS");
}

static void
xzs_d8m2_branch_on(const char *action, uint32_t offset)
{
	uint32_t domain;
	uint32_t old;
	uint32_t wrote;
	uint32_t readback;

	xzs_d8m2_line("ACTION=");
	xzs_diag_emit(action);
	xzs_diag_emit("\n");
	xzs_d8m2_line("PRE\n");
	domain = xzs_mmcc_read32(XZS_MMCC_MDSS_GDSC);
	old = xzs_mmcc_read32(offset);
	xzs_d8m2_u32("[D8-M2] domain=", domain);
	xzs_d8m2_u32("[D8-M2] old=", old);
	xzs_d8m2_line("APPLY\n");
	if (!xzs_gdsc_already_on(domain)) {
		xzs_d8m2_line("write=none\n");
		xzs_d8m2_finish("POWER_OFF");
		return;
	}
	if (xzs_branch_running(old)) {
		xzs_d8m2_line("write=none\n");
		xzs_d8m2_u32("[D8-M2] new=", old);
		xzs_d8m2_finish("ALREADY_ON");
		return;
	}
	/*
	 * clk_branch2 enable sets CBCR bit 0, then polls CBCR_CLK_OFF clear
	 * or the NoC FSM ON state. Halt check is BRANCH_HALT. 200 x 1 us in
	 * Linux; this poll uses the same 2000 us cap as the GDSC poll.
	 */
	wrote = xzs_mmcc_rmw(offset, XZS_CBCR_ENABLE, XZS_CBCR_ENABLE);
	xzs_d8m2_u32("[D8-M2] wrote=", wrote);
	if (!xzs_poll_bit31(offset, 0, &readback)) {
		xzs_d8m2_u32("[D8-M2] readback=", readback);
		xzs_d8m2_finish("TIMEOUT");
		return;
	}
	xzs_d8m2_u32("[D8-M2] new=", readback);
	xzs_d8m2_u32("[D8-M2] readback=", readback);
	xzs_d8m2_finish("PASS");
}

static const char *
xzs_ahb_src_name(uint32_t cfg)
{
	switch ((cfg >> 8) & 7u) {
	case 0:
		return "XO";
	case 1:
		return "MMPLL0";
	case 5:
		return "GPLL0";
	case 2:
		return "MMPLL1";
	case 6:
		return "GPLL0_DIV";
	default:
		return "unknown";
	}
}

static void
xzs_d8m2_bit(const char *label, int set)
{
	xzs_d8m2_line(label);
	xzs_diag_emit(set ? "1\n" : "0\n");
}

/*
 * Read-only view of mdss_ahb and its audited parents.
 * ahb_clk_src is MMCC CMD 0x5000 / CFG 0x5004. Parent map is
 * XO=0, MMPLL0=1, GPLL0=5, GPLL0_DIV=6. Linux treats the RCG as
 * enabled when CMD bit 31 (ROOT_OFF) is clear. The branch parent
 * is that RCG. mmss_mmagic_ahb (0x5024) and mmss_mmagic_cfg_ahb
 * (0x5054) share it. mmagic_mdss_noc_cfg_ahb (0x2478) is parented
 * by gcc_mmss_noc_cfg_ahb at GCC 0x00300000 + 0x9008.
 */
static void
xzs_d8m2_ahb_status(void)
{
	uint32_t branch;
	uint32_t cmd;
	uint32_t cfg;
	uint32_t mmagic_ahb;
	uint32_t cfg_ahb;
	uint32_t noc;
	uint32_t gcc;

	xzs_d8m2_line("ACTION=CLK-AHB-STATUS-001\n");
	xzs_d8m2_line("PRE\n");
	branch = xzs_mmcc_read32(XZS_MMCC_MDSS_AHB);
	cmd = xzs_mmcc_read32(XZS_MMCC_AHB_CMD);
	cfg = xzs_mmcc_read32(XZS_MMCC_AHB_CFG);
	mmagic_ahb = xzs_mmcc_read32(XZS_MMCC_MMAGIC_AHB);
	cfg_ahb = xzs_mmcc_read32(XZS_MMCC_CFG_AHB);
	noc = xzs_mmcc_read32(XZS_MMCC_MMAGIC_MDSS_NOC);
	gcc = xzs_phys_read32(XZS_GCC_BASE + XZS_GCC_MMSS_NOC_CFG_AHB);
	xzs_d8m2_u32("[D8-M2] ahb_cbcr=", branch);
	xzs_d8m2_u32("[D8-M2] ahb_cmd=", cmd);
	xzs_d8m2_u32("[D8-M2] ahb_cfg=", cfg);
	xzs_d8m2_line("src=");
	xzs_diag_emit(xzs_ahb_src_name(cfg));
	xzs_diag_emit("\n");
	xzs_d8m2_bit("root_off=", (cmd & XZS_CBCR_CLK_OFF) != 0);
	xzs_d8m2_bit("root_en=", (cmd & XZS_GDSC_HW_CONTROL) != 0);
	xzs_d8m2_bit("cmd_update=", (cmd & XZS_CBCR_ENABLE) != 0);
	xzs_d8m2_u32("[D8-M2] mmagic_ahb=", mmagic_ahb);
	xzs_d8m2_u32("[D8-M2] mmagic_cfg_ahb=", cfg_ahb);
	xzs_d8m2_u32("[D8-M2] mmagic_mdss_noc=", noc);
	xzs_d8m2_u32("[D8-M2] gcc_mmss_noc=", gcc);
	xzs_d8m2_u32("[D8-M2] mdss=", xzs_mmcc_read32(XZS_MMCC_MDSS_GDSC));
	xzs_d8m2_line("APPLY\n");
	xzs_d8m2_line("write=none\n");
	xzs_d8m2_finish("PASS");
}

/*
 * BCR bit 0 is the assert control from qcom_reset_set_assert:
 * writing 1 asserts and writing 0 deasserts. There is no status
 * callback. reset() pulses that bit. A read is the retained level.
 */
static void
xzs_d8m2_bcr(const char *name, uint32_t offset)
{
	uint32_t value = xzs_mmcc_read32(offset);

	xzs_d8m2_u32(name, value);
	xzs_d8m2_line("bcr_bit0=");
	xzs_diag_emit((value & 1u) ? "1" : "0");
	xzs_diag_emit(" source=mmio assert_control=reference\n");
}

static void
xzs_d8m2_ahb_debug(void)
{
	uint32_t mode;
	uint32_t vote;

	xzs_d8m2_ahb_status();
	xzs_d8m2_line("ACTION=CLK-AHB-DEBUG-001\n");
	xzs_d8m2_line("PRE\n");
	xzs_d8m2_bcr("[D8-M2] mdss_bcr=", XZS_MMCC_MDSS_BCR);
	xzs_d8m2_bcr("[D8-M2] mmagic_mdss_bcr=", XZS_MMCC_MMAGIC_MDSS_BCR);
	xzs_d8m2_bcr("[D8-M2] mmagic_ahb_bcr=", XZS_MMCC_MMAGIC_AHB_BCR);
	xzs_d8m2_bcr("[D8-M2] mmagic_cfg_bcr=", XZS_MMCC_MMAGIC_CFG_BCR);
	mode = xzs_phys_read32(XZS_GCC_BASE + XZS_GCC_GPLL0_MODE);
	vote = xzs_phys_read32(XZS_GCC_BASE + XZS_GCC_GPLL0_VOTE);
	xzs_d8m2_u32("[D8-M2] gpll0_mode=", mode);
	xzs_d8m2_bit("gpll0_lock=", (mode & 0x80000000u) != 0);
	xzs_diag_emit("[D8-M2] gpll0_lock_bit=PLL_LOCK_DET source=reference\n");
	xzs_d8m2_u32("[D8-M2] gpll0_vote=", vote);
	xzs_d8m2_bit("gpll0_vote_en=", (vote & 1u) != 0);
	xzs_d8m2_line("APPLY\n");
	xzs_d8m2_line("write=none\n");
	xzs_d8m2_finish("PASS");
}

static void
xzs_d8m2_branch_line(const char *name, uint32_t value)
{
	xzs_d8m2_u32(name, value);
	xzs_d8m2_line("enable=");
	xzs_diag_emit((value & 1u) ? "1" : "0");
	xzs_diag_emit(" halt=");
	xzs_diag_emit((value & 0x80000000u) ? "1" : "0");
	xzs_diag_emit(" source=mmio\n");
}

static void
xzs_d8m2_critical_status(void)
{
	uint32_t ahb_cmd;
	uint32_t ahb_cfg;
	uint32_t axi_cmd;
	uint32_t axi_cfg;

	xzs_d8m2_line("ACTION=CLK-CRIT-STATUS-001\n");
	xzs_d8m2_line("PRE\n");
	xzs_d8m2_u32("[D8-M2] mmagic=", xzs_mmcc_read32(XZS_MMCC_MMAGIC_MDSS_GDSC));
	xzs_d8m2_branch_line("[D8-M2] mmagic_ahb=", xzs_mmcc_read32(XZS_MMCC_MMAGIC_AHB));
	xzs_d8m2_branch_line("[D8-M2] mmagic_cfg_ahb=", xzs_mmcc_read32(XZS_MMCC_CFG_AHB));
	xzs_d8m2_branch_line("[D8-M2] mmagic_mdss_noc=", xzs_mmcc_read32(XZS_MMCC_MMAGIC_MDSS_NOC));
	xzs_d8m2_branch_line("[D8-M2] mmagic_mdss_axi=", xzs_mmcc_read32(XZS_MMCC_MMAGIC_MDSS_AXI));
	ahb_cmd = xzs_mmcc_read32(XZS_MMCC_AHB_CMD);
	ahb_cfg = xzs_mmcc_read32(XZS_MMCC_AHB_CFG);
	axi_cmd = xzs_mmcc_read32(XZS_MMCC_AXI_CMD);
	axi_cfg = xzs_mmcc_read32(XZS_MMCC_AXI_CFG);
	xzs_d8m2_u32("[D8-M2] ahb_cmd=", ahb_cmd);
	xzs_d8m2_u32("[D8-M2] ahb_cfg=", ahb_cfg);
	xzs_d8m2_line("ahb_src=");
	xzs_diag_emit(xzs_ahb_src_name(ahb_cfg));
	xzs_diag_emit(" source=mmio\n");
	xzs_d8m2_bit("ahb_root_off=", (ahb_cmd & 0x80000000u) != 0);
	xzs_d8m2_u32("[D8-M2] axi_cmd=", axi_cmd);
	xzs_d8m2_u32("[D8-M2] axi_cfg=", axi_cfg);
	xzs_d8m2_line("axi_src=");
	xzs_diag_emit(xzs_ahb_src_name(axi_cfg));
	xzs_diag_emit(" source=mmio map=XO0,MMPLL0_1,MMPLL1_2,GPLL0_5,GPLL0_DIV_6 source=reference\n");
	xzs_d8m2_bit("axi_root_off=", (axi_cmd & 0x80000000u) != 0);
	xzs_d8m2_u32("[D8-M2] gcc_mmss_noc=",
	    xzs_phys_read32(XZS_GCC_BASE + XZS_GCC_MMSS_NOC_CFG_AHB));
	xzs_d8m2_line("APPLY\n");
	xzs_d8m2_line("write=none\n");
	xzs_d8m2_finish("PASS");
}

/*
 * These branches live in MMCC, not behind MDSS_GDSC. Linux enables
 * them from CLK_IS_CRITICAL during MMCC registration, before MDSS
 * powers on. The parent root must already be running. Only bit 0 is
 * written. Halt polling is the same 2000 us branch2 check.
 */
static void
xzs_d8m2_critical_on(const char *action, uint32_t offset, int parent)
{
	uint32_t gate;
	uint32_t old;
	uint32_t wrote;
	uint32_t readback;
	int ready;

	xzs_d8m2_line("ACTION=");
	xzs_diag_emit(action);
	xzs_diag_emit("\n");
	xzs_d8m2_line("PRE\n");
	if (parent == 1) {
		gate = xzs_phys_read32(XZS_GCC_BASE + XZS_GCC_MMSS_NOC_CFG_AHB);
		ready = (gate & 1u) != 0 && (gate & 0x80000000u) == 0;
	} else if (parent == 2) {
		gate = xzs_mmcc_read32(XZS_MMCC_AXI_CMD);
		ready = (gate & 0x80000000u) == 0;
	} else {
		gate = xzs_mmcc_read32(XZS_MMCC_AHB_CMD);
		ready = (gate & 0x80000000u) == 0;
	}
	old = xzs_mmcc_read32(offset);
	xzs_d8m2_u32("[D8-M2] parent=", gate);
	xzs_d8m2_u32("[D8-M2] old=", old);
	xzs_d8m2_line("APPLY\n");
	if (!ready) {
		xzs_d8m2_line("write=none\n");
		xzs_d8m2_finish("PARENT_OFF");
		return;
	}
	if (xzs_branch_running(old)) {
		xzs_d8m2_line("write=none\n");
		xzs_d8m2_u32("[D8-M2] new=", old);
		xzs_d8m2_finish("ALREADY_ON");
		return;
	}
	wrote = xzs_mmcc_rmw(offset, XZS_CBCR_ENABLE, XZS_CBCR_ENABLE);
	xzs_d8m2_u32("[D8-M2] wrote=", wrote);
	if (!xzs_poll_bit31(offset, 0, &readback)) {
		xzs_d8m2_u32("[D8-M2] readback=", readback);
		xzs_d8m2_finish("TIMEOUT");
		return;
	}
	xzs_d8m2_u32("[D8-M2] new=", readback);
	xzs_d8m2_u32("[D8-M2] readback=", readback);
	xzs_d8m2_finish("PASS");
}

#include "xzs_d8m3.h"
#include "xzs_d8m4.h"
#include "xzs_d8p1.h"
#include "xzs_d8p2.h"
#include "xzs_d8m5.h"
#include "xzs_d8m6.h"
#include "xzs_d8m8.h"

void
xzs_diag_dispatch(uint64_t which, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
	xzs_watchdog_pet();
	switch (which) {
	case 1:
		xzs_display_dump_state();
		break;
	case 2:
		xzs_dsi_dump_state();
		break;
	case 3:
		xzs_clock_dump_state();
		break;
	case 4:
		xzs_irq_dump_state();
		break;
	case 5:
		xzs_fb_dump_state();
		break;
	case 6:
		xzs_memory_dump_state();
		break;
	case 7:
		xzs_d8m2_power_status();
		break;
	case 8:
		xzs_d8m2_mmagic_on();
		break;
	case 9:
		xzs_d8m2_mdss_on();
		break;
	case 10:
		xzs_d8m2_branch_on("CLK-AHB-001", XZS_MMCC_MDSS_AHB);
		break;
	case 11:
		xzs_d8m2_branch_on("CLK-AXI-001", XZS_MMCC_MDSS_AXI);
		break;
	case 12:
		xzs_d8m2_branch_on("CLK-MDP-001", XZS_MMCC_MDSS_MDP);
		break;
	case 13:
		xzs_d8m2_ahb_status();
		break;
	case 14:
		xzs_d8m2_ahb_debug();
		break;
	case 15:
		xzs_d8m2_critical_status();
		break;
	case 16:
		xzs_d8m2_critical_on("CLK-MMAGIC-AHB-001", XZS_MMCC_MMAGIC_AHB, 0);
		break;
	case 17:
		xzs_d8m2_critical_on("CLK-MMAGIC-CFG-001", XZS_MMCC_CFG_AHB, 0);
		break;
	case 18:
		xzs_d8m2_critical_on("CLK-MMAGIC-NOC-001", XZS_MMCC_MMAGIC_MDSS_NOC, 1);
		break;
	case 19:
		xzs_d8m2_critical_on("CLK-MMAGIC-AXI-001", XZS_MMCC_MMAGIC_MDSS_AXI, 2);
		break;
	case 20: {
		extern void xzs_diag_xzsfs_ubc(uint64_t user_path);
		xzs_diag_xzsfs_ubc(arg1);
		break;
	}
	case 21: {
		extern void xzs_diag_xzsfs_pagecheck(uint64_t user_path, uint64_t offset, uint64_t size);
		xzs_diag_xzsfs_pagecheck(arg1, arg2, arg3);
		break;
	}
	case 22:
		xzs_d8m3_run(0);
		break;
	case 23:
		xzs_d8m3_run(1);
		break;
	case 24:
		xzs_d8m3_run(2);
		break;
	case 25:
		xzs_d8m3_dump_regs();
		break;
	case 26:
		xzs_d8m3_dump_pll();
		break;
	case 27:
		xzs_d8m3_dump_phy();
		break;
	case 28:
		xzs_d8m4_dump_status();
		break;
	case 29:
		xzs_d8m4_run(0);
		break;
	case 30:
		xzs_d8m4_run(1);
		break;
	case 31:
		xzs_d8m4_run(2);
		break;
	case 32:
		xzs_d8p1_dump_gpio_status();
		break;
	case 33:
		xzs_d8p1_run(0);
		break;
	case 34:
		xzs_d8p1_run(1);
		break;
	case 35:
		xzs_d8p2_dump_spmi_status();
		break;
	case 36:
		xzs_d8p2_dump_lab_status();
		break;
	case 37:
		xzs_d8p2_dump_ibb_status();
		break;
	case 38:
		xzs_d8p2_dryrun();
		break;
	case 39:
		xzs_d8p2_run(1);
		break;
	case 40:
		xzs_d8p2_run(2);
		break;
	case 41:
		xzs_d8m5_status();
		break;
	case 42:
		xzs_d8m5_dryrun();
		break;
	case 43:
		xzs_d8m5_stage1();
		break;
	case 44:
		xzs_d8m5_run();
		break;
	case 45:
		xzs_d8m6_dryrun();
		break;
	case 46:
		xzs_d8m6_stage1();
		break;
	case 47:
		xzs_d8m6_stage2();
		break;
	case 48:
		xzs_d8m6_run();
		break;
	case 50:
	case 51:
		xzs_d8m8_panel_prepare();
		break;
	case 52:
		xzs_d8m8_fb_init();
		break;
	case 53:
		xzs_d8m8_rgb0_config();
		break;
	case 54:
		xzs_d8m8_lm0_config();
		break;
	case 55:
		xzs_d8m8_stream_config();
		break;
	case 56:
		xzs_d8m8_ctl_config();
		break;
	case 57:
		xzs_d8m8_flush_config();
		break;
	case 58:
		xzs_d8m8_prekick_status();
		break;
	case 59:
		xzs_d8m8_kickoff();
		break;
	case 60:
		xzs_d8m8_vsync_clock_on();
		break;
	default:
		xzs_diag_emit("[XZS-D8M1] unknown diag\n");
		break;
	}
}
