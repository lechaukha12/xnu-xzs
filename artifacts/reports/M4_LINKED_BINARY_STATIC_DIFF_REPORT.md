# M4-LINKED BINARY STATIC DIFF & LINK-LAYOUT AUDIT REPORT

**Date:** 2026-09-20  
**Target Hardware:** Sony Xperia XZs (G8231 / MSM8996 / Kryo)  
**Control Passing Binary:** `artifacts/archive/d44-diag-465f88c/kernel.development.vmapple` (Commit `465f88c`)  
**Experiment Failing Binary:** `artifacts/archive/d44-m4-linked-a84e2e7/kernel.development.vmapple` (Commit `a84e2e748a2cbae6c0e84ed47ed04c79523ea4f5`)  
**Analysis Objective:** Static Mach-O link-layout and binary differential audit comparing the passing control against the failing M4-linked binary (M4 execution disabled) to isolate layout and static-state deltas without modifying source or executing hardware.

---

## 1. Canonical Result Classification

```text
CONTROL_DIAGNOSTIC_COMMIT=465f88c
CONTROL_RESULT=D520_90_PASS

M4_LINKED_DIAGNOSTIC_COMMIT=a84e2e748a2cbae6c0e84ed47ed04c79523ea4f5

D440_INSTRUMENTATION_IDENTICAL=yes
M4_CODE_LINKED=yes
M4_EXECUTION_ENABLED=no

D530_CHECKPOINT_COUNT=0
XZSFS_VFS_MOUNT_INVOKED=no

CONFIG_THREAD_CREATE_REQUESTED=yes
CONFIG_THREAD_OBJECT_RETURNED=no

KERNEL_THREAD_START_RETURNED=no

M4_LINKED_BINARY_PERTURBATION_OBSERVED=yes
BINARY_LAYOUT_CAUSAL=UNPROVEN
SCHEDULER_CAUSAL=UNPROVEN
MEMORY_CORRUPTION_CAUSAL=UNPROVEN
STATIC_INITIALIZATION_CAUSAL=UNPROVEN
TIMING_OR_LAYOUT_SENSITIVITY=possible
```

> [!NOTE]
> The observed stall boundary is inside `kernel_thread_start(...)` during `IOKitBSDInit()`, but that is not yet the demonstrated root cause.

---

## 2. Kernel File Size & Global VA Bounds

```text
CONTROL_KERNEL_FILE_SIZE=21860440 bytes
EXPERIMENT_KERNEL_FILE_SIZE=21861752 bytes
KERNEL_FILE_SIZE_DELTA=+1312 bytes

CONTROL_KERNEL_FLAT_SIZE=23019520 bytes (0x015F4000)
EXPERIMENT_KERNEL_FLAT_SIZE=23019520 bytes (0x015F4000)
KERNEL_FLAT_SIZE_DELTA=+0 bytes

CONTROL_KERNEL_BASE_VA=0xfffffe0006200000
CONTROL_KERNEL_END_VA=0xfffffe00077f4000

EXPERIMENT_KERNEL_BASE_VA=0xfffffe0006200000
EXPERIMENT_KERNEL_END_VA=0xfffffe00077f4000
```

---

## 3. Mach-O Segment & Section Comparison

### Segment-Level Comparison
All segments in this Mach-O kernel are page/segment-aligned (16 KB granularity). Segment virtual spans and memory bounds are identical between Control and Experiment:

