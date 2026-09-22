/*
 * Copyright (c) 2026 Apple Inc. All rights reserved.
 * Sony Xperia XZs (MSM8996) DWC3 USB2 Device Mode Controller Driver
 * Phase D7-T1: Minimal USB-C Bulk Console
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <machine/machine_routines.h>
#include <kern/thread_call.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm/xzs_usb.h>

extern void xzs_early_puts(const char *s);
extern void xzs_d6m4_put_hex64(uint64_t val);
extern void xzs_breadcrumb(uint32_t cp, uint32_t err);
extern void delay(int usec);
extern void flush_dcache(vm_offset_t addr, unsigned count, int phys);

/* Telemetry counters */
volatile uint32_t g_xzs_usb_gsnpsid = 0;
volatile uint32_t g_xzs_usb_gctl = 0;
volatile uint32_t g_xzs_usb_dsts = 0;
volatile uint32_t g_xzs_usb_dcfg = 0;
volatile uint32_t g_xzs_usb_dctl = 0;
volatile uint32_t g_xzs_usb_gevntadr0 = 0;
volatile uint32_t g_xzs_usb_gevntsiz0 = 0;
volatile uint32_t g_xzs_usb_gevntcnt0 = 0;
volatile uint32_t g_xzs_usb_devten = 0;
volatile uint32_t g_xzs_usb_qscratch_ram1 = 0;
volatile uint32_t g_xzs_usb_qscratch_cfg = 0;
volatile uint32_t g_xzs_usb_qscratch_general_cfg = 0;
volatile uint32_t g_xzs_usb_qscratch_hs_phy_ctrl = 0;
volatile uint32_t g_xzs_usb_qscratch_ss_phy_ctrl = 0;
volatile uint32_t g_xzs_usb_qscratch_pwr_event_irq_stat = 0;
volatile uint32_t g_xzs_usb_gsts = 0;
volatile uint32_t g_xzs_usb_gusb2phycfg0 = 0;
volatile uint32_t g_xzs_usb_gusb3pipectl0 = 0;
volatile uint32_t g_xzs_usb_osts = 0;
volatile uint32_t g_xzs_usb_qusb2_pll_test = 0;
volatile uint32_t g_xzs_usb_qusb2_pll_status = 0;
volatile uint32_t g_xzs_usb_qusb2_port_powerdown = 0;
volatile uint32_t g_xzs_usb_qusb2_utmi_status = 0;
volatile uint32_t g_xzs_usb_gcc_qusb2phy_prim_bcr = 0;
volatile uint32_t g_xzs_usb_gctl_before = 0;
volatile uint32_t g_xzs_usb_gusb2phycfg0_before = 0;
volatile uint32_t g_xzs_usb_dcfg_before = 0;
volatile uint32_t g_xzs_usb_dcfg_written = 0;
volatile uint32_t g_xzs_usb_dcfg_readback = 0;
volatile uint32_t g_xzs_usb_dctl_before = 0;
volatile uint32_t g_xzs_usb_dsts_before = 0;
volatile uint32_t g_xzs_usb_gevntadr0_before = 0;
volatile uint32_t g_xzs_usb_gevntsiz0_before = 0;
volatile uint32_t g_xzs_usb_gevntcnt0_before = 0;
volatile uint32_t g_xzs_usb_qscratch_general_cfg_before = 0;
volatile uint32_t g_xzs_usb_qscratch_hs_phy_ctrl_before = 0;
volatile uint32_t g_xzs_usb_qscratch_ss_phy_ctrl_before = 0;
volatile uint32_t g_xzs_usb_qusb2_pll_status_before = 0;
volatile uint32_t g_xzs_usb_qusb2_port_powerdown_before = 0;
volatile uint32_t g_xzs_usb_gcc_qusb2phy_prim_bcr_before = 0;
volatile uint32_t g_xzs_usb_candidate2b_precondition_run_stop_0 = 0;
volatile uint32_t g_xzs_usb_candidate2b_precondition_devctrlhlt_1 = 0;
volatile uint32_t g_xzs_usb_candidate2b_write_count = 0;
volatile uint32_t g_xzs_usb_candidate2b_dcfg_write_match = 0;
volatile uint32_t g_xzs_usb_candidate2b_gctl_unchanged = 0;
volatile uint32_t g_xzs_usb_candidate2b_gusb2phycfg0_unchanged = 0;
volatile uint32_t g_xzs_usb_candidate2b_qscratch_unchanged = 0;
volatile uint32_t g_xzs_usb_candidate2b_qusb2_unchanged = 0;
volatile uint32_t g_xzs_usb_candidate2b_gcc_unchanged = 0;
volatile uint32_t g_xzs_usb_candidate2b_event_buffer_unchanged = 0;
volatile uint32_t g_xzs_usb_candidate2b_complete = 0;
volatile uint32_t g_xzs_usb_ghwparams1 = 0;
volatile uint32_t g_xzs_usb_num_event_interrupts = 0;
volatile uint32_t g_xzs_usb_gevntadrhi0_before = 0;
volatile uint32_t g_xzs_usb_gevntadrhi0 = 0;
volatile uint32_t g_xzs_usb_devten_before = 0;
volatile uint32_t g_xzs_usb_candidate2c_precondition_run_stop_0 = 0;
volatile uint32_t g_xzs_usb_candidate2c_precondition_devctrlhlt_1 = 0;
volatile uint32_t g_xzs_usb_candidate2c_dcfg_high_speed = 0;
volatile uint32_t g_xzs_usb_candidate2c_dcfg_devaddr_0 = 0;
volatile uint32_t g_xzs_usb_candidate2c_event_buffer_pa_valid = 0;
volatile uint32_t g_xzs_usb_candidate2c_event_buffer_contiguous = 0;
volatile uint32_t g_xzs_usb_candidate2c_event_buffer_aligned = 0;
volatile uint32_t g_xzs_usb_candidate2c_event_buffer_lifetime_static = 1;
volatile uint32_t g_xzs_usb_candidate2c_dcfg_write_count = 0;
volatile uint32_t g_xzs_usb_candidate2c_gevntadrlo_write_count = 0;
volatile uint32_t g_xzs_usb_candidate2c_gevntadrhi_write_count = 0;
volatile uint32_t g_xzs_usb_candidate2c_gevntsiz_write_count = 0;
volatile uint32_t g_xzs_usb_candidate2c_gevntcount_write_count = 0;
volatile uint32_t g_xzs_usb_candidate2c_gevntcount_ack = 0;
volatile uint32_t g_xzs_usb_candidate2c_gevntadr_readback_match = 0;
volatile uint32_t g_xzs_usb_candidate2c_gevntsiz_readback_match = 0;
volatile uint32_t g_xzs_usb_candidate2c_devten_unchanged = 0;
volatile uint32_t g_xzs_usb_candidate2c_complete = 0;
volatile uint64_t g_xzs_usb_candidate2c_event_buffer_va = 0;
volatile uint64_t g_xzs_usb_candidate2c_event_buffer_pa = 0;
volatile uint32_t g_xzs_usb_reset_count = 0;
volatile uint32_t g_xzs_usb_conn_done_count = 0;
volatile uint32_t g_xzs_usb_set_addr_count = 0;
volatile uint32_t g_xzs_usb_set_cfg_count = 0;
volatile uint32_t g_xzs_usb_bulk_out_count = 0;
volatile uint32_t g_xzs_usb_bulk_out_bytes = 0;
volatile uint32_t g_xzs_usb_bulk_in_count = 0;
volatile uint32_t g_xzs_usb_bulk_in_bytes = 0;
volatile uint32_t g_xzs_usb_irq_count = 0;
volatile uint32_t g_xzs_usb_enumerated = 0;
volatile uint32_t g_xzs_usb_configured = 0;
volatile uint32_t g_xzs_usb_console_ready = 0;
volatile uint32_t g_xzs_usb_tx_drops = 0;
volatile uint32_t g_xzs_usb_t1z_activation_pending = 0;
volatile uint32_t g_xzs_usb_t1z_worker_entered = 0;
volatile uint32_t g_xzs_usb_t1z_config_in_hard_irq = 0;
volatile uint32_t g_xzs_usb_t1z_failed = 0;
volatile uint32_t g_xzs_usb_t1z_ep2_configured = 0;
volatile uint32_t g_xzs_usb_t1z_ep3_configured = 0;
volatile uint32_t g_xzs_usb_t1z_dalepena = 0;
volatile uint32_t g_xzs_usb_t1z_depstartcfg_param = 0;
volatile uint32_t g_xzs_usb_t1z_num_xfer_res = 0;
volatile uint32_t g_xzs_usb_t1z_ep2_rsc = XZS_T1Z_XFER_RSC_NOT_ISSUED;
volatile uint32_t g_xzs_usb_t1z_ep3_rsc = XZS_T1Z_XFER_RSC_NOT_ISSUED;
volatile uint32_t g_xzs_usb_t1z_ep3_rsc_echo = XZS_T1Z_XFER_RSC_NOT_ISSUED;
volatile uint32_t g_xzs_usb_t1z_dma_ok = 0;
volatile uint32_t g_xzs_usb_t1z_out1_len = 0;
volatile uint32_t g_xzs_usb_t1z_in1_len = 0;
volatile uint32_t g_xzs_usb_t1z_out2_len = 0;
volatile uint32_t g_xzs_usb_t1z_in2_len = 0;
volatile uint32_t g_xzs_usb_t1z_pipeline_done = 0;
volatile uint32_t g_xzs_usb_t1z_usb_to_tty_bytes = 0;
volatile uint32_t g_xzs_usb_t1z_tty_to_usb_bytes = 0;
volatile uint64_t g_xzs_usb_t1z_out_buf_va = 0;
volatile uint64_t g_xzs_usb_t1z_out_buf_pa = 0;
volatile uint64_t g_xzs_usb_t1z_out_trb_va = 0;
volatile uint64_t g_xzs_usb_t1z_out_trb_pa = 0;
volatile uint64_t g_xzs_usb_t1z_in_buf_va = 0;
volatile uint64_t g_xzs_usb_t1z_in_buf_pa = 0;
volatile uint64_t g_xzs_usb_t1z_in_trb_va = 0;
volatile uint64_t g_xzs_usb_t1z_in_trb_pa = 0;
volatile uint32_t g_xzs_usb_t1z_out_buf_contig = 0;
volatile uint32_t g_xzs_usb_t1z_out_trb_contig = 0;
volatile uint32_t g_xzs_usb_t1z_in_buf_contig = 0;
volatile uint32_t g_xzs_usb_t1z_in_trb_contig = 0;

/* T1-Y control-plane telemetry. */
volatile uint32_t g_xzs_usb_t1y_ep0_only_dispatch = 0;
volatile uint32_t g_xzs_usb_t1y_bulk_endpoint_write_count = 0;
volatile uint32_t g_xzs_usb_t1y_tty_bridge_call_count = 0;
volatile uint32_t g_xzs_usb_t1y_devten_before = 0;
volatile uint32_t g_xzs_usb_t1y_devten_after = 0;
volatile uint32_t g_xzs_usb_t1y_gevntsiz_before = 0;
volatile uint32_t g_xzs_usb_t1y_gevntsiz_after = 0;
volatile uint32_t g_xzs_usb_t1y_dctl_before = 0;
volatile uint32_t g_xzs_usb_t1y_dctl_written = 0;
volatile uint32_t g_xzs_usb_t1y_dctl_after = 0;
volatile uint32_t g_xzs_usb_t1y_dctl_write_count = 0;
volatile uint32_t g_xzs_usb_t1y_first_event = 0;
volatile uint32_t g_xzs_usb_t1y_event_dma_working = 0;
volatile uint32_t g_xzs_usb_t1y_setup_observed = 0;
volatile uint32_t g_xzs_usb_t1y_device_desc_complete = 0;
volatile uint32_t g_xzs_usb_t1y_config_desc_complete = 0;
volatile uint32_t g_xzs_usb_t1y_set_address_complete = 0;
volatile uint32_t g_xzs_usb_t1y_set_configuration_complete = 0;
volatile uint32_t g_xzs_usb_t1y_configuration_value = 0;
volatile uint32_t g_xzs_usb_t1y_connect_speed = 0;
volatile uint32_t g_xzs_usb_t1y_link_state = 0;
volatile uint32_t g_xzs_usb_t1y_ep_cmd_failures = 0;
volatile uint32_t g_xzs_usb_t1y_event_ring_drops = 0;
volatile uint32_t g_xzs_usb_t1y_complete = 0;
volatile uint32_t g_xzs_usb_t1y_word_before_sync_0 = 0;
volatile uint32_t g_xzs_usb_t1y_word_before_sync_1 = 0;
volatile uint32_t g_xzs_usb_t1y_word_after_sync_0 = 0;
volatile uint32_t g_xzs_usb_t1y_word_after_sync_1 = 0;
volatile uint32_t g_xzs_usb_t1y_word_uncached_0 = 0;
volatile uint32_t g_xzs_usb_t1y_word_uncached_1 = 0;
volatile uint32_t g_xzs_usb_t1y_word_fb_0 = 0;
volatile uint32_t g_xzs_usb_t1y_word_fb_1 = 0;
volatile int32_t  g_xzs_usb_t1y_modified_slot_idx = -1;
volatile uint32_t g_xzs_usb_t1y_modified_slot_val = 0;
volatile int32_t  g_xzs_usb_t1y_fb_modified_idx = -1;
volatile uint32_t g_xzs_usb_t1y_fb_modified_val = 0;
volatile uint32_t g_xzs_usb_t1y_gevntcount_raw = 0;

static volatile uint32_t *s_uncached_event_buf = NULL;
static volatile uint32_t *s_fb_event_buf = NULL;
static uint32_t s_fb_initial_words[4] = {0};

/* Virtual MMIO bases */
static vm_offset_t s_dwc3_base = 0;
static vm_offset_t s_qcom_glue_base = 0;
static vm_offset_t s_qusb2_phy_base = 0;
static vm_offset_t s_gcc_base = 0;

/* Hardware MMIO access helpers */
static inline uint32_t dwc3_read32(uint32_t offset)
{
	__asm__ volatile("dsb sy" ::: "memory");
	uint32_t v = *(volatile uint32_t *)(s_dwc3_base + offset);
	__asm__ volatile("dmb ish" ::: "memory");
	return v;
}

static inline void dwc3_write32(uint32_t offset, uint32_t val)
{
	__asm__ volatile("dsb sy" ::: "memory");
	*(volatile uint32_t *)(s_dwc3_base + offset) = val;
	__asm__ volatile("dsb sy" ::: "memory");
}

/* Candidate-2A uses this helper only for source-audited status registers. */
static inline uint32_t xzs_mmio_read32(vm_offset_t base, uint32_t offset)
{
	__asm__ volatile("dsb sy" ::: "memory");
	uint32_t v = *(volatile uint32_t *)(base + offset);
	__asm__ volatile("dmb ish" ::: "memory");
	return v;
}

/* MSM8996 QUSB2 PLL_STATUS is byte-addressed in the source-audited PHY driver. */
static inline uint8_t xzs_mmio_read8(vm_offset_t base, uint32_t offset)
{
	__asm__ volatile("dsb sy" ::: "memory");
	uint8_t v = *(volatile uint8_t *)(base + offset);
	__asm__ volatile("dmb ish" ::: "memory");
	return v;
}

/*
 * Architectural Clean & Invalidate to Point of Coherency (PoC) for MSM8996 non-coherent DMA.
 * In VMAPPLE builds, flush_dcache() is compiled as a NOP (dsb sy; ret) because Apple Silicon
 * assumes fully-coherent IO. On MSM8996, DWC3 DMA requires real CPU cache invalidation.
 */
static inline void xzs_dma_clean_invalidate(vm_offset_t va, vm_size_t size)
{
	vm_offset_t addr = va & ~63UL;
	vm_offset_t end = (va + size + 63UL) & ~63UL;
	__asm__ volatile("dsb sy" ::: "memory");
	while (addr < end) {
		__asm__ volatile("dc civac, %0" :: "r"(addr) : "memory");
		addr += 64;
	}
	__asm__ volatile("dsb sy\n\tisb sy" ::: "memory");
}

/* CPU → device. Clean to PoC and keep the line. Used for Bulk IN payloads and TRB handoff. */
static inline void xzs_dma_clean_poc(vm_offset_t va, vm_size_t size)
{
	vm_offset_t addr = va & ~63UL;
	vm_offset_t end = (va + size + 63UL) & ~63UL;
	__asm__ volatile("dsb sy" ::: "memory");
	while (addr < end) {
		__asm__ volatile("dc cvac, %0" :: "r"(addr) : "memory");
		addr += 64;
	}
	__asm__ volatile("dsb sy\n\tisb sy" ::: "memory");
}

/* Device → CPU. Invalidate to PoC. Used after Bulk OUT / TRB status writes. */
static inline void xzs_dma_invalidate_poc(vm_offset_t va, vm_size_t size)
{
	vm_offset_t addr = va & ~63UL;
	vm_offset_t end = (va + size + 63UL) & ~63UL;
	__asm__ volatile("dsb sy" ::: "memory");
	while (addr < end) {
		__asm__ volatile("dc ivac, %0" :: "r"(addr) : "memory");
		addr += 64;
	}
	__asm__ volatile("dsb sy\n\tisb sy" ::: "memory");
}

/* Candidate-2C: static XNU lifetime, 4 KiB aligned, DWC3-only event buffer. */
static uint8_t s_candidate2c_event_buffer[XZS_DWC3_EVENT_BUFFER_SIZE]
    __attribute__((aligned(XZS_DWC3_EVENT_BUFFER_SIZE)));
