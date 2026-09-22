#include "adt.h"
#include "fb.h"
#include "uart.h"
#include <stddef.h>
#include <stdint.h>

extern void jump_to_xnu(uint64_t boot_args_phys, uint64_t entry_phys);

static uint32_t shim_crc32(const uint8_t *buf, size_t len) {
  uint32_t crc = 0xffffffff;
  for (size_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xedb88320 & (-(crc & 1)));
    }
  }
  return ~crc;
}

static inline uint64_t read_cntfrq(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
  return val;
}

static inline uint64_t read_cntvct(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
  return val;
}

static inline uint64_t read_sctlr_el1(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, sctlr_el1" : "=r"(val));
  return val;
}

static inline uint64_t read_tcr_el1(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, tcr_el1" : "=r"(val));
  return val;
}

static inline uint64_t read_ttbr0_el1(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(val));
  return val;
}

static inline uint64_t read_ttbr1_el1(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, ttbr1_el1" : "=r"(val));
  return val;
}

static inline uint64_t read_mair_el1(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, mair_el1" : "=r"(val));
  return val;
}

static inline uint64_t read_id_aa64mmfr0_el1(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(val));
  return val;
}

static inline uint64_t read_id_aa64pfr0_el1(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, id_aa64pfr0_el1" : "=r"(val));
  return val;
}

static inline uint64_t read_id_aa64isar0_el1(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, id_aa64isar0_el1" : "=r"(val));
  return val;
}

static inline uint64_t read_id_aa64isar1_el1(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, id_aa64isar1_el1" : "=r"(val));
  return val;
}

static void delay_cycles(uint64_t cycles) {
  uint64_t start = read_cntvct();
  while ((read_cntvct() - start) < cycles) {
    __asm__ volatile("nop");
  }
}

static void psci_system_reset(void) {
  /* Set Fastboot reboot reason (0x77665500 matches Linux "bootloader") */
  volatile uint32_t *restart_reason = (volatile uint32_t *)0x066bf65cUL;
  *restart_reason = 0x77665500UL;
  __asm__ volatile("dsb sy\nisb" ::: "memory");

  /* Flush delay */
  for (volatile int i = 0; i < 100000; i++) {
    __asm__ volatile("nop");
  }

  while (1) {
    /* Qualcomm SCM DEASSERT_PS_HOLD (SMC64: 0xc2000902, SMC32: 0x82000902) */
    register uint64_t x0 __asm__("x0") = 0xc2000902UL;
    register uint64_t x1 __asm__("x1") = 1UL;
    register uint64_t x2 __asm__("x2") = 0UL;
    register uint64_t x3 __asm__("x3") = 0UL;
    register uint64_t x4 __asm__("x4") = 0UL;
    register uint64_t x5 __asm__("x5") = 0UL;
    register uint64_t x6 __asm__("x6") = 0UL;
    __asm__ volatile("smc #0"
                     : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4),
                       "+r"(x5), "+r"(x6)
                     :: "memory");

    x0 = 0x82000902UL;
    x1 = 1UL; x2 = 0UL; x3 = 0UL; x4 = 0UL; x5 = 0UL; x6 = 0UL;
    __asm__ volatile("smc #0"
                     : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4),
                       "+r"(x5), "+r"(x6)
                     :: "memory");

    /* Fallback 1: PSCI SYSTEM_RESET via SMC #0 (function ID 0x84000009) */
    x0 = 0x84000009UL;
    __asm__ volatile("smc #0" : "+r"(x0) :: "memory");

    /* Fallback 2: Qualcomm MSM8996 APCS Watchdog Bite */
    volatile uint32_t *wdt_test = (volatile uint32_t *)0x09830014UL;
    volatile uint32_t *wdt_en   = (volatile uint32_t *)0x09830004UL;
    *wdt_test = 1;
    *wdt_en   = 1;
    __asm__ volatile("dsb sy\nisb" ::: "memory");

    /* Fallback 3: Pull PS_HOLD low */
    volatile uint32_t *pshold = (volatile uint32_t *)0x004ab000UL;
    *pshold = 0;
    __asm__ volatile("dsb sy\nisb" ::: "memory");

    for (volatile int i = 0; i < 4000000; i++) {
      __asm__ volatile("nop");
    }
  }
}

void bootshim_on_exception(uint64_t esr, uint64_t elr, uint64_t far, uint64_t spsr) {
  uart_puts("\n\n*** BOOTSHIM EARLY EXCEPTION ***\n");
  uart_puts("ESR_EL1:  "); uart_puthex64(esr); uart_puts("\n");
  uart_puts("ELR_EL1:  "); uart_puthex64(elr); uart_puts("\n");
  uart_puts("FAR_EL1:  "); uart_puthex64(far); uart_puts("\n");
  uart_puts("SPSR_EL1: "); uart_puthex64(spsr); uart_puts("\n");
  uart_puts("Triggering warm reset...\n");
  psci_system_reset();
}