| Segment | Control VMAddr | Control VMSize | Exp VMAddr | Exp VMSize | VM Delta | Control FileOff | Exp FileOff | Control FileSize | Exp FileSize | FileSize Delta |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `__TEXT` | `0xfffffe0006200000` | `0x19c000` | `0xfffffe0006200000` | `0x19c000` | `+0` | `0x0` | `0x0` | `1687552` | `1687552` | `+0` |
| `__DATA_CONST` | `0xfffffe000639c000` | `0x1b0000` | `0xfffffe000639c000` | `0x1b0000` | `+0` | `0x19c000` | `0x19c000` | `1769472` | `1769472` | `+0` |
| `__TEXT_EXEC` | `0xfffffe000654c000` | `0xaa0000` | `0xfffffe000654c000` | `0xaa0000` | `+0` | `0x34c000` | `0x34c000` | `11141120` | `11141120` | `+0` |
| `__KLD` | `0xfffffe0006fec000` | `0x4000` | `0xfffffe0006fec000` | `0x4000` | `+0` | `0xdec000` | `0xdec000` | `16384` | `16384` | `+0` |
| `__LASTDATA_CONST` | `0xfffffe0006ff0000` | `0x4000` | `0xfffffe0006ff0000` | `0x4000` | `+0` | `0xdf0000` | `0xdf0000` | `16384` | `16384` | `+0` |
| `__KLDDATA` | `0xfffffe0006ff4000` | `0x4000` | `0xfffffe0006ff4000` | `0x4000` | `+0` | `0xdf4000` | `0xdf4000` | `16384` | `16384` | `+0` |
| `__DATA` | `0xfffffe0006ff8000` | `0x16c000` | `0xfffffe0006ff8000` | `0x16c000` | `+0` | `0xdf8000` | `0xdf8000` | `344064` | `344064` | `+0` |
| `__BOOTDATA` | `0xfffffe0007164000` | `0xdc000` | `0xfffffe0007164000` | `0xdc000` | `+0` | `0xe4c000` | `0xe4c000` | `901120` | `901120` | `+0` |
| `__LINKINFO` | `0xfffffe0007240000` | `0x50000` | `0xfffffe0007240000` | `0x50000` | `+0` | `0xf28000` | `0xf28000` | `327680` | `327680` | `+0` |
| `__LINKEDIT` | `0xfffffe0007290000` | `0x564000` | `0xfffffe0007290000` | `0x564000` | `+0` | `0xf78000` | `0xf78000` | `5640280` | `5641592` | `+1312` |

### Internal Section-Level Deltas
While segment boundaries remained fixed, the internal section sizes and positions changed:

| Segment, Section | Control Start VA | Control Size | Exp Start VA | Exp Size | Addr Delta | Size Delta | Analysis |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `__TEXT,__const` | `0xfffffe00062016c0` | `0x39d50` | `0xfffffe00062016c0` | `0x39d60` | `+0` | `+16 B` | M4 string/const pointers |
| `__TEXT,__cstring` | `0xfffffe000623b410` | `0x11b594` | `0xfffffe000623b420` | `0x11bdf3` | `+16 B` | `+2,143 B` | M4 telemetry/format string additions |
| `__TEXT,__copyio_vectors` | `0xfffffe00063569a4` | `0x138` | `0xfffffe0006357213` | `0x138` | `+2,159 B` | `+0` | Displaced by `__cstring` |
| `__TEXT,__os_log` | `0xfffffe0006356adc` | `0x412bd` | `0xfffffe000635734b` | `0x412bd` | `+2,159 B` | `+0` | Displaced by `__cstring` |
| `__DATA_CONST,__sdt` | `0xfffffe0006514018` | `0x14f58` | `0xfffffe0006514018` | `0x14fa0` | `+0` | `+72 B` | SDT probe descriptors |
| `__DATA_CONST,__kalloc_type` | `0xfffffe0006528f70` | `0x16c00` | `0xfffffe0006528fb8` | `0x16c00` | `+72 B` | `+0` | Displaced by `__sdt` |
| `__DATA_CONST,__mod_init_func` | `0xfffffe0006549558` | `0x310` | `0xfffffe00065495a0` | `0x310` | `+72 B` | `+0` | **Exact 98 constructor pointers, 0 added** |
| `__TEXT_EXEC,__text` | `0xfffffe000654e000` | `0xa9a8a8` | `0xfffffe000654e000` | `0xa9b2fc` | `+0` | `+2,644 B` | **M4 executable text expansion** |
| `__TEXT_EXEC,__commpage_text` | `0xfffffe0006fe88a8` | `0x2dc` | `0xfffffe0006fe92fc` | `0x2dc` | `+2,644 B` | `+0` | Displaced by `__text` |
| `__DATA,__common` | `0xfffffe0007049000` | `0xa52d4` | `0xfffffe0007049000` | `0xa52d4` | `+0` | `+0` | M4 added 8 bytes; absorbed by alignment |

---

## 4. Symbol Displacement Audit

The kernel link order places Mach kernel subsystems (`osfmk/`) before BSD subsystems (`bsd/`), which in turn sit before IOKit (`iokit/`).

Consequently:
- All `osfmk/` symbols (including `kernel_thread_start` and scheduler internals) remain at **identical virtual addresses** (Delta = +0).
- All `bsd/` symbols located after `xzsfs` and all `iokit/` symbols are displaced by **exactly +2,644 bytes**.

