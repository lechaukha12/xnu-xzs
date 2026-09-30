# Sony Xperia XZs (MSM8996) — Technical Debt & Architectural Backlog

This document explicitly catalogs temporary bring-up compromises, architectural workarounds, and known technical debt in the XNU Xperia XZs port. These items must be systematically resolved as the port matures toward full production userspace.

---

## 1. Active Technical Debt Inventory

### DEBT-01: TTBR0 Identity Mapping Preservation (`invalid_tte[0] = boot_tte[0]`)
* **Location**: `osfmk/arm64/arm_vm_init.c`
* **Nature**: `arm_vm_init()` copies the initial identity page table entry from `boot_tte[0]` into `invalid_tte[0]`.
* **Rationale**: During early CPU bringup, secondary cores require a physical identity map (TTBR0) to transition from physical addresses to High KVA (`0xfffffe0000000000`) before MMU enablement.
* **Risk / Impact**: Leaves the lowest 1GB physical memory window accessible in user virtual space if TTBR0 is active.
* **Remediation Plan**: Construct a dedicated trampoline page table for secondary CPU boot, switch to `cpu_tte` immediately after MMU enablement, and unmap identity mappings entirely once all cores are online.

---

### DEBT-02: 88MB Safe Low-DRAM Allocation Constraint
* **Location**: `src/xzs-bootshim/adt.c`, `osfmk/arm/pmap/pmap.c`, `osfmk/vm/vm_kern.c`
* **Nature**: The bootshim advertises `0x05800000` (88 MB) of DRAM starting at `0x80000000` to XNU.
* **Rationale**: Qualcomm modem, TrustZone (QSEE), camera, and GPU carveouts consume specific physical regions between `0x85800000` and `0xa7f00000`. Limiting XNU to the contiguous 88MB safe low-DRAM region guaranteed zero memory collisions during early bring-up.
* **Risk / Impact**: Insufficient physical memory to run full multi-process BSD userspace and heavy services.
* **Remediation Plan**: Audit Qualcomm DTS memory carveouts, parse the complete 4GB LPDDR4 physical memory map in `adt.c`, and pass all non-reserved memory banks to XNU as disjoint memory regions.

---

### DEBT-03: Timer-Seeded Software PRNG (`xzs_prng`)
* **Location**: `osfmk/prng/prng_random.c`
* **Nature**: Cryptographic entropy relies on a software pseudo-random generator seeded by the physical timer counter (`CNTPCT_EL0`).
* **Rationale**: Apple silicon hardware entropy (`coreentropy`) and Secure Enclave RNG are absent on Qualcomm hardware.
* **Risk / Impact**: Predictable entropy if timer counters are sampled at fixed intervals; insufficient for high-security cryptographic operations.
* **Remediation Plan**: Implement a native driver for Qualcomm MSM8996 Hardware True Random Number Generator (TRNG) located in the Crypto Core (CE) engine.

---

### DEBT-04: Headless Platform Expert Rootfs Bypass
* **Location**: `iokit/bsddev/IOKitBSDInit.cpp`
* **Nature**: Bypasses Apple ACPI/IOPlatformExpert device tree matching when selecting the boot device.
* **Rationale**: Apple XNU expects an `AppleARMPE` device node providing the root filesystem device handle.
* **Risk / Impact**: Prevents dynamic root device selection via standard macOS `boot-args` (e.g. `rd=disk0s2`).
* **Remediation Plan**: Construct a compliant `AppleARMPERoot` IOKit nub in the ADT or implement a dedicated `XperiaXZsPlatformExpert` driver.

---

### DEBT-05: ARMv8.0-A Pointer Authentication (PAC) Disable
* **Location**: `makedefs/MakeInc.def`, `osfmk/arm/commpage/commpage_asm.s`, `osfmk/arm64/locore.s`
* **Nature**: The kernel is built with `-mno-ptrauth` and all PAC instructions (`pacia`, `autia`, `braa`, etc.) are stripped or bypassed.
* **Rationale**: Qualcomm Kryo (Snapdragon 820) implements ARMv8.0-A, which lacks ARMv8.3-A Pointer Authentication.
* **Risk / Impact**: None for functionality; standard for ARMv8.0-A platforms.
* **Remediation Plan**: Maintain the `scripts/check-no-pac.sh` build guardrail to permanently prevent arm64e PAC regressions.

---

