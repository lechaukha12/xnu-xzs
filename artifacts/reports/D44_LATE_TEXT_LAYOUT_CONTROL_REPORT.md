# D44 Same-Size / Different-Placement Control Report

**Base Commit:** `465f88cffbf7b48f2803d70182fa193bc288b896`  
**Late-Pad Diagnostic Commit:** `b9a6a4a40879942a781b0a514d87455d315904d9`  
**Investigation Objective:** Distinguish whether the failure reproduced in the early-pad experiment was caused by downstream BSD/IOKit address displacement vs generic kernel-size / text-growth perturbation, by placing the exact same inert text delta (+2,644 bytes) strictly LATE in the link order.

---

## 1. Executive Summary & Decision Classification

In this experiment:
1. **Base source is identical to passing control `465f88c`** (D440 dense telemetry preserved exactly).
2. **Zero M4 filesystem code, zero M4 symbols, and zero M4 semantics are linked**.
3. **Zero mutable globals or static constructors are added** (`MUTABLE_GLOBAL_DELTA_BYTES = 0`, `CXX_NEW_CONSTRUCTOR_COUNT = 0`).
4. **Exactly 2,644 bytes of inert text padding** (`661 nop` instructions in a naked C function) are placed at the end of `src/xnu/security/mac_label.c` (the terminal translation unit of `__TEXT_EXEC,__text`), strictly after the entire BSD and IOKit diagnostic regions.
5. **All critical BSD and IOKit symbol addresses are identical to control** (`ADDRESS_DELTA = 0`).

### Silicon Outcome (Case B — FAIL)
In silicon execution on device `BH905SX976`:
- `bsd_autoconf()` (`D44`) **PASSED COMPLETELY**: All `D440` checkpoints from `0x00` to `0x90` were dispatched.
- `_IOConfigThread` creation, wake, entry, work start, and matching completed successfully (`CONFIG_THREAD_ENTRY_REACHED=yes`).
- `D45` (`[D45] BSD AUTOCONF COMPLETE`) and `D50` (`ROOT DEVICE SELECTION ENTER`) were reached.
- Root device matching succeeded (`IOFindBSDRoot RETURN err=0x0, rootdev=md0`).
- Execution progressed into RAMDisk verification (`0xD510`) up to `0xD510/0x53` (`Step 5: Full Logical Image Read & CRC32 Verification`), where execution halted.
- The device underwent an automated warm reset after ~40s (`PLUS_40S_WATCHDOG_CORRELATION=strong`), returning to Fastboot.
- `D520_90` was **NOT REACHED** (`D520_90_REACHED = no`).

### Decision Classification Matrix (Case B — FAIL)
```text
D520_90_REACHED=no

CRITICAL_D44_CODE_ADDRESSES_IDENTICAL_TO_CONTROL=yes

GENERIC_BINARY_PERTURBATION_EFFECT_SUPPORTED=yes
CRITICAL_CODE_ADDRESS_DISPLACEMENT_NOT_REQUIRED_FOR_FAILURE=yes

M4_SEMANTICS_REQUIRED_FOR_FAILURE=no_evidence
BINARY_LAYOUT_CAUSAL=UNPROVEN
CACHE_CAUSAL=UNPROVEN
ALIGNMENT_CAUSAL=UNPROVEN
TIMING_CAUSAL=UNPROVEN
SCHEDULER_CAUSAL=UNPROVEN
MEMORY_CORRUPTION_CAUSAL=UNPROVEN
PLUS_40S_WATCHDOG_CORRELATION=strong
APCS_WATCHDOG_CAUSAL=UNPROVEN
```

---

## 2. Static Layout & 3-Way Symbol Comparison

### Critical Symbol Comparison: Control vs Early-Pad vs Late-Pad