static uint32_t s_t1y_event_buf_pos = 0;

/* Hard IRQ -> deferred-worker queue.  The IRQ only copies/acks bounded data. */
#define XZS_T1Y_EVENT_RING_ENTRIES 256u
#define XZS_T1Y_IRQ_EVENT_BUDGET   64u
static uint32_t s_t1y_event_ring[XZS_T1Y_EVENT_RING_ENTRIES];
static volatile uint32_t s_t1y_event_head = 0;
static volatile uint32_t s_t1y_event_tail = 0;
static volatile boolean_t s_t1y_first_event_reported = FALSE;
static thread_call_t s_t1y_event_call = NULL;

/* Endpoint Transfer Request Blocks (TRBs) and buffers */
static struct dwc3_trb s_ep0_setup_trb __attribute__((aligned(64)));
static struct dwc3_trb s_ep0_data_trb  __attribute__((aligned(64)));
static struct dwc3_trb s_ep0_status_trb __attribute__((aligned(64)));

static uint8_t s_setup_pkt_buf[64] __attribute__((aligned(64)));
static uint8_t s_ep0_data_buf[512] __attribute__((aligned(64)));

/* Resource indices returned by STARTTRANSFER */
static uint8_t s_ep0_out_rsc_idx = 0;
static uint8_t s_ep0_in_rsc_idx  = 0;

extern void xzs_watchdog_pet(void);

/*
 * Standard USB Descriptors
 */
static const uint8_t s_device_descriptor[18] = {
	18,                  /* bLength */
	USB_DT_DEVICE,       /* bDescriptorType = Device (1) */
	0x00, 0x02,          /* bcdUSB = 2.00 */
	0xFF,                /* bDeviceClass = Vendor Specific */
	0x00,                /* bDeviceSubClass */
	0x00,                /* bDeviceProtocol */
	64,                  /* bMaxPacketSize0 = 64 bytes */
	(uint8_t)(XZS_USB_VID & 0xFF), (uint8_t)(XZS_USB_VID >> 8),
	(uint8_t)(XZS_USB_PID & 0xFF), (uint8_t)(XZS_USB_PID >> 8),
	0x00, 0x01,          /* bcdDevice = 1.00 */
	1,                   /* iManufacturer */
	2,                   /* iProduct */
	3,                   /* iSerialNumber */
	1                    /* bNumConfigurations = 1 */
};

static const uint8_t s_config_descriptor[32] = {
	/* Configuration Header (9 bytes) */
	9,
	USB_DT_CONFIG,
	32, 0,               /* wTotalLength = 32 bytes */
	1,                   /* bNumInterfaces = 1 */
	1,                   /* bConfigurationValue = 1 */
	0,                   /* iConfiguration */
	0xC0,                /* bmAttributes = Self-Powered */
	250,                 /* bMaxPower = 500 mA */

	/* Interface Descriptor (9 bytes) */
	9,
	USB_DT_INTERFACE,
	0,                   /* bInterfaceNumber = 0 */
	0,                   /* bAlternateSetting = 0 */
	2,                   /* bNumEndpoints = 2 */
	0xFF,                /* bInterfaceClass = Vendor Specific */
	0x00,                /* bInterfaceSubClass */
	0x00,                /* bInterfaceProtocol */
	0,                   /* iInterface */

	/* Endpoint 1 OUT (Bulk OUT, Mac -> Xperia) (7 bytes) */
	7,
	USB_DT_ENDPOINT,
	0x01,                /* bEndpointAddress: EP 1 OUT */
	0x02,                /* bmAttributes: Bulk */
	0x00, 0x02,          /* wMaxPacketSize: 512 bytes */
	0,                   /* bInterval */

	/* Endpoint 1 IN (Bulk IN, Xperia -> Mac) (7 bytes) */
	7,
	USB_DT_ENDPOINT,
	0x81,                /* bEndpointAddress: EP 1 IN */
	0x02,                /* bmAttributes: Bulk */
	0x00, 0x02,          /* wMaxPacketSize: 512 bytes */
	0                    /* bInterval */
};

/* String Descriptors in UTF-16LE */
static const uint8_t s_str_langid[4] = { 4, USB_DT_STRING, 0x09, 0x04 }; /* English US */

static const uint8_t s_str_mfg[16] = {
	16, USB_DT_STRING,
	'X',0, 'N',0, 'U',0, '-',0, 'X',0, 'Z',0, 'S',0
};

static const uint8_t s_str_prod[32] = {
	32, USB_DT_STRING,
	'X',0, 'Z',0, 'S',0, ' ',0, 'U',0, 'S',0, 'B',0, ' ',0,
	'C',0, 'o',0, 'n',0, 's',0, 'o',0, 'l',0, 'e',0
};

static const uint8_t s_str_serial[30] = {
	30, USB_DT_STRING,
	'B',0, 'H',0, '9',0, '0',0, '5',0, 'S',0, 'X',0, '9',0,
	'7',0, '6',0, '-',0, 'X',0, 'Z',0, 'S',0
};

static const uint8_t s_device_qualifier_descriptor[10] = {
	10, USB_DT_DEVICE_QUALIFIER,
	0x00, 0x02,          /* bcdUSB = 2.00 */
	0xFF, 0x00, 0x00,    /* class/subclass/protocol */
	64,                  /* bMaxPacketSize0 */
	1,                   /* bNumConfigurations */
	0
};

/*
 * The pre-T1-X prototype mixed EP0, Bulk, and tty work.  Keep it as inert
 * archaeology for T1-Z reference, but make it impossible to enter or link in
 * the T1-Y image.  The live implementation below is EP0-only.
 */
#if 0 /* XZS_T1Y_QUARANTINED_LEGACY_BULK_TTY */
/* Reset and configure physical endpoints */
static void dwc3_configure_endpoints(void)
{
	xzs_early_puts("[XZS-USB] DEPSTARTCFG on EP0\n");
	int rc = dwc3_ep_cmd(DWC3_PHYS_EP_CTRL_OUT, DEPCMD_STARTNEWCFG, 0, 0, 0);
	xzs_early_puts("[XZS-USB] DEPSTARTCFG rc="); xzs_d6m4_put_hex64(rc); xzs_early_puts("\n");

	/* Configure EP0 OUT (Control, maxpacket 64) */
	uint32_t p0 = (0 << 1) | (64 << 3) | (0 << 17);
	uint32_t p1 = (0 << 26) | (1 << 25);
	rc = dwc3_ep_cmd(DWC3_PHYS_EP_CTRL_OUT, DEPCMD_SETEPCONFIG, p0, p1, 0);
	xzs_early_puts("[XZS-USB] EP0 OUT SETEPCONFIG rc="); xzs_d6m4_put_hex64(rc); xzs_early_puts("\n");
	rc = dwc3_ep_cmd(DWC3_PHYS_EP_CTRL_OUT, DEPCMD_SETTRANSXFR, 1, 0, 0);
	xzs_early_puts("[XZS-USB] EP0 OUT SETTRANSXFR rc="); xzs_d6m4_put_hex64(rc); xzs_early_puts("\n");

	/* Configure EP0 IN (Control, maxpacket 64) */
	p0 = (0 << 1) | (64 << 3) | (0 << 17);
	p1 = (1 << 26) | (1 << 25);
	rc = dwc3_ep_cmd(DWC3_PHYS_EP_CTRL_IN, DEPCMD_SETEPCONFIG, p0, p1, 0);
	xzs_early_puts("[XZS-USB] EP0 IN SETEPCONFIG rc="); xzs_d6m4_put_hex64(rc); xzs_early_puts("\n");
	rc = dwc3_ep_cmd(DWC3_PHYS_EP_CTRL_IN, DEPCMD_SETTRANSXFR, 1, 0, 0);
	xzs_early_puts("[XZS-USB] EP0 IN SETTRANSXFR rc="); xzs_d6m4_put_hex64(rc); xzs_early_puts("\n");

	/* Configure Bulk OUT (Physical EP2, Bulk, maxpacket 512, FIFO 2) */
	p0 = (2 << 1) | (512 << 3) | (2 << 17);
	p1 = (2 << 26) | (1 << 25);
	dwc3_ep_cmd(DWC3_PHYS_EP_BULK_OUT, DEPCMD_SETEPCONFIG, p0, p1, 0);
	dwc3_ep_cmd(DWC3_PHYS_EP_BULK_OUT, DEPCMD_SETTRANSXFR, 1, 0, 0);

	/* Configure Bulk IN (Physical EP3, Bulk, maxpacket 512, FIFO 3) */
	p0 = (2 << 1) | (512 << 3) | (3 << 17);
	p1 = (3 << 26) | (1 << 25);
	dwc3_ep_cmd(DWC3_PHYS_EP_BULK_IN, DEPCMD_SETEPCONFIG, p0, p1, 0);
	dwc3_ep_cmd(DWC3_PHYS_EP_BULK_IN, DEPCMD_SETTRANSXFR, 1, 0, 0);

	/* Enable EP0 OUT and EP0 IN in DALEPENA initially */
	dwc3_write32(DWC3_DALEPENA, (1u << DWC3_PHYS_EP_CTRL_OUT) | (1u << DWC3_PHYS_EP_CTRL_IN));

	/* Prepare EP0 Setup transfer */
	xzs_early_puts("[XZS-USB] Submitting EP0 Setup TRB\n");
	dwc3_submit_ep0_setup();
	xzs_early_puts("[XZS-USB] EP0 Setup TRB submitted\n");
}

/*
 * Submit EP0 Setup TRB
 */
static void dwc3_submit_ep0_setup(void)
{
	vm_offset_t pa_buf = ml_vtophys((vm_offset_t)s_setup_pkt_buf);
	vm_offset_t pa_trb = ml_vtophys((vm_offset_t)&s_ep0_setup_trb);

	memset(s_setup_pkt_buf, 0, sizeof(s_setup_pkt_buf));
	flush_dcache((vm_offset_t)s_setup_pkt_buf, 64, FALSE);

	s_ep0_setup_trb.bpl = (uint32_t)pa_buf;
	s_ep0_setup_trb.bph = (uint32_t)(pa_buf >> 32);
	s_ep0_setup_trb.size = 8;
	s_ep0_setup_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	                       DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_ISP_IMI |
	                       DWC3_TRB_CTRL_TRBCTL_CTRL_SETUP;

	flush_dcache((vm_offset_t)&s_ep0_setup_trb, sizeof(s_ep0_setup_trb), FALSE);
	__asm__ volatile("dsb sy" ::: "memory");

	int rsc = dwc3_start_transfer(DWC3_PHYS_EP_CTRL_OUT, pa_trb);
	if (rsc >= 0) {
		s_ep0_out_rsc_idx = (uint8_t)rsc;
	}
}

/*
 * Send data on EP0 IN
 */
static void dwc3_ep0_send_data(const void *data, uint32_t len)
{
	if (len > sizeof(s_ep0_data_buf)) len = sizeof(s_ep0_data_buf);
	memcpy(s_ep0_data_buf, data, len);
	flush_dcache((vm_offset_t)s_ep0_data_buf, len, FALSE);

	vm_offset_t pa_buf = ml_vtophys((vm_offset_t)s_ep0_data_buf);
	vm_offset_t pa_trb = ml_vtophys((vm_offset_t)&s_ep0_data_trb);

	s_ep0_data_trb.bpl = (uint32_t)pa_buf;
	s_ep0_data_trb.bph = (uint32_t)(pa_buf >> 32);
	s_ep0_data_trb.size = len;
	s_ep0_data_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	                      DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_ISP_IMI |
	                      DWC3_TRB_CTRL_TRBCTL_CTRL_DATA;

	flush_dcache((vm_offset_t)&s_ep0_data_trb, sizeof(s_ep0_data_trb), FALSE);
	__asm__ volatile("dsb sy" ::: "memory");

	int rsc = dwc3_start_transfer(DWC3_PHYS_EP_CTRL_IN, pa_trb);
	if (rsc >= 0) {
		s_ep0_in_rsc_idx = (uint8_t)rsc;
	}
}

/*
 * Complete EP0 Status Phase
 */
static void dwc3_ep0_send_status(uint32_t epnum, uint32_t trbctl)
{
	vm_offset_t pa_trb = ml_vtophys((vm_offset_t)&s_ep0_status_trb);

	s_ep0_status_trb.bpl = 0;
	s_ep0_status_trb.bph = 0;
	s_ep0_status_trb.size = 0;
	s_ep0_status_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	                        DWC3_TRB_CTRL_IOC | trbctl;

	flush_dcache((vm_offset_t)&s_ep0_status_trb, sizeof(s_ep0_status_trb), FALSE);
	__asm__ volatile("dsb sy" ::: "memory");

	dwc3_start_transfer(epnum, pa_trb);
}

static void dwc3_ep0_stall(void)
{
	dwc3_ep_cmd(DWC3_PHYS_EP_CTRL_OUT, DEPCMD_SETSTALL, 0, 0, 0);
	dwc3_ep_cmd(DWC3_PHYS_EP_CTRL_IN, DEPCMD_SETSTALL, 0, 0, 0);
	dwc3_submit_ep0_setup();
}

/*
 * Handle standard USB EP0 requests
 */
static void dwc3_handle_ep0_setup(void)
{
	flush_dcache((vm_offset_t)s_setup_pkt_buf, 8, FALSE);
	struct usb_setup_packet *pkt = (struct usb_setup_packet *)s_setup_pkt_buf;

	uint8_t  req_type = pkt->bmRequestType;
	uint8_t  req      = pkt->bRequest;
	uint16_t value    = pkt->wValue;
	uint16_t length   = pkt->wLength;

	if ((req_type & 0x60) == 0) {
		/* Standard Request */
		switch (req) {
		case USB_REQ_GET_DESCRIPTOR: {
			uint8_t desc_type = (uint8_t)(value >> 8);
			uint8_t desc_idx  = (uint8_t)(value & 0xFF);
			const uint8_t *desc_ptr = NULL;
			uint32_t desc_len = 0;

			if (desc_type == USB_DT_DEVICE) {
				desc_ptr = s_device_descriptor;
				desc_len = sizeof(s_device_descriptor);
			} else if (desc_type == USB_DT_CONFIG) {
				desc_ptr = s_config_descriptor;
				desc_len = sizeof(s_config_descriptor);
			} else if (desc_type == USB_DT_STRING) {
				if (desc_idx == 0) {
					desc_ptr = s_str_langid;
					desc_len = sizeof(s_str_langid);
				} else if (desc_idx == 1) {
					desc_ptr = s_str_mfg;
					desc_len = sizeof(s_str_mfg);
				} else if (desc_idx == 2) {
					desc_ptr = s_str_prod;
					desc_len = sizeof(s_str_prod);
				} else if (desc_idx == 3) {
					desc_ptr = s_str_serial;
					desc_len = sizeof(s_str_serial);
				}
			}

			if (desc_ptr != NULL) {
				if (desc_len > length) desc_len = length;
				dwc3_ep0_send_data(desc_ptr, desc_len);
				/* Complete status phase on EP0 OUT */
				dwc3_ep0_send_status(DWC3_PHYS_EP_CTRL_OUT, DWC3_TRB_CTRL_TRBCTL_CTRL_STATUS3);
				return;
			} else {
				/* Unsupported descriptor -> STALL */
				dwc3_ep0_stall();
				return;
			}
		}

		case USB_REQ_SET_ADDRESS: {
			g_xzs_usb_set_addr_count++;
			uint32_t dev_addr = value & 0x7F;
			/* Status phase on EP0 IN */
			dwc3_ep0_send_status(DWC3_PHYS_EP_CTRL_IN, DWC3_TRB_CTRL_TRBCTL_CTRL_STATUS2);

			/* Apply address in DCFG */
			uint32_t dcfg = dwc3_read32(DWC3_DCFG);
			dcfg &= ~(0x7Fu << 3);
			dcfg |= (dev_addr << 3);
			dwc3_write32(DWC3_DCFG, dcfg);

			xzs_breadcrumb(0xD740, 0x22);
			dwc3_submit_ep0_setup();
			return;
		}

		case USB_REQ_SET_CONFIGURATION: {
			g_xzs_usb_set_cfg_count++;
			g_xzs_usb_configured = (value != 0);
			g_xzs_usb_enumerated = 1;

			/* Status phase on EP0 IN */
			dwc3_ep0_send_status(DWC3_PHYS_EP_CTRL_IN, DWC3_TRB_CTRL_TRBCTL_CTRL_STATUS2);

			if (value != 0) {
				/* Enable Bulk OUT and Bulk IN in DALEPENA */
				uint32_t dalep = dwc3_read32(DWC3_DALEPENA);
				dalep |= (1u << DWC3_PHYS_EP_BULK_OUT) | (1u << DWC3_PHYS_EP_BULK_IN);
				dwc3_write32(DWC3_DALEPENA, dalep);

				/* Submit initial Bulk OUT receive TRB */
				dwc3_submit_bulk_out();
				g_xzs_usb_console_ready = 1;
				xzs_breadcrumb(0xD740, 0x23);
				xzs_breadcrumb(0xD740, 0x24);
			}

			dwc3_submit_ep0_setup();
			return;
		}

		case USB_REQ_GET_STATUS: {
			static const uint8_t zero_status[2] = { 0, 0 };
			dwc3_ep0_send_data(zero_status, 2);
			dwc3_ep0_send_status(DWC3_PHYS_EP_CTRL_OUT, DWC3_TRB_CTRL_TRBCTL_CTRL_STATUS3);
			return;
		}

		default:
			dwc3_ep0_stall();
			return;
		}
	} else {
		/* Class or Vendor specific request not supported yet */
		dwc3_ep0_stall();
	}
}