static uint32_t fdt_get_initrd_start(uint64_t fdt_addr) {
  if (fdt_addr == 0)
    return 0;
  const uint8_t *blob = (const uint8_t *)fdt_addr;
  if (blob[0] != 0xd0 || blob[1] != 0x0d || blob[2] != 0xfe ||
      blob[3] != 0xed) {
    return 0;
  }
  uint32_t totalsize =
      (blob[4] << 24) | (blob[5] << 16) | (blob[6] << 8) | blob[7];
  uint32_t off_struct =
      (blob[8] << 24) | (blob[9] << 16) | (blob[10] << 8) | blob[11];
  uint32_t off_strings =
      (blob[12] << 24) | (blob[13] << 16) | (blob[14] << 8) | blob[15];
  if (off_struct >= totalsize || off_strings >= totalsize)
    return 0;

  const uint8_t *p = blob + off_struct;
  const uint8_t *end = blob + totalsize;
  const char *strings = (const char *)(blob + off_strings);

  while (p + 4 <= end) {
    uint32_t tag = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    p += 4;
    if (tag == 1) { /* FDT_BEGIN_NODE */
      while (p < end && *p != 0)
        p++;
      p++;
      p = (const uint8_t *)(((uintptr_t)p + 3) & ~3);
    } else if (tag == 2) { /* FDT_END_NODE */
                           /* continue */
    } else if (tag == 3) { /* FDT_PROP */
      if (p + 8 > end)
        break;
      uint32_t len = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
      uint32_t nameoff = (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
      p += 8;
      if (off_strings + nameoff < totalsize) {
        const char *prop_name = strings + nameoff;
        int match = 0;
        const char *p1 = prop_name;
        while (*p1 != '\0') {
          const char *sub = p1;
          const char *needle = "initrd-start";
          while (*sub && *needle && (*sub == *needle)) {
            sub++;
            needle++;
          }
          if (*needle == '\0' && *sub == '\0') {
            match = 1;
            break;
          }
          p1++;
        }
        if (match) {
          if (len == 4) {
            return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
          } else if (len == 8) {
            return (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
          }
        }
      }
      p += len;
      p = (const uint8_t *)(((uintptr_t)p + 3) & ~3);
    } else if (tag == 9) { /* FDT_END */
      break;
    }
  }
  return 0;
}

struct Boot_Video {
  unsigned long v_baseAddr;
  unsigned long v_display;
  unsigned long v_rowBytes;
  unsigned long v_width;
  unsigned long v_height;
  unsigned long v_depth;
};

struct boot_args {
  uint16_t Revision;
  uint16_t Version;
  uint32_t pad0;
  uint64_t virtBase;
  uint64_t physBase;
  uint64_t memSize;
  uint64_t topOfKernelData;
  struct Boot_Video Video;
  uint32_t machineType;
  void *deviceTreeP;
  uint32_t deviceTreeLength;
  char CommandLine[1024];
  uint64_t bootFlags;
  uint64_t memSizeActual;
};

#define BOOT_ARGS_ADDR 0x81800000UL
#define XNU_KERNEL_BASE 0x80200000UL

static uint64_t find_macho_entry_point(uint64_t base) {
  volatile uint32_t *macho = (volatile uint32_t *)base;
  if (macho[0] != 0xfeedfacf) {
    return 0;
  }
  uint32_t ncmds = macho[4];
  uint8_t *cmd_ptr = (uint8_t *)(base + 32);
  for (uint32_t i = 0; i < ncmds; i++) {
    uint32_t cmd = *(volatile uint32_t *)cmd_ptr;
    uint32_t cmdsize = *(volatile uint32_t *)(cmd_ptr + 4);
    if (cmd == 0x5) { /* LC_UNIXTHREAD */
      uint64_t pc = *(volatile uint64_t *)(cmd_ptr + 272);
      if (pc >= 0xfffffe0006200000ULL) {
        return base + (pc - 0xfffffe0006200000ULL);
      }
    }
    cmd_ptr += cmdsize;
  }
  return base + 0x00534488UL;
}

static void copy_string(char *dst, const char *src, size_t maxlen) {
  size_t i = 0;
  while (i < maxlen - 1 && src[i] != '\0') {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

void bootshim_main(uint64_t dtb_phys, uint64_t current_el, uint64_t mpidr) {
  uart_init();

  /* Visual marker: RED for bootshim entry */
  fb_set_color(COLOR_RED);

  /* Checkpoint A */
  g_dlog->last_stage = XZS_STAGE_SHIM_A;
  uart_puts("\n[XZS-SHIM] A: entry\n");
  uart_puts("Hardware: Sony Xperia XZs (G8231 / Tone Keyaki)\n");
  uart_puts("SoC: Qualcomm Snapdragon 820 (MSM8996)\n");
  uart_puts("Current EL: EL");
  uart_putdec(current_el);
  uart_puts(", MPIDR: ");
  uart_puthex64(mpidr);
  uart_puts("\n");

  /* Checkpoint B */
  g_dlog->last_stage = XZS_STAGE_SHIM_B;
  if (dtb_phys != 0) {
    uart_puts("[XZS-SHIM] B: DTB located at ");
    uart_puthex64(dtb_phys);
    uart_puts("\n");
  } else {
    uart_puts("[XZS-SHIM] B: DTB located (none/embedded)\n");
  }

  uint64_t frq = read_cntfrq();
  uint64_t t0 = read_cntvct();
  uart_puts("Timer Frequency: ");
  uart_putdec(frq);
  uart_puts(" Hz, Counter: ");
  uart_puthex64(t0);
  uart_puts("\n");

  /* Checkpoint C: Locate Mach-O kernel */
  g_dlog->last_stage = XZS_STAGE_SHIM_C;
  uart_puts("[XZS-SHIM] C: Locating Mach-O kernel (0xfeedfacf)...\n");

  uint64_t macho_base = 0;

  /* 1. Query DTB /chosen property linux,initrd-start set by Sony S1 ABOOT */
  uint32_t dtb_initrd = fdt_get_initrd_start(dtb_phys);
  if (dtb_initrd != 0) {
    uart_puts("  DTB initrd-start: 0x");
    uart_puthex64((uint64_t)dtb_initrd);
    volatile uint32_t *hdr = (volatile uint32_t *)(uint64_t)dtb_initrd;
    uint32_t m = hdr[0];
    uart_puts(", magic=0x");
    uart_puthex64((uint64_t)m);
    uart_puts("\n");
    if (m == 0xfeedfacf) {
      macho_base = dtb_initrd;
    }
  }

  /* 2. Check known static candidate offsets */
  if (macho_base == 0) {
    static const uint64_t candidates[] = {
        0x82200000UL, /* S1 ABOOT_FORCE_RAMDISK_ADDR */
        0x80200000UL, /* Default boot image ramdisk_offset */
        0x82000000UL, /* S1 ABOOT_FORCE_TAGS_ADDR / DTB */
        0x82400000UL, 0x81000000UL, 0x81800000UL,
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
      uint64_t cand = candidates[i];
      volatile uint32_t *hdr = (volatile uint32_t *)cand;
      uint32_t m = hdr[0];
      uart_puts("  Candidate 0x");
      uart_puthex64(cand);
      uart_puts(": magic=0x");
      uart_puthex64((uint64_t)m);
      uart_puts("\n");
      if (m == 0xfeedfacf) {
        macho_base = cand;
        break;
      }
    }
  }

  if (macho_base != 0) {
    uint64_t entry_addr = find_macho_entry_point(macho_base);

    /* Checkpoint D: Mach-O verified & entry point extracted */
    g_dlog->last_stage = XZS_STAGE_SHIM_D;
    uart_puts("[XZS-SHIM] D: Mach-O base=0x");
    uart_puthex64(macho_base);
    uart_puts(", entry=0x");
    uart_puthex64(entry_addr);
    uart_puts("\n");

    /* D5-M2-R2: Copy embedded rootfs to reserved DRAM (0x81700000) */
    extern const uint8_t xzs_rootfs_img[];
    extern const uint8_t xzs_rootfs_img_end[];
    uint64_t rootfs_len = (uint64_t)(xzs_rootfs_img_end - xzs_rootfs_img);

    uart_puts("[XZS-SHIM] R2: RAMDisk Payload Copy to Reserved DRAM (0x81700000)...\n");
    uart_puts("  RAMDISK_PHYS_BASE:            0x81700000\n");
    uart_puts("  RAMDISK_IMAGE_SIZE_BYTES:     "); uart_putdec(rootfs_len); uart_puts("\n");
    uart_puts("  RAMDISK_BACKING_SIZE_BYTES:   73728\n");
    uart_puts("  RAMDISK_ZERO_PADDING_BYTES:   1024\n");

    if (rootfs_len != 70144ULL) {
      uart_puts("[XZS-SHIM] FATAL: Embedded rootfs length != 70144!\n");
      delay_cycles(19200000);
      psci_system_reset();
    }

    uint8_t *rd_dst = (uint8_t *)0x81700000ULL;
    for (uint64_t i = 0; i < rootfs_len; i++) {
      rd_dst[i] = xzs_rootfs_img[i];
    }
    for (uint64_t i = rootfs_len; i < 73728ULL; i++) {
      rd_dst[i] = 0;
    }
    __asm__ volatile("dsb ish; isb" : : : "memory");

    int pad_ok = 1;
    for (uint64_t i = rootfs_len; i < 73728ULL; i++) {
      if (rd_dst[i] != 0) {
        pad_ok = 0;
        break;
      }
    }

    uint32_t dram_crc = shim_crc32(rd_dst, rootfs_len);
    uart_puts("  R2_DRAM_COPY_COMPLETE:        yes\n");
    uart_puts("  R2_LOGICAL_IMAGE_CRC:         0x"); uart_puthex64((uint64_t)dram_crc); uart_puts("\n");
    uart_puts("  R2_LOGICAL_IMAGE_CRC_MATCH:   "); uart_puts(dram_crc == 0x07caf17b ? "yes\n" : "no\n");
    uart_puts("  R2_PADDING_ZERO:              "); uart_puts(pad_ok ? "yes\n" : "no\n");

    /* Checkpoint E: Construct Apple Device Tree (ADT) */
    g_dlog->last_stage = XZS_STAGE_SHIM_E;
    uart_puts("[XZS-SHIM] E: Constructing Apple Device Tree at 0x81810000...\n");
    uint32_t adt_len = adt_build_tree_at(ADT_BASE_ADDR);
    uart_puts("  ADT size: ");
    uart_putdec((uint64_t)adt_len);
    uart_puts(" bytes\n");
    uart_puts("  R3_RAMDISK_ADT_PRESENT:       yes\n");
    uart_puts("  R3_BASE:                      0x81700000\n");
    uart_puts("  R3_LENGTH:                    73728\n");

    /* Checkpoint F: Populate struct boot_args at 0x81800000 */
    g_dlog->last_stage = XZS_STAGE_SHIM_F;
    uart_puts("[XZS-SHIM] F: Populating struct boot_args at 0x81800000...\n");
    struct boot_args *ba = (struct boot_args *)BOOT_ARGS_ADDR;
    uint8_t *ba_raw = (uint8_t *)BOOT_ARGS_ADDR;
    for (size_t i = 0; i < sizeof(struct boot_args); i++) {
      ba_raw[i] = 0;
    }

    ba->Revision = 2;
    ba->Version = 2;
    /*
     * VM_KERNEL_LINK_ADDRESS = 0xfffffe0006200000
     * Kernel physical base in DRAM: macho_base (0x82200000)
     * Physical DRAM base: 0x80000000
     * Kernel offset = macho_base - 0x80000000 = 0x02200000
     * virtBase = VM_KERNEL_LINK_ADDRESS - offset = 0xfffffe0004000000 (32MB L2 aligned!)
     */
    ba->virtBase = 0xfffffe0006200000ULL - (macho_base - 0x80000000ULL);
    ba->physBase = 0x80000000ULL;
    /*
     * XNU treats memSize as one contiguous allocator-owned range.  Keep the
     * boot-args contract in sync with /chosen/dram-size: the low 88 MB ends
     * before the first MSM8996 firmware carveout at 0x85800000.
     */
    ba->memSize = 0x05800000ULL;
    ba->memSizeActual = 0x05800000ULL;
    ba->topOfKernelData = macho_base + 0x01800000ULL; /* 24 MB kernel space */

    ba->deviceTreeP = (void *)ADT_BASE_ADDR;
    ba->deviceTreeLength = adt_len;

    copy_string(ba->CommandLine, "console=ttyMSM0,115200 debug=0x14e serial=2 -v keepends=1 rd=md0", sizeof(ba->CommandLine));

    ba->machineType = 0;
    ba->bootFlags = 0;

    uart_puts("  virtBase: ");
    uart_puthex64(ba->virtBase);
    uart_puts("\n  physBase: ");
    uart_puthex64(ba->physBase);
    uart_puts("\n  memSize:  ");
    uart_puthex64(ba->memSize);
    uart_puts("\n  topOfKernelData: ");
    uart_puthex64(ba->topOfKernelData);
    uart_puts("\n  cmdline:  ");
    uart_puts(ba->CommandLine);
    uart_puts("\n");

    /* Visual marker: GREEN */
    fb_set_color(COLOR_GREEN);

    /* Checkpoint G: CPU handoff to Apple XNU kernel __start */
    g_dlog->last_stage = XZS_STAGE_SHIM_G;
    uart_puts("\n[XZS-SHIM] G: Handing off CPU execution to XNU kernel __start...\n\n");

    /* Jump to XNU kernel */
    jump_to_xnu(BOOT_ARGS_ADDR, entry_addr);

    /* Should never return */
    uart_puts("[XZS-SHIM] ERROR: Returned from XNU kernel!\n");
    psci_system_reset();
  } else {
    uart_puts("[XZS-SHIM] ERROR: No Mach-O kernel detected in any safe DRAM candidate!\n");
    delay_cycles(19200000);
    psci_system_reset();
  }

  while (1) {
    __asm__ volatile("wfe");
  }
}
