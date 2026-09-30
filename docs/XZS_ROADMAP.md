# Xperia XZs / Qualcomm MSM8996 Port Roadmap

This document outlines the technical architecture roadmap for bringing up Apple XNU natively on Sony Xperia XZs (Qualcomm MSM8996).

Progress is strictly gated by physical hardware verification. Speculative percentages and estimated completion dates (ETAs) are deliberately omitted.

---

## 1. Executive Status Summary

| Phase | Description | Current Status |
| :--- | :--- | :---: |
| **Phase A** | Native kernel entry (Bootshim, ADT, MMU, High KVA) | **COMPLETE** |
| **Phase B** | Platform bring-up (UARTDM, GICv3, Timer, Pmap, VM) | **COMPLETE** |
| **Phase C** | SMP / Mach scheduler (PSCI, 4 Kryo cores, IPI, AST, Preemption) | **COMPLETE** |
| **Phase D1** | BSD / VFS bootstrap to root-storage boundary | **COMPLETE** |
| **Phase D2** | Physical eMMC storage bring-up (SDCC1, CMD0..CMD17, PIO) | **COMPLETE** |
| **Phase D3** | GUID Partition Table (GPT) discovery & partition enumeration | **COMPLETE** |
| **Phase D4** | Block-storage driver integration (`bdevsw` / `disk0`) | **COMPLETE** |
| **Phase D5** | Real root filesystem mount (RAMDisk XZSFS) | **COMPLETE / SEALED** |
| **Phase D6** | PID 1 / First EL0 userspace (`initproc` / launchd) | **COMPLETE / SEALED** |
| **Phase D7** | Interactive USB shell (`/bin/sh`) & Generic Mach-O exec | **COMPLETE / SEALED (D7-M1..M4, D7-T1, D7-T2 COMPLETE)** |
| **Phase D8** | Native display / framebuffer / touch / recovery console | **IN PROGRESS (D8-M1..M6 PASS / SEALED, D8-P1..P2 COMPLETE / SEALED, D8-P3 NEXT)** |
| **Phase D9** | XZSPlatform hardware/platform compatibility layer | **PLANNED** |
| **Phase D10**| Core native device drivers | **PLANNED** |
| **Phase D11**| System hardware integration | **PLANNED** |
| **Phase D12**| XZSAppleCompat (Apple-facing hardware compatibility layer) | **PLANNED** |
| **Phase D13**| Darwin / iOS userland compatibility | **PLANNED** |
| **Phase D14**| First old-iOS userland boot | **PLANNED** |
| **Phase D15**| iOS service bring-up | **PLANNED** |
| **Phase D16**| Graphical iOS userland / SpringBoard investigation | **PLANNED** |


---

## 2. Detailed Phase Specifications

### Phase A — Native Kernel Entry
* **Goal**: Execute Apple XNU kernel natively at EL1 on Qualcomm MSM8996 via Sony S1 bootloader.
* **Status**: **COMPLETE**
* **Hardware Acceptance Criteria**: S1 bootloader executes `xzs-bootshim`; bootshim builds Apple Device Tree (ADT); kernel `_start` sets exception vectors, configures page tables, and successfully enables MMU, jumping to High KVA (`0xfffffe0000000000`).
* **Completed Items**:
  - S1 Android-standard boot image packaging (`mkbootimg.py`).
  - `xzs-bootshim` parsing Qualcomm DTB and constructing compliant ADT.
  - Exception vectors installed at `LowExceptionVectorBase`.
  - TTBR0/TTBR1 and TCR/MAIR configuration in `start.s`.
  - Transition to High KVA C runtime in `arm_init()`.
* **Remaining Items**: None.
* **Known Blockers**: None.
* **Dependencies**: Sony S1 ABOOT bootloader.

---

### Phase B — Platform Bring-up
* **Goal**: Initialize fundamental platform peripherals (UART console, interrupt distributor, hardware timers, and memory allocators).
* **Status**: **COMPLETE**
* **Hardware Acceptance Criteria**: Qualcomm BLSP2 UARTDM transmitting serial logs at 115200 8N1; ARM GICv3 distributor (`GICD`) and redistributor (`GICR`) active; ARM generic timer PPI firing at 19.2 MHz; kernel VM zones and page allocators active.
* **Completed Items**:
  - Qualcomm BLSP2 UARTDM driver in `pexpert/arm/pe_serial.c`.
  - Persistent RAM pstore logging in `start.s` (`0xa7fbe000` console, `0xa7f00000` dmesg).
  - GICv3 distributor initialization in `pexpert/arm/pe_fiq.c`.
  - ARM Generic Timer PPI configuration (Virtual PPI 27 / Physical PPI 30).
  - Early memory allocation and pmap bootstrap.
* **Remaining Items**: Native Qualcomm PMIC RTC driver (currently using non-blocking RTC workaround).
* **Known Blockers**: None for bring-up.
* **Dependencies**: Phase A.

---

### Phase C — SMP / Mach Scheduler Integration
* **Goal**: Bring all 4 Qualcomm Kryo cores online into Mach processor set `pset0` and achieve preemption and thread migration.
* **Status**: **COMPLETE**
* **Hardware Acceptance Criteria**: CPU0-CPU3 online; secondary cores booted via standard PSCI `CPU_ON` (`0xC4000003`); Mach `idle_thread` running on all 4 cores; cross-cluster reschedule IPIs delivered via GICv3 SGI 1; 40,000 concurrent atomic lock operations completed without data corruption.
* **Completed Items**:
  - PSCI `CPU_ON` multi-core bootstrap in `osfmk/kern/startup.c`.
  - Per-core GICR configuration and system register interface (`ICC_SRE_EL1.SRE = 1`).
  - Secondary core entry trampolines (`xzs_secondary_entry`, `xzs_secondary_kva_entry`).
  - All 4 processors registered in `pset0`.
  - Reschedule AST preemption (`thread_preempted_in_kernel`).
  - Thread migration and 40k contended spinlock verification (`0x9c40`).
* **Remaining Items**: Dynamic CPU hotplug and CPU deep sleep power management.
* **Known Blockers**: None for current scope.
* **Dependencies**: Phase B.

---