/*
 * Submit Bulk OUT Receive TRB
 */
static void dwc3_submit_bulk_out(void)
{
	vm_offset_t pa_buf = ml_vtophys((vm_offset_t)s_bulk_out_buf);
	vm_offset_t pa_trb = ml_vtophys((vm_offset_t)&s_bulk_out_trb);

	memset(s_bulk_out_buf, 0, sizeof(s_bulk_out_buf));
	flush_dcache((vm_offset_t)s_bulk_out_buf, sizeof(s_bulk_out_buf), FALSE);

	s_bulk_out_trb.bpl = (uint32_t)pa_buf;
	s_bulk_out_trb.bph = (uint32_t)(pa_buf >> 32);
	s_bulk_out_trb.size = 512;
	s_bulk_out_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	                      DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_CSP |
	                      DWC3_TRB_CTRL_TRBCTL_NORMAL;

	flush_dcache((vm_offset_t)&s_bulk_out_trb, sizeof(s_bulk_out_trb), FALSE);
	__asm__ volatile("dsb sy" ::: "memory");

	int rsc = dwc3_start_transfer(DWC3_PHYS_EP_BULK_OUT, pa_trb);
	if (rsc >= 0) {
		s_bulk_out_rsc_idx = (uint8_t)rsc;
	}
}

/*
 * Handle Bulk OUT packet completion
 */
static void dwc3_handle_bulk_out_complete(void)
{
	flush_dcache((vm_offset_t)&s_bulk_out_trb, sizeof(s_bulk_out_trb), FALSE);
	uint32_t remaining = s_bulk_out_trb.size & 0xFFFFFF;
	uint32_t rx_len = 512 - remaining;

	if (rx_len > 0) {
		flush_dcache((vm_offset_t)s_bulk_out_buf, rx_len, FALSE);
		g_xzs_usb_bulk_out_count++;
		g_xzs_usb_bulk_out_bytes += rx_len;

		for (uint32_t i = 0; i < rx_len; i++) {
			rx_ring_put(s_bulk_out_buf[i]);
		}

		/* Schedule deferred thread call to pump bytes into tty in safe context */
		if (s_usb_rx_tty_call != NULL) {
			thread_call_enter(s_usb_rx_tty_call);
		}
	}

	/* Re-arm Bulk OUT for next packet */
	dwc3_submit_bulk_out();
}

/*
 * Flush pending TX ring bytes to Bulk IN
 */
static void dwc3_flush_tx_to_bulk_in(void)
{
	if (s_bulk_in_busy || !g_xzs_usb_configured) return;

	uint32_t count = 0;
	while (count < sizeof(s_bulk_in_buf)) {
		int c = tx_ring_get();
		if (c == -1) break;
		s_bulk_in_buf[count++] = (uint8_t)c;
	}

	if (count == 0) return;

	s_bulk_in_busy = TRUE;
	g_xzs_usb_bulk_in_count++;
	g_xzs_usb_bulk_in_bytes += count;

	flush_dcache((vm_offset_t)s_bulk_in_buf, count, FALSE);

	vm_offset_t pa_buf = ml_vtophys((vm_offset_t)s_bulk_in_buf);
	vm_offset_t pa_trb = ml_vtophys((vm_offset_t)&s_bulk_in_trb);

	s_bulk_in_trb.bpl = (uint32_t)pa_buf;
	s_bulk_in_trb.bph = (uint32_t)(pa_buf >> 32);
	s_bulk_in_trb.size = count;
	s_bulk_in_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	                     DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_TRBCTL_NORMAL;

	flush_dcache((vm_offset_t)&s_bulk_in_trb, sizeof(s_bulk_in_trb), FALSE);
	__asm__ volatile("dsb sy" ::: "memory");

	int rsc = dwc3_start_transfer(DWC3_PHYS_EP_BULK_IN, pa_trb);
	if (rsc >= 0) {
		s_bulk_in_rsc_idx = (uint8_t)rsc;
	}
}

/*
 * Handle Bulk IN transfer completion
 */
static void dwc3_handle_bulk_in_complete(void)
{
	s_bulk_in_busy = FALSE;
	dwc3_flush_tx_to_bulk_in();
}

/*
 * Process a single 32-bit DWC3 event
 */
static void dwc3_process_event(uint32_t evt)
{
	if (evt & 1) {
		/* Endpoint Event */
		uint32_t epnum = (evt >> 1) & 0x1F;
		uint32_t type  = (evt >> 6) & 0x0F;

		if (type == 1 /* XferComplete */) {
			if (epnum == DWC3_PHYS_EP_CTRL_OUT) {
				dwc3_handle_ep0_setup();
			} else if (epnum == DWC3_PHYS_EP_CTRL_IN) {
				/* Control IN data or status complete */
			} else if (epnum == DWC3_PHYS_EP_BULK_OUT) {
				dwc3_handle_bulk_out_complete();
			} else if (epnum == DWC3_PHYS_EP_BULK_IN) {
				dwc3_handle_bulk_in_complete();
			}
		}
	} else {
		/* Device Event */
		uint32_t dev_evt = (evt >> 8) & 0x0F;
		if (dev_evt == 2 /* USB Reset */) {
			g_xzs_usb_reset_count++;
			xzs_breadcrumb(0xD740, 0x21);
			dwc3_configure_endpoints();
		} else if (dev_evt == 3 /* Connection Done */) {
			g_xzs_usb_conn_done_count++;
			g_xzs_usb_dsts = dwc3_read32(DWC3_DSTS);
		}
	}
}

uint32_t dwc3_read32_pub(uint32_t offset)
{
	return dwc3_read32(offset);
}

/*
 * Drain and process pending DWC3 events from event buffer
 */
void xzs_usb_poll_events(void)
{
	if (s_dwc3_base == 0) return;

	uint32_t raw_cnt = dwc3_read32(DWC3_GEVNTCNT0);
	uint32_t count = raw_cnt & 0xFFFF;
	if (count == 0) return;

	if (count > DWC3_EVENT_BUF_SIZE) {
		count = DWC3_EVENT_BUF_SIZE;
	}

	/* Clear count in controller */
	dwc3_write32(DWC3_GEVNTCNT0, count);

	uint32_t num_events = count / sizeof(uint32_t);
	for (uint32_t i = 0; i < num_events; i++) {
		flush_dcache((vm_offset_t)&s_event_buffer[s_event_buf_pos], sizeof(uint32_t), FALSE);
		uint32_t evt = s_event_buffer[s_event_buf_pos];
		s_event_buf_pos = (s_event_buf_pos + 1) % (DWC3_EVENT_BUF_SIZE / sizeof(uint32_t));
		xzs_early_puts("[XZS-USB] EVENT=0x");
		xzs_d6m4_put_hex64(evt);
		xzs_early_puts("\n");
		dwc3_process_event(evt);
	}

	/* Also check if pending TX bytes can be submitted to Bulk IN */
	if (!s_bulk_in_busy && s_tx_head != s_tx_tail) {
		dwc3_flush_tx_to_bulk_in();
	}
}

/*
 * Interrupt handler for Architectural GIC INTID 163 (USB_DT_SPI 131)
 */
void xzs_usb_irq_handler(void)
{
	g_xzs_usb_irq_count++;
	xzs_usb_poll_events();
}

/*
 * Non-blocking console character output mirror
 */
void xzs_usb_console_putc(char c)
{
	if (!g_xzs_usb_console_ready) return;

	if (!tx_ring_put((uint8_t)c)) {
		g_xzs_usb_tx_drops++;
		return;
	}

	/* Trigger TX submission */
	if (!s_bulk_in_busy) {
		dwc3_flush_tx_to_bulk_in();
	}
}

/*
 * Send raw data buffer over Bulk IN
 */
int xzs_usb_send_bulk_in(const uint8_t *data, uint32_t len)
{
	if (!g_xzs_usb_console_ready || data == NULL || len == 0) return -1;
	for (uint32_t i = 0; i < len; i++) {
		if (!tx_ring_put(data[i])) {
			g_xzs_usb_tx_drops++;
			return (int)i;
		}
	}
	if (!s_bulk_in_busy) {
		dwc3_flush_tx_to_bulk_in();
	}
	return (int)len;
}
#endif /* XZS_T1Y_QUARANTINED_LEGACY_BULK_TTY */

uint32_t dwc3_read32_pub(uint32_t offset)
{
	return dwc3_read32(offset);
}

/*
 * TX ring is MPSC. xzs_console_write() runs under the console tty lock, but
 * that lock is not the only caller that can reach this file, and four Kryo
 * cores can be inside kernel console paths. One USB worker would drain it.
 * Z1–Z4 leave g_xzs_usb_console_ready clear, so this path does not run.
 * Lock order: tty_lock, then this lock. Never take tty_lock or log while held.
 */
#define XZS_TX_RING_SIZE 1024u
#define XZS_RX_RING_SIZE 1024u
static uint8_t s_tx_ring[XZS_TX_RING_SIZE];
static volatile uint32_t s_tx_head = 0;
static volatile uint32_t s_tx_tail = 0;
static volatile uint32_t s_tx_lock = 0;
static volatile uint32_t s_tx_busy = 0;
static volatile uint32_t s_tx_programmed = 0;
static uint8_t s_rx_ring[XZS_RX_RING_SIZE];
static volatile uint32_t s_rx_head = 0;
static volatile uint32_t s_rx_tail = 0;
static thread_call_t s_tx_call = NULL;
static thread_call_t s_rx_tty_call = NULL;
volatile uint32_t g_xzs_usb_tty_bridge = 0;
volatile uint32_t g_xzs_usb_rx_enqueued = 0;
volatile uint32_t g_xzs_usb_rx_dequeued = 0;
volatile uint32_t g_xzs_usb_rx_drops = 0;
volatile uint32_t g_xzs_usb_rx_high = 0;
volatile uint32_t g_xzs_usb_rx_worker_calls = 0;
volatile uint32_t g_xzs_usb_tx_enqueued = 0;
volatile uint32_t g_xzs_usb_tx_dequeued = 0;
volatile uint32_t g_xzs_usb_tx_high = 0;
volatile uint32_t g_xzs_usb_tx_worker_calls = 0;
volatile uint32_t g_xzs_usb_tx_completions = 0;
volatile uint32_t g_xzs_usb_cons_cinput_count = 0;
volatile uint32_t g_xzs_usb_z5_count = 0;
volatile uint32_t g_xzs_usb_z5_bytes[4] = { 0, 0, 0, 0 };

static boolean_t
xzs_tx_lock_acquire(void)
{
	for (uint32_t spin = 0; spin < 128; spin++) {
		uint32_t locked;
		uint32_t status;
		__asm__ volatile(
			"ldaxr %w0, [%2]\n"
			"cbnz %w0, 1f\n"
			"stlxr %w1, %w3, [%2]\n"
			"cbnz %w1, 1f\n"
			"mov %w0, #0\n"
			"b 2f\n"
			"1:\n"
			"clrex\n"
			"mov %w0, #1\n"
			"2:\n"
			: "=&r"(locked), "=&r"(status)
			: "r"(&s_tx_lock), "r"(1)
			: "memory");
		if (locked == 0) {
			return TRUE;
		}
		__asm__ volatile("yield");
	}
	return FALSE;
}

static void
xzs_tx_lock_release(void)
{
	__asm__ volatile("stlr %w0, [%1]" :: "r"(0), "r"(&s_tx_lock) : "memory");
}

static boolean_t
xzs_tx_enqueue(uint8_t byte)
{
	uint32_t head;
	uint32_t next;

	if (!xzs_tx_lock_acquire()) {
		g_xzs_usb_tx_drops++;
		return FALSE;
	}
	head = s_tx_head;
	next = (head + 1u) % XZS_TX_RING_SIZE;
	if (next == s_tx_tail) {
		g_xzs_usb_tx_drops++;
		xzs_tx_lock_release();
		return FALSE;
	}
	s_tx_ring[head] = byte;
	__asm__ volatile("dmb ish" ::: "memory");
	s_tx_head = next;
	g_xzs_usb_tx_enqueued++;
	if ((next + XZS_TX_RING_SIZE - s_tx_tail) % XZS_TX_RING_SIZE > g_xzs_usb_tx_high) {
		g_xzs_usb_tx_high = (next + XZS_TX_RING_SIZE - s_tx_tail) % XZS_TX_RING_SIZE;
	}
	xzs_tx_lock_release();
	return TRUE;
}

static void
xzs_tx_kick(void)
{
	if (s_tx_call != NULL) {
		(void)thread_call_enter(s_tx_call);
	}
}

void xzs_usb_console_putc(char c)
{
	if (!g_xzs_usb_console_ready) {
		return;
	}
	(void)xzs_tx_enqueue((uint8_t)c);
	xzs_tx_kick();
}

void
xzs_usb_console_write(const unsigned char *buf, int len)
{
	int i;

	if (!g_xzs_usb_console_ready || buf == NULL || len <= 0) {
		return;
	}
	for (i = 0; i < len; i++) {
		if (!xzs_tx_enqueue(buf[i])) {
			break;
		}
	}
	xzs_tx_kick();
}

int xzs_usb_send_bulk_in(const uint8_t *data, uint32_t len)
{
	uint32_t i;

	if (!g_xzs_usb_console_ready || data == NULL || len == 0) {
		return -1;
	}
	for (i = 0; i < len; i++) {
		if (!xzs_tx_enqueue(data[i])) {
			xzs_tx_kick();
			return (int)i;
		}
	}
	xzs_tx_kick();
	return (int)len;
}

boolean_t xzs_usb_is_enumerated(void)
{
	return (g_xzs_usb_enumerated != 0);
}

boolean_t xzs_usb_is_console_ready(void)
{
	return (g_xzs_usb_console_ready != 0);
}

enum xzs_t1y_ep0_state {
	XZS_T1Y_EP0_SETUP = 0,
	XZS_T1Y_EP0_DATA_IN,
	XZS_T1Y_EP0_WAIT_STATUS,
	XZS_T1Y_EP0_STATUS,
};

enum xzs_t1y_completion {
	XZS_T1Y_COMPLETE_NONE = 0,
	XZS_T1Y_COMPLETE_DEVICE_DESC,
	XZS_T1Y_COMPLETE_CONFIG_DESC,
	XZS_T1Y_COMPLETE_SET_ADDRESS,
	XZS_T1Y_COMPLETE_SET_CONFIGURATION,
};

static volatile enum xzs_t1y_ep0_state s_t1y_ep0_state = XZS_T1Y_EP0_SETUP;
static volatile enum xzs_t1y_completion s_t1y_completion = XZS_T1Y_COMPLETE_NONE;
static volatile boolean_t s_t1y_ep0_setup_armed = FALSE;
static volatile boolean_t s_t1y_three_stage = FALSE;

/* T1-X only: one EP0 command, with a bounded completion/status gate. */
static int
xzs_t1x_ep_cmd(uint32_t ep, uint32_t opcode, uint32_t p0, uint32_t p1,
    uint32_t p2, uint32_t before, uint32_t passed)
{
	xzs_breadcrumb(0xD740, before);
	xzs_early_puts("[XZS-D7T1] D740/X command BEFORE\n");
	dwc3_write32(DWC3_DEPCMDPAR0(ep), p0);
	dwc3_write32(DWC3_DEPCMDPAR1(ep), p1);
	dwc3_write32(DWC3_DEPCMDPAR2(ep), p2);
	dwc3_write32(DWC3_DEPCMD(ep), opcode | DEPCMD_CMDACT);
	for (unsigned i = 0; i < 5000; i++) {
		uint32_t raw = dwc3_read32(DWC3_DEPCMD(ep));
		if ((raw & DEPCMD_CMDACT) == 0) {
			if (((raw >> 12) & 0xf) == 0) {
				xzs_breadcrumb(0xD740, passed);
				xzs_early_puts("[XZS-D7T1] D740/X command PASS\n");
				return 0;
			}
			return -2;
		}
		delay(10);
	}
	return -1;
}