| Symbol | Control (`465f88c`) | Early-Pad (`15eb0cd`) | Late-Pad (`b9a6a4a`) | Late Delta vs Control |
| :--- | :--- | :--- | :--- | :--- |
| `kernel_thread_start` | `0xfffffe0006630d08` | `0xfffffe0006630d08` | `0xfffffe0006630d08` | **`+0`** |
| `kernel_thread_create` | `0xfffffe0006630ab0` | `0xfffffe0006630ab0` | `0xfffffe0006630ab0` | **`+0`** |
| `thread_start` | `0xfffffe00066334a4` | `0xfffffe00066334a4` | `0xfffffe00066334a4` | **`+0`** |
| `thread_create_internal`| `0xfffffe000662f138` | `0xfffffe000662f138` | `0xfffffe000662f138` | **`+0`** |
| `bsd_autoconf` | `0xfffffe0006b93668` | `0xfffffe0006b940bc` | `0xfffffe0006b93668` | **`+0`** |
| `IOServiceJob::pingConfig` | `0xfffffe0006e4ea30` | `0xfffffe0006e4f484` | `0xfffffe0006e4ea30` | **`+0`** |
| `_IOConfigThread::configThread`| `0xfffffe0006e52f8c` | `0xfffffe0006e539e0` | `0xfffffe0006e52f8c` | **`+0`** |
| `_IOConfigThread::main` | `0xfffffe0006e530f4` | `0xfffffe0006e53b48` | `0xfffffe0006e530f4` | **`+0`** |
| `IOKitBSDInit` | `0xfffffe0006f1c22c` | `0xfffffe0006f1cc80` | `0xfffffe0006f1c22c` | **`+0`** |
| `xzs_inert_layout_pad` | `N/A` | `0xfffffe00068b556c` | `0xfffffe0006f93298` | **`N/A`** |

```text
CRITICAL_D44_CODE_ADDRESSES_IDENTICAL_TO_CONTROL=yes
LATE_PAD_PLACEMENT_PROVEN=yes
```

### Section Sizes: Control vs Early-Pad vs Late-Pad

| Segment, Section | Control Size | Early-Pad Size | Late-Pad Size | Delta vs Control |
| :--- | :--- | :--- | :--- | :--- |
| `__TEXT_EXEC,__text` | `0xa9a8a8` (11,118,760) | `0xa9b2fc` (11,121,404) | `0xa9b2fc` (11,121,404) | **`+2,644 bytes` (+0xa54)** |
| `__DATA,__data` | `0x32268` (205,416) | `0x32268` (205,416) | `0x32268` (205,416) | **`+0`** |
| `__DATA,__bss` | `0x741b0` (475,568) | `0x741b0` (475,568) | `0x741b0` (475,568) | **`+0`** |
| `__BOOTDATA,__data` | `0x18000` (98,304) | `0x18000` (98,304) | `0x18000` (98,304) | **`+0`** |

### Proof of Late Placement from Mach-O
- Total symbols before `xzs_inert_layout_pad` (`0xfffffe0006f93298`): **60,428 symbols**, all with `ADDRESS_DELTA = 0`.
- All symbols in `osfmk/`, `bsd/`, `iokit/`, and `pexpert/` have `ADDRESS_DELTA = 0`.
- Only symbols after `0xfffffe0006f93298` (in terminal `security/` module `mac_label.c`) moved by `+2,644`:
  - `0xfffffe0006f93298 -> 0xfffffe0006f93cec (+2644): _mac_labelzone_alloc`
  - `0xfffffe0006fe8880 -> 0xfffffe0006fe92d4 (+2644): _panic_label_set_sentinel`

### Binary Invariants
```text
M4_SYMBOL_COUNT=0
CXX_NEW_CONSTRUCTOR_COUNT=0
C_STATIC_INITIALIZER_DELTA=0
MUTABLE_GLOBAL_DELTA_BYTES=0
BSS_DELTA_BYTES=0
KERNEL_PHYSICAL_SPAN_UNCHANGED=yes
RESERVED_MEMORY_OVERLAP_FOUND=no
```

---

## 3. Build & Packaging Artifacts