### Phase D1 — BSD / VFS to Root Storage Boundary
* **Goal**: Bootstrap BSD subsystem, process table, VFS layer, devfs, and IOKit autoconfiguration up to the physical block-device rootfs boundary.
* **Status**: **COMPLETE**
* **Hardware Acceptance Criteria**: `bsd_init()` executes to completion; `IOKitBSDInit()` publishes `IOBSD`; `IOFindBSDRoot()` traverses canonical matching and returns `kIOReturnNotFound` (`0xe00002f0`) on missing physical storage; `vfs_mountroot()` probes root block device via `bdevvp()` and reports canonical `ENODEV` (`0x13`).
* **Completed Items**:
  - BSD kernel process (`kernproc`) and credential bootstrap.
  - VFS mount table and vnode allocation pools initialized.
  - Memory status jetsam snapshot buffer workaround.
  - Non-Apple silicon CTRR compatibility adaptation.
  - Thread call zone priming to eliminate early allocator lock contention.
  - Bounded root-device wait selftest in `IOFindBSDRoot()`.
  - Block device open error propagation in `bdevvp()`.
  - Automated +6s warm reboot to Fastboot upon reaching `[D51-TERMINAL]`.
* **Remaining Items**: None. Phase D1 acceptance gate fully passed.
* **Known Blockers**: None.
* **Dependencies**: Phase C.

---

### Phase D2 — Physical eMMC Storage Bring-up
* **Goal**: Drive Qualcomm MSM8996 SDCC1 / SDHCI v5 host controller (`0x07464900`) and establish physical data transfers with the Samsung BJNB4R 32GB eMMC 5.1 device.
* **Status**: **COMPLETE**
* **Hardware Acceptance Criteria**: SDC1 clock configured at 400 kHz; host controller reset; MMC full bus initialization sequence completed (CMD0 -> CMD1 -> CMD2 -> CMD3 -> CMD9 -> CMD7 -> CMD8 -> CMD17); single-block physical sector read (LBA 1) matches independent TWRP oracle byte-for-byte (`e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a`).
* **Completed Milestones**:
  - **D2-M1 (SDC1 Audit & Identity Oracle)**: ✅ Probed host version (`0x4902`), capabilities (`0x742dc8b2`), HC mode (`0x00000001`).
  - **D2-M2 (Clock & Reset Replay)**: ✅ 400-kHz RCG configuration (`F(400000, P_XO, 12, 1, 4)`), `SDHCI_RESET_ALL` cleared in 10 µs.
  - **D2-M3 (Host Power & Card Clock)**: ✅ 1.8V bus power (`0x0B`), internal/card clock active (`0x0007`), timeout `0x0F`.
  - **D2-M4A (CMD0 Go Idle)**: ✅ Issued `CMD0` (`0x0000`), hardware execution verified.
  - **D2-M4B/C (CMD1 Card Power-Up)**: ✅ Polled `CMD1` until `CARD_READY=yes`, `FINAL_OCR=0xC0FF8080` (Sector Mode).
  - **D2-M4D-A (CMD2 CID Identification)**: ✅ `CID_MATCH=yes` (`150100424a4e4234520fdac7c0381400`, Samsung BJNB4R).
  - **D2-M4D-B (CMD3 RCA Assignment)**: ✅ Assigned `RCA = 2`, verified transition to STBY state (`R1 = 0x00000500`).
  - **D2-M4D-C (CMD9 CSD Capture)**: ✅ `CSD_MATCH=yes` (`d02701320f5903fff6dbffef8e404000`).
  - **D2-M4D-D (CMD7 Card Selection)**: ✅ Issued `CMD7` addressed to RCA 2, card selected (`CARD_SELECTION_CONFIRMED=yes`).
  - **D2-M4E (CMD8 EXT_CSD Read)**: ✅ First physical 512-byte data transfer via PIO, `EXT_CSD_REV=0x08`, `SEC_COUNT=61071360`, `TRAN` directly observed (`R1 = 0x00000900`).
  - **D2-M5 (CMD17 Physical LBA 1 Read)**: ✅ Single sector read at `LBA = 1`, 512 bytes captured from `SDHCI_BUFFER`, 100% byte-for-byte match with TWRP disk oracle (`cmp -l` exit 0).
  - **D2 Final Gate**: `D2_STORAGE_COMPLETE = yes`.
* **Remaining Items**: None. Phase D2 sealed.
* **Dependencies**: Phase D1.

---

### Phase D3 — GUID Partition Table (GPT) Discovery
* **Goal**: Parse primary and backup GUID Partition Tables (GPT) from the physical eMMC user area and enumerate partitions.
* **Status**: **COMPLETE**
* **Detailed Milestone Execution & Verification**:
  - **D3-M1 (Primary GPT Header)**:
    - Fresh physical LBA 1 read & parse; verified `"EFI PART"` signature, revision `0x00010000`, size 92, and calculated CRC32 `0xBFDF741D` matching stored header field.
  - **D3-M2A (Primary Partition Entry Array)**:
    - Sequential 32 single-block PIO `CMD17` reads of LBA 2..33 (16,384 bytes); calculated CRC32 `0x64EDE0F4` matching header; 100% byte-for-byte match against independent TWRP oracle.
  - **D3-M2B (Primary Partition Map)**:
    - Decoded all 128 slots into static kernel storage; validated 55 used entries (`PartitionTypeGUID != 0`) and 73 unused entries; validated extents within usable bounds `[34, 61071326]`, arithmetic overflow safety, pairwise non-overlap across all 55 used partitions, unique GUID validity, and deterministic canonical name decoding.
  - **D3-M3 (Backup GPT & Authoritative Seal)**:
    - Dynamically read and verified Backup GPT Header at sector 61071359 (CRC32 `0x03F02415`); confirmed reciprocal links (`PRIMARY.MyLBA == BACKUP.AlternateLBA`, `PRIMARY.AlternateLBA == BACKUP.MyLBA`); derived Backup Array geometry (LBAs 61071327..61071358); verified Backup Array CRC32 (`0x64EDE0F4`); confirmed 100% byte-for-byte and map-for-map cross-validation with Primary GPT.
    - Verified `AUTHORITATIVE_GPT_PARTITION_MAP_VERIFIED = yes` and `PRIMARY_BACKUP_GPT_CONSISTENT = yes`.
* **Hardware Acceptance Criteria**:
  - Primary and backup GPT headers verified on silicon with dynamic CRCs.
  - Reciprocal links between primary and backup verified.
  - Exact 16,384-byte array equality between primary and backup.
  - 55 valid, non-overlapping partitions identified with exact sector ranges.
  - Zero partition content reads, zero filesystem probes, zero rootfs selection, zero storage writes.
