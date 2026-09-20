# D44 EXACT TEXT-LAYOUT PERTURBATION CONTROL REPORT

**Date:** 2026-09-20  
**Target Hardware:** Sony Xperia XZs (G8231 / MSM8996 / Kryo)  
**Diagnostic Branch:** `xzs-d44-layout-pad-diag`  
**Base Passing Control:** `465f88c` (Result: `D520_90_PASS`)  
**Experiment Commit:** `15eb0cd5b6ef22e84179bc3e284a6a5e1286c06b`  
**Investigation Objective:** Isolate downstream `__TEXT_EXEC` displacement (`+2,644 bytes`) as an independent variable without introducing any M4 filesystem semantics, mutable globals, static initializers, or registration flags.

---

## 1. Executive Summary & Decision Classification

Under a pure text-layout control experiment where:
1. **Base source is identical to passing control `465f88c`** (D440 dense telemetry preserved exactly).
2. **Zero M4 filesystem code, zero M4 symbols, and zero M4 semantics are linked**.
3. **Zero mutable globals or static constructors are added** (`MUTABLE_GLOBAL_DELTA_BYTES = 0`, `CXX_NEW_CONSTRUCTOR_COUNT = 0`).
4. **Exactly 2,644 bytes of inert text padding** (`661 nop` instructions in a naked C function) are placed at the end of `xzsfs_vfsops.c`, reproducing the exact `+2,644` byte downstream displacement observed in the failing M4-linked binary (`a84e2e7`).

### Silicon Outcome (Case A — FAIL)
The inert layout-control kernel **stalled during BSD Autoconf (`bsd_autoconf()`)** inside `fsevents_init` (`[D440/0x19]`), failing to reach `D45`, `D510`, or `D520`:
- Checkpoint `[D440/0x19] fsevents_init ENTER` was dispatched.
- Stalled inside `fsevents_internal_init` during zone allocation / initial filling.
- Device halted at +1.46s until an automated reset returned the device to Fastboot at +45s (`PLUS_40S_WATCHDOG_CORRELATION=strong`).

### Classification Matrix (Case A — FAIL)
```text
D520_90_REACHED=no

GENERIC_TEXT_LAYOUT_PERTURBATION_REPRODUCES_FAILURE=yes
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

## 2. Static Layout & Symbol Displacement Proof

### Section Sizes: Control vs Layout Control
```text
Control __TEXT_EXEC,__text size: 0xa9a8a8 (11,118,760 bytes)
New     __TEXT_EXEC,__text size: 0xa9b2fc (11,121,404 bytes)
TEXT_DELTA_VS_CONTROL=+2644 bytes
```

### Critical Symbol Displacement Gate
All symbols were audited from the linked Mach-O binary (`kernel.development.vmapple`) before silicon execution:

| Symbol | Control Address | Layout-Pad Address | Observed Delta | Expected Delta | Gate Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `kernel_thread_start` | `0xfffffe0006630d08` | `0xfffffe0006630d08` | **`+0`** | `+0` | **PASS** |
| `kernel_thread_create` | `0xfffffe0006630ab0` | `0xfffffe0006630ab0` | **`+0`** | `+0` | **PASS** |
| `thread_start` | `0xfffffe00066334a4` | `0xfffffe00066334a4` | **`+0`** | `+0` | **PASS** |
| `thread_create_internal` | `0xfffffe000662f138` | `0xfffffe000662f138` | **`+0`** | `+0` | **PASS** |
| `bsd_autoconf` | `0xfffffe0006b93668` | `0xfffffe0006b940bc` | **`+2644`** | `+2644` | **PASS** |
| `IOServiceJob::pingConfig` | `0xfffffe0006e4ea30` | `0xfffffe0006e4f484` | **`+2644`** | `+2644` | **PASS** |
| `_IOConfigThread::configThread`| `0xfffffe0006e52f8c` | `0xfffffe0006e539e0` | **`+2644`** | `+2644` | **PASS** |
| `_IOConfigThread::main` | `0xfffffe0006e530f4` | `0xfffffe0006e53b48` | **`+2644`** | `+2644` | **PASS** |
| `IOKitBSDInit` | `0xfffffe0006f1c22c` | `0xfffffe0006f1cc80` | **`+2644`** | `+2644` | **PASS** |

```text
LAYOUT_MATCHES_M4_TEXT_DISPLACEMENT=yes
```

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
LAYOUT_PAD_DIAGNOSTIC_COMMIT=15eb0cd5b6ef22e84179bc3e284a6a5e1286c06b

KERNEL_SHA256:
8340f267548f5e085729c33e90520cfc1aa0abff511a92c63df8beb39303228e  src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple

KERNEL_FLAT_SHA256:
2d0ce163e5074dc7bc2196e4ce301a54ebdd3d1d90e0abae4cf58374a105adea  artifacts/builds/kernel.flat

BOOT_IMAGE_SHA256:
341c102880f7140bc0f9ae70d6ab32f1303f1efff3c0493dfe9eba75a7bf9444  artifacts/builds/xzs-xnu-boot.img

ARCHIVE_PATH:
artifacts/archive/d44-layout-pad-15eb0cd/
```