### Quality Gates
```text
HOST_XZSFS_TESTS=PASS (13/13 negative tests rejected; host oracle passed)
KERNEL_BUILD=PASS
PAC_INSTRUCTION_COUNT=0
GIT_DIFF_CHECK=PASS
```

### Artifact Hashes
```text
CONTROL_KERNEL_SHA256:
730c886b53a502a056305f474327192f915ac5023a289ac798eb37ced619512e

LATE_PAD_KERNEL_SHA256:
6c9e795e7b82ed51e73d5e7e628521040a78e9c05049c4bc5bf41924fbd033ad  src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple

LATE_PAD_KERNEL_FLAT_SHA256:
6293f6e893e73aa7ee3d51406a996ac2bf8c5fc806851a24598410a7c6c84cea  artifacts/builds/kernel.flat

LATE_PAD_BOOT_IMAGE_SHA256:
6c5e6c114027fd6f47ba49d20940d6bd5a6ac187350d859a367540f6357d5492  artifacts/builds/xzs-xnu-boot.img

ARCHIVE_PATH:
artifacts/archive/d44-late-pad-b9a6a4a/
```

---

## 4. Silicon Run Telemetry (Single Run)

```text
LAST_D440_CHECKPOINT=[D440/0x90] bsd_autoconf COMPLETE
LAST_OLD_CHECKPOINT=[XZS-RAMDISK] PHASE D5-M2-R8: FINAL SEAL ACCEPTANCE PROBE (0xD510/0x53)

CONFIG_THREAD_CREATE_REQUESTED=yes
CONFIG_THREAD_OBJECT_RETURNED=yes
CONFIG_THREAD_ENTRY_REACHED=yes
CONFIG_THREAD_WORK_STARTED=yes
CONFIG_THREAD_SERVICE_MATCH_COMPLETED=yes

D45_REACHED=yes
D510_REACHED=yes
D520_REACHED=no
D520_90_REACHED=no
```

### Execution Progression Trace
1. **Early Kernel & Block Storage (D1–D4)**: All completed with zero errors.
2. **BSD Autoconf (`bsd_autoconf`)**:
   - `[D440/0x00] bsd_autoconf ENTER` dispatched.
   - `kminit` (`0x01` -> `0x02`): PASS.
   - Pseudo-initializers (`0x10` -> `0x2f`): All 14 pseudo-inits completed cleanly (including `fsevents_init` `0x19` -> `0x1a`).
   - `IOKitBSDInit` (`0x30` -> `0x3c`): PASS.
   - `IOServiceJob::pingConfig` and `_IOConfigThread`:
     - `[D440/0x37] configThread CREATE REQUESTED`: YES.
     - `[D440/0x38] configThread OBJECT RETURNED`: YES.
     - `[D440/0x40] configThread ENTRY REACHED`: YES (CPU=1 TH=0xfffffe25359b0000).
     - Matching jobs completed asynchronously.
   - `[D440/0x90] bsd_autoconf COMPLETE`: YES.
3. **Post-Autoconf Initialization (`D45`–`D49`)**:
   - `[XZS-BOOT] [D45] BSD AUTOCONF COMPLETE`: YES.
   - `loopattach`, `ether_family_init`, `cfil_init`, `net_init_run`, `inittodr`: ALL PASS.
4. **Root Device Selection (`D50`)**:
   - `[XZS-BOOT] [D50] ROOT DEVICE SELECTION ENTER`: YES.
   - `[XZS-BOOT] [D50b] IOFindBSDRoot RETURN err=0x0`: YES.
   - `[XZS-BOOT] [D50d] rootdev major=0x2 minor=0x0 rootdevice=md0`: YES.
5. **RAMDisk Transport Probe (`D510`)**:
   - Dispatched breadcrumbs `0xD510/0x00`, `0x10`, `0x40`, `0x50`, `0x51`, `0x52`, `0x53`.
   - Stalled inside Step 5 (`0xD510/0x53`: 70-sector read loop).
   - Execution halted; automated warm reset returned the device to Fastboot at +40s (`PLUS_40S_WATCHDOG_CORRELATION=strong`).