* **Explicit Invariants & Preserved Semantics**:
  - `GPT_NAME_PAIRED_A_B_ENTRIES_OBSERVED = yes`
  - `A_B_BOOT_SLOT_SEMANTICS = NOT_ESTABLISHED`
  - `FILESYSTEMS = NOT_PROBED`
  - `ROOTFS = NOT_SELECTED`
* **Remaining Items**: None. Phase D3 is COMPLETE and SEALED. Phase D4 is NEXT.
* **Dependencies**: Phase D2.

---

### Phase D4 — Block Storage Driver & BSD Integration
* **Goal**: Transition eMMC hardware operations from diagnostic probes into a persistent, reusable block storage runtime, implement BSD block device switch (`bdevsw`), devfs whole-disk node (`/dev/disk0`), runtime GPT partition map loaded via block layer, devfs partition slice devices (`/dev/disk0s1`..`disk0s55`), IOKit BSD root discovery bridge (`XZSeMMCStorageNub`), and real block vnode acquisition (`bdevvp`).
* **Architecture**: `XZS_HYBRID_IOKIT_BDEVSW_STORAGE` (BSD `bdevsw` switch table backed by persistent eMMC runtime with lightweight IOKit media nub discovery).
* **Status**: **COMPLETE**
* **Detailed Milestone Execution & Verification**:
  - **D4-M1 (Persistent eMMC Runtime Context & Multi-Sector Pipeline)**: ✅ **COMPLETE**
    - Established static persistent runtime context (`xzs_emmc_context_t`, `g_xzs_emmc_ctx`) with fail-closed semantics.
    - Implemented idempotent persistent initialization (`xzs_emmc_init_persistent()`): `INIT_CALL_COUNT=2`, `INITIALIZATION_COUNT=1`, `INITIALIZATION_REUSE_COUNT=1`, `CONTROLLER_RESET_COUNT=1`.
    - Implemented 64-bit checked single-sector primitive (`xzs_emmc_read_sector_sync()`) with checked narrowing.
    - Implemented multi-sector synchronous software pipeline (`xzs_emmc_read_blocks_sync()`) with 7-point overflow and range bounds validation.
    - Verified on physical hardware: 4 non-contiguous reads (`LBA 1`, `LBA 2`, `LBA 33`, Backup Header `LBA 61071359`) across a single persistent session with zero intermediate controller resets.
    - 100% byte-for-byte exact identity against independent frozen TWRP oracles. Both primary and backup GPT header CRCs dynamically verified on read buffers.
    - `PERSISTENT_EMMC_RUNTIME_VERIFIED = yes`, `MULTI_SECTOR_PIPELINE_VERIFIED = yes`.
  - **D4-M2 (BSD Block Device Switch Layer)**: ✅ **COMPLETE**
    - Implemented controller serialization lock (`lck_mtx_t g_xzs_emmc_mtx`).
    - Implemented BSD `bdevsw` switch table (`xzs_bdev_open`, `xzs_bdev_close`, `xzs_bdev_strategy`, `xzs_bdev_psize`, `xzs_bdev_ioctl`).
    - Registered major block device dynamically via `bdevsw_add(-1, &g_xzs_bdevsw)` at `BSD_POST_VFSINIT` (allocated major 1).
    - Integrated genuine in-tree `buf_t` handling (`buf_alloc(NULL)`, `buf_reset()`, `buf_map()`, `buf_biowait()`, `buf_free()`).
    - Verified 512B LBA1 read (CRC32 `0xD3A34BC1`, SHA256 `e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a`, 100% match).
    - Verified 1024B LBA1..2 multi-sector read (CRC32 `0xA21C1724`, SHA256 `4a161d7ec294bc215b8dd22989a4f87f1c501fac3250acf66e5e2a4085ece8d0`, 100% match).
    - Verified synthetic failure rejection: out-of-range -> `EINVAL`, misaligned -> `EINVAL`, write -> `EROFS`, with zero physical commands issued.
    - Verified geometry query ioctls and `d_psize` (61,071,360 512-byte blocks).
    - `BSD_BLOCK_STRATEGY_VERIFIED = yes`, `BDEVSW_IMPLEMENTED = yes`, `CONTROLLER_SERIALIZATION_ENABLED = yes`.
  - **D4-M3 (Whole-Disk devfs Publication)**: ✅ **COMPLETE**
    - Created `/dev/disk0` via `devfs_make_node()`, preserved handle `g_xzs_disk0_devfs_handle`.
    - `DEVFS_DISK0_PUBLISHED = yes`, `DEVFS_RDISK0_PUBLISHED = no`.
    - `WHOLE_DISK_DEVFS_IDENTITY_VERIFIED = yes`.
    - Write open rejected with `EROFS` (`DISK0_WRITE_OPEN_REJECTED = yes`).
  - **D4-M4 (Runtime GPT & Partition Slices)**: ✅ **COMPLETE**
    - Initialized runtime GPT map through block layer (`d_strategy` reads of LBA 1 and LBA 2..33).
    - Header CRC32 (`0xBFDF741D`) and Array CRC32 (`0x64EDE0F4`) verified.
    - Parsed all 128 slots: 55 used entries, matching D3 canonical evidence.
    - Derived minor mapping: `minor 0 = whole disk`, `minor N = Nth used GPT entry`.
    - Published all 55 partitions as `/dev/disk0s1`..`disk0s55`, stored handles.
    - Verified slice translation on literal `"boot"` entry (slot 29, minor 30, FirstLBA 208896, LastLBA 339967):
      - First sector (slice block 0): CRC32 `0x7756D106` matching independent oracle (`SLICE_FIRST_SECTOR_BYTE_MATCH = yes`).
      - Last sector (slice block 131071): CRC32 `0xB2AA7578` matching independent oracle (`SLICE_LAST_SECTOR_BYTE_MATCH = yes`).
      - One-past-end read (slice block 131072) rejected with `EINVAL`, `resid=512`, `SLICE_OUT_OF_RANGE_CMD17_DELTA = 0`.
    - `PARTITION_SLICES_PUBLISHED = yes`, `PUBLISHED_SLICE_COUNT = 55`.
  - **D4-M5 (IOKit BSD Root Discovery Bridge)**: ✅ **COMPLETE**
    - Implemented C++ `XZSeMMCStorageNub : public IOService` in `src/xnu/iokit/bsddev/xzs_storage_nub.cpp`.
    - Published canonical properties: `kIOBSDNameKey = "disk0"`, `kIOBSDMajorKey = 1`, `kIOBSDMinorKey = 0`.
    - Verified discovery via `IOBSDNameMatching("disk0")`: `IOKIT_BSD_IDENTITY_DISCOVERABLE = yes`.
    - Global `rootdev` NOT mutated (`GLOBAL_ROOTDEV_MUTATED = no`, `ROOTFS_SELECTION_PERFORMED = no`).
  - **D4-M6 (Real Block Vnode Acquisition & D4 Seal)**: ✅ **COMPLETE**
    - Block vnode acquired via `bdevvp(makedev(1, 0), &vp)`, `VNOP_OPEN(FREAD)` succeeded.
    - Controlled read of LBA 1 via `buf_bread()`: CRC32 `0xD3A34BC1` (100% byte match).
    - Vnode cleanly released via audited API `vnode_close(vp, FREAD, vfs_context_kernel())`.
    - `BDEVVP_ACQUISITION_VERIFIED = yes`, `BDEVVP_LBA1_BYTE_MATCH = yes`.
    - `D4_COMPLETE = yes`.
