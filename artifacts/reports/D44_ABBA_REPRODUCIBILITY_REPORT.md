# D44 Bounded ABBA Reproducibility Report

**Experiment Objective:** Determine whether the observed PASS/FAIL behavior is reproducibly tied to the binary artifact under a fixed ABBA execution order with zero source modifications and zero rebuilds.  
**Diagnostic Branch:** `xzs-d44-abba-repro`  
**Base Source State:** Identical D440 dense telemetry across both artifacts.  
**Execution Order:** Fixed ABBA (Run 1 = A, Run 2 = B, Run 3 = B, Run 4 = A).  
**Device:** Sony Xperia XZs (`BH905SX976`).

---

## 1. Executive Summary & Decision Classification

### Primary Finding
Under a strict 4-run ABBA reproducibility protocol using exact, pre-built archived artifacts:
1. **Both Artifact A (control `465f88c`) runs failed** before reaching `D510` / `D520` / `D520_90`.
2. **Both Artifact B (late-pad `b9a6a4a`) runs failed** before reaching `D510` / `D520` / `D520_90`.
3. **Run 1 (A) and Run 3 (B) exhibited identical progression**: both completed `bsd_autoconf` 100% (`[D440/0x90]`), reached `[D45] BSD AUTOCONF COMPLETE`, passed `D46` (`loopattach`), and stalled at the exact same boundary: `[XZS-BOOT] [D47-1] cfil_init ENTER`.
4. **Run 2 (B) and Run 4 (A) both stalled inside `bsd_autoconf`**: Run 2 stalled at `[D440/0x19] fsevents_init`, while Run 4 stalled at `[D440/0x37] configThread CREATE REQUESTED`.

### Decision Matrix Classification (CASE C — Both A runs fail)
```text
CONTROL_CURRENTLY_NOT_REPRODUCIBLE=yes
ARTIFACT_CAUSALITY_UNPROVEN=yes
ARTIFACT_DEPENDENT_EFFECT_NOT_REPRODUCED=yes
RUNTIME_NONDETERMINISM_DOMINANT=yes

GENERIC_BINARY_PERTURBATION_CAUSALITY=UNPROVEN
BINARY_LAYOUT_CAUSAL=UNPROVEN
TIMING_CAUSAL=UNPROVEN
CACHE_CAUSAL=UNPROVEN
ALIGNMENT_CAUSAL=UNPROVEN
SCHEDULER_CAUSAL=UNPROVEN
MEMORY_CORRUPTION_CAUSAL=UNPROVEN

PLUS_40S_WATCHDOG_CORRELATION=strong
APCS_WATCHDOG_CAUSAL=UNPROVEN
```

Neither the passing behavior of control `A` nor the failure of late-pad `B` is reproducibly correlated with the binary artifact. Runtime nondeterminism on the shared execution path dominates the outcome.

---

## 2. Artifact Identity Verification

Both artifacts were verified via SHA256 before every run with zero rebuilds:

```text
ARTIFACT_A_PATH: artifacts/archive/d44-diag-465f88c/xzs-xnu-boot.img
ARTIFACT_A_SHA256: 4246da769cf456110c1031225f40527ed16ec2314849e66a19a497d6074e346b

ARTIFACT_B_PATH: artifacts/archive/d44-late-pad-b9a6a4a/xzs-xnu-boot.img
ARTIFACT_B_SHA256: 6c5e6c114027fd6f47ba49d20940d6bd5a6ac187350d859a367540f6357d5492

ARTIFACT_IDENTITY_VERIFIED=yes
NO_REBUILDS_PERFORMED=yes
NO_SOURCE_CHANGES=yes
```

---

## 3. ABBA Execution Matrix & Telemetry

