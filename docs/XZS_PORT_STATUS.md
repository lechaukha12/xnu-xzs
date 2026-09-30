# Sony Xperia XZs (MSM8996) — Port Status

Current sealed display milestones are D8-M1..M6, D8-P1, D8-P2, D8-M8, and D8-M8.5. Milestone D8-M8.5 (First Visible Display) is COMPLETE / SEALED / HARDWARE PROVEN (tag `xzs-d8-m8.5-complete`; report `docs/XZS_D8_M8_5_FIRST_VISIBLE_DISPLAY.md`). First visible pixels are verified on physical hardware across 3/3 fresh cold boots.

```text
D7-T1   = PASS (USB transport)
D7-T2   = PASS (generic native Mach-O execution; DEBT-001 RESOLVED)
D8-M1   = PASS (display topology audit)
D8-M2   = PASS (display power & core clocks)
D8-M3   = PASS (DSI PLL / clocks / 14nm PHY Stage B; COMPLETE & SEALED)
D8-M4   = PASS (DSI0 host controller in command mode; COMPLETE & SEALED)
D8-P1   = PASS (TLMM GPIO prerequisite; COMPLETE & SEALED)
D8-P2   = PASS (SPMI + LAB/IBB display bias rails; COMPLETE & SEALED)
D8-M5   = PASS (Panel power & reset lifecycle; COMPLETE & SEALED)
D8-M6   = PASS (Panel vendor/DCS initialization sequence; COMPLETE & SEALED)
D8-M8   = PASS (MDP scanout / command transport; COMPLETE & SEALED)
D8-M8.5 = PASS (First Visible Display: WLED Backlight + Visible Frame; COMPLETE & SEALED)
```

D8-M3 hardware verification summary:
- DSI0 PLL lock and ready: `0x009948cc = 0x0000002f` (`pll_locked=1, pll_ready=1`).
- BYTE0 clock branch unhalted: `BYTE0_CFG_RCGR = 0x00000100` (`src_sel=1:DSI0_BYTE`), `BYTE0_CBCR = 0x00000001` (`halt=0, enable=1`).
- PCLK0 clock branch unhalted: `PCLK0_CFG_RCGR = 0x00000100` (`src_sel=1:DSI0_PIXEL`), `PCLK0_CBCR = 0x00000001` (`halt=0, enable=1`).
- ESC0 clock branch unhalted: `ESC0_CFG_RCGR = 0x00000000` (`src_sel=0:XO`), `ESC0_CBCR = 0x00000001` (`halt=0, enable=1`).
- 14nm PHY Stage B: All 5 lane regulator biases verified (`0x1d`), drive strengths calibrated (`0x0ff`), full acceptance readback verified.
- 2 independent fresh-boot runs: 100% pass on physical silicon (`BH905SX976`).
- Zero panel GPIO writes, zero LAB/IBB writes, zero WLED writes, zero DCS packets sent.
- `PANIC = 0`, `RESET = 0`, `BUS_ABORT = 0`, USB console shell alive. Full report: `docs/XZS_D8_M3_SEAL_REPORT.md`.

---

## Current Milestone