* **Dependencies**: Phase D3. Phase D5 is NEXT.

---

### Phase D5 — Real Root Filesystem Mount
* **Goal**: Mount an actual read-only root filesystem (RAMDisk XZSFS) into VFS root vnode (`/`).
* **Status**: **COMPLETE / SEALED — ALL D5 MILESTONES (M1-M6) COMPLETE ON HARDWARE; D6 NEXT**
  - **D5-M1 (Format Freeze & Tooling)**: **COMPLETE** (XZSFS v1 on-disk format frozen, `mkxzsfs.py` generator, `verify_xzsfs.py` independent verifier, static ARM64 Mach-O binaries verified, deterministic rootfs image built).
  - **D5-M2 (RAMDisk Block Transport)**: **COMPLETE / SEALED** (ADT `/chosen/memory-map/RAMDisk` -> `rd=md0` -> `mdevadd`; immutable logical image CRC32 `0x131e9191`).
  - **D5-M3 (Kernel XZSFS Driver)**: **COMPLETE / SEALED** (native read-only XZSFS VFS driver; superblock, metadata CRC, object graph, lookup, read, readdir, getattr, and `EROFS` behavior verified). Runtime regression gate passed on two consecutive hardware runs through `D520/90`.
  - **D5-M4 (Silicon Root Mount Proof)**: **COMPLETE / SEALED** (`vfs_mountroot()` -> `xzsfs_mount()` -> real root vnode -> `VFS_ROOT()` -> real VNOP dispatch -> global `rootvnode`). Full `D530/00` through `D530/91` and terminal `D530/01` sequence verified on Xperia XZs G8231 hardware.
  - **D5-M5 (Namespace + devfs/console)**: **COMPLETE / SEALED** (Pathname resolution of `/` and `/sbin/launchd` via `namei()`, `devfs_kernel_mount("/dev")`, crossing into devfs root vnode, `/dev/console` resolution to cdev 0:0, and full telemetry verification).
  - **D5-M6 (Final D5 Seal)**: **COMPLETE / SEALED** (Namespace resolution of `/bin/sh` to fileid 3, full D5 regression continuity across M1-M6 verified on hardware, zero storage writes, hard boundary preserved before PID 1).
* **D5 Final Hardware Evidence**:
  - Tested boot image SHA-256: `18f1f3ade838d859f5e4a3eb0bede5953cd4a0b46c008d97033ce8e06b1d5fe9`.
  - Checkpoint sequence: `D530/00`..`91` -> `D540/00`..`01` -> `D550/00`..`91` and terminal `D550/01` (return time: +4s).
  - `scripts/verify_d5_final_acceptance.py`: **100% PASS** for all D530, D540, D550 checkpoints and all canonical telemetry invariants.
  - Pathname lookups: `namei("/")` returned global `rootvnode`; `namei("/sbin/launchd")` returned `VREG` (fileid 7, size 16472, mode 0755); `namei("/bin/sh")` returned `VREG` (fileid 3, size 16472, mode 0755); `namei("/dev")` crossed into devfs root vnode (`VDIR`, `devfs`); `namei("/dev/console")` returned `VCHR` (major 0, minor 0).
  - Hard boundary preserved: `PID1_STARTED=no`, `EXECVE_ATTEMPTED=no`, `EL0_ENTRY_ATTEMPTED=no`.
  - Storage remained read-only: `CMD24_COUNT=0`, `CMD25_COUNT=0`, `ZERO_STORAGE_WRITES=yes`.
* **Hardware Acceptance Criteria**: **SATISFIED FOR PHASE D5**. Full read-only rootfs mount, namespace traversal, devfs overlay, and console vnode verified on silicon.
* **Known Blockers**: None remaining for Phase D5.
* **Dependencies**: Phase D4.

---

### Phase D6 — PID 1 / First EL0 Userspace
* **Goal**: Bootstrap the first Mach/BSD userspace process (`initproc` / PID 1) from the root filesystem and transition from EL1 to EL0.
* **Status**: **COMPLETE / SEALED ON HARDWARE (ALL MILESTONES D6-M1 THROUGH D6-M7 COMPLETE & SEALED; D7 NEXT)**
* **Milestones**:
  - **D6-M1 (PID1 Skeleton)**: **COMPLETE / SEALED** (Created and validated BSD `initproc` (PID 1, PPID 0), Mach task (non-kernel), Mach thread, and embedded uthread; zero userspace execution; full D5 regression prefix).
  - **D6-M2 (Minimal Mach-O Loader)**: **COMPLETE / SEALED** (Opened `/sbin/launchd`, validated ARM64 Mach-O header/load commands, enumerated segments, resolved entry PC `0x1000002f0`; verified static/no-dyld contract; no VM mapping; zero EL0 entry).
  - **D6-M3 (User VM + Initial Stack)**: **COMPLETE / SEALED** (Mapped and content-verified `__TEXT`, finalized current and maximum protection to RX, preserved hard PAGEZERO, left `__LINKEDIT` unmapped, constructed a native Darwin initial frame on an RW/NX stack, installed and read back PC/SP, and verified zero unexpected RWX mappings while PID1 remained suspended).
  - **D6-M4 (First EL0 Transition)**: **COMPLETE / SEALED** (Released PID1 task/thread holds, verified full return-to-user path `D630/33`..`37` -> `eret`, executed canonical 5-instruction `/sbin/launchd` EL0 sequence on hardware, captured canonical `svc #0x80` before dispatch with full register signature; `FIRST_REAL_EL0_INSTRUCTION_HARDWARE_VERIFIED=yes`, `CANONICAL_SVC64_SIGNATURE_HARDWARE_VERIFIED=yes`).
  - **D6-M5 (First Syscall Round-Trip)**: **COMPLETE / SEALED** (Routed canonical `svc #0x80`, `x16=4` through normal `handle_svc()` and `unix_syscall()` dispatch to the real `sysent[4]` `write()` handler; hardware verified deterministic `EBADF=9` error ABI, normal return to EL0, and a post-return instruction signature at `ELR=0x100000310`).
  - **D6-M6 (Minimal Stable PID1 Runtime)**: **COMPLETE / SEALED** (Configured native fd 0/1/2 mapped to `/dev/console` (cdev 0:0, VCHR), executed real EL0 `write(1)` of 26 bytes verified by telemetry, entered deterministic sustained loop performing consecutive successful `getpid` round-trips without faulting).
  - **D6-M7 (Final D6 Seal)**: **COMPLETE / SEALED** (Consolidated architecture documentation, executed full regression sweep across M1-M6 via `scripts/verify_d6_acceptance.py` on physical silicon, applied git tag `xzs-d6-userspace-complete`, and formally sealed Phase D6).