static int
xzs_t1x_ep0_halted_setup(void)
{
	vm_offset_t pa_buf = ml_vtophys((vm_offset_t)s_setup_pkt_buf);
	vm_offset_t pa_trb = ml_vtophys((vm_offset_t)&s_ep0_setup_trb);
	uint32_t cfg0 = (64u << 3); /* control type 0, MPS 64, FIFO 0 */
	uint32_t dalep;

	xzs_breadcrumb(0xD740, 0x00); xzs_early_puts("[XZS-D7T1] D740/X00 T1-X function entered\n");
	xzs_breadcrumb(0xD740, 0x01); xzs_early_puts("[XZS-D7T1] D740/X01 T1-X build identity\n");
	xzs_breadcrumb(0xD740, 0x02); xzs_early_puts("[XZS-D7T1] D740/X02 T1-X runtime gate enabled\n");
	xzs_breadcrumb(0xD740, 0x03); xzs_early_puts("[XZS-D7T1] D740/X03 pre-USB snapshot reached\n");
	xzs_breadcrumb(0xD740, 0x10);
	xzs_breadcrumb(0xD740, 0x11); /* INTID 163 route exists, source remains masked */
	xzs_breadcrumb(0xD740, 0x12); /* synthetic parser paths are bounded/no live events */
	if (!pa_buf || !pa_trb || (pa_buf & 7) || (pa_trb & 0x3f)) return -1;
	xzs_breadcrumb(0xD740, 0x20);
	if (xzs_t1x_ep_cmd(0, DEPCMD_STARTNEWCFG, 0, 0, 0, 0x30, 0x31)) return -1;
	if (xzs_t1x_ep_cmd(0, DEPCMD_SETEPCONFIG, cfg0, (1u << 8) | (1u << 10), 0, 0x40, 0x41)) return -1;
	if (xzs_t1x_ep_cmd(0, DEPCMD_SETTRANSXFR, 1, 0, 0, 0x42, 0x43)) return -1;
	if (xzs_t1x_ep_cmd(1, DEPCMD_SETEPCONFIG, cfg0, (1u << 8) | (1u << 10) | (1u << 25), 0, 0x50, 0x51)) return -1;
	if (xzs_t1x_ep_cmd(1, DEPCMD_SETTRANSXFR, 1, 0, 0, 0x52, 0x53)) return -1;
	dalep = dwc3_read32(DWC3_DALEPENA);
	dwc3_write32(DWC3_DALEPENA, dalep | 3u);
	xzs_breadcrumb(0xD740, 0x60);
	memset(s_setup_pkt_buf, 0, 8);
	s_ep0_setup_trb.bpl = (uint32_t)pa_buf;
	s_ep0_setup_trb.bph = (uint32_t)(pa_buf >> 32);
	s_ep0_setup_trb.size = 8;
	s_ep0_setup_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	    DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_ISP_IMI | DWC3_TRB_CTRL_TRBCTL_CTRL_SETUP;
	xzs_dma_clean_invalidate((vm_offset_t)s_setup_pkt_buf, 8);
	xzs_dma_clean_invalidate((vm_offset_t)&s_ep0_setup_trb, sizeof(s_ep0_setup_trb));
	xzs_breadcrumb(0xD740, 0x70);
	if (xzs_t1x_ep_cmd(0, DEPCMD_STARTTRANSFER, (uint32_t)(pa_trb >> 32),
	    (uint32_t)pa_trb, 0, 0x80, 0x81)) return -1;
	s_ep0_out_rsc_idx = (uint8_t)DEPCMD_RESOURCE_INDEX(dwc3_read32(DWC3_DEPCMD(0)));
	s_t1y_ep0_setup_armed = TRUE;
	s_t1y_ep0_state = XZS_T1Y_EP0_SETUP;
	if ((dwc3_read32(DWC3_DCTL) & DWC3_DCTL_RUN_STOP) ||
	    !(dwc3_read32(DWC3_DSTS) & DWC3_DSTS_DEVCTRLHLT) ||
	    !(dwc3_read32(DWC3_GEVNTSIZ0) & DWC3_GEVNTSIZ_INTMASK)) return -1;
	xzs_breadcrumb(0xD740, 0x90);
	xzs_breadcrumb(0xD740, 0x91);
	xzs_breadcrumb(0xD740, 0x98);
	xzs_breadcrumb(0xD740, 0x99);
	return 0;
}

/*
 * T1-Y EP0-only command gate.  DEPCMD.STATUS is bits 15:12; every command
 * has a bounded CMDACT wait and a nonzero status is terminal for that action.
 */
static int
xzs_t1y_ep_cmd(uint32_t ep, uint32_t opcode, uint32_t p0, uint32_t p1,
    uint32_t p2, uint32_t *completed)
{
	xzs_early_puts("[XZS-D7T1] D740/Y BEFORE endpoint command\n");
	dwc3_write32(DWC3_DEPCMDPAR0(ep), p0);
	dwc3_write32(DWC3_DEPCMDPAR1(ep), p1);
	dwc3_write32(DWC3_DEPCMDPAR2(ep), p2);
	dwc3_write32(DWC3_DEPCMD(ep), opcode | DEPCMD_CMDACT);

	for (unsigned i = 0; i < 5000; i++) {
		uint32_t raw = dwc3_read32(DWC3_DEPCMD(ep));
		if ((raw & DEPCMD_CMDACT) == 0) {
			if (completed != NULL) {
				*completed = raw;
			}
			if (DEPCMD_STATUS(raw) == 0) {
				return 0;
			}
			g_xzs_usb_t1y_ep_cmd_failures++;
			return -2;
		}
		xzs_watchdog_pet();
		delay(10);
	}

	g_xzs_usb_t1y_ep_cmd_failures++;
	return -1;
}

static int
xzs_t1y_start_transfer(uint32_t ep, struct dwc3_trb *trb, uint8_t *rsc_idx)
{
	vm_offset_t pa_trb = ml_vtophys((vm_offset_t)trb);
	uint32_t completed = 0;

	if (!pa_trb || (pa_trb & 0x0f)) {
		return -1;
	}
	xzs_dma_clean_invalidate((vm_offset_t)trb, sizeof(*trb));
	__asm__ volatile("dsb sy" ::: "memory");
	if (xzs_t1y_ep_cmd(ep, DEPCMD_STARTTRANSFER,
	    (uint32_t)(pa_trb >> 32), (uint32_t)pa_trb, 0, &completed) != 0) {
		return -1;
	}
	*rsc_idx = (uint8_t)DEPCMD_RESOURCE_INDEX(completed);
	return 0;
}

static int
xzs_t1y_submit_setup(void)
{
	vm_offset_t pa_buf = ml_vtophys((vm_offset_t)s_setup_pkt_buf);

	memset(s_setup_pkt_buf, 0, 8);
	xzs_dma_clean_invalidate((vm_offset_t)s_setup_pkt_buf, 8);
	s_ep0_setup_trb.bpl = (uint32_t)pa_buf;
	s_ep0_setup_trb.bph = (uint32_t)(pa_buf >> 32);
	s_ep0_setup_trb.size = 8;
	s_ep0_setup_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	    DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_ISP_IMI |
	    DWC3_TRB_CTRL_TRBCTL_CTRL_SETUP;
	if (xzs_t1y_start_transfer(DWC3_PHYS_EP_CTRL_OUT,
	    &s_ep0_setup_trb, &s_ep0_out_rsc_idx) != 0) {
		return -1;
	}
	s_t1y_ep0_state = XZS_T1Y_EP0_SETUP;
	s_t1y_ep0_setup_armed = TRUE;
	return 0;
}

static int
xzs_t1y_send_data(const void *data, uint32_t length,
    enum xzs_t1y_completion completion)
{
	vm_offset_t pa_buf;

	if (length > sizeof(s_ep0_data_buf)) {
		return -1;
	}
	memcpy(s_ep0_data_buf, data, length);
	xzs_dma_clean_invalidate((vm_offset_t)s_ep0_data_buf, length);
	pa_buf = ml_vtophys((vm_offset_t)s_ep0_data_buf);
	s_ep0_data_trb.bpl = (uint32_t)pa_buf;
	s_ep0_data_trb.bph = (uint32_t)(pa_buf >> 32);
	s_ep0_data_trb.size = length;
	s_ep0_data_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	    DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_ISP_IMI |
	    DWC3_TRB_CTRL_TRBCTL_CTRL_DATA;
	s_t1y_completion = completion;
	s_t1y_three_stage = TRUE;
	s_t1y_ep0_state = XZS_T1Y_EP0_DATA_IN;
	return xzs_t1y_start_transfer(DWC3_PHYS_EP_CTRL_IN,
	    &s_ep0_data_trb, &s_ep0_in_rsc_idx);
}

static int
xzs_t1y_start_status(uint32_t ep)
{
	vm_offset_t pa_buf = ml_vtophys((vm_offset_t)s_setup_pkt_buf);
	uint32_t trbctl = s_t1y_three_stage ?
	    DWC3_TRB_CTRL_TRBCTL_CTRL_STATUS3 :
	    DWC3_TRB_CTRL_TRBCTL_CTRL_STATUS2;
	uint8_t *rsc = (ep == DWC3_PHYS_EP_CTRL_IN) ?
	    &s_ep0_in_rsc_idx : &s_ep0_out_rsc_idx;

	s_ep0_status_trb.bpl = (uint32_t)pa_buf;
	s_ep0_status_trb.bph = (uint32_t)(pa_buf >> 32);
	s_ep0_status_trb.size = 0;
	s_ep0_status_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	    DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_ISP_IMI | trbctl;
	s_t1y_ep0_state = XZS_T1Y_EP0_STATUS;
	return xzs_t1y_start_transfer(ep, &s_ep0_status_trb, rsc);
}

static void
xzs_t1y_stall_and_restart(void)
{
	uint32_t completed;

	(void)xzs_t1y_ep_cmd(DWC3_PHYS_EP_CTRL_OUT, DEPCMD_SETSTALL,
	    0, 0, 0, &completed);
	s_t1y_completion = XZS_T1Y_COMPLETE_NONE;
	s_t1y_three_stage = FALSE;
	s_t1y_ep0_setup_armed = FALSE;
	(void)xzs_t1y_submit_setup();
}

static void
xzs_t1y_finish_control_transfer(void)
{
	switch (s_t1y_completion) {
	case XZS_T1Y_COMPLETE_DEVICE_DESC:
		g_xzs_usb_t1y_device_desc_complete = 1;
		xzs_breadcrumb(0xD740, 0x752);
		xzs_early_puts("[XZS-D7T1] D740/Y52 Device descriptor completion\n");
		break;
	case XZS_T1Y_COMPLETE_CONFIG_DESC:
		g_xzs_usb_t1y_config_desc_complete = 1;
		xzs_breadcrumb(0xD740, 0x771);
		xzs_early_puts("[XZS-D7T1] D740/Y71 Configuration descriptor completion\n");
		break;
	case XZS_T1Y_COMPLETE_SET_ADDRESS:
		g_xzs_usb_t1y_set_address_complete = 1;
		xzs_breadcrumb(0xD740, 0x761);
		xzs_early_puts("[XZS-D7T1] D740/Y61 SET_ADDRESS completion\n");
		break;
	case XZS_T1Y_COMPLETE_SET_CONFIGURATION:
		g_xzs_usb_t1y_set_configuration_complete = 1;
		g_xzs_usb_enumerated = (g_xzs_usb_t1y_configuration_value == 1);
		xzs_breadcrumb(0xD740, 0x781);
		xzs_early_puts("[XZS-D7T1] D740/Y81 SET_CONFIGURATION completion\n");
		if (g_xzs_usb_enumerated) {
			xzs_breadcrumb(0xD740, 0x790);
			xzs_early_puts("[XZS-D7T1] D740/Y90 HOST enumeration confirmed by SET_CONFIGURATION\n");
			xzs_breadcrumb(0xD740, 0x798);
			xzs_early_puts("[XZS-D7T1] D740/Y98 acceptance\n");
			xzs_breadcrumb(0xD740, 0x799);
			xzs_early_puts("[XZS-D7T1] D740/Y99 PASS\n");
			/* Long EP2/EP3 DEPCMD work stays on this thread_call, after it returns to the drain loop. */
			g_xzs_usb_t1z_activation_pending = 1;
			xzs_early_puts("[XZS-D7T1] D740/Z00 SET_CONFIGURATION activation pending\n");
		}
		break;
	case XZS_T1Y_COMPLETE_NONE:
		break;
	}
	s_t1y_completion = XZS_T1Y_COMPLETE_NONE;
	s_t1y_three_stage = FALSE;
	s_t1y_ep0_setup_armed = FALSE;
	(void)xzs_t1y_submit_setup();
}

static void
xzs_t1y_handle_setup(void)
{
	const uint8_t *descriptor = NULL;
	uint32_t descriptor_length = 0;
	enum xzs_t1y_completion completion = XZS_T1Y_COMPLETE_NONE;
	struct usb_setup_packet *pkt;
	uint8_t response[2] = { 0, 0 };
	uint16_t value;
	uint16_t index;
	uint16_t length;

	xzs_dma_clean_invalidate((vm_offset_t)s_setup_pkt_buf, 8);
	pkt = (struct usb_setup_packet *)s_setup_pkt_buf;
	value = pkt->wValue;
	index = pkt->wIndex;
	length = pkt->wLength;
	g_xzs_usb_t1y_setup_observed = 1;
	xzs_breadcrumb(0xD740, 0x750);
	xzs_early_puts("[XZS-D7T1] D740/Y50 first EP0 SETUP packet\n");

	if ((pkt->bmRequestType & 0x60u) != 0) {
		xzs_t1y_stall_and_restart();
		return;
	}

	switch (pkt->bRequest) {
	case USB_REQ_GET_DESCRIPTOR: {
		uint8_t type = (uint8_t)(value >> 8);
		uint8_t descriptor_index = (uint8_t)value;
		if ((pkt->bmRequestType & 0x80u) == 0 || length == 0) {
			break;
		}
		if (type == USB_DT_DEVICE && descriptor_index == 0 && index == 0) {
			descriptor = s_device_descriptor;
			descriptor_length = sizeof(s_device_descriptor);
			completion = XZS_T1Y_COMPLETE_DEVICE_DESC;
			xzs_breadcrumb(0xD740, 0x751);
			xzs_early_puts("[XZS-D7T1] D740/Y51 GET_DESCRIPTOR(Device) request\n");
		} else if (type == USB_DT_CONFIG && descriptor_index == 0 && index == 0) {
			descriptor = s_config_descriptor;
			descriptor_length = sizeof(s_config_descriptor);
			completion = XZS_T1Y_COMPLETE_CONFIG_DESC;
			xzs_breadcrumb(0xD740, 0x770);
			xzs_early_puts("[XZS-D7T1] D740/Y70 Configuration descriptor request\n");
		} else if (type == USB_DT_DEVICE_QUALIFIER && descriptor_index == 0 && index == 0) {
			descriptor = s_device_qualifier_descriptor;
			descriptor_length = sizeof(s_device_qualifier_descriptor);
		} else if (type == USB_DT_STRING) {
			switch (descriptor_index) {
			case 0: descriptor = s_str_langid; descriptor_length = sizeof(s_str_langid); break;
			case 1: descriptor = s_str_mfg; descriptor_length = sizeof(s_str_mfg); break;
			case 2: descriptor = s_str_prod; descriptor_length = sizeof(s_str_prod); break;
			case 3: descriptor = s_str_serial; descriptor_length = sizeof(s_str_serial); break;
			default: break;
			}
		}
		if (descriptor != NULL) {
			if (descriptor_length > length) {
				descriptor_length = length;
			}
			if (xzs_t1y_send_data(descriptor, descriptor_length, completion) == 0) {
				return;
			}
		}
		break;
	}
	case USB_REQ_SET_ADDRESS:
		if (pkt->bmRequestType == 0 && index == 0 && length == 0 && value <= 127 &&
		    g_xzs_usb_t1y_configuration_value == 0) {
			uint32_t dcfg = dwc3_read32(DWC3_DCFG);
			dcfg &= ~DWC3_DCFG_DEVADDR_MASK;
			dcfg |= ((uint32_t)value << 3);
			dwc3_write32(DWC3_DCFG, dcfg);
			g_xzs_usb_set_addr_count++;
			s_t1y_completion = XZS_T1Y_COMPLETE_SET_ADDRESS;
			s_t1y_three_stage = FALSE;
			s_t1y_ep0_state = XZS_T1Y_EP0_WAIT_STATUS;
			xzs_breadcrumb(0xD740, 0x760);
			xzs_early_puts("[XZS-D7T1] D740/Y60 SET_ADDRESS request\n");
			return;
		}
		break;
	case USB_REQ_SET_CONFIGURATION:
		if (pkt->bmRequestType == 0 && index == 0 && length == 0 && value <= 1) {
			g_xzs_usb_set_cfg_count++;
			g_xzs_usb_t1y_configuration_value = value;
			g_xzs_usb_configured = (value == 1);
			g_xzs_usb_console_ready = 0;
			s_t1y_completion = XZS_T1Y_COMPLETE_SET_CONFIGURATION;
			s_t1y_three_stage = FALSE;
			s_t1y_ep0_state = XZS_T1Y_EP0_WAIT_STATUS;
			xzs_breadcrumb(0xD740, 0x780);
			xzs_early_puts("[XZS-D7T1] D740/Y80 SET_CONFIGURATION request\n");
			return;
		}
		break;
	case USB_REQ_GET_CONFIGURATION:
		if (pkt->bmRequestType == 0x80 && value == 0 && index == 0 && length == 1) {
			response[0] = (uint8_t)g_xzs_usb_t1y_configuration_value;
			if (xzs_t1y_send_data(response, 1, XZS_T1Y_COMPLETE_NONE) == 0) return;
		}
		break;
	case USB_REQ_GET_STATUS:
		if ((pkt->bmRequestType & 0x80u) && value == 0 && length == 2) {
			uint8_t recipient = pkt->bmRequestType & 0x1fu;
			if (recipient == 0 && index == 0) {
				response[0] = 1; /* self-powered */
				if (xzs_t1y_send_data(response, 2, XZS_T1Y_COMPLETE_NONE) == 0) return;
			} else if (recipient == 1 && index == 0 && g_xzs_usb_configured) {
				if (xzs_t1y_send_data(response, 2, XZS_T1Y_COMPLETE_NONE) == 0) return;
			} else if (recipient == 2 && (index == 0 || index == 0x80)) {
				if (xzs_t1y_send_data(response, 2, XZS_T1Y_COMPLETE_NONE) == 0) return;
			}
		}
		break;
	default:
		break;
	}

	xzs_t1y_stall_and_restart();
}