---

## 4. Silicon Run Telemetry (Single Run)

```text
LAST_D440_CHECKPOINT=[D440/0x19] fsevents_init
LAST_OLD_CHECKPOINT=[D44] BSD AUTOCONF ENTER

CONFIG_THREAD_CREATE_REQUESTED=no
CONFIG_THREAD_OBJECT_RETURNED=no
CONFIG_THREAD_ENTRY_REACHED=no
CONFIG_THREAD_WORK_STARTED=no
CONFIG_THREAD_SERVICE_MATCH_COMPLETED=no

D45_REACHED=no
D510_REACHED=no
D520_REACHED=no
D520_90_REACHED=no
```

### Execution Trace Details
1. Kernel early boot, D1-D4 block storage, and BSD initialization progressed normally up to `[D44] BSD AUTOCONF ENTER`.
2. `bsd_autoconf` entered (`0xD440/0x00`).
3. `kminit` executed and returned (`0xD440/0x01` → `0xD440/0x02`).
4. Pseudo-initializers:
   - `pty_init` (`0x11` → `0x12`): PASS
   - `ptmx_init` (`0x13` → `0x14`): PASS
   - `mdevinit` (`0x15` → `0x16`): PASS
   - `bpf_init` (`0x17` → `0x18`): PASS
   - `fsevents_init` (`0x19`): **STALL** inside `fsevents_internal_init`.
5. No further instructions executed.
6. Automated reset triggered after ~40s, returning the device to Fastboot (`PLUS_40S_WATCHDOG_CORRELATION=strong`).

---

## 5. Comparative Matrix: Control vs M4-Linked vs Layout-Control

| Metric / Checkpoint | Passing Control (`465f88c`) | Failing M4-Linked (`a84e2e7`) | Inert Layout Control (`15eb0cd`) |
| :--- | :--- | :--- | :--- |
| **M4 Code Linked** | No | Yes | **No** |
| **M4 Semantics / Flags** | None | `VFS_TBLCANMOUNTROOT`, `xzsfs_mount` | **None** |
| **Downstream Text Displacement** | Baseline (`+0`) | `+2,644 bytes` | **`+2,644 bytes` (Exact match)** |
| **Mach Kernel Addresses (`osfmk/`)**| Baseline (`+0`) | `+0` | **`+0`** |
| **`bsd_autoconf` Address** | `0xfffffe0006b93668` | `0xfffffe0006b940bc` (`+2644`) | **`0xfffffe0006b940bc` (`+2644`)** |
| **`IOKitBSDInit` Address** | `0xfffffe0006f1c22c` | `0xfffffe0006f1cc80` (`+2644`) | **`0xfffffe0006f1cc80` (`+2644`)** |
| **Silicon Result** | **D520_90 PASS** | **D44 STALL (`0x37`)** | **D44 STALL (`0x19`)** |
| **Classification** | Control Baseline | M4-Perturbation Observed | **Generic Layout Perturbation Reproduces Failure** |

---

## 6. Key Scientific Findings

1. **Text Layout Displacement Alone Reproduces Autoconf Failure**:
   - The inert layout control contained **zero lines of M4 code**, zero filesystem registration flags, and zero new mutable variables.
   - Merely introducing 2,644 bytes of inert `nop` instructions into `__TEXT_EXEC` to displace downstream BSD/IOKit symbols by `+2,644 bytes` was sufficient to cause execution to stall inside `bsd_autoconf()`.
   - This provides direct evidence that **M4 filesystem logic is NOT required to produce the failure**.

2. **Autoconf Stalls at Nondeterministic Boundaries Under Layout Displacement**:
   - In historical uninstrumented D5-M3: stalled at `[D44] BSD AUTOCONF`.
   - In M4-linked binary (`a84e2e7`): stalled at `[D440/0x37] configThread CREATE REQUESTED`.
   - In inert layout-pad binary (`15eb0cd`): stalled at `[D440/0x19] fsevents_init`.
   - In all three failing cases, the failure occurs within `bsd_autoconf()` prior to `D45` / `D510` / `D520`.

3. **Causal Interpretation**:
   - `M4_SEMANTICS_REQUIRED_FOR_FAILURE=no_evidence`
   - `GENERIC_TEXT_LAYOUT_PERTURBATION_REPRODUCES_FAILURE=yes`
   - The specific root cause mechanism (instruction cache alignment, branch displacement, scheduler timing, or memory corruption sensitivity) remains `UNPROVEN`.

---

# HARD STOP

The exact text-layout perturbation control experiment is complete.
- Target device is quiescent in Fastboot (`BH905SX976`).
- Normal D5-M4 development remains halted.
- Awaiting review and instructions.