* **D6-M6 / D6-M7 Hardware Evidence**:
  - Tested source commits: `e64ce18b506bdafbd78c0c0d428a19d7dac85124` (M6), `f5dd7a36bfedfbcf68d7a6bf2a95d4d09dab5ac0` (M7).
  - Raw console log: `artifacts/logs/xnu-console-extracted.log`.
  - Checkpoint sequence: `D650/00`, `/10`, `/20`, `/30`, `/90`, `/91`, `/01` (PASS).
  - `scripts/verify_d6_acceptance.py`: **100% PASS**, including full D5, D6-M1, D6-M2, D6-M3, D6-M4, D6-M5, and D6-M6 regression assertions.
  - Process & descriptor state: `PID1_STARTED=yes`, `PID1_STABLE_RUNTIME=yes`, `fd 0 -> /dev/console`, `fd 1 -> /dev/console`, `fd 2 -> /dev/console` (`VCHR`, `0:0`).
  - Real console write: `write(1, 0x100000320, 26)` -> `WRITE_RETURN_VALUE=26`, `PID1_CONSOLE_OUTPUT_VERIFIED=yes`.
  - Sustained EL0 execution: `STABLE_RUNTIME_SYSCALL=getpid`, `STABLE_RUNTIME_ROUND_TRIPS>=256` verified on silicon.
  - Console transport boundary: `UARTDM TX = working`, `UARTDM RX = not implemented`, `PHYSICAL_CONSOLE_RX_AVAILABLE=no`.
  - Platform workaround: commit `3e417bb` (`devfs_getattr` pointer-hardening bypass, classification: `XZS PLATFORM WORKAROUND / BRING-UP COMPATIBILITY FIX`).
* **Hardware Acceptance Criteria**: **SATISFIED AND SEALED FOR PHASE D6**.
* **Dependencies**: Phase D5 (COMPLETE / SEALED). Phase D6 is COMPLETE / SEALED. Phase D7 is NEXT.

---

### Phase D6-M7 — Final D6 Regression and Seal
* **Goal**: Small consolidation milestone to regress all D6 milestones and formally seal the userspace foundation.
* **Status**: **COMPLETE / SEALED**
* **Completed Items**:
  - Regressed D6-M1 (PID1 skeleton)
  - Regressed D6-M2 (Mach-O loader)
  - Regressed D6-M3 (User VM and initial stack)
  - Regressed D6-M4 (First EL0 transition)
  - Regressed D6-M5 (First syscall round-trip)
  - Regressed D6-M6 (Stable PID1 runtime and native console)
  - Created standalone final acceptance verifier `scripts/verify_d6_acceptance.py`
  - Consolidated architecture documentation and verified consistency
  - Sealed D6 userspace foundation and tagged `xzs-d6-userspace-complete`
* **Acceptance Gate**:
  ```text
  D6_COMPLETE=yes
  D6_SEALED=yes
  ```
* **Git Tag**: `xzs-d6-userspace-complete`.


---

### Phase D7 — Interactive EL0 Shell
* **Goal**: Establish a headless interactive command-line environment over the serial console.
* **Design Paradigm**: **HEADLESS INTERACTIVE SHELL FIRST**. The physical Xperia display is NOT required for D7.
* **Canonical Boot Pipeline**:
  ```text
  fastboot boot
      ↓
  bootshim
      ↓
  XNU
      ↓
  XZSFS
      ↓
  PID1
      ↓
  /bin/sh
      ↓
  xzs#
  ```
* **Subtasks**:
  - **D7-M1 (Shell Artifact & Dependency Audit)**: ✅ **COMPLETE** (Audited `/bin/sh` static ARM64 Mach-O stub, zero dyld dependencies, Darwin initial stack compatible, stdio fd 0/1/2 inheritance verified, UARTDM RX registers identified, Strategy B selected for D7-M2).
  - **D7-M2 (PID1 -> `/bin/sh` Handoff)**: ✅ **COMPLETE / SEALED** (PID1 in-place same-thread reload to `/bin/sh` static Mach-O image; old bootstrap image deallocated, new `__TEXT` mapped RX, stack reinitialized RW/NX with canonical Darwin initial frame; hardware verified real EL0 transition, `SYS_write(1, "XZS: /bin/sh EL0 online\n", 24)` returning 24 with zero error, subsequent EL0 instruction execution, and clean exit trapped via `SYS_exit(0)`).
  - **D7-M3 (Shell Stdout & Visual Identity)**: ✅ **COMPLETE / SEALED** (Userspace `/bin/sh` in EL0 executing Darwin `write(1, banner, 1332)` and `write(1, prompt, 5)` to `/dev/console`; exact banner and prompt text observed on physical console transport; passive syscall dispatch verified; 64 sustained post-prompt EL0 getpid round-trips; UART RX preserved as unavailable).
  - **D7-M4 (Shell Stdin)**: ✅ **COMPLETE / SEALED** (MSM8996 UARTDM RX engine, GICv3 SPI 114 / INTID 146 level-high IRQ, bounded SPSC RX ring, deferred tty delivery via thread_call, native tty line discipline, canonical blocking Darwin read(0), native wakeup, exact ABC\n payload return to EL0, and post-read EL0 execution continuity hardware-verified on Xperia XZs. Physical UART test-point input is classified as out of scope under the original non-invasive project boundary; native kernel stdin semantics are 100% verified.)
  - **D7-T1 (USB Console Transport)**: ✅ **COMPLETE / SEALED** (DWC3 USB gadget console; Z1-Z4 transport handshake; bi-directional bulk loopback; interactive `/bin/sh` shell over USB console).
  - **D7-T2 (Native Generic Mach-O Execution)**: ✅ **COMPLETE / SEALED** (VREG UBC attachment via `ubc_info_init()`; file-backed Mach-O mapping via `vm_map_enter_mem_object_control()`; XZSFS read-only `VNOP_PAGEIN` demand-paging via `DIRECT_UPL`; AST_APC clean old-thread retirement; syscall return Carry flag clear; `/bin/hello` and `/bin/args` hardware-sealed across 14 external exec cycles; `DEBT-001` RESOLVED). Tag: `xzs-d7t2-full-complete`.