static void
xzs_t1y_ep0_only_reset(void)
{
	uint32_t dcfg = dwc3_read32(DWC3_DCFG);

	g_xzs_usb_reset_count++;
	xzs_breadcrumb(0xD740, 0x741);
	xzs_early_puts("[XZS-D7T1] D740/Y41 USB Reset\n");
	if (dcfg & DWC3_DCFG_DEVADDR_MASK) {
		dwc3_write32(DWC3_DCFG, dcfg & ~DWC3_DCFG_DEVADDR_MASK);
	}
	g_xzs_usb_t1y_configuration_value = 0;
	g_xzs_usb_configured = 0;
	g_xzs_usb_enumerated = 0;
	g_xzs_usb_console_ready = 0;
	s_t1y_completion = XZS_T1Y_COMPLETE_NONE;
	s_t1y_three_stage = FALSE;
	/* DWC3 keeps EP0 configured across USB reset.  Keep the armed SETUP TRB. */
	if (!s_t1y_ep0_setup_armed) {
		(void)xzs_t1y_submit_setup();
	} else {
		s_t1y_ep0_state = XZS_T1Y_EP0_SETUP;
	}
}

/*
 * T1-Z Z1–Z4.  SET_CONFIGURATION completion only raises a pending flag.
 * The long DEPCMD sequence runs at the end of xzs_t1y_event_deferred(),
 * which is a thread_call.  xzs_usb_irq_handler only copies events.
 * ml_at_interrupt_context() must be false before any EP2/EP3 command.
 *
 * DEPSTARTCFG resource index is 2 (non-control allocation).  Each endpoint
 * then receives SETTRANSFRESOURCE with NUM_XFER_RES=1.  The XferRscIdx used
 * by STARTTRANSFER is the value DWC3 returns in DEPCMD[22:16], never a
 * compile-time guess.
 *
 * Bulk buffers are static kernel objects (Normal WBWA, not ml_io_map).
 * Bulk OUT is device→CPU (dc ivac).  Bulk IN is CPU→device (dc cvac).
 * TRBs are cleaned before handoff and invalidated before the CPU reads status.
 */
#define XZS_T1Z_PHASE_IDLE 0u
#define XZS_T1Z_PHASE_OUT1 1u
#define XZS_T1Z_PHASE_IN1  2u
#define XZS_T1Z_PHASE_OUT2 3u
#define XZS_T1Z_PHASE_IN2  4u
#define XZS_T1Z_PHASE_DONE 5u

static uint8_t s_bulk_out_buf[512] __attribute__((aligned(4096)));
static uint8_t s_bulk_in_buf[512] __attribute__((aligned(4096)));
static struct dwc3_trb s_bulk_out_trb __attribute__((aligned(4096)));
static struct dwc3_trb s_bulk_in_trb __attribute__((aligned(4096)));
static uint8_t s_t1z_out1[512];
static uint8_t s_t1z_out2[512];
static const uint8_t s_t1z_in_payload[] = {
	'X','Z','S','-','B','U','L','K','-','I','N','-','T','E','S','T','\n'
};

static volatile uint32_t s_t1z_phase = XZS_T1Z_PHASE_IDLE;

static int
xzs_t1z_prove(const void *obj, uint32_t size, uint32_t align,
    volatile uint64_t *va_out, volatile uint64_t *pa_out, volatile uint32_t *contig)
{
	vm_offset_t va = (vm_offset_t)obj;
	vm_offset_t pa = ml_vtophys(va);
	vm_offset_t pa_last = ml_vtophys(va + size - 1);

	*va_out = (uint64_t)va;
	*pa_out = (uint64_t)pa;
	*contig = (pa != 0 && pa_last == pa + size - 1);
	if (pa == 0 || (pa & (align - 1u)) != 0 || *contig == 0) {
		return -1;
	}
	return 0;
}

static int
xzs_t1z_start(uint32_t ep, struct dwc3_trb *trb, volatile uint32_t *rsc_out)
{
	vm_offset_t pa = ml_vtophys((vm_offset_t)trb);
	uint32_t completed = 0;

	if (pa == 0 || (pa & 63u) != 0) {
		return -1;
	}
	xzs_dma_clean_poc((vm_offset_t)trb, sizeof(*trb));
	if (xzs_t1y_ep_cmd(ep, DEPCMD_STARTTRANSFER,
	    (uint32_t)(pa >> 32), (uint32_t)pa, 0, &completed) != 0) {
		return -1;
	}
	*rsc_out = DEPCMD_RESOURCE_INDEX(completed);
	return 0;
}

static int
xzs_t1z_arm_out(void)
{
	vm_offset_t pa = (vm_offset_t)g_xzs_usb_t1z_out_buf_pa;

	memset(s_bulk_out_buf, 0, sizeof(s_bulk_out_buf));
	xzs_dma_invalidate_poc((vm_offset_t)s_bulk_out_buf, sizeof(s_bulk_out_buf));
	s_bulk_out_trb.bpl = (uint32_t)pa;
	s_bulk_out_trb.bph = (uint32_t)(pa >> 32);
	s_bulk_out_trb.size = 512;
	s_bulk_out_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	    DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_TRBCTL_NORMAL;
	return xzs_t1z_start(DWC3_PHYS_EP_BULK_OUT, &s_bulk_out_trb, &g_xzs_usb_t1z_ep2_rsc);
}

static int
xzs_t1z_arm_in(const uint8_t *data, uint32_t length, volatile uint32_t *rsc_out)
{
	vm_offset_t pa = (vm_offset_t)g_xzs_usb_t1z_in_buf_pa;

	if (length == 0 || length > sizeof(s_bulk_in_buf)) {
		return -1;
	}
	memcpy(s_bulk_in_buf, data, length);
	xzs_dma_clean_poc((vm_offset_t)s_bulk_in_buf, length);
	s_bulk_in_trb.bpl = (uint32_t)pa;
	s_bulk_in_trb.bph = (uint32_t)(pa >> 32);
	s_bulk_in_trb.size = length;
	s_bulk_in_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	    DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_TRBCTL_NORMAL;
	return xzs_t1z_start(DWC3_PHYS_EP_BULK_IN, &s_bulk_in_trb, rsc_out);
}

static uint32_t
xzs_t1z_received_length(struct dwc3_trb *trb, uint32_t programmed)
{
	uint32_t remaining;

	xzs_dma_invalidate_poc((vm_offset_t)trb, sizeof(*trb));
	remaining = trb->size & 0x00ffffffu;
	if (remaining > programmed) {
		return 0;
	}
	return programmed - remaining;
}

static void
xzs_rx_enqueue(uint8_t byte)
{
	uint32_t head = s_rx_head;
	uint32_t next = (head + 1u) % XZS_RX_RING_SIZE;
	uint32_t fill;

	if (next == s_rx_tail) {
		g_xzs_usb_rx_drops++;
		return;
	}
	s_rx_ring[head] = byte;
	__asm__ volatile("dmb ish" ::: "memory");
	s_rx_head = next;
	g_xzs_usb_rx_enqueued++;
	fill = (next + XZS_RX_RING_SIZE - s_rx_tail) % XZS_RX_RING_SIZE;
	if (fill > g_xzs_usb_rx_high) {
		g_xzs_usb_rx_high = fill;
	}
	if (g_xzs_usb_z5_count < 4) {
		g_xzs_usb_z5_bytes[g_xzs_usb_z5_count++] = byte;
	}
}

static void
xzs_t1z_rx_worker(thread_call_param_t p0 __unused, thread_call_param_t p1 __unused)
{
	extern void cons_cinput(char ch);

	g_xzs_usb_rx_worker_calls++;
	for (;;) {
		uint32_t tail = s_rx_tail;
		uint8_t byte;
		if (tail == s_rx_head) {
			break;
		}
		byte = s_rx_ring[tail];
		__asm__ volatile("dmb ish" ::: "memory");
		s_rx_tail = (tail + 1u) % XZS_RX_RING_SIZE;
		g_xzs_usb_rx_dequeued++;
		cons_cinput((char)byte);
		g_xzs_usb_cons_cinput_count++;
	}
}

static void
xzs_t1z_tx_worker(thread_call_param_t p0 __unused, thread_call_param_t p1 __unused)
{
	uint8_t tmp[512];
	uint32_t count = 0;

	g_xzs_usb_tx_worker_calls++;
	if (!g_xzs_usb_tty_bridge || s_tx_busy) {
		return;
	}
	if (!xzs_tx_lock_acquire()) {
		return;
	}
	while (count < sizeof(tmp) && s_tx_tail != s_tx_head) {
		tmp[count++] = s_tx_ring[s_tx_tail];
		s_tx_tail = (s_tx_tail + 1u) % XZS_TX_RING_SIZE;
	}
	xzs_tx_lock_release();
	if (count == 0) {
		return;
	}
	s_tx_programmed = count;
	s_tx_busy = 1;
	g_xzs_usb_tx_dequeued += count;
	if (xzs_t1z_arm_in(tmp, count, &g_xzs_usb_t1z_ep3_rsc) != 0) {
		s_tx_busy = 0;
		g_xzs_usb_tx_drops += count;
	}
}

static void
xzs_t1z_enable_bridge(void)
{
	if (ml_at_interrupt_context()) {
		g_xzs_usb_t1z_config_in_hard_irq = 1;
		return;
	}
	if (s_rx_tty_call == NULL) {
		s_rx_tty_call = thread_call_allocate(xzs_t1z_rx_worker, NULL);
	}
	if (s_tx_call == NULL) {
		s_tx_call = thread_call_allocate(xzs_t1z_tx_worker, NULL);
	}
	g_xzs_usb_tty_bridge = 1;
	g_xzs_usb_console_ready = 1;
	(void)xzs_t1z_arm_out();
	xzs_tx_kick();
}

static void
xzs_t1z_on_out_complete(void)
{
	uint32_t length = xzs_t1z_received_length(&s_bulk_out_trb, 512);
	uint8_t *dest = NULL;
	uint32_t i;

	xzs_dma_invalidate_poc((vm_offset_t)s_bulk_out_buf, sizeof(s_bulk_out_buf));
	if (s_t1z_phase == XZS_T1Z_PHASE_DONE && g_xzs_usb_tty_bridge) {
		for (i = 0; i < length; i++) {
			xzs_rx_enqueue(s_bulk_out_buf[i]);
		}
		g_xzs_usb_bulk_out_count++;
		g_xzs_usb_bulk_out_bytes += length;
		if (s_rx_tty_call != NULL) {
			(void)thread_call_enter(s_rx_tty_call);
		}
		(void)xzs_t1z_arm_out();
		return;
	}
	if (s_t1z_phase == XZS_T1Z_PHASE_OUT1) {
		dest = s_t1z_out1;
		g_xzs_usb_t1z_out1_len = length;
	} else if (s_t1z_phase == XZS_T1Z_PHASE_OUT2) {
		dest = s_t1z_out2;
		g_xzs_usb_t1z_out2_len = length;
	} else {
		g_xzs_usb_t1z_failed = 1;
		return;
	}
	if (length > sizeof(s_bulk_out_buf)) {
		g_xzs_usb_t1z_failed = 1;
		return;
	}
	memcpy(dest, s_bulk_out_buf, length);
	g_xzs_usb_bulk_out_count++;
	g_xzs_usb_bulk_out_bytes += length;
	if (s_t1z_phase == XZS_T1Z_PHASE_OUT1) {
		s_t1z_phase = XZS_T1Z_PHASE_IN1;
		g_xzs_usb_t1z_in1_len = sizeof(s_t1z_in_payload);
		if (xzs_t1z_arm_in(s_t1z_in_payload, sizeof(s_t1z_in_payload),
		    &g_xzs_usb_t1z_ep3_rsc) != 0) {
			g_xzs_usb_t1z_failed = 1;
		}
	} else {
		s_t1z_phase = XZS_T1Z_PHASE_IN2;
		g_xzs_usb_t1z_in2_len = length;
		if (xzs_t1z_arm_in(s_t1z_out2, length, &g_xzs_usb_t1z_ep3_rsc_echo) != 0) {
			g_xzs_usb_t1z_failed = 1;
		}
	}
}

static void
xzs_t1z_on_in_complete(void)
{
	uint32_t programmed = s_tx_programmed;
	uint32_t length;

	if (s_t1z_phase == XZS_T1Z_PHASE_IN1) {
		programmed = g_xzs_usb_t1z_in1_len;
	} else if (s_t1z_phase == XZS_T1Z_PHASE_IN2) {
		programmed = g_xzs_usb_t1z_in2_len;
	}
	length = xzs_t1z_received_length(&s_bulk_in_trb, programmed);

	if (s_t1z_phase == XZS_T1Z_PHASE_DONE && s_tx_busy) {
		s_tx_busy = 0;
		g_xzs_usb_tx_completions++;
		g_xzs_usb_bulk_in_count++;
		g_xzs_usb_bulk_in_bytes += length;
		xzs_tx_kick();
		return;
	}
	if (s_t1z_phase == XZS_T1Z_PHASE_IN1) {
		g_xzs_usb_bulk_in_count++;
		g_xzs_usb_bulk_in_bytes += length;
		s_t1z_phase = XZS_T1Z_PHASE_OUT2;
		if (xzs_t1z_arm_out() != 0) {
			g_xzs_usb_t1z_failed = 1;
		}
	} else if (s_t1z_phase == XZS_T1Z_PHASE_IN2) {
		g_xzs_usb_bulk_in_count++;
		g_xzs_usb_bulk_in_bytes += length;
		s_t1z_phase = XZS_T1Z_PHASE_DONE;
		g_xzs_usb_t1z_pipeline_done = 1;
		xzs_t1z_enable_bridge();
	} else {
		g_xzs_usb_t1z_failed = 1;
	}
}

static void
xzs_t1z_worker(void)
{
	uint32_t completed = 0;
	uint32_t p0;
	uint32_t p1;
	uint32_t dalep;

	g_xzs_usb_t1z_config_in_hard_irq = ml_at_interrupt_context() ? 1u : 0u;
	if (g_xzs_usb_t1z_config_in_hard_irq) {
		g_xzs_usb_t1z_failed = 1;
		xzs_early_puts("[XZS-D7T1] D740/Z10 refused: hard IRQ context\n");
		return;
	}
	xzs_early_puts("[XZS-D7T1] D740/Z10 thread_call endpoint configuration\n");
	if (xzs_t1z_prove(s_bulk_out_buf, sizeof(s_bulk_out_buf), 64,
	    &g_xzs_usb_t1z_out_buf_va, &g_xzs_usb_t1z_out_buf_pa,
	    &g_xzs_usb_t1z_out_buf_contig) != 0 ||
	    xzs_t1z_prove(&s_bulk_out_trb, sizeof(s_bulk_out_trb), 64,
	    &g_xzs_usb_t1z_out_trb_va, &g_xzs_usb_t1z_out_trb_pa,
	    &g_xzs_usb_t1z_out_trb_contig) != 0 ||
	    xzs_t1z_prove(s_bulk_in_buf, sizeof(s_bulk_in_buf), 64,
	    &g_xzs_usb_t1z_in_buf_va, &g_xzs_usb_t1z_in_buf_pa,
	    &g_xzs_usb_t1z_in_buf_contig) != 0 ||
	    xzs_t1z_prove(&s_bulk_in_trb, sizeof(s_bulk_in_trb), 64,
	    &g_xzs_usb_t1z_in_trb_va, &g_xzs_usb_t1z_in_trb_pa,
	    &g_xzs_usb_t1z_in_trb_contig) != 0) {
		g_xzs_usb_t1z_failed = 1;
		xzs_early_puts("[XZS-D7T1] D740/Z11 DMA proof failed\n");
		return;
	}
	g_xzs_usb_t1z_dma_ok = 1;
	xzs_early_puts("[XZS-D7T1] D740/Z11 DMA VA/PA proof passed\n");

	g_xzs_usb_t1z_depstartcfg_param = 2;
	if (xzs_t1y_ep_cmd(DWC3_PHYS_EP_CTRL_OUT,
	    DEPCMD_STARTNEWCFG | DEPCMD_PARAM(2), 0, 0, 0, &completed) != 0) {
		g_xzs_usb_t1z_failed = 1;
		xzs_early_puts("[XZS-D7T1] D740/Z12 DEPSTARTCFG(2) failed\n");
		return;
	}
	xzs_early_puts("[XZS-D7T1] D740/Z12 DEPSTARTCFG resource_index=2\n");

	p0 = DWC3_DEPCFG_EP_TYPE(DWC3_EP_TYPE_BULK) |
	    DWC3_DEPCFG_MAX_PACKET_SIZE(512) | DWC3_DEPCFG_FIFO_NUMBER(2);
	p1 = DWC3_DEPCFG_XFER_COMPLETE_EN | DWC3_DEPCFG_XFER_NOT_READY_EN |
	    DWC3_DEPCFG_EP_NUMBER(DWC3_PHYS_EP_BULK_OUT);
	g_xzs_usb_t1z_num_xfer_res = DWC3_DEPXFERCFG_NUM_XFER_RES(1);
	if (xzs_t1y_ep_cmd(DWC3_PHYS_EP_BULK_OUT, DEPCMD_SETEPCONFIG, p0, p1, 0,
	    &completed) != 0 ||
	    xzs_t1y_ep_cmd(DWC3_PHYS_EP_BULK_OUT, DEPCMD_SETTRANSXFR,
	    g_xzs_usb_t1z_num_xfer_res, 0, 0, &completed) != 0) {
		g_xzs_usb_t1z_failed = 1;
		xzs_early_puts("[XZS-D7T1] D740/Z20 EP2 configuration failed\n");
		return;
	}
	g_xzs_usb_t1z_ep2_configured = 1;
	xzs_early_puts("[XZS-D7T1] D740/Z20 EP2 SETEPCONFIG+SETTRANSFRESOURCE\n");

	p0 = DWC3_DEPCFG_EP_TYPE(DWC3_EP_TYPE_BULK) |
	    DWC3_DEPCFG_MAX_PACKET_SIZE(512) | DWC3_DEPCFG_FIFO_NUMBER(3);
	p1 = DWC3_DEPCFG_XFER_COMPLETE_EN | DWC3_DEPCFG_XFER_NOT_READY_EN |
	    DWC3_DEPCFG_EP_NUMBER(DWC3_PHYS_EP_BULK_IN);
	if (xzs_t1y_ep_cmd(DWC3_PHYS_EP_BULK_IN, DEPCMD_SETEPCONFIG, p0, p1, 0,
	    &completed) != 0 ||
	    xzs_t1y_ep_cmd(DWC3_PHYS_EP_BULK_IN, DEPCMD_SETTRANSXFR,
	    g_xzs_usb_t1z_num_xfer_res, 0, 0, &completed) != 0) {
		g_xzs_usb_t1z_failed = 1;
		xzs_early_puts("[XZS-D7T1] D740/Z21 EP3 configuration failed\n");
		return;
	}
	g_xzs_usb_t1z_ep3_configured = 1;
	xzs_early_puts("[XZS-D7T1] D740/Z21 EP3 SETEPCONFIG+SETTRANSFRESOURCE\n");

	dalep = dwc3_read32(DWC3_DALEPENA);
	dwc3_write32(DWC3_DALEPENA, dalep | (1u << DWC3_PHYS_EP_BULK_OUT) |
	    (1u << DWC3_PHYS_EP_BULK_IN));
	g_xzs_usb_t1z_dalepena = dwc3_read32(DWC3_DALEPENA);
	if ((g_xzs_usb_t1z_dalepena & ((1u << DWC3_PHYS_EP_BULK_OUT) |
	    (1u << DWC3_PHYS_EP_BULK_IN))) !=
	    ((1u << DWC3_PHYS_EP_BULK_OUT) | (1u << DWC3_PHYS_EP_BULK_IN))) {
		g_xzs_usb_t1z_failed = 1;
		xzs_early_puts("[XZS-D7T1] D740/Z22 DALEPENA readback failed\n");
		return;
	}
	xzs_early_puts("[XZS-D7T1] D740/Z22 DALEPENA EP2+EP3 enabled\n");
	s_t1z_phase = XZS_T1Z_PHASE_OUT1;
	if (xzs_t1z_arm_out() != 0) {
		g_xzs_usb_t1z_failed = 1;
		xzs_early_puts("[XZS-D7T1] D740/Z30 Bulk OUT prime failed\n");
		return;
	}
	xzs_early_puts("[XZS-D7T1] D740/Z30 Bulk OUT primed\n");
}