| Symbol | Control Address | Experiment Address | Address Delta | Subsystem |
| :--- | :--- | :--- | :--- | :--- |
| `kernel_thread_start` | `0xfffffe0006630d08` | `0xfffffe0006630d08` | **`+0`** | `osfmk/kern` |
| `kernel_thread_create` | `0xfffffe0006630ab0` | `0xfffffe0006630ab0` | **`+0`** | `osfmk/kern` |
| `thread_start` | `0xfffffe00066334a4` | `0xfffffe00066334a4` | **`+0`** | `osfmk/kern` |
| `thread_create_internal` | `0xfffffe000662f138` | `0xfffffe000662f138` | **`+0`** | `osfmk/kern` |
| `thread_resume` | `0xfffffe00066341b8` | `0xfffffe00066341b8` | **`+0`** | `osfmk/kern` |
| `thread_create_with_continuation` | `0xfffffe000663028c` | `0xfffffe000663028c` | **`+0`** | `osfmk/kern` |
| `xzs_breadcrumb` | `0xfffffe0006554b44` | `0xfffffe0006554b44` | **`+0`** | `osfmk/arm64` |
| `xzs_spin_halt` | `0xfffffe0006554a2c` | `0xfffffe0006554a2c` | **`+0`** | `osfmk/arm64` |
| `_xzsfs_d5m3_probe` | `0xfffffe00068b3728` | `0xfffffe00068b3728` | **`+0`** | `bsd/xzsfs` (Last unshifted) |
| `_xzsfs_get_vnode` | `0xfffffe00068b48e0` | `0xfffffe00068b516c` | **`+2188`** | `bsd/xzsfs_node.c` |
| `_xzsfs_mount` | `0xfffffe00068b5108` | `0xfffffe00068b59bc` | **`+2228`** | `bsd/xzsfs_vfsops.c` |
| `bsd_autoconf` | `0xfffffe0006b93668` | `0xfffffe0006b940bc` | **`+2644`** | `bsd/kern` |
| `xzs_d440_crumb` | `0xfffffe0006b94208` | `0xfffffe0006b94c5c` | **`+2644`** | `bsd/kern` |
| `_IOServiceJob::pingConfig` | `0xfffffe0006e4ea30` | `0xfffffe0006e4f484` | **`+2644`** | `iokit/Kernel` |
| `_IOConfigThread::configThread` | `0xfffffe0006e52f8c` | `0xfffffe0006e539e0` | **`+2644`** | `iokit/Kernel` |
| `_IOConfigThread::main` | `0xfffffe0006e530f4` | `0xfffffe0006e53b48` | **`+2644`** | `iokit/Kernel` |
| `IOKitBSDInit` | `0xfffffe0006f1c22c` | `0xfffffe0006f1cc80` | **`+2644`** | `iokit/bsddev` |

---

## 5. M4 Introduced-Symbol & Global-State Audit

### Symbol Classification
Every symbol newly introduced or significantly altered by M4 was audited from the symbol table:

| Symbol Name | Classification | Virtual Address | Section / Scope | Notes |
| :--- | :--- | :--- | :--- | :--- |
| `_xzsfs_d5m4_probe` | `FUNCTION` | `0xfffffe00068b48e0` | `__TEXT_EXEC,__text` (Global) | Phase D5-M4 acceptance probe (never called) |
| `_xzs_early_putdec` | `FUNCTION` | `0xfffffe00068b5088` | `__TEXT_EXEC,__text` (Static) | Decimal formatter helper (never called) |
| `_xzsfs_mount` | `FUNCTION` | `0xfffffe00068b59bc` | `__TEXT_EXEC,__text` (Global) | Full VFS mount (expanded from stub, never called) |
| `_xzsfs_root_vnode_create_count` | `MUTABLE_GLOBAL` | `0xfffffe00070ae2b0` | `__DATA,__common` (Global) | 4-byte counter in `xzsfs_node.c` (value = 0) |
| `_xzsfs_root_vnode_reclaim_count` | `MUTABLE_GLOBAL` | `0xfffffe00070ae2b4` | `__DATA,__common` (Global) | 4-byte counter in `xzsfs_node.c` (value = 0) |
| `_xzsfs_mount.kalloc_type_view_40` | `CONST_DATA` | `0xfffffe000652e178` | `__DATA_CONST,__kalloc_type` | Typed kalloc metadata view |
| `_xzsfs_mount.kalloc_type_view_156`| `CONST_DATA` | `0xfffffe000652e1b8` | `__DATA_CONST,__kalloc_type` | Typed kalloc metadata view |
| `_xzsfs_unmount.kalloc_type_view_195`| `CONST_DATA`| `0xfffffe000652e1f8` | `__DATA_CONST,__kalloc_type` | Typed kalloc metadata view |