### DEBT-06: Persistent Logging Serialization & Buffer Capacity
* **Location**: `osfmk/arm64/start.s`
* **Nature**: Character output writes synchronously to a 256 KB persistent RAM buffer (`console-ramoops`).
* **Rationale**: Necessary for post-mortem analysis of early crashes without hardware JTAG.
* **Risk / Impact**: Once the buffer fills 256 KB, oldest logs are truncated. Synchronous UART MMIO stalls real-time performance if called excessively.
* **Remediation Plan**: Shift standard kernel output to the canonical XNU `kprintf` ring buffer after bootstrap, reserving `xzs_early_puts` strictly for fatal panic handlers.

---

### DEBT-07: Storage & Filesystem Drivers (UFS / SMMU)
* **Location**: `bsd/vfs/`, `iokit/Drivers/`
* **Nature**: Currently no storage driver exists for Qualcomm Universal Flash Storage (UFS 2.0) on MSM8996.
* **Rationale**: Bring-up focused on kernel bootstrap, memory, and SMP scheduler (Mục tiêu A, B, C).
* **Risk / Impact**: Cannot mount internal eMMC/UFS storage partitions; booting BSD requires an initrd/RAM disk.
* **Remediation Plan**: Implement a RAM-disk rootfs driver first (Phase D.1), followed by a native Qualcomm UFS controller driver with SMMU v2 stage 1 bypass (Phase D.2).

---

### DEBT-08: devfs_getattr Pointer-Hardening Bypass (`3e417bb`)
* **Location**: `src/xnu/bsd/miscfs/devfs/devfs_vnops.c` (`devfs_getattr`)
* **Classification**: `XZS PLATFORM WORKAROUND / BRING-UP COMPATIBILITY FIX`
* **Nature**: Upstream XNU invokes `VM_KERNEL_ADDRHASH(file_node->dn_dvm)` to compute `va_fsid` from the directory vnode pointer. This macro routes through the generic SHA-256 pointer-hardening path (`vm_kernel_addrhash()`), which does not return / stalls on Qualcomm MSM8996 during early single-instance devfs initialization. The workaround replaces this with a static non-pointer fsid `(uint32_t)0x64657666` (`"devf"`).
* **Rationale**: Required during D6-M6 to permit `vn_authorize_open_existing()` and `vnode_getattr()` to complete when opening `/dev/console` from userspace/bootstrap, allowing PID1 to acquire native file descriptors 0, 1, and 2.
* **Risk / Impact**: Non-canonical `va_fsid` generation for devfs. Modifies generic XNU vfs/devfs code under `#if CONFIG_XZS_BRINGUP`.
* **Migration Plan / Remediation**:
  The `devfs_getattr` pointer-hardening bypass must be re-audited when XZSPlatform and the long-term platform abstraction are introduced.
  **Goal**: avoid carrying Xperia/MSM8996 bring-up exceptions as permanent, generic XNU-core behavior. Once platform crypto/entropy and long-term abstractions mature, investigate why `vm_kernel_addrhash()` stalls or move platform-specific exceptions into `XZSPlatform`.

---

### DISPLAY-DEBT-001: DSI DCS RX / Bus Turnaround (BTA) Readback Timeout
* **Location**: `src/xnu/pexpert/arm/xzs_d8m8.h` (`xzs_d8m8_audit_ddic_state_f18`), `src/xnu/pexpert/arm/xzs_d8m4.h`
* **Classification**: `NON-BLOCKING TECHNICAL DEBT`
* **Nature**: MIPI DSI Bus Turnaround (BTA) requests (`DSI_CMD_DMA_CTRL` with BTA trigger) time out (`DSI_BTA_TIMEOUT = 0x00000100`) on the MSM8996 DSI v1.4 host when attempting to read back DCS registers (e.g. DDIC ID `0xDA`/`0xDB`/`0xDC`, power mode `0x0A`, MADCTL `0x0B`) from the Sharp display driver IC.
* **Rationale**: BTA readback was originally investigated to audit DDIC sleep/wake status and OTP revisions. D8-M8.5 hardware proof demonstrated that forward DCS command transmission (`DSI_FORWARD_WRITE_PATH = HW_PROVEN`) and autonomous MDP5 frame scanout (`DSI_SCANOUT_PATH = HW_PROVEN`) function reliably without DCS readback.
* **Risk / Impact**: Zero impact on visible scanout, video refresh, or framebuffer console rendering. Prevents software-based ESD detection, panel manufacture ID querying, and dynamic DDIC fault monitoring.
* **Remediation Plan**: Revisit DSI PHY LP-RX calibration, timer thresholds (`DSI_LP_TIMER_CTRL`), and DSI host BTA timeout configurations when implementing dynamic panel power management (Phase D9).