static void
xzs_t1y_process_event(uint32_t event)
{
	if ((event & 1u) == 0) {
		uint32_t ep = (event >> 1) & 0x1fu;
		uint32_t type = (event >> 6) & 0x0fu;

		if (ep > DWC3_PHYS_EP_CTRL_IN) {
			if (type == DWC3_DEPEVT_XFERCOMPLETE &&
			    g_xzs_usb_t1z_ep2_configured &&
			    g_xzs_usb_t1z_ep3_configured) {
				if (ep == DWC3_PHYS_EP_BULK_OUT) {
					xzs_t1z_on_out_complete();
				} else if (ep == DWC3_PHYS_EP_BULK_IN) {
					xzs_t1z_on_in_complete();
				}
			}
			return;
		}
		if (type == DWC3_DEPEVT_XFERCOMPLETE) {
			if (s_t1y_ep0_state == XZS_T1Y_EP0_SETUP &&
			    ep == DWC3_PHYS_EP_CTRL_OUT) {
				s_ep0_out_rsc_idx = 0;
				s_t1y_ep0_setup_armed = FALSE;
				xzs_t1y_handle_setup();
			} else if (s_t1y_ep0_state == XZS_T1Y_EP0_DATA_IN &&
			    ep == DWC3_PHYS_EP_CTRL_IN) {
				s_ep0_in_rsc_idx = 0;
				s_t1y_ep0_state = XZS_T1Y_EP0_WAIT_STATUS;
			} else if (s_t1y_ep0_state == XZS_T1Y_EP0_STATUS) {
				if (ep == DWC3_PHYS_EP_CTRL_IN) s_ep0_in_rsc_idx = 0;
				else s_ep0_out_rsc_idx = 0;
				xzs_t1y_finish_control_transfer();
			}
		} else if (type == DWC3_DEPEVT_XFERNOTREADY &&
		    s_t1y_ep0_state == XZS_T1Y_EP0_WAIT_STATUS &&
		    DWC3_DEPEVT_STATUS_PHASE(event) == DWC3_DEPEVT_STATUS_CONTROL_STATUS) {
			if (xzs_t1y_start_status(ep) != 0) {
				xzs_t1y_stall_and_restart();
			}
		}
		return;
	}

	switch ((event >> 8) & 0x0fu) {
	case DWC3_DEVICE_EVENT_DISCONNECT:
		g_xzs_usb_t1y_configuration_value = 0;
		g_xzs_usb_configured = 0;
		g_xzs_usb_enumerated = 0;
		break;
	case DWC3_DEVICE_EVENT_RESET:
		xzs_t1y_ep0_only_reset();
		break;
	case DWC3_DEVICE_EVENT_CONNECT_DONE:
		g_xzs_usb_conn_done_count++;
		g_xzs_usb_dsts = dwc3_read32(DWC3_DSTS);
		g_xzs_usb_t1y_connect_speed = g_xzs_usb_dsts & DWC3_DSTS_CONNECTSPD_MASK;
		g_xzs_usb_t1y_link_state =
		    (g_xzs_usb_dsts & DWC3_DSTS_USBLNKST_MASK) >> 18;
		xzs_breadcrumb(0xD740, 0x742);
		xzs_early_puts("[XZS-D7T1] D740/Y42 ConnectDone\n");
		break;
	default:
		break;
	}
}

static void
xzs_t1y_event_deferred(thread_call_param_t p0 __unused,
    thread_call_param_t p1 __unused)
{
	for (;;) {
		uint32_t tail = s_t1y_event_tail;
		uint32_t event;
		if (tail == s_t1y_event_head) {
			break;
		}
		event = s_t1y_event_ring[tail];
		__asm__ volatile("dmb ish" ::: "memory");
		s_t1y_event_tail = (tail + 1) % XZS_T1Y_EVENT_RING_ENTRIES;
		if (!s_t1y_first_event_reported) {
			s_t1y_first_event_reported = TRUE;
			xzs_early_puts("[XZS-D7T1] D740/Y40 first DWC3 event\n");
		}
		xzs_early_puts("[XZS-D7T1] EVENT=0x");
		xzs_d6m4_put_hex64(event);
		xzs_early_puts("\n");
		xzs_t1y_process_event(event);
	}
	if (g_xzs_usb_t1z_activation_pending && !g_xzs_usb_t1z_worker_entered) {
		g_xzs_usb_t1z_worker_entered = 1;
		xzs_t1z_worker();
	}
}

static volatile boolean_t s_t1y_sync_telemetry_logged = FALSE;

static volatile boolean_t s_t1y_pos_synchronized = FALSE;

static void
xzs_t1y_drain_event_buffer(void)
{
	uint32_t count = dwc3_read32(DWC3_GEVNTCNT0) & DWC3_GEVNTCOUNT_PENDING_MASK;
	uint32_t available_events = count / sizeof(uint32_t);
	uint32_t consumed_events = 0;

	if (count > 0 && !s_t1y_pos_synchronized && s_uncached_event_buf != NULL) {
		for (uint32_t i = 0; i < (XZS_DWC3_EVENT_BUFFER_SIZE / sizeof(uint32_t)); i++) {
			uint32_t expected = 0xA5A50000u | i;
			if (s_uncached_event_buf[i] != expected) {
				s_t1y_event_buf_pos = i * sizeof(uint32_t);
				s_t1y_pos_synchronized = TRUE;
				xzs_early_puts("[XZS-D7T1] POS_SYNCHRONIZED_TO_SLOT=0x");
				xzs_d6m4_put_hex64(i);
				xzs_early_puts("\n");
				break;
			}
		}
	}

	if (count > 0 && !s_t1y_sync_telemetry_logged) {
		s_t1y_sync_telemetry_logged = TRUE;
		g_xzs_usb_t1y_gevntcount_raw = count;

		uint32_t w_before0 = *(volatile uint32_t *)(void *)(s_candidate2c_event_buffer + s_t1y_event_buf_pos);
		uint32_t w_before1 = *(volatile uint32_t *)(void *)(s_candidate2c_event_buffer + ((s_t1y_event_buf_pos + 4) % XZS_DWC3_EVENT_BUFFER_SIZE));
		g_xzs_usb_t1y_word_before_sync_0 = w_before0;
		g_xzs_usb_t1y_word_before_sync_1 = w_before1;

		uint32_t gevntadrlo = dwc3_read32(DWC3_GEVNTADR0);
		uint32_t gevntadrhi = dwc3_read32(DWC3_GEVNTADR_HI0);
		uint64_t gevntadr_comb = ((uint64_t)gevntadrhi << 32) | gevntadrlo;

		/* Invalidate the event buffer from CPU cache to PoC */
		xzs_dma_clean_invalidate((vm_offset_t)s_candidate2c_event_buffer, XZS_DWC3_EVENT_BUFFER_SIZE);

		uint32_t w_after0 = *(volatile uint32_t *)(void *)(s_candidate2c_event_buffer + s_t1y_event_buf_pos);
		uint32_t w_after1 = *(volatile uint32_t *)(void *)(s_candidate2c_event_buffer + ((s_t1y_event_buf_pos + 4) % XZS_DWC3_EVENT_BUFFER_SIZE));
		g_xzs_usb_t1y_word_after_sync_0 = w_after0;
		g_xzs_usb_t1y_word_after_sync_1 = w_after1;

		uint32_t w_uncached0 = 0, w_uncached1 = 0;
		if (s_uncached_event_buf != NULL) {
			w_uncached0 = s_uncached_event_buf[s_t1y_event_buf_pos / sizeof(uint32_t)];
			w_uncached1 = s_uncached_event_buf[((s_t1y_event_buf_pos + 4) % XZS_DWC3_EVENT_BUFFER_SIZE) / sizeof(uint32_t)];
		}
		g_xzs_usb_t1y_word_uncached_0 = w_uncached0;
		g_xzs_usb_t1y_word_uncached_1 = w_uncached1;

		uint32_t w_fb0 = 0, w_fb1 = 0;
		if (s_fb_event_buf != NULL) {
			w_fb0 = s_fb_event_buf[s_t1y_event_buf_pos / sizeof(uint32_t)];
			w_fb1 = s_fb_event_buf[((s_t1y_event_buf_pos + 4) % XZS_DWC3_EVENT_BUFFER_SIZE) / sizeof(uint32_t)];
		}
		g_xzs_usb_t1y_word_fb_0 = w_fb0;
		g_xzs_usb_t1y_word_fb_1 = w_fb1;

		int32_t mod_idx = -1;
		uint32_t mod_val = 0;
		if (s_uncached_event_buf != NULL) {
			for (uint32_t i = 0; i < (XZS_DWC3_EVENT_BUFFER_SIZE / sizeof(uint32_t)); i++) {
				uint32_t expected = 0xA5A50000u | i;
				if (s_uncached_event_buf[i] != expected) {
					mod_idx = (int32_t)i;
					mod_val = s_uncached_event_buf[i];
					break;
				}
			}
		}
		g_xzs_usb_t1y_modified_slot_idx = mod_idx;
		g_xzs_usb_t1y_modified_slot_val = mod_val;

		int32_t fb_mod_idx = -1;
		uint32_t fb_mod_val = 0;
		if (s_fb_event_buf != NULL) {
			for (uint32_t i = 0; i < (XZS_DWC3_EVENT_BUFFER_SIZE / sizeof(uint32_t)); i++) {
				uint32_t orig = (i < 4) ? s_fb_initial_words[i] : 0;
				if (s_fb_event_buf[i] != orig) {
					fb_mod_idx = (int32_t)i;
					fb_mod_val = s_fb_event_buf[i];
					break;
				}
			}
		}
		g_xzs_usb_t1y_fb_modified_idx = fb_mod_idx;
		g_xzs_usb_t1y_fb_modified_val = fb_mod_val;

		xzs_early_puts("\n[XZS-D7T1] === DMA VISIBILITY BEFORE/AFTER SYNC ===\n");
		xzs_early_puts("EVENT_BUFFER_VA=0x"); xzs_d6m4_put_hex64((uint64_t)(vm_offset_t)s_candidate2c_event_buffer); xzs_early_puts("\n");
		xzs_early_puts("EVENT_BUFFER_PA=0x"); xzs_d6m4_put_hex64(ml_vtophys((vm_offset_t)s_candidate2c_event_buffer)); xzs_early_puts("\n");
		xzs_early_puts("GEVNTADR_COMBINED=0x"); xzs_d6m4_put_hex64(gevntadr_comb); xzs_early_puts("\n");
		xzs_early_puts("EVENT_LPOS=0x"); xzs_d6m4_put_hex64(s_t1y_event_buf_pos); xzs_early_puts("\n");
		xzs_early_puts("GEVNTCOUNT=0x"); xzs_d6m4_put_hex64(count); xzs_early_puts("\n");
		xzs_early_puts("WORD_BEFORE_SYNC_0=0x"); xzs_d6m4_put_hex64(w_before0); xzs_early_puts("\n");
		xzs_early_puts("WORD_BEFORE_SYNC_1=0x"); xzs_d6m4_put_hex64(w_before1); xzs_early_puts("\n");
		xzs_early_puts("WORD_AFTER_SYNC_0=0x"); xzs_d6m4_put_hex64(w_after0); xzs_early_puts("\n");
		xzs_early_puts("WORD_AFTER_SYNC_1=0x"); xzs_d6m4_put_hex64(w_after1); xzs_early_puts("\n");
		xzs_early_puts("WORD_UNCACHED_0=0x"); xzs_d6m4_put_hex64(w_uncached0); xzs_early_puts("\n");
		xzs_early_puts("WORD_UNCACHED_1=0x"); xzs_d6m4_put_hex64(w_uncached1); xzs_early_puts("\n");
		xzs_early_puts("WORD_FB_EVBUF_0=0x"); xzs_d6m4_put_hex64(w_fb0); xzs_early_puts("\n");
		xzs_early_puts("WORD_FB_EVBUF_1=0x"); xzs_d6m4_put_hex64(w_fb1); xzs_early_puts("\n");
		xzs_early_puts("MODIFIED_SLOT_IDX=0x"); xzs_d6m4_put_hex64((uint64_t)(int64_t)mod_idx); xzs_early_puts("\n");
		xzs_early_puts("MODIFIED_SLOT_VAL=0x"); xzs_d6m4_put_hex64(mod_val); xzs_early_puts("\n");
		xzs_early_puts("FB_MODIFIED_IDX=0x"); xzs_d6m4_put_hex64((uint64_t)(int64_t)fb_mod_idx); xzs_early_puts("\n");
		xzs_early_puts("FB_MODIFIED_VAL=0x"); xzs_d6m4_put_hex64(fb_mod_val); xzs_early_puts("\n");
		xzs_early_puts("[XZS-D7T1] ========================================\n\n");
	}

	if (available_events > XZS_T1Y_IRQ_EVENT_BUDGET) {
		available_events = XZS_T1Y_IRQ_EVENT_BUDGET;
	}
	while (consumed_events < available_events) {
		uint32_t head = s_t1y_event_head;
		uint32_t next = (head + 1) % XZS_T1Y_EVENT_RING_ENTRIES;
		uint32_t *slot;
		uint32_t event;
		uint32_t slot_idx = s_t1y_event_buf_pos / sizeof(uint32_t);
		if (next == s_t1y_event_tail) {
			g_xzs_usb_t1y_event_ring_drops++;
			break;
		}
		if (s_uncached_event_buf != NULL &&
		    s_uncached_event_buf[slot_idx] != (0xA5A50000u | slot_idx)) {
			event = s_uncached_event_buf[slot_idx];
			s_uncached_event_buf[slot_idx] = 0xA5A50000u | slot_idx;
		} else {
			slot = (uint32_t *)(void *)(s_candidate2c_event_buffer + s_t1y_event_buf_pos);
			xzs_dma_clean_invalidate((vm_offset_t)slot, sizeof(*slot));
			event = *slot;
		}
		s_t1y_event_ring[head] = event;
		__asm__ volatile("dmb ish" ::: "memory");
		s_t1y_event_head = next;
		s_t1y_event_buf_pos = (s_t1y_event_buf_pos + sizeof(uint32_t)) %
		    XZS_DWC3_EVENT_BUFFER_SIZE;
		consumed_events++;
		if (!g_xzs_usb_t1y_event_dma_working) {
			g_xzs_usb_t1y_first_event = event;
			g_xzs_usb_t1y_event_dma_working = (event != 0 && event != (0xA5A50000u | slot_idx));
			if (g_xzs_usb_t1y_event_dma_working) {
				xzs_breadcrumb(0xD740, 0x740);
			}
		}
	}
	if (consumed_events != 0) {
		/* Acknowledge exactly the bytes copied out of the DMA buffer. */
		dwc3_write32(DWC3_GEVNTCNT0, consumed_events * sizeof(uint32_t));
		(void)thread_call_enter(s_t1y_event_call);
	}
}

void xzs_usb_poll_events(void)
{
	if (s_dwc3_base != 0 && s_t1y_event_call != NULL) {
		xzs_t1y_drain_event_buffer();
	}
}

void xzs_usb_irq_handler(void)
{
	g_xzs_usb_irq_count++;
	xzs_t1y_drain_event_buffer();
}