```text
Phase D4 acceptance gate completed & sealed:
Read-only BSD block storage, whole disk devfs /dev/disk0, 55 partition slices /dev/disk0s1..s55, C++ IOKit storage nub with registry discovery, and real bdevvp block vnode acquisition verified on physical silicon.
100% byte-for-byte parity against independent host TWRP oracles for LBA 1, LBA 2..33, and test partition ('boot') FirstLBA and LastLBA.
Dynamic major allocated (1), read-only open/strategy policy enforced, zero storage writes.
PERSISTENT_EMMC_RUNTIME_VERIFIED = yes.
BSD_BLOCK_STRATEGY_VERIFIED = yes.
DEVFS_DISK0_PUBLISHED = yes.
PARTITION_SLICES_PUBLISHED = yes.
D4_RUNTIME_GPT_MAP_INITIALIZED = yes.
IOKIT_STORAGE_NUB_PUBLISHED = yes.
IOKIT_BSD_IDENTITY_DISCOVERABLE = yes.
BDEVVP_ACQUISITION_VERIFIED = yes.
BDEVVP_LBA1_BYTE_MATCH = yes.
ZERO_STORAGE_WRITES = yes.
D4_COMPLETE = yes.

Phase D5-M1 (RootFS Tooling & XZSFS Format Freeze) is COMPLETE:
XZSFS v1 on-disk format defined & frozen (docs/XZSFS_FORMAT_V1.md).
Deterministic image generator (scripts/mkxzsfs.py) and independent verifier (scripts/verify_xzsfs.py) operational.
Static ARM64 Mach-O binaries (/sbin/launchd, /bin/sh) format-verified.
Deterministic rootfs image generated (artifacts/builds/xzs-rootfs.img, 35.0 KiB).
All 12 negative corruption tests rejected. Host-side only; zero silicon writes; no kernel VFS implementation yet.
XZSFS_FORMAT_V1_FROZEN = yes.
XZSFS_DETERMINISTIC_BUILD = yes.
XZSFS_HOST_STRUCTURAL_VERIFY = yes.
XZSFS_HOST_PAYLOAD_VERIFY = yes.
D5-M1_COMPLETE = yes.
D5_COMPLETE = no.
Phase D5-M2 (RAMDisk Block Transport) is NEXT.
```

* **Target Device**: Sony Xperia XZs (Model G8231 / Platform Tone / Board Keyaki)
* **SoC**: Qualcomm Snapdragon 820 (MSM8996 Pro)
* **CPU Architecture**: Quad-core Qualcomm Kryo ARMv8.0-A (2x Silver + 2x Gold)
* **Storage Device**: Samsung BJNB4R 32GB eMMC 5.1 (`CID: 150100424a4e4234520fdac7c0381400`)
* **Active Branches**: `main` (integrated), `xzs-port` (synchronized), `xzs-d4-block` (active D4 milestone)
* **Milestone Tag**: `xzs-d3-gpt-complete` (D3 sealed; `xzs-d4-block-storage-complete` pending merge)

---

## Highest Hardware-Verified Checkpoint

```text
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000000 (Enter Phase D4)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000021 (Dynamic Major Allocated: 1)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000031 (bdevsw Switch Registered)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000052 (Strategy LBA1 512B Oracle Pass)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000062 (Strategy 1024B Oracle Pass)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000090 (Phase D4-M2 Complete)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x00000000000000a0 (Phase D4-M3: Whole-Disk /dev/disk0 Published)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x00000000000000b0 (Phase D4-M4: Runtime GPT Loaded via Block Layer)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x00000000000000b1 (Phase D4-M4: 55 Partition Slices Published)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x00000000000000b2 (Phase D4-M4: Slice First/Last Sector Oracle Pass)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x00000000000000b3 (Phase D4-M4: One-Past-End Rejection Pass: EINVAL)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x00000000000000c0 (Phase D4-M5: IOKit Storage Nub Published)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x00000000000000c1 (Phase D4-M5: IOKit BSD Discovery Verified)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x00000000000000d0 (Phase D4-M6: bdevvp Block Vnode Acquired)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x00000000000000d1 (Phase D4-M6: buf_bread LBA1 Byte Match Pass)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x00000000000000d2 (Phase D4-M6: vnode_close Clean Release)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000100 (Phase D4 Complete)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000001 (Terminal Warm Reset to Fastboot)
```

The kernel confirms that Mach SMP, BSD initialization, IOKit autoconfiguration, physical eMMC persistent runtime lifecycle, dynamic `bdevsw` registration, mutex serialization, devfs disk and slice publication, C++ IOKit storage nub registry discovery, and real `bdevvp` block vnode acquisition operate genuinely on physical silicon. The block driver (`bdevsw[1]`) processes buffer transfers directly to eMMC CMD17 with 100% byte parity against independent physical oracles.