---

## 5. Comparative Matrix: 4-Way Progression Analysis

| Metric / Checkpoint | Passing Control (`465f88c`) | Failing M4-Linked (`a84e2e7`) | Early-Pad (`15eb0cd`) | Late-Pad (`b9a6a4a`) |
| :--- | :--- | :--- | :--- | :--- |
| **M4 Code Linked** | No | Yes | No | **No** |
| **M4 Semantics / Flags** | None | `VFS_TBLCANMOUNTROOT`, `xzsfs_mount` | None | **None** |
| **Total `__TEXT_EXEC` Delta** | Baseline (`+0`) | `+2,644 bytes` | `+2,644 bytes` | **`+2,644 bytes`** |
| **`bsd_autoconf` Delta** | `+0` | `+2,644 bytes` | `+2,644 bytes` | **`+0` (Identical)** |
| **`_IOConfigThread` Delta** | `+0` | `+2,644 bytes` | `+2,644 bytes` | **`+0` (Identical)** |
| **`IOKitBSDInit` Delta** | `+0` | `+2,644 bytes` | `+2,644 bytes` | **`+0` (Identical)** |
| **D44 Progress** | `D440/0x90` PASS | Stalled at `0x37` | Stalled at `0x19` | **`D440/0x90` PASS** |
| **`D45` Reached** | Yes | No | No | **Yes** |
| **`D50` Reached** | Yes | No | No | **Yes** |
| **Deepest Boundary** | `D520_90` | `D440/0x37` | `D440/0x19` | **`D510/0x53`** |
| **`D520_90` Reached** | **Yes** | **No** | **No** | **No** |
| **Silicon Classification** | Control Baseline | M4 Perturbation | Early Layout Perturbation | **Generic Binary Perturbation** |

---

## 6. Key Scientific Findings

1. **Restoring Critical BSD/IOKit Addresses Completely Restores `bsd_autoconf`**:
   - In both `a84e2e7` (M4-linked) and `15eb0cd` (early-pad), where BSD and IOKit symbols were displaced by `+2,644 bytes`, execution stalled *inside* `bsd_autoconf()` (`0x37` and `0x19` respectively).
   - In `b9a6a4a` (late-pad), where the exact same `+2,644` bytes were added but critical BSD/IOKit symbols remained at `ADDRESS_DELTA = 0`, `bsd_autoconf()` **passed 100%**, dispatching all checkpoints up to `D440/0x90` and reaching `D45` and `D50`.
   - This proves that the specific stall *inside* `bsd_autoconf()` in the early-pad and M4-linked runs was directly sensitive to code placement/displacement within the BSD/IOKit region.

2. **Generic Binary Perturbation Still Prevents Reaching `D520_90`**:
   - Despite restoring all critical D44 code addresses to match the passing control, the late-pad binary still failed to reach `D520_90`, stalling at `0xD510/0x53`.
   - Therefore, by the decision matrix:
     ```text
     GENERIC_BINARY_PERTURBATION_EFFECT_SUPPORTED=yes
     CRITICAL_CODE_ADDRESS_DISPLACEMENT_NOT_REQUIRED_FOR_FAILURE=yes
     ```
   - The latent failure boundary is broader than the specific BSD/IOKit text displacement.

3. **Causal Invariants Maintained**:
   - Specific microarchitectural mechanisms remain unproven:
     ```text
     CACHE_CAUSAL=UNPROVEN
     ALIGNMENT_CAUSAL=UNPROVEN
     TIMING_CAUSAL=UNPROVEN
     MEMORY_CORRUPTION_CAUSAL=UNPROVEN
     ```
   - Normal D5-M4 filesystem development remains HALTED.

---

# HARD STOP

The D44 same-size / different-placement control experiment is complete.
- Target device `BH905SX976` is quiescent in Fastboot.
- Branch `xzs-d44-late-pad-diag` is committed and pushed.
- Normal D5-M4 development remains HALTED.
- Awaiting review and instructions.