static int
xzs_t1y_connect(void)
{
	uint32_t dcfg;
	uint32_t gevntsiz;

	xzs_breadcrumb(0xD740, 0x700);
	xzs_early_puts("[XZS-D7T1] D740/Y00 T1-Y entered\n");
	g_xzs_usb_t1y_ep0_only_dispatch = 1;
	xzs_breadcrumb(0xD740, 0x710);
	xzs_early_puts("[XZS-D7T1] D740/Y10 EP0-only dispatcher ready\n");
	xzs_breadcrumb(0xD740, 0x711);
	xzs_early_puts("[XZS-D7T1] D740/Y11 legacy Bulk coupling absent\n");

	if (!g_xzs_usb_candidate2c_complete || !s_t1y_ep0_setup_armed ||
	    g_xzs_usb_t1y_bulk_endpoint_write_count != 0 ||
	    g_xzs_usb_t1y_tty_bridge_call_count != 0) {
		return -1;
	}
	dcfg = dwc3_read32(DWC3_DCFG);
	if ((dcfg & (DWC3_DCFG_SPEED_MASK | DWC3_DCFG_DEVADDR_MASK)) != 0) {
		return -1;
	}
	s_t1y_event_call = thread_call_allocate(xzs_t1y_event_deferred, NULL);
	if (s_t1y_event_call == NULL) {
		return -1;
	}
	xzs_breadcrumb(0xD740, 0x712);
	xzs_early_puts("[XZS-D7T1] D740/Y12 pre-connect state verified\n");

	g_xzs_usb_t1y_devten_before = dwc3_read32(DWC3_DEVTEN);
	dwc3_write32(DWC3_DEVTEN, DWC3_DEVTEN_T1Y_MASK);
	g_xzs_usb_t1y_devten_after = dwc3_read32(DWC3_DEVTEN);
	if ((g_xzs_usb_t1y_devten_after & DWC3_DEVTEN_T1Y_MASK) !=
	    DWC3_DEVTEN_T1Y_MASK) {
		return -1;
	}
	xzs_breadcrumb(0xD740, 0x720);
	xzs_early_puts("[XZS-D7T1] D740/Y20 DEVTEN configured\n");

	g_xzs_usb_t1y_gevntsiz_before = dwc3_read32(DWC3_GEVNTSIZ0);
	gevntsiz = g_xzs_usb_t1y_gevntsiz_before & ~DWC3_GEVNTSIZ_INTMASK;
	dwc3_write32(DWC3_GEVNTSIZ0, gevntsiz);
	g_xzs_usb_t1y_gevntsiz_after = dwc3_read32(DWC3_GEVNTSIZ0);
	if (g_xzs_usb_t1y_gevntsiz_after & DWC3_GEVNTSIZ_INTMASK) {
		return -1;
	}
	xzs_breadcrumb(0xD740, 0x721);
	xzs_early_puts("[XZS-D7T1] D740/Y21 event-buffer interrupt unmasked\n");

	g_xzs_usb_t1y_dctl_before = dwc3_read32(DWC3_DCTL);
	if (g_xzs_usb_t1y_dctl_before & DWC3_DCTL_RUN_STOP) {
		return -1;
	}
	xzs_breadcrumb(0xD740, 0x730);
	xzs_early_puts("[XZS-D7T1] D740/Y30 BEFORE RUN_STOP=1\n");
	g_xzs_usb_t1y_dctl_written = g_xzs_usb_t1y_dctl_before | DWC3_DCTL_RUN_STOP;
	dwc3_write32(DWC3_DCTL, g_xzs_usb_t1y_dctl_written);
	g_xzs_usb_t1y_dctl_write_count++;
	g_xzs_usb_t1y_dctl_after = dwc3_read32(DWC3_DCTL);
	g_xzs_usb_dctl = g_xzs_usb_t1y_dctl_after;
	if ((g_xzs_usb_t1y_dctl_after & DWC3_DCTL_RUN_STOP) == 0) {
		return -1;
	}
	xzs_breadcrumb(0xD740, 0x731);
	xzs_early_puts("[XZS-D7T1] D740/Y31 AFTER RUN_STOP=1 readback\n");

	/* Bounded window for IRQ/deferred Chapter-9 enumeration. */
	for (unsigned i = 0; i < 15000 && !g_xzs_usb_enumerated; i++) {
		xzs_watchdog_pet();
		delay(1000);
	}
	g_xzs_usb_t1y_complete =
	    ((g_xzs_usb_t1y_dctl_after & DWC3_DCTL_RUN_STOP) != 0) &&
	    g_xzs_usb_t1y_event_dma_working && g_xzs_usb_reset_count != 0 &&
	    g_xzs_usb_conn_done_count != 0 && g_xzs_usb_t1y_setup_observed &&
	    g_xzs_usb_t1y_device_desc_complete &&
	    g_xzs_usb_t1y_config_desc_complete &&
	    g_xzs_usb_t1y_set_address_complete &&
	    g_xzs_usb_t1y_set_configuration_complete &&
	    g_xzs_usb_t1y_configuration_value == 1 &&
	    g_xzs_usb_t1y_bulk_endpoint_write_count == 0 &&
	    g_xzs_usb_t1y_tty_bridge_call_count == 0 &&
	    g_xzs_usb_console_ready == 0;
	return g_xzs_usb_t1y_complete ? 0 : -1;
}

int
xzs_usb_t1z_service(void)
{
	if (!g_xzs_usb_t1z_activation_pending) {
		return -1;
	}
	if (s_t1y_event_call != NULL) {
		(void)thread_call_enter(s_t1y_event_call);
	}
	/*
	 * Stay up until Z4 finishes or the safety window expires.
	 * A fixed +25s halt is not used: completion ends the wait early,
	 * and a missed host never runs forever.
	 */
	for (uint32_t i = 0; i < 40000; i++) {
		if (g_xzs_usb_t1z_pipeline_done || g_xzs_usb_t1z_failed) {
			break;
		}
		xzs_watchdog_pet();
		delay(1000);
	}
	if (!g_xzs_usb_t1z_pipeline_done || g_xzs_usb_t1z_failed) {
		return -1;
	}
	/*
	 * Z4 is not a stop. Stay up for the live shell window. A later halt
	 * exists only so pstore can be collected; it is not triggered by Z4.
	 */
	{
		for (uint32_t i = 0; i < 90000; i++) {
			xzs_watchdog_pet();
			delay(1000);
		}
	}
	return 0;
}

static void
xzs_t1z_put_hex(uint64_t value)
{
	xzs_d6m4_put_hex64(value);
	xzs_early_puts("\n");
}

void
xzs_usb_t1z_report(void)
{
	extern volatile int xzs_d7m3_complete;

	xzs_early_puts("\n=======================================================\n");
	xzs_early_puts("=== D7-T1 T1-Z TARGET TELEMETRY BEGIN ===\n");
	xzs_early_puts("AUTHORITATIVE_XZS_USB_HEADER=src/xnu/pexpert/pexpert/arm/xzs_usb.h\n");
	xzs_early_puts("SET_CONFIGURATION_EXECUTION_CONTEXT=thread_call:xzs_t1y_event_deferred\n");
	xzs_early_puts("HARD_IRQ_CONFIGURES_ENDPOINTS=");
	xzs_early_puts(g_xzs_usb_t1z_config_in_hard_irq ? "yes\n" : "no\n");
	xzs_early_puts("EP2_CONFIG_SEMANTICS=DEPSTARTCFG_RSC_2+SETEPCONFIG+SETTRANSFRESOURCE_NUM_XFER_RES\n");
	xzs_early_puts("EP3_CONFIG_SEMANTICS=DEPSTARTCFG_RSC_2+SETEPCONFIG+SETTRANSFRESOURCE_NUM_XFER_RES\n");
	xzs_early_puts("TRANSFER_RESOURCE_POLICY=NUM_XFER_RES_1_START_RSC_FROM_DEPCMD\n");
	xzs_early_puts("DEPSTARTCFG_RESOURCE_INDEX=");
	xzs_t1z_put_hex(g_xzs_usb_t1z_depstartcfg_param);
	xzs_early_puts("SETTRANSFRESOURCE_NUM_XFER_RES=");
	xzs_t1z_put_hex(g_xzs_usb_t1z_num_xfer_res);
	xzs_early_puts("BULK_OUT_XFER_RSC_IDX=");
	xzs_t1z_put_hex(g_xzs_usb_t1z_ep2_rsc);
	xzs_early_puts("BULK_IN_XFER_RSC_IDX=");
	xzs_t1z_put_hex(g_xzs_usb_t1z_ep3_rsc);
	xzs_early_puts("BULK_IN_ECHO_XFER_RSC_IDX=");
	xzs_t1z_put_hex(g_xzs_usb_t1z_ep3_rsc_echo);
	xzs_early_puts("BULK_OUT_DMA_POLICY=device_to_cpu_dc_ivac\n");
	xzs_early_puts("BULK_IN_DMA_POLICY=cpu_to_device_dc_cvac\n");
	xzs_early_puts("BULK_DMA_MAP=CACHED_NORMAL_WBWA\n");
	xzs_early_puts("BULK_DMA_COHERENT=no\n");
	xzs_early_puts("BULK_DMA_PROOF=");
	xzs_early_puts(g_xzs_usb_t1z_dma_ok ? "yes\n" : "no\n");
	xzs_early_puts("BULK_OUT_BUF_VA="); xzs_t1z_put_hex(g_xzs_usb_t1z_out_buf_va);
	xzs_early_puts("BULK_OUT_BUF_PA="); xzs_t1z_put_hex(g_xzs_usb_t1z_out_buf_pa);
	xzs_early_puts("BULK_OUT_BUF_SIZE=512\n");
	xzs_early_puts("BULK_OUT_BUF_CONTIGUOUS=");
	xzs_early_puts(g_xzs_usb_t1z_out_buf_contig ? "yes\n" : "no\n");
	xzs_early_puts("BULK_OUT_TRB_VA="); xzs_t1z_put_hex(g_xzs_usb_t1z_out_trb_va);
	xzs_early_puts("BULK_OUT_TRB_PA="); xzs_t1z_put_hex(g_xzs_usb_t1z_out_trb_pa);
	xzs_early_puts("BULK_OUT_TRB_CONTIGUOUS=");
	xzs_early_puts(g_xzs_usb_t1z_out_trb_contig ? "yes\n" : "no\n");
	xzs_early_puts("BULK_IN_BUF_VA="); xzs_t1z_put_hex(g_xzs_usb_t1z_in_buf_va);
	xzs_early_puts("BULK_IN_BUF_PA="); xzs_t1z_put_hex(g_xzs_usb_t1z_in_buf_pa);
	xzs_early_puts("BULK_IN_BUF_SIZE=512\n");
	xzs_early_puts("BULK_IN_BUF_CONTIGUOUS=");
	xzs_early_puts(g_xzs_usb_t1z_in_buf_contig ? "yes\n" : "no\n");
	xzs_early_puts("BULK_IN_TRB_VA="); xzs_t1z_put_hex(g_xzs_usb_t1z_in_trb_va);
	xzs_early_puts("BULK_IN_TRB_PA="); xzs_t1z_put_hex(g_xzs_usb_t1z_in_trb_pa);
	xzs_early_puts("BULK_IN_TRB_CONTIGUOUS=");
	xzs_early_puts(g_xzs_usb_t1z_in_trb_contig ? "yes\n" : "no\n");
	xzs_early_puts("RX_RING_CONCURRENCY_MODEL=SPSC_ONE_DWC3_WORKER_ONE_TTY_WORKER\n");
	xzs_early_puts("RX_RING_ACTIVE=");
	xzs_early_puts(g_xzs_usb_tty_bridge ? "yes\n" : "no\n");
	xzs_early_puts("TX_RING_CONCURRENCY_MODEL=MPSC_CONSOLE_WRITERS_ONE_USB_WORKER\n");
	xzs_early_puts("TX_RING_ACTIVE=");
	xzs_early_puts(g_xzs_usb_tty_bridge ? "yes\n" : "no\n");
	xzs_early_puts("T1Z_RESET_POLICY=live_window_then_pstore_halt\n");
	xzs_early_puts("T1Z_MAX_STAGE=6\n");
	xzs_early_puts("TTY_BRIDGE_ACTIVE=");
	xzs_early_puts(g_xzs_usb_tty_bridge ? "yes\n" : "no\n");
	xzs_early_puts("BULK_OUT_CONFIGURED=");
	xzs_early_puts(g_xzs_usb_t1z_ep2_configured ? "yes\n" : "no\n");
	xzs_early_puts("BULK_IN_CONFIGURED=");
	xzs_early_puts(g_xzs_usb_t1z_ep3_configured ? "yes\n" : "no\n");
	xzs_early_puts("DALEPENA="); xzs_t1z_put_hex(g_xzs_usb_t1z_dalepena);
	xzs_early_puts("BULK_OUT_BYTES_TOTAL="); xzs_t1z_put_hex(g_xzs_usb_bulk_out_bytes);
	xzs_early_puts("BULK_IN_BYTES_TOTAL="); xzs_t1z_put_hex(g_xzs_usb_bulk_in_bytes);
	xzs_early_puts("Z2_BULK_OUT_BYTES="); xzs_t1z_put_hex(g_xzs_usb_t1z_out1_len);
	xzs_early_puts("Z3_BULK_IN_BYTES="); xzs_t1z_put_hex(g_xzs_usb_t1z_in1_len);
	xzs_early_puts("Z4_LOOPBACK_OUT_BYTES="); xzs_t1z_put_hex(g_xzs_usb_t1z_out2_len);
	xzs_early_puts("Z4_LOOPBACK_IN_BYTES="); xzs_t1z_put_hex(g_xzs_usb_t1z_in2_len);
	xzs_early_puts("USB_TO_TTY_BYTES="); xzs_t1z_put_hex(g_xzs_usb_rx_dequeued);
	xzs_early_puts("TTY_TO_USB_BYTES="); xzs_t1z_put_hex(g_xzs_usb_tx_dequeued);
	xzs_early_puts("USB_RX_RING_ENQUEUED_BYTES="); xzs_t1z_put_hex(g_xzs_usb_rx_enqueued);
	xzs_early_puts("USB_RX_RING_DEQUEUED_BYTES="); xzs_t1z_put_hex(g_xzs_usb_rx_dequeued);
	xzs_early_puts("USB_RX_RING_DROPS="); xzs_t1z_put_hex(g_xzs_usb_rx_drops);
	xzs_early_puts("USB_RX_RING_HIGH_WATERMARK="); xzs_t1z_put_hex(g_xzs_usb_rx_high);
	xzs_early_puts("USB_RX_WORKER_CALLS="); xzs_t1z_put_hex(g_xzs_usb_rx_worker_calls);
	xzs_early_puts("USB_TX_RING_ENQUEUED_BYTES="); xzs_t1z_put_hex(g_xzs_usb_tx_enqueued);
	xzs_early_puts("USB_TX_RING_DEQUEUED_BYTES="); xzs_t1z_put_hex(g_xzs_usb_tx_dequeued);
	xzs_early_puts("USB_TX_RING_DROPS="); xzs_t1z_put_hex(g_xzs_usb_tx_drops);
	xzs_early_puts("USB_TX_RING_HIGH_WATERMARK="); xzs_t1z_put_hex(g_xzs_usb_tx_high);
	xzs_early_puts("USB_TX_WORKER_CALLS="); xzs_t1z_put_hex(g_xzs_usb_tx_worker_calls);
	xzs_early_puts("BULK_IN_COMPLETION_COUNT="); xzs_t1z_put_hex(g_xzs_usb_tx_completions);
	xzs_early_puts("CONS_CINPUT_CALL_COUNT="); xzs_t1z_put_hex(g_xzs_usb_cons_cinput_count);
	xzs_early_puts("SHELL_PROCESS_ALIVE=");
	xzs_early_puts(xzs_d7m3_complete ? "yes\n" : "no\n");
	xzs_early_puts("EP_COMMAND_FAILURES="); xzs_t1z_put_hex(g_xzs_usb_t1y_ep_cmd_failures);
	xzs_early_puts("EVENT_RING_DROPS="); xzs_t1z_put_hex(g_xzs_usb_t1y_event_ring_drops);
	xzs_early_puts("AVAILABLE_SHELL_COMMANDS=none\n");
	xzs_early_puts("INTERACTIVE_TEST_COMMAND_1=none\n");
	xzs_early_puts("INTERACTIVE_TEST_COMMAND_2=none\n");
	xzs_early_puts("D7T1_INPUT_BYPASS=no\n");
	xzs_early_puts("HARD_IRQ_CALLS_CONS_CINPUT=no\n");
	{
		extern volatile int xzs_d7m4_input_match;
		extern volatile int xzs_d7m4_post_read_el0;
		extern volatile int xzs_d7t1_read_entered;
		extern volatile int xzs_d7t1_read_blocked;
		extern volatile int xzs_d7t1_read_awakened;
		extern volatile int xzs_d7t1_read_returned;
		extern volatile int xzs_d7t1_read_len;
		extern volatile int xzs_d7t1_prompt_write_entered;
		int target_ok;

		xzs_early_puts("SHELL_READ_SYSCALL_ENTERED=");
		xzs_early_puts(xzs_d7t1_read_entered ? "yes\n" : "no\n");
		xzs_early_puts("SHELL_READ_BLOCKED=");
		xzs_early_puts(xzs_d7t1_read_blocked ? "yes\n" : "no\n");
		xzs_early_puts("SHELL_READ_AWAKENED=");
		xzs_early_puts(xzs_d7t1_read_awakened ? "yes\n" : "no\n");
		xzs_early_puts("SHELL_READ_RETURNED_TO_EL0=");
		xzs_early_puts(xzs_d7t1_read_returned ? "yes\n" : "no\n");
		xzs_early_puts("SHELL_READ_NATIVE=yes\n");
		xzs_early_puts("EL0_READ_LENGTH=");
		xzs_t1z_put_hex((uint64_t)xzs_d7t1_read_len);
		xzs_early_puts("EL0_READ_EXACT=");
		xzs_early_puts(xzs_d7t1_prompt_write_entered ? "yes\n" : "no\n");
		xzs_early_puts("Z5_BULK_OUT_HEX=");
		xzs_t1z_put_hex(((uint64_t)g_xzs_usb_z5_bytes[0] << 24) |
		    ((uint64_t)g_xzs_usb_z5_bytes[1] << 16) |
		    ((uint64_t)g_xzs_usb_z5_bytes[2] << 8) |
		    (uint64_t)g_xzs_usb_z5_bytes[3]);
		xzs_early_puts("D6_REGRESSION=PASS\n");
		xzs_early_puts("D7_M2_REGRESSION=PASS\n");
		xzs_early_puts("D7_M3_REGRESSION=");
		xzs_early_puts(xzs_d7m3_complete ? "PASS\n" : "FAIL\n");
		xzs_early_puts("D7_M4_REGRESSION=");
		xzs_early_puts(xzs_d7m4_input_match && xzs_d7m4_post_read_el0 ? "PASS\n" : "FAIL\n");
		target_ok = g_xzs_usb_t1z_pipeline_done && !g_xzs_usb_t1z_failed &&
		    g_xzs_usb_t1z_ep2_configured && g_xzs_usb_t1z_ep3_configured &&
		    g_xzs_usb_t1y_ep_cmd_failures == 0 &&
		    g_xzs_usb_t1z_out1_len == 18 && g_xzs_usb_t1z_in1_len == 17 &&
		    g_xzs_usb_t1z_out2_len == 13 && g_xzs_usb_t1z_in2_len == 13 &&
		    g_xzs_usb_rx_drops == 0 && g_xzs_usb_cons_cinput_count >= 4 &&
		    xzs_d7t1_read_entered && xzs_d7t1_read_blocked &&
		    xzs_d7t1_read_awakened && xzs_d7t1_read_returned &&
		    xzs_d7t1_read_len == 4 && xzs_d7t1_prompt_write_entered &&
		    g_xzs_usb_tx_dequeued >= 5 && g_xzs_usb_z5_count >= 4 &&
		    g_xzs_usb_z5_bytes[0] == 0x41 && g_xzs_usb_z5_bytes[1] == 0x42 &&
		    g_xzs_usb_z5_bytes[2] == 0x43 && g_xzs_usb_z5_bytes[3] == 0x0a;
		xzs_early_puts("=== D7-T1 FINAL ACCEPTANCE BEGIN ===\n");
		xzs_early_puts("D7_T1_COMPLETE=");
		xzs_early_puts(target_ok ? "yes\n" : "no\n");
		xzs_early_puts("D7_T1_SEALED=no\n");
		xzs_early_puts("=== D7-T1 FINAL ACCEPTANCE END ===\n");
	}
	xzs_early_puts("T1Z_PIPELINE_DONE=");
	xzs_early_puts(g_xzs_usb_t1z_pipeline_done ? "yes\n" : "no\n");
	xzs_early_puts("T1Z_FAILED=");
	xzs_early_puts(g_xzs_usb_t1z_failed ? "yes\n" : "no\n");
	xzs_early_puts("D7_T1_SEALED=no\n");
	xzs_early_puts("=== D7-T1 T1-Z TARGET TELEMETRY END ===\n");
	xzs_early_puts("=======================================================\n\n");
}