### Global State & Static Initializer Counts
```text
CXX_GLOBAL_CONSTRUCTOR_COUNT=0
C_STATIC_INITIALIZER_COUNT=0
MUTABLE_GLOBAL_BYTES=8
BSS_DELTA_BYTES=0

M4_PRE_D530_EXECUTION_PATH_FOUND=no
```

- **Constructor Verification**: `__DATA_CONST,__mod_init_func` contains exactly 98 function pointers in both Control and Experiment; zero entries added.
- **Startup Entry Verification**: `__BOOTDATA,__init_entry_set` is identical in size (`0x1a6d0`); zero entries added.
- **Pre-D530 Execution**: Zero static initializers or constructors execute before `bsd_init` reaches D530.

---

## 6. Memory-Range Collision Audit

Physical and virtual memory ranges of the kernel compared against all reserved XZS bring-up memory regions:

```text
CONTROL_KERNEL_PHYSICAL_RANGE=[0x82200000, 0x837F4000)
EXPERIMENT_KERNEL_PHYSICAL_RANGE=[0x82200000, 0x837F4000)

CONTROL_KERNEL_VIRTUAL_RANGE=[0xfffffe0006200000, 0xfffffe00077f4000)
EXPERIMENT_KERNEL_VIRTUAL_RANGE=[0xfffffe0006200000, 0xfffffe00077f4000)
```

| Region | Physical Range | Allocated By / Purpose | Kernel Overlap? |
| :--- | :--- | :--- | :--- |
| **Persistent DRAM Telemetry** | `[0x80060000, 0x80061000)` | `uart.c` / `g_dlog` debug log | **`no`** |
| **Bootshim Code / Data** | `[0x80080000, 0x80090000)` | `start.S` / `linker.ld` | **`no`** |
| **XZSFS RAMDisk (Payload)** | `[0x81700000, 0x81709000)` | Embedded rootfs image (36 KB) | **`no`** |
| **Boot Arguments (`boot_args`)**| `[0x81800000, 0x81801000)` | Bootshim struct boot_args | **`no`** |
| **Apple Device Tree (ADT)** | `[0x81810000, 0x81820000)` | Bootshim synthesized ADT | **`no`** |
| **Device Tree Blob (DTB)** | `[0x82000000, 0x8218683c)` | Android bootimg tags offset | **`no`** |
| **Kernel Flattened Image** | `[0x82200000, 0x837F4000)` | Mach-O linear memory image | **Self** |
| **Kernel Allocation Ceiling** | `topOfKernelData = 0x83A00000` | 24 MB kernel headroom (`+2.04 MB` slack) | **`no`** |
| **Ramoops / pstore dmesg** | `[0xa7f00000, 0xa7f01000)` | Linux pstore panic dmesg | **`no`** |
| **Ramoops / pstore console** | `[0xa7fbe000, 0xa7ffe000)` | Linux pstore console log | **`no`** |

```text
KERNEL_OVERLAP_WITH_BOOTSHIM=no
KERNEL_OVERLAP_WITH_DTB=no
KERNEL_OVERLAP_WITH_TELEMETRY=no
KERNEL_OVERLAP_WITH_RAMDISK=no
KERNEL_OVERLAP_WITH_RESERVED_MEMORY=no
```

---

## 7. Link-Order Audit & M4 Object Size Table

All 5 XZSFS object files were already present in `src/xnu/bsd/conf/files` in D5-M3. Link order is fixed by the BSD makefile:
1. `xzsfs_core.o`
2. `xzsfs_parser.o`
3. `xzsfs_node.o`
4. `xzsfs_vnops.o`
5. `xzsfs_vfsops.o`

### `M4_OBJECT_SIZE_TABLE`