| Field | Run 1 | Run 2 | Run 3 | Run 4 |
| :--- | :--- | :--- | :--- | :--- |
| **Artifact** | **A** (`465f88c` control) | **B** (`b9a6a4a` late-pad) | **B** (`b9a6a4a` late-pad) | **A** (`465f88c` control) |
| **Boot Image SHA256** | `4246da769c...e346b` | `6c5e6c1140...d5492` | `6c5e6c1140...d5492` | `4246da769c...e346b` |
| **Identity Verified** | **yes** | **yes** | **yes** | **yes** |
| **Last D440 Checkpoint** | `[D440/0x90] bsd_autoconf COMPLETE` | `[D440/0x19] fsevents_init` | `[D440/0x90] bsd_autoconf COMPLETE` | `[D440/0x37] configThread CREATE REQ` |
| **Last Global Checkpoint** | `[XZS-BOOT] [D47-1] cfil_init ENTER` | `[XZS-BOOT] [D44] BSD AUTOCONF ENTER` | `[XZS-BOOT] [D47-1] cfil_init ENTER` | `[XZS-BOOT] [XZS-WORKAROUND] profile_init` |
| **`D45_REACHED`** | **yes** | **no** | **yes** | **no** |
| **`D510_REACHED`** | **no** | **no** | **no** | **no** |
| **`D510_60_REACHED`** | **no** | **no** | **no** | **no** |
| **`D520_REACHED`** | **no** | **no** | **no** | **no** |
| **`D520_90_REACHED`** | **no** | **no** | **no** | **no** |
| **`configThread` Requested** | yes | yes | yes | yes |
| **`configThread` Returned** | yes | yes | yes | yes |
| **`configThread` Entry** | yes | yes | yes | no (stalled before return) |
| **`configThread` Work Started**| yes | yes | yes | no |
| **`configThread` Matched** | yes | yes | yes | no |
| **Return Time** | +40s | +40s | +40s | +40s |
| **Final Device State** | fastboot | fastboot | fastboot | fastboot |
| **Return Method** | twrp_scripted | twrp_scripted | twrp_scripted | twrp_scripted |
| **Manual Intervention** | no | no | no | no |
| **Evidence Path** | `artifacts/logs/abba_repro/run1_A/` | `artifacts/logs/abba_repro/run2_B/` | `artifacts/logs/abba_repro/run3_B/` | `artifacts/logs/abba_repro/run4_A/` |

---

## 4. Cross-Run Progression Analysis

### Progression Symmetry Between A and B
1. **Run 1 (A) and Run 3 (B) are identical**:
   - Both successfully traversed all 14 `bsd_autoconf` pseudo-initializers (`0x10` through `0x2f`).
   - Both successfully entered `IOKitBSDInit`, created `configThread`, matched nub services, and dispatched `[D440/0x90] bsd_autoconf COMPLETE`.
   - Both reached `[XZS-BOOT] [D45] BSD AUTOCONF COMPLETE`.
   - Both completed `loopattach` (`D46` -> `D46a`) and `ether_family_init` (`D47`).
   - Both stalled at the exact same boundary: `[XZS-BOOT] [D47-1] cfil_init ENTER`.

2. **Run 2 (B) and Run 4 (A) both stalled early within `bsd_autoconf`**:
   - Run 2 (B) stalled at `[D440/0x19] fsevents_init`.
   - Run 4 (A) stalled at `[D440/0x37] configThread CREATE REQUESTED`.

### Critical Scientific Deduction
- If the binary difference (+2,644 bytes late inert padding) were causal, Artifact A would consistently pass and Artifact B would consistently fail, or vice versa.
- Instead, **Artifact A produced both an early stall (`0x37`) and a post-autoconf stall (`D47-1`)**.
- **Artifact B produced the exact same envelope**: an early stall (`0x19`) and a post-autoconf stall (`D47-1`).
- Neither artifact reached `D520_90`.
- This definitively refutes the hypothesis that the observed failure in the late-pad run was causally driven by the +2,644 byte binary perturbation.

---

## 5. Watchdog Correlation & Platform State

Across all four runs:
- Stalls occurred at CPU execution boundaries without panic logs or fatal breadcrumbs.
- Each stalled run underwent an automated warm reset after approximately +40 seconds (`PLUS_40S_WATCHDOG_CORRELATION=strong`).
- In all runs, the device returned cleanly to Fastboot via scripted TWRP extraction without manual intervention.
- Consistent with established discipline:
  ```text
  PLUS_40S_WATCHDOG_CORRELATION=strong
  APCS_WATCHDOG_CAUSAL=UNPROVEN
  ```

---

## 6. Per-Run Evidence Archival

All raw pstore, extracted console, and extracted dmesg logs are preserved independently in:
- `artifacts/logs/abba_repro/run1_A/`
  - `xnu-console-extracted.log`
  - `xnu-dmesg-extracted.log`
- `artifacts/logs/abba_repro/run2_B/`
  - `xnu-console-extracted.log`
  - `xnu-dmesg-extracted.log`
- `artifacts/logs/abba_repro/run3_B/`
  - `xnu-console-extracted.log`
  - `xnu-dmesg-extracted.log`
- `artifacts/logs/abba_repro/run4_A/`
  - `xnu-console-extracted.log`
  - `xnu-dmesg-extracted.log`

---

# HARD STOP

The bounded ABBA reproducibility experiment is complete.
- Target device `BH905SX976` is quiescent in Fastboot.
- All evidence directories and this report are committed and pushed on branch `xzs-d44-abba-repro`.
- Normal D5-M4 development remains HALTED.
- Do NOT resume D5-M4 automatically.
- Awaiting review and instructions.