* **D7-M2 Hardware Evidence**:
  - Tested boot image SHA-256: `a78af05bbd39e2c93a04d3b3384d885a83005e40b25dd6cb5b7140795c71e24e`.
  - Tested commit: `694446c0af85095e3045fb3d15bd12215aea203e`.
  - Checkpoint sequence: All 27 canonical checkpoints `D710/00` through `D710/01` in monotonic order.
  - Verifiers: `scripts/verify_d7m2_acceptance.py` (100% PASS) and `scripts/verify_d6_acceptance.py` (100% PASS).
  - Telemetry: `D7-M2_COMPLETE=yes`, `ROADMAP_ADVANCED_TO=D7-M3`, `D6_REGRESSION_VERIFIER=PASS`.
  - Real EL0 Execution: `SYS_write(1, "XZS: /bin/sh EL0 online\n", 24)` returned 24; subsequent EL0 instructions executed; clean exit via `SYS_exit(0)` trapped.
* **D7-M3 Hardware Evidence**:
  - Tested boot image SHA-256: `c2b66cea0a6475bc140e729b2827e41d636b855f2a29f7619a56c5d1cccbe68c`.
  - Checkpoint sequence: Monotonic breadcrumbs `D720/00`, `/10`, `/20`, `/30`, `/31`, `/32`, `/33`, `/40`, `/50`, `/60`, `/61`, `/62`, `/90`, `/91`, `/01`.
  - Verifiers: `scripts/verify_d7m3_acceptance.py` (100% PASS), `scripts/verify_d7m2_acceptance.py --regression` (100% PASS), `scripts/verify_d6_acceptance.py` (100% PASS).
  - Negative Test: `python3 scripts/verify_d7m3_acceptance.py artifacts/logs/d7m3_false_positive_console.log` (FAILS with exit code 1 as required).
  - Telemetry: `D7_M3_COMPLETE=yes`, `SHELL_RUNNING_IN_EL0=yes`, `SHELL_BANNER_WRITE_RESULT=1332`, `SHELL_PROMPT_WRITE_RESULT=5`, `POST_PROMPT_GETPID_ROUNDTRIPS=64`, `SHELL_STDOUT_WORKING=yes`, `SHELL_PROMPT_VISIBLE=yes`, `UARTDM_RX_AVAILABLE=no`.
  - Genuine Console Transport: Verified ASCII banner (`Native Darwin/XNU bring-up for Xperia XZs`, `[ xnu-xzs userspace online ]`) and prompt (`xzs#`) observed on physical console transport.
* **D7-M4 Shell Stdin Hardware Evidence**:
  - Tested source commit: `639fe1c42478c8ca07aa0019cbe59bfd846d9866`.
  - Kernel SHA-256: `3cc027c7057a7ba9aec1fc0f7e94c7a9d6a7fc1ecc78f81bb299253be05bee75`.
  - Rootfs SHA-256: `0d14a1dfa3726abb85ddbbc10f2aeb111fd632be42df9d2ba9a728e94eace264`.
  - Boot image SHA-256: `329f869a55e7185bda99d14f38575d13c693298b04c7fa0b87f5d3439b34044a`.
  - Raw console SHA-256: `49422168deee99ad9c8fa3b5d07adb70edf6c367b185174e44cb8e3ff769d469`.
  - IRQ contract: DT `GIC_SPI 114`, architectural `INTID 146`, level-high, routed to CPU0; hardware IRQ delivered four bytes (`41 42 43 0a` = `ABC\n`) into the common RX ring.
  - Native path: `UARTDM IRQ -> RX ring -> deferred thread_call -> cons_cinput -> tty line discipline -> native wakeup -> read(0) -> EL0`.
  - Verifiers: D6 PASS, D7-M2 regression PASS, D7-M3 acceptance PASS, D7-M4 canonical acceptance PASS (`scripts/verify_d7m4_acceptance.py`).
  - Architectural Boundary: `SHELL_STDIN_KERNEL_PATH_WORKING=yes`, `SHELL_STDIN_WORKING=yes`, `EXTERNAL_UART_PIN_TEST=OUT_OF_SCOPE_NON_INVASIVE_PROJECT`, `HOST_INTERACTIVE_STDIN_TRANSPORT_AVAILABLE=no`, `D7_M4_COMPLETE=yes`, `D7_M4_HARDWARE_VERIFIED=yes`, `D7_M4_SEALED=yes`.
* **Visual Identity & Banner**:
  The xnu-xzs shell features a recognizable terminal ASCII banner upon entering `/bin/sh`:
  ```text
                     _.-""""-._
                  .-'          '-.
                 /                \
                |                  |
                 \                /
                  '._          _.'
                     '-.____.-'
                   .-'        '-.
                 .'              '.
                /                  \
               |                    |
                \                  /
                 '._            _.'
                    '----------'

  +------------------------------------------------------------+
  |                         XNU-XZS                            |
  |                                                            |
  |        Native Darwin/XNU bring-up for Xperia XZs           |
  |                                                            |
  |        Sony G8231  |  MSM8996  |  ARM64  |  XNU           |
  +------------------------------------------------------------+
  | Kernel       native XNU                                    |
  | CPUs         4 x Kryo                                      |
  | Filesystem   XZSFS                                         |
  | Userspace    EL0                                           |
  | Syscalls     Darwin ARM64                                  |
  | Console      /dev/console                                  |
  | Shell        /bin/sh                                       |
  +------------------------------------------------------------+

               [ xnu-xzs userspace online ]

  xzs#
  ```