| Object File | Role | Text Delta (Bytes) | Data Delta (Bytes) | BSS Delta (Bytes) | New Symbols Introduced |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `xzsfs_core.o` | Immutable parser engine | `+0` | `+0` | `+0` | `0` |
| `xzsfs_parser.o`| Diagnostic probes (`d5m3`, `d5m4`) | `+2,188` | `+0` | `+0` | `_xzsfs_d5m4_probe`, `_xzs_early_putdec` |
| `xzsfs_node.o` | Vnode lifecycle & counters | `+40` | `+8` (`__common`) | `+0` | `_xzsfs_root_vnode_create_count`, `_xzsfs_root_vnode_reclaim_count` |
| `xzsfs_vnops.o` | VFS VNOP operations | `+0` | `+0` | `+0` | `0` (symbols shifted by `+2228`) |
| `xzsfs_vfsops.o`| Mount/unmount VFS ops | `+416` | `+0` | `+0` | `0` (updated flags & mount logic) |
| **Total M4 Delta** | — | **`+2,644`** | **`+8`** | **`+0`** | **`4 non-local symbols`** |

---

## 8. Binary Diff Classification

```text
KERNEL_SIZE_DELTA_BYTES=+1312 (Mach-O file size), +0 (Physical flat memory size)
TEXT_DELTA_BYTES=+2644
DATA_DELTA_BYTES=+0 (Segment span unchanged; +8 B __common internal)
BSS_DELTA_BYTES=+0

CRITICAL_SYMBOL_DISPLACEMENT_OBSERVED=yes
RESERVED_MEMORY_OVERLAP_FOUND=no
PRE_D530_STATIC_EXECUTION_FOUND=no
```

---

## 9. Next Minimal Isolation Experiment Matrix

To isolate which portion of the linked delta induces the perturbation, we define 3 single-variable compilable groups (execution hooks remain strictly disabled in all groups):

```text
GROUP 1: xzsfs_node delta only
FILES: src/xnu/bsd/xzsfs/xzsfs_node.c, src/xnu/bsd/xzsfs/xzsfs.h
NEW_TEXT_BYTES=40
NEW_DATA_BYTES=8
NEW_BSS_BYTES=0
EXECUTION_HOOKS_DISABLED=yes
OBJECTIVE: Test whether 8 bytes of mutable globals in __common and 40 bytes in node.c alter runtime behavior.

GROUP 2: xzsfs_vfsops delta only
FILES: src/xnu/bsd/xzsfs/xzsfs_vfsops.c
NEW_TEXT_BYTES=416
NEW_DATA_BYTES=0
NEW_BSS_BYTES=0
EXECUTION_HOOKS_DISABLED=yes
OBJECTIVE: Test whether VFS_TBLCANMOUNTROOT registration flag and expanded mount function body alter runtime behavior without d5m4_probe.

GROUP 3: xzsfs_parser delta only (with node counters)
FILES: src/xnu/bsd/xzsfs/xzsfs_parser.c, src/xnu/bsd/xzsfs/xzsfs_node.c, src/xnu/bsd/xzsfs/xzsfs.h
NEW_TEXT_BYTES=2228
NEW_DATA_BYTES=8
NEW_BSS_BYTES=0
EXECUTION_HOOKS_DISABLED=yes
OBJECTIVE: Test whether the large text addition (+2,188 bytes) in parser.c induces perturbation in the absence of vfsops changes.
```

---

## 10. Special Control: Inert Size / Layout Perturbation Feasibility

### Feasibility Assessment: **HIGHLY FEASIBLE**
We can construct a pure layout control without any XZSFS code changes:
- **Concept:** Take the passing D440 control (`465f88c`) and insert an inert, never-executed block of `nop` instructions or dead data totaling exactly **2,644 bytes** in `__TEXT_EXEC` (e.g., in a dedicated static function in `bsd_init.c`).
- **Data Counterpart:** Add an 8-byte dummy integer in `__DATA,__common`.
- **Diagnostic Value:**
  - If the inert +2,644 byte padded kernel **fails at `[D440/0x37]`**, it proves conclusively that the failure is an **architectural layout / cache / alignment sensitivity** affecting `_IOConfigThread::main` or `kernel_thread_start`, completely independent of any filesystem logic or registration flags.
  - If the inert padded kernel **passes**, it proves that the perturbation is tied specifically to the M4 code contents (such as `VFS_TBLCANMOUNTROOT` in `vfs_fsadd`, or table pointer alignment).

---

# HARD STOP

The static binary and link-layout audit is complete.
- **No silicon run performed.**
- **No source code modified.**
- **Normal D5-M4 development remains halted.**

Awaiting user review and direction.