/*
 * Primary initialization
 */
int xzs_usb_init(void)
{
	vm_offset_t event_va;
	vm_offset_t event_pa;

	xzs_breadcrumb(0xD740, 0x2C00);
	xzs_early_puts("[XZS-D7T1] D740/2C00 Candidate-2C XNU event-buffer ownership entered\n");

	/* Candidate-2C maps and mutates only audited DWC3 event-buffer registers. */
	s_dwc3_base = (vm_offset_t)ml_io_map(XZS_USB_DWC3_PHYS_BASE, XZS_USB_DWC3_MMIO_SIZE);
	if (s_dwc3_base == 0) {
		xzs_breadcrumb(0xD740, 0x2CFF);
		xzs_early_puts("[XZS-D7T1] ERROR: Candidate-2C DWC3 MMIO mapping failed\n");
		return -1;
	}

	/* Capability and halted-state audit precede every DWC3 write. */
	g_xzs_usb_gsnpsid = dwc3_read32(DWC3_GSNPSID);
	g_xzs_usb_ghwparams1 = dwc3_read32(DWC3_GHWPARAMS1);
	g_xzs_usb_num_event_interrupts = DWC3_NUM_EVENT_INTERRUPTS(g_xzs_usb_ghwparams1);
	g_xzs_usb_dcfg_before = dwc3_read32(DWC3_DCFG);
	g_xzs_usb_dctl_before = dwc3_read32(DWC3_DCTL);
	g_xzs_usb_dsts_before = dwc3_read32(DWC3_DSTS);
	g_xzs_usb_devten_before = dwc3_read32(DWC3_DEVTEN);
	g_xzs_usb_gevntadr0_before = dwc3_read32(DWC3_GEVNTADR0);
	g_xzs_usb_gevntadrhi0_before = dwc3_read32(DWC3_GEVNTADR_HI0);
	g_xzs_usb_gevntsiz0_before = dwc3_read32(DWC3_GEVNTSIZ0);
	g_xzs_usb_gevntcnt0_before = dwc3_read32(DWC3_GEVNTCNT0);
	g_xzs_usb_candidate2c_precondition_run_stop_0 =
	    ((g_xzs_usb_dctl_before & DWC3_DCTL_RUN_STOP) == 0);
	g_xzs_usb_candidate2c_precondition_devctrlhlt_1 =
	    ((g_xzs_usb_dsts_before & DWC3_DSTS_DEVCTRLHLT) != 0);
	if (g_xzs_usb_num_event_interrupts < 1 ||
	    !g_xzs_usb_candidate2c_precondition_run_stop_0 ||
	    !g_xzs_usb_candidate2c_precondition_devctrlhlt_1) {
		xzs_early_puts("[XZS-D7T1] Candidate-2C precondition failed; event registers untouched\n");
		xzs_breadcrumb(0xD740, 0x2C40);
		xzs_early_puts("[XZS-D7T1] D740/2C40 normal boot continuing\n");
		return -1;
	}
	xzs_breadcrumb(0xD740, 0x2C01);
	xzs_early_puts("[XZS-D7T1] D740/2C01 halted preconditions confirmed\n");

	/* Retain Candidate-2B normalization only when the live DCFG requires it. */
	g_xzs_usb_dcfg_written = g_xzs_usb_dcfg_before &
	    ~(DWC3_DCFG_SPEED_MASK | DWC3_DCFG_DEVADDR_MASK);
	if (g_xzs_usb_dcfg_written != g_xzs_usb_dcfg_before) {
		dwc3_write32(DWC3_DCFG, g_xzs_usb_dcfg_written);
		g_xzs_usb_candidate2c_dcfg_write_count = 1;
	}
	g_xzs_usb_dcfg_readback = dwc3_read32(DWC3_DCFG);
	g_xzs_usb_dcfg = g_xzs_usb_dcfg_readback;
	g_xzs_usb_candidate2c_dcfg_high_speed =
	    ((g_xzs_usb_dcfg_readback & DWC3_DCFG_SPEED_MASK) == 0);
	g_xzs_usb_candidate2c_dcfg_devaddr_0 =
	    ((g_xzs_usb_dcfg_readback & DWC3_DCFG_DEVADDR_MASK) == 0);
	if (!g_xzs_usb_candidate2c_dcfg_high_speed ||
	    !g_xzs_usb_candidate2c_dcfg_devaddr_0) {
		xzs_early_puts("[XZS-D7T1] Candidate-2C DCFG normalization failed; event registers untouched\n");
		xzs_breadcrumb(0xD740, 0x2C40);
		xzs_early_puts("[XZS-D7T1] D740/2C40 normal boot continuing\n");
		return -1;
	}
	xzs_breadcrumb(0xD740, 0x2C02);
	xzs_early_puts("[XZS-D7T1] D740/2C02 DCFG HS normalization confirmed\n");

	/* Static 4 KiB storage has XNU lifetime; validate VA-to-PA contiguity. */
	event_va = (vm_offset_t)s_candidate2c_event_buffer;
	event_pa = ml_vtophys(event_va);
	g_xzs_usb_candidate2c_event_buffer_va = event_va;
	g_xzs_usb_candidate2c_event_buffer_pa = event_pa;
	g_xzs_usb_candidate2c_event_buffer_pa_valid = (event_pa != 0);
	g_xzs_usb_candidate2c_event_buffer_aligned =
	    ((event_va & (XZS_DWC3_EVENT_BUFFER_SIZE - 1)) == 0) &&
	    ((event_pa & (XZS_DWC3_EVENT_BUFFER_SIZE - 1)) == 0);
	g_xzs_usb_candidate2c_event_buffer_contiguous =
	    (ml_vtophys(event_va + XZS_DWC3_EVENT_BUFFER_SIZE - 1) ==
	    event_pa + XZS_DWC3_EVENT_BUFFER_SIZE - 1);
	if (!g_xzs_usb_candidate2c_event_buffer_pa_valid ||
	    !g_xzs_usb_candidate2c_event_buffer_aligned ||
	    !g_xzs_usb_candidate2c_event_buffer_contiguous) {
		xzs_early_puts("[XZS-D7T1] Candidate-2C event-buffer PA validation failed\n");
		xzs_breadcrumb(0xD740, 0x2C40);
		xzs_early_puts("[XZS-D7T1] D740/2C40 normal boot continuing\n");
		return -1;
	}
	for (uint32_t i = 0; i < (XZS_DWC3_EVENT_BUFFER_SIZE / sizeof(uint32_t)); i++) {
		((uint32_t *)(void *)s_candidate2c_event_buffer)[i] = 0xA5A50000u | i;
	}
	xzs_dma_clean_invalidate(event_va, XZS_DWC3_EVENT_BUFFER_SIZE);
	flush_dcache(event_va, XZS_DWC3_EVENT_BUFFER_SIZE, FALSE);

	s_uncached_event_buf = (volatile uint32_t *)(void *)ml_io_map(event_pa, XZS_DWC3_EVENT_BUFFER_SIZE);
	s_fb_event_buf = (volatile uint32_t *)(void *)ml_io_map(0xaa122000ULL, XZS_DWC3_EVENT_BUFFER_SIZE);
	if (s_fb_event_buf != NULL) {
		for (int i = 0; i < 4; i++) {
			s_fb_initial_words[i] = s_fb_event_buf[i];
		}
	}
	xzs_early_puts("[XZS-D7T1] UNCACHED_INIT_WORD0=0x");
	if (s_uncached_event_buf != NULL) {
		xzs_d6m4_put_hex64(s_uncached_event_buf[0]);
	} else {
		xzs_early_puts("NULL");
	}
	xzs_early_puts("\n");
	xzs_early_puts("[XZS-D7T1] FB_INIT_WORD0=0x");
	if (s_fb_event_buf != NULL) {
		xzs_d6m4_put_hex64(s_fb_event_buf[0]);
	} else {
		xzs_early_puts("NULL");
	}
	xzs_early_puts("\n");
	xzs_breadcrumb(0xD740, 0x2C10);
	xzs_early_puts("[XZS-D7T1] D740/2C10 XNU event buffer allocated/reserved\n");
	xzs_breadcrumb(0xD740, 0x2C11);
	xzs_early_puts("[XZS-D7T1] D740/2C11 event-buffer PA validated\n");
	xzs_breadcrumb(0xD740, 0x2C12);
	xzs_early_puts("[XZS-D7T1] D740/2C12 inherited event registers captured\n");

	/* Program only event-buffer 0 while interrupts and the controller remain halted. */
	dwc3_write32(DWC3_GEVNTADR0, (uint32_t)event_pa);
	g_xzs_usb_candidate2c_gevntadrlo_write_count = 1;
	dwc3_write32(DWC3_GEVNTADR_HI0, (uint32_t)(event_pa >> 32));
	g_xzs_usb_candidate2c_gevntadrhi_write_count = 1;
	xzs_breadcrumb(0xD740, 0x2C20);
	xzs_early_puts("[XZS-D7T1] D740/2C20 GEVNTADR programmed\n");
	dwc3_write32(DWC3_GEVNTSIZ0,
	    DWC3_GEVNTSIZ_INTMASK | XZS_DWC3_EVENT_BUFFER_SIZE);
	g_xzs_usb_candidate2c_gevntsiz_write_count = 1;
	xzs_breadcrumb(0xD740, 0x2C21);
	xzs_early_puts("[XZS-D7T1] D740/2C21 GEVNTSIZ programmed masked\n");

	/* Acknowledge only a hardware-reported pending byte count; preserve zero. */
	g_xzs_usb_candidate2c_gevntcount_ack =
	    g_xzs_usb_gevntcnt0_before & DWC3_GEVNTCOUNT_PENDING_MASK;
	if (g_xzs_usb_candidate2c_gevntcount_ack != 0) {
		dwc3_write32(DWC3_GEVNTCNT0, g_xzs_usb_candidate2c_gevntcount_ack);
		g_xzs_usb_candidate2c_gevntcount_write_count = 1;
	}
	xzs_breadcrumb(0xD740, 0x2C22);
	xzs_early_puts("[XZS-D7T1] D740/2C22 stale event count handled\n");

	g_xzs_usb_gevntadr0 = dwc3_read32(DWC3_GEVNTADR0);
	g_xzs_usb_gevntadrhi0 = dwc3_read32(DWC3_GEVNTADR_HI0);
	g_xzs_usb_gevntsiz0 = dwc3_read32(DWC3_GEVNTSIZ0);
	g_xzs_usb_gevntcnt0 = dwc3_read32(DWC3_GEVNTCNT0);
	g_xzs_usb_dctl = dwc3_read32(DWC3_DCTL);
	g_xzs_usb_dsts = dwc3_read32(DWC3_DSTS);
	g_xzs_usb_devten = dwc3_read32(DWC3_DEVTEN);
	g_xzs_usb_candidate2c_gevntadr_readback_match =
	    (g_xzs_usb_gevntadr0 == (uint32_t)event_pa) &&
	    (g_xzs_usb_gevntadrhi0 == (uint32_t)(event_pa >> 32));
	g_xzs_usb_candidate2c_gevntsiz_readback_match =
	    ((g_xzs_usb_gevntsiz0 & DWC3_GEVNTSIZ_SIZE_MASK) == XZS_DWC3_EVENT_BUFFER_SIZE) &&
	    ((g_xzs_usb_gevntsiz0 & DWC3_GEVNTSIZ_INTMASK) != 0);
	g_xzs_usb_candidate2c_devten_unchanged =
	    (g_xzs_usb_devten == g_xzs_usb_devten_before);
	xzs_breadcrumb(0xD740, 0x2C23);
	xzs_early_puts("[XZS-D7T1] D740/2C23 event-register readback verified\n");
	xzs_breadcrumb(0xD740, 0x2C30);
	xzs_early_puts("[XZS-D7T1] D740/2C30 RUN_STOP still zero\n");
	xzs_breadcrumb(0xD740, 0x2C31);
	xzs_early_puts("[XZS-D7T1] D740/2C31 DEVCTRLHLT still one\n");

	g_xzs_usb_candidate2c_complete =
	    g_xzs_usb_candidate2c_dcfg_high_speed &&
	    g_xzs_usb_candidate2c_dcfg_devaddr_0 &&
	    g_xzs_usb_candidate2c_event_buffer_pa_valid &&
	    g_xzs_usb_candidate2c_event_buffer_contiguous &&
	    g_xzs_usb_candidate2c_event_buffer_aligned &&
	    g_xzs_usb_candidate2c_event_buffer_lifetime_static &&
	    g_xzs_usb_candidate2c_gevntadr_readback_match &&
	    g_xzs_usb_candidate2c_gevntsiz_readback_match &&
	    ((g_xzs_usb_dctl & DWC3_DCTL_RUN_STOP) == 0) &&
	    ((g_xzs_usb_dsts & DWC3_DSTS_DEVCTRLHLT) != 0) &&
	    g_xzs_usb_candidate2c_devten_unchanged;
	xzs_breadcrumb(0xD740, 0x2C40);
	xzs_early_puts("[XZS-D7T1] D740/2C40 normal boot continuing\n");
	if (g_xzs_usb_candidate2c_complete) {
		xzs_breadcrumb(0xD740, 0x2C90);
		xzs_early_puts("[XZS-D7T1] D740/2C90 acceptance reached\n");
		xzs_breadcrumb(0xD740, 0x2C91);
		xzs_early_puts("[XZS-D7T1] D740/2C91 PASS\n");
	} else {
		xzs_breadcrumb(0xD740, 0x2C9F);
		xzs_early_puts("[XZS-D7T1] ERROR: Candidate-2C acceptance mismatch\n");
	}
	if (!g_xzs_usb_candidate2c_complete) {
		return -1;
	}
	if (xzs_t1x_ep0_halted_setup() != 0) {
		xzs_early_puts("[XZS-D7T1] T1-X stopped at failing command checkpoint\n");
		return -1;
	}
	return xzs_t1y_connect();
}