---

## What Works (Hardware Verified)

- [x] **Sony S1 Bootloader Handoff**: Android boot image loading via fastboot.
- [x] **xzs-bootshim**: Qualcomm DTB parsing, Apple Device Tree (ADT) creation at `0x81810000`, `boot_args` population at `0x81800000`.
- [x] **Low-Level ARM64 MMU**: TCR/MAIR configuration, 16KB granule, transition to High KVA (`0xfffffe0000000000`).
- [x] **Qualcomm BLSP2 UARTDM**: Serial logging at 115200 8N1 at `0x075b0000`.
- [x] **Persistent RAM / Pstore Ramoops**: Historical console path at `0xa7fbe000`. Later recovery pulls did not preserve an XNU marker, so D8 does not depend on it. Display audit is `docs/XZS_DISPLAY_BRINGUP.md`.
- [x] **ARM GICv3**: Distributor (`0x09bc0000`) and per-core Redistributors (`0x09c00000` array) in native system register mode.
- [x] **ARM Generic Timers**: PPI 27 (Virtual) & PPI 30 (Physical) firing reliably across all cores at 19.2 MHz.
- [x] **ARM PSCI v1.0 Multi-Core**: SMC `CPU_ON` (`0xC4000003`) bringing all 4 Kryo cores online.
- [x] **Mach SMP Scheduler**: All 4 cores in processor set `pset0`, running `idle_thread`, servicing reschedule IPIs via SGI 1, handling AST urgent preemption.
- [x] **Multi-Core Cache Coherency**: Inner Shareable WBWA memory, 40,000-op atomic lock contention verified (`0x9c40`).
- [x] **BSD Subsystem Bootstrap**: Process 0 (`kernproc`), credentials, zones, domains, sysctl tree.
- [x] **VFS Core Framework**: Mount lists, vnode pools (`vnodes=263168`), devfs bootstrap.
- [x] **IOKit Autoconfiguration**: `IOKitBSDInit` publishing `IOBSD` plane to BSD.
- [x] **Physical eMMC Storage Bring-up (D2)**: SDCC1 clock (400 kHz), controlled reset, power-up, CMD0..CMD17, 512-byte PIO sector read, TWRP oracle byte-for-byte match.
- [x] **GUID Partition Table Discovery & Seal (D3)**: Primary & Backup GPT Header CRC32 verified, 16-KiB entry array verified, 55 partitions enumerated, reciprocal links confirmed, 100% byte-for-byte and map-for-map match. `AUTHORITATIVE_GPT_PARTITION_MAP_VERIFIED = yes`.
- [x] **Persistent eMMC Runtime Context & Multi-Sector Pipeline (D4-M1)**: `xzs_emmc_context_t` lifecycle, idempotent initialization (`INIT_CALL_COUNT=2`, `INITIALIZATION_COUNT=1`, `INITIALIZATION_REUSE_COUNT=1`, `CONTROLLER_RESET_COUNT=1`), 64-bit multi-sector pipeline (`xzs_emmc_read_blocks_sync`), checked request ranges, zero resets between reads, 100% byte-for-byte oracle match on 4 non-contiguous sectors.
- [x] **BSD `bdevsw` Read-Only Block Device Layer (D4-M2)**: Dynamic major registration via `bdevsw_add()`, controller mutex serialization (`lck_mtx_t`), `d_open`/`d_close`/`d_strategy`/`d_ioctl`/`d_psize` handlers, genuine `buf_t` mapping and biowait completion, 512B and 1024B strategy reads matching physical oracles with 100% parity, synthetic error rejection (out-of-range, misaligned, write) issuing zero physical commands.
- [x] **Whole-Disk BSD devfs Publication (D4-M3)**: Created `/dev/disk0`, verified read-only identity, whole-disk geometry (61,071,360 512-byte blocks), write open rejected with `EROFS`, stored devfs handle.
- [x] **Runtime GPT & Partition Slices (D4-M4)**: Runtime GPT loaded via block layer (`d_strategy`), Header CRC and Array CRC dynamically verified, 55 partition slice devices published (`/dev/disk0s1`..`disk0s55`), derived minor mapping, independent TWRP oracle verification of first and last sectors of test partition (`boot`), one-past-end rejection with 0 physical commands.
- [x] **IOKit BSD Root Discovery Bridge (D4-M5)**: C++ `XZSeMMCStorageNub : public IOService` published to IOKit registry with canonical properties (`kIOBSDNameKey = "disk0"`, `kIOBSDMajorKey = 1`, `kIOBSDMinorKey = 0`), discovery verified via `IOBSDNameMatching("disk0")`, global `rootdev` NOT mutated.
- [x] **Real Block Vnode Acquisition & Read Parity (D4-M6)**: Real block vnode acquired via `bdevvp(makedev(1, 0), &vp)`, `VNOP_OPEN(FREAD)` succeeded, controlled `buf_bread` read of LBA 1 verified with 100% byte match (`0xD3A34BC1`), clean release via `vnode_close()`.
- [x] **Automated Recovery**: Warm reboot back to Fastboot within +5 seconds via Qualcomm APCS watchdog bite upon reaching diagnostic terminal state.