* **Final Acceptance Criteria**:
  ```text
  SHELL_RUNNING_IN_EL0=yes
  SHELL_PROMPT_VISIBLE=yes
  SHELL_STDIN_WORKING=yes
  SHELL_STDOUT_WORKING=yes
  SHELL_MULTIPLE_COMMANDS_WORKING=yes
  D7_COMPLETE=yes
  D7_SEALED=yes
  ```

---

### Phase D8 — Native Display / Recovery
* **Goal**: Bring the Xperia XZs physical display (1080x1920 IPS LCD) to life under native XNU and render the console directly on-device.
* **Important Constraint**: **GPU acceleration is NOT required for initial D8 framebuffer/text console.**
* **Display Priority Path**:
  ```text
  PRIMARY DISPLAY OBJECTIVE:
  reach physical first pixels as directly as possible.

  Priority path:
  M4 (DSI Host)
  → P1/P2 (GPIO & PMIC LAB/IBB)
  → M5 (Panel Power & Reset)
  → M6 (Panel Vendor & DCS Init)
  → P3/M7 (WLED Backlight)
  → M8 (MDP Framebuffer Scanout)
  → M9 FIRST PIXELS
  ```
  *(Do not expand into unrelated GPU/network/userland work before first pixels).*

* **Subtasks & Milestone Status**:
  - **D8-M1**: MDSS/MMCC topology + safe MMIO audit — **PASS**
  - **D8-M2**: Power domain + core clocks (AHB, AXI, MDP) — **PASS**
  - **D8-A0**: Exact Keyaki display audit — **PASS**
  - **D8-A1**: Linux/TWRP golden state trace — **PASS**
  - **D8-A2**: Register comparison tooling & test suite — **PASS**
  - **D8-A3**: TRACE_ONLY state machine & dry-run validation — **PASS**
  - **D8-M3**: DSI PLL + clocks (BYTE0/PCLK0/ESC0) + 14nm PHY Stage B — **COMPLETE / SEALED / HW_PROVEN** (tag `xzs-d8m3-display-pll-phy-complete`)
  - **D8-M4**: DSI host/controller configuration (Command Mode, 4-lane) — **COMPLETE / SEALED / HW_PROVEN** (tag `xzs-d8m4-dsi-host-complete`)
  - **D8-P1**: TLMM GPIO prerequisite — **COMPLETE / SEALED / HW_PROVEN** (tag `xzs-d8p1-gpio-complete`)
  - **D8-P2**: SPMI + LAB/IBB power rail driver — **COMPLETE / SEALED / HW_PROVEN** (tag `xzs-d8p2-power-rails-complete`)
  - **D8-M5**: Panel power/reset sequence — **COMPLETE / SEALED / HW_PROVEN** (tag `xzs-d8m5-panel-power-reset-complete`)
  - **D8-M6**: Panel vendor/DCS initialization sequence — **COMPLETE / SEALED / HW_PROVEN** (tag `xzs-d8m6-panel-dcs-complete`)
  - **D8-M8**: MDP framebuffer scanout configuration — **COMPLETE / SEALED / HW_PROVEN** (Autonomous VSYNC refresh)
  - **D8-M8.5**: First Visible Display Campaign — **COMPLETE / SEALED / HW_PROVEN** (tag `xzs-d8-m8.5-complete`; report `docs/XZS_D8_M8_5_FIRST_VISIBLE_DISPLAY.md`)
  - **D8-P3 / D8-M7**: PMI8994 QPNP WLED backlight driver — **COMPLETE / SEALED / HW_PROVEN** (integrated into D8-M8.5)
  - **D8-M9**: Physical first pixels on screen — **COMPLETE / SEALED / HW_PROVEN** (8 vertical color bars + center checkerboard badge + outer border)
  - **D8-M10**: Framebuffer text console (`/dev/tty0`) — **PENDING**
  - **D8-M11**: Boot splash/logo — **PENDING**
  - **D8-M12**: Display regression suite and final seal — **PENDING**
* **Phase D8 Status**: **First visible pixels achieved (`FIRST_VISIBLE_PIXELS=yes`) and MDP frame scanout verified across 3/3 fresh cold boots.**
* **Hardware Targets**:
  `TEST_PATTERN_VISIBLE=yes` (PASS) -> XNU-XZS text visible -> shell output visible -> interactive recovery.

---

### Phase D9 — XZSPlatform Hardware/Platform Compatibility Layer
* **Goal**: Isolate Qualcomm/Xperia platform-specific code behind a clean platform abstraction layer, keeping generic XNU close to upstream semantics.
* **Core Principle**: **KEEP GENERIC XNU AS CLOSE TO UPSTREAM SEMANTICS AS PRACTICAL.** Platform exceptions migrate into XZSPlatform and native drivers.
* **Architecture**:
  ```text
                    XNU / IOKit
                        |
                   device drivers
                        |
                  XZSPlatform API
                        |
                    qcom-common
                        |
                   MSM8996 backend
                        |
                Xperia XZs board data
                        |
                     hardware
  ```
* **Subtasks**:
  - **D9-M1**: Comprehensive inventory of current platform workarounds and hacks.
  - **D9-M2**: Platform resource and device model.
  - **D9-M3**: MMIO abstraction and mapping services.
  - **D9-M4**: Clock and reset controller framework.
  - **D9-M5**: Power domain and regulator (RPM/SPMI) framework.
  - **D9-M6**: GPIO and pinmux (TLMM) framework.
  - **D9-M7**: IRQ controller and interrupt routing abstraction.
  - **D9-M8**: DMA and IOMMU (SMMU v2) abstraction.
  - **D9-M9**: PHY and platform subsystem services.
  - **D9-M10**: Migrate storage drivers to XZSPlatform.
  - **D9-M11**: Migrate display and touch drivers to XZSPlatform.
  - **D9-M12**: Regression, cleanup of generic XNU hooks, and seal.

---

### Phase D10 — Core Native Device Drivers
* **Goal**: Develop native Darwin/IOKit drivers for Xperia XZs hardware peripherals.
* **Prioritized Driver Inventory**:
  - **Bring-up & Core**: UART/debug, RTC/timers, storage stabilization (eMMC / UFS), USB (DWC3).
  - **Human Interface**: Display (MDP5), touch/HID (Synaptics), buttons (power/volume GPIO).
  - **Power & Sensing**: Battery monitoring, PMIC charging, I2C, SPI, GPIO, thermal sensors.
  - **Later Peripheral Integration**: Wi-Fi (WCN3990), Bluetooth, Audio (WCD9335), GPU (Adreno 530), Camera, Modem/cellular.
* **Note**: Drivers are prioritized based on actual userland dependencies, not developed blindly ahead of need.

---

### Phase D11 — System Hardware Integration
* **Goal**: Unify independent drivers into a coherent mobile device platform.
* **Key Areas**:
  - Sleep/wake and system power states.
  - Dynamic power management and CPU frequency scaling (cpufreq / EAS).
  - Thermal management and throttling zones.
  - Battery health and charging state machines.
  - USB device/gadget modes.
  - Hardware buttons (power, volume up/down, camera shutter).
  - Device orientation and inertial sensors.
  - Persistent platform/NVRAM-like configuration storage.

---

### Phase D12 — XZSAppleCompat (Apple-Facing Hardware Compatibility Layer)
* **Goal**: Provide standard Apple IOKit service contracts and platform properties to Darwin userland.
* **Architectural Distinction**:
  ```text
  XZSPlatform
  = How XNU controls Qualcomm/Sony hardware.

  XZSAppleCompat
  = How Apple/Darwin-facing software sees the service contracts and platform properties it expects.
  ```
* **Architecture Diagram**:
  ```text
  Apple/Darwin-facing component (launchd, dyld, daemons, user clients)
            │
            ▼
  expected IOKit / platform contract (IORegistry, AppleARMPERoot, IOPower)
            │
            ▼
      XZSAppleCompat
            │
            ▼
       XZSPlatform
            │
            ▼
  native Qualcomm/Sony driver
  ```
* **Compatibility Areas**:
  - IORegistry topology emulation.
  - Platform properties and device-tree-style queries (`/chosen`, `IOPlatformExpertDevice`).
  - Standard Apple HID contracts.
  - Display plane service contracts (`IOMobileFramebuffer`).
  - Storage identity contracts (`IOBlockStorageDevice`).
  - Power-source and battery services (`IOPMPowerSource`).
  - USB device identity contracts.
  - NVRAM / platform variable services.
* **Scope Boundary**: The project does NOT claim or intend to fully emulate an Apple SoC; it provides compatibility shims for required IOKit contracts.

---

### Phase D13 — Darwin / iOS Userland Compatibility
* **Goal**: Research and implement missing OS primitives and ABI contracts required to run genuine Darwin/iOS userland binaries.
* **Target Research Areas**:
  - Mach traps and ARM64 Mach system call table.
  - BSD syscall compatibility and error code handling.
  - Mach IPC messaging (`mach_msg_trap`, port rights, voucher ports).
  - Bootstrap ports and launchd registration contracts.
  - Dynamic linker (`dyld`) compatibility and Mach-O load command support.
  - Dyld shared cache mapping and slide resolution.
  - VM behavior, memory allocators, and guard pages.
  - POSIX threading (`pthread`) kernel hooks and workqueues.
  - Codesigning expectations and CS flags.
  - Apple Mobile File Integrity (AMFI) dependencies and enforcement modes.
  - Sandbox kernel hooks and policy evaluations.
  - Notification services (`notifyd` kernel hooks).
  - XPC and Mach service lookup semantics.
  - Kernel sysctl trees and hardware capability queries.
  - IOKit user clients and memory mapping APIs.

---

### Phase D14 — First Old-iOS Userland Boot
* **Goal**: Boot an authentic older Apple iOS userland root filesystem on Sony Xperia XZs.
* **Clarification of "Install iOS" Scope**:
  "Installing iOS" in this project does **NOT** initially mean flashing an IPSW directly to Xperia, booting via Apple iBoot, or performing a Finder/iTunes restore.
* **Intended Research Pipeline**:
  ```text
  Legally obtained Apple IPSW
          ↓
  Extract compatible iOS root/userland
          ↓
  Prepare project-specific root filesystem/image
          ↓
  Boot using xnu-xzs / target-compatible XNU
          ↓
  Mount iOS userland
          ↓
  Execute authentic Apple launchd
  ```
* **First Acceptance Targets**:
  - `APPLE_LAUNCHD_MACHO_LOADED=yes`
  - `APPLE_LAUNCHD_EL0_ENTRY=yes`
* **Follow-on Targets**:
  - `APPLE_LAUNCHD_STABLE=yes`
  - `MACH_BOOTSTRAP_WORKING=yes`
  - `FIRST_APPLE_DAEMON_STARTED=yes`

---

### Architectural Note: XNU Version Compatibility
> [!IMPORTANT]
> **XNU Version vs. iOS Userland Compatibility**:
> An older iOS userland cannot automatically be assumed compatible with the current bring-up XNU kernel version (`xnu-12377.1.9` / macOS 15.0 / iOS 18 baseline).
>
> The project must eventually audit the strict version relationships:
> ```text
> Target iOS Version ↔ Darwin Version ↔ XNU Version ↔ dyld Version ↔ launchd Version ↔ IOKit ABI
> ```
> The long-term architecture decouples platform code so that **XZSPlatform**, **native drivers**, and **board support** can be re-targeted to an XNU branch closer to the selected iOS userland if required. This architectural independence is one of the primary reasons `XZSPlatform` (Phase D9) is mandatory.

---

### Phase D15 — iOS Service Bring-Up
* **Goal**: Progressively bring up authentic Apple core system daemons under launchd.
* **Scope**:
  - `launchd` job descriptor parsing and service population.
  - Core system daemons (`notifyd`, `syslogd`, `configd`).
  - Mach IPC bootstrap and service lookup registry.
  - Security subsystem dependencies.
  - Power and device monitoring services.
  - IOKit-facing service daemons.
  - Prerequisites for the graphics subsystem.

---

### Phase D16 — Graphical iOS Userland / SpringBoard Investigation
* **Goal**: Investigate and experiment with bringing up the graphical iOS interface.
* **Scope**:
  - Note: D16 replaces the previous incorrect "xnu-xzs SDK" milestone.
  - Pipeline:
    ```text
    Apple launchd
        ↓
    Basic system services
        ↓
    IOKit compatibility
        ↓
    Graphics prerequisites (IOMobileFramebuffer)
        ↓
    GraphicsServices & related frameworks
        ↓
    SpringBoard investigation
        ↓
    First graphical iOS userland frame
    ```
* **Research Challenge Notice**:
  Key graphics and UI components (UIKit, SpringBoard, CoreAnimation, GraphicsServices) contain substantial proprietary closed-source Apple components. This milestone is a research and integration effort; success is an experimental goal rather than a guaranteed outcome.