---

## What Is Deferred (Temporary Bring-up Workarounds)

The following non-essential subsystems are temporarily deferred to eliminate allocator and lock contention before the storage boundary:
* **Skywalk** (`skywalk_init`): Userspace networking memory arenas deferred.
* **Loopback & Tunnels** (`lo0` / `gif0`): Virtual network interfaces deferred.
* **Ethernet Family** (`ether_family_init`): DLIL ethernet registration deferred.
* **TCP Fast Open** (`tcp_fastopen = 0`): Deferred pending CoreCrypto AES registration.
* **DTrace Providers** (`fbt_init`, `profile_init`, `dtrace_postinit`): Tracing probes deferred.
* **Polled Corefiles** (`IOPOLLED_COREFILE`): Crashdump allocation deferred until storage exists.

*(Full matrix: see [`docs/XZS_WORKAROUNDS.md`](XZS_WORKAROUNDS.md))*

---

## What Does Not Exist Yet

- [ ] **Root Filesystem (D5)**: No APFS, HFS+, or ramdisk mounted at `/`.
- [ ] **Userspace Process (Phase E)**: No PID 1 (`launchd`), shell, or `/dev/console` interactive session.

---

## How to Reproduce

### 1. Build Pipeline
```bash
DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer \
make -C src/xnu \
  KERNEL_CONFIGS=DEVELOPMENT \
  ARCH_CONFIGS=ARM64 \
  MACHINE_CONFIGS=VMAPPLE \
  RC_DARWIN_KERNEL_VERSION=24.0.0 \
  build -j$(sysctl -n hw.ncpu)

./scripts/check-no-pac.sh src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple
./scripts/package-boot.sh
```

### 2. Hardware Deployment & Telemetry Verification
```bash
./scripts/run-and-extract.sh
python3 scripts/verify_d4_acceptance.py artifacts/logs/console-ramoops.log
```

### 3. Cold-Reset Note
> [!IMPORTANT]
> **Cold-Reset Procedure Required for Determinism**:
> To guarantee reproducible D1 terminal execution, always cold-reset the phone before testing:
> 1. Hold `Power + Volume Up` until the phone vibrates 3 times (full power cut).
> 2. Hold `Volume Down` and insert USB cable to enter Fastboot (blue LED).
> 3. Run `./scripts/run-and-extract.sh`.

---

## Next Technical Boundary

```text
Phase D5: Real Root Filesystem Mount
```
Mount an actual read-only root filesystem partition (ramdisk, unencrypted HFS+, or APFS container) into the VFS root vnode (`/`) and verify directory lookup.

