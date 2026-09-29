# XNU Xperia XZs — D8-M8 F7: Pure Sony Cold-Boot DCS Order A/B
## Post-Kick TE & Command Frame Investigation
### Milestone D8-M8 — Phase F7 Investigation & Hardware Proof Report

---

## 1. Executive Summary

Milestone D8-M8 Phase F7 conducted an isolated, single-variable causal A/B test on physical Xperia XZs hardware (`BH905SX976`) to determine whether adopting the authentic Sony LittleKernel (LK) cold-boot DCS command sequence:
```text
TEON (0x35 0x00) → DISPON (0x29) → SLPOUT (0x11) + 120 ms
```
in place of the inherited baseline sequence:
```text
SLPOUT (0x11) + 120 ms → TEON (0x35 0x00) → DISPON (0x29)
```
restores post-kick physical Tearing Effect (TE) synchronization pulses and unlatches the Ping-Pong (`PP0`) line counter under XNU.

All electrical power rails (LAB `+5.6V`, IBB `-5.6V`, VDDIO `1.8V`), panel reset (`GPIO8`), and in-cell touch reset (`GPIO89`) timing were strictly preserved from the hardware-verified F2/F5 baseline.

### Primary Experimental Conclusion:
```text
F7_CLASS = F7-D DCS_ORDER_NOT_SUFFICIENT

DID_DCS_ORDER_RESTORE_POST_KICK_TE = NO_HW_PROVEN
DID_DCS_ORDER_START_PP_FRAME       = NO_HW_PROVEN
```

The pure Sony cold-boot DCS lifecycle ordering is a **real binary divergence (`BINARY_PROVEN`)**, but on physical hardware it is **NOT sufficient (`HW_PROVEN`)** to cause the panel DDIC to emit TE or unblock scanout.

---

## 2. Hardware Run & Build Identity

| Parameter | Value | Evidence Level |
|---|---|---|
| **Target Device** | Sony Xperia XZs (`BH905SX976`, Tone platform, MSM8996) | `HW_PROVEN` |
| **Branch** | `xzs-d8-display-m8-resume` | `REPO_PROVEN` |
| **Base Commit** | `bfd7539c4ba5ee12d97b30f6f27c4e2dd4d92eb4` (`xzs-d8m8-deferred`) | `REPO_PROVEN` |
| **F7 Functional Commit** | `b253a45ae8bcdaf8c7029d23d4c1631805750800` | `REPO_PROVEN` |
| **Kernel Image SHA256** | `4802a77aa80756fd2b56a9c7a3fdd6382c703d242b945282674275f2eabcd6e5` | `BUILD_PROVEN` |
| **Boot Image SHA256** | `b532c66d6c1707f665a08f22a5e584b4ba876bb0e4a6fcc8e432f08db19d2c31` | `BUILD_PROVEN` |
| **Pointer Authentication (PAC)** | `0` executable instructions (`100% ARMv8.0-A compliant`) | `AUDIT_PROVEN` |
| **Flash Policy** | Strictly **NO FLASH** (RAM-only `fastboot boot`) | `HW_PROVEN` |
| **Artifact Directory** | [`artifacts/hw/d8m8/f7-dcs-order-ab/`](file:///Users/lechaukha12/Desktop/xnu-xzs/artifacts/hw/d8m8/f7-dcs-order-ab/) | `FS_PROVEN` |

---

## 3. Power Precheck & DCS Transaction Audit

Before issuing any DSI command packets, hardware readback validated the electrical state:

| Signal / Rail | Expected | Observed Hardware Value | Status |
|---|---|---|---|
| Panel Reset (`GPIO8`) | HIGH (`1`) | `HIGH` | `PASS` |
| Touch Reset (`GPIO89`) | HIGH (`1`) | `HIGH` | `PASS` |
| Touch VDDIO (`GPIO50`) | HIGH (`1`) | `HIGH` | `PASS` |
| Panel VDDIO (`GPIO51`) | HIGH (`1`) | `HIGH` | `PASS` |
| PMIC LAB Rail (`+5.6V`) | `STATUS1_VREG_OK` (`0xa0`) | `0xa0` (`VREG_OK=1`) | `PASS` |
| PMIC IBB Rail (`-5.6V`) | `STATUS1_VREG_OK` (`0x80`) | `0x80` (`VREG_OK=1`) | `PASS` |
| **POWER_PRECHECK** | `PASS` | `PASS` | `PASS` |

### DCS Command Execution

Commands were transmitted strictly in Sony LK cold-boot order:

1. **`TEON` (`0x35 0x00`)**:
   - `len = 8`, `paddr = 0x831c2340`, bytes: `[ 02 00 39 c0 35 00 ff ff ]`
   - `elapsed_us = 39`, `ACK_ERR = 0x00000000`, `TIMEOUT = 0x00000000`
   - `TEON_ACK = PASS`, `TEON_SENT = yes`
2. **`DISPON` (`0x29`)**:
   - `len = 4`, `paddr = 0x831c2340`, bytes: `[ 29 00 05 80 ]`
   - `elapsed_us = 39`, `ACK_ERR = 0x00000000`, `TIMEOUT = 0x00000000`
   - `DISPON_ACK = PASS`, `DISPON_SENT = yes`
3. **`SLPOUT` (`0x11`) + 120 ms Settle**:
   - `len = 4`, `paddr = 0x831c2340`, bytes: `[ 11 00 05 80 ]`
   - `elapsed_us = 39`, `ACK_ERR = 0x00000000`, `TIMEOUT = 0x00000000`
   - `post_wait = 120000 us` (120 ms)
   - `SLPOUT_ACK = PASS`, `SLPOUT_SENT = yes`
4. **Stability Gate**:
   - `F7_PANEL_PREPARE_STABLE = YES` (zero PMIC brownout, zero SError, system alive and fully responsive).

---

## 4. Mandatory A/B Evidence Table

| Signal / Parameter | F5 Safe Baseline | F7 DCS-Order Correction | Result Classification |
|---|---:|---:|---|
| **Panel Reset Timing** | Low 10ms → High 10ms | Low 10ms → High 10ms | `IDENTICAL` |
| **Touch Reset Timing** | Low 2ms → High 5ms → 40ms | Low 2ms → High 5ms → 40ms | `IDENTICAL` |
| **GPIO50 / GPIO51** | `1.8V` HIGH | `1.8V` HIGH | `IDENTICAL` |
| **LAB / IBB Rails** | `+5.6V` / `-5.6V` | `+5.6V` / `-5.6V` | `IDENTICAL` |
| **MDP Clock Rate** | `171.428571 MHz` (`CFG=0x506`) | `171.428571 MHz` (`CFG=0x506`) | `IDENTICAL` |
| **PP Golden Timing Image** | Match | Match | `IDENTICAL` |
| **DSI Static Config** | `0x06100006`, `0x80000004` | `0x06100006`, `0x80000004` | `IDENTICAL` |
| **DCS Order** | `SLPOUT → TEON → DISPON` | **`TEON → DISPON → SLPOUT`** | **`SOLE FUNCTIONAL VARIABLE`** |
| **CTL_START** | `1` | `1` | `IDENTICAL` |
| **WR_PTR Asserted** | `YES` (`0x00010000`) | `YES` (`0x00010000`) | `REPRODUCED` |
| **Observation Window** | `175294 us` | **`254371 us`** (>15 frame intervals) | `FULL OBSERVATION` |
| **Fresh RD_PTR Post-Kick** | `NO` | **`NO`** (`count = 0`) | `HW_PROVEN` |
| **Counter Backward Jump / Reload** | `NO` | **`NO`** (`jumps = 0`) | `HW_PROVEN` |
| **PP_LINE Counter** | `0` | **`0`** (`max = 0x0`) | `HW_PROVEN` |
| **PP_OUT Counter** | `0` | **`0`** (`max = 0x0`) | `HW_PROVEN` |
| **PP0_DONE** | `NO` | **`NO`** | `HW_PROVEN` |
| **DSI_MDP_BUSY** | `NO` | **`NO`** | `HW_PROVEN` |
| **CMD_MDP_DONE** | `NO` | **`NO`** | `HW_PROVEN` |

---

## 5. Telemetry & Post-Kick Observation Trace

During the post-commit observation window spanning **254,371 µs** (over 15 nominal 60-Hz frames):
- **15,351 discrete hardware register polls** were sampled in memory.
- `CTL_START = 1` was written exactly once.
- `CTL_FLUSH` was consumed from `0x00020048` to `0x00000000` at timestamp `+172 µs` (`C1160`).
- `RGB0_CURRENT_SRC0_ADDR` transitioned from `0x00000000` to `0x98000000` at `+172 µs` (`C1160`).
- Ping-Pong write pointer (`WR_PTR`, bit 16) asserted immediately (`MDP_INTR = 0x00010000`) and remained asserted across all 15,351 samples.
- The internal timing counter (`PP_INT_COUNT_VAL`) advanced monotonically from `0x00009e1c` to `0x0000f9f3`.
- **Zero backward jumps** occurred (`PP_COUNTER_BACKWARD_JUMPS = 0`, `PP_COUNTER_LARGEST_NEG_DELTA = 0`).
- **Zero fresh RD_PTR assertions** occurred (`FRESH_RD_PTR_AFTER_CTL_START = NO`, `F2_PP0_RD_PTR_COUNT = 0`).
- `PP_LINE` and `PP_OUT` remained strictly `0`.
- The panel was cleanly and gracefully powered down via DCS `DISPOFF` (0x28), `SLPIN` (0x10), and PMIC power collapse (`R11C_SAFE_SHUTDOWN = PASS`).

---

## 6. Required Executive Output

```text
F7_CLASS=F7-D DCS_ORDER_NOT_SUFFICIENT

BRANCH=xzs-d8-display-m8-resume

BASE_COMMIT=bfd7539c4ba5ee12d97b30f6f27c4e2dd4d92eb4

F7_FUNCTIONAL_COMMIT=b253a45ae8bcdaf8c7029d23d4c1631805750800

COMMIT_BOOTED=b253a45ae8bcdaf8c7029d23d4c1631805750800

KERNEL_SHA256=4802a77aa80756fd2b56a9c7a3fdd6382c703d242b945282674275f2eabcd6e5

BOOT_SHA256=b532c66d6c1707f665a08f22a5e584b4ba876bb0e4a6fcc8e432f08db19d2c31

PAC=0

POWER_PRECHECK=PASS

GPIO8_PRE_DCS=HIGH

GPIO89_PRE_DCS=HIGH

GPIO50_PRE_DCS=HIGH

GPIO51_PRE_DCS=HIGH

LAB_READY=YES

IBB_READY=YES

DCS_SEQUENCE=TEON -> DISPON -> SLPOUT

TEON_ACK=PASS

DISPON_ACK=PASS

SLPOUT_ACK=PASS

F7_PANEL_PREPARE_STABLE=YES

PP_GOLDEN_CONFIG_READBACK=PASS

MDP_RATE_CONFIRMED=YES

CMD_MDP_CTRL_BASELINE_MATCH=YES

PRE_CLEAR_INTR_STATUS=0x00001000

POST_CLEAR_INTR_STATUS=0x00000000

F7_POST_CLEAR_PASS=YES

CTL_START_COUNT=1

MDP_KICKOFF_COUNT=1

OBSERVATION_WINDOW_US=254371

POLL_ITERATIONS=15351

PP_INT_COUNT_MIN=0x00009e1c

PP_INT_COUNT_MAX=0x0000f9f3

PP_COUNTER_BACKWARD_JUMPS=0

PP_COUNTER_LARGEST_NEG_DELTA=0

PP_COUNTER_RELOAD_SEEN=NO

FRESH_RD_PTR_AFTER_CTL_START=NO

PP0_WR_PTR_SEEN=YES

PP_LINE_NONZERO=NO

PP_LINE_MAX=0x00000000

PP_OUT_NONZERO=NO

PP_OUT_MAX=0x00000000

PP0_DONE_SEEN=NO

DSI_MDP_BUSY_SEEN=NO

CMD_MDP_DONE_SEEN=NO

DID_DCS_ORDER_RESTORE_POST_KICK_TE=NO_HW_PROVEN

DID_DCS_ORDER_START_PP_FRAME=NO_HW_PROVEN

FARTHEST_PIPELINE_STAGE_REACHED=CTL_START_FLUSH_CONSUMED_NO_TE

D8_M8_FIRST_COMMAND_FRAME=NOT_YET

R11C_SAFE_SHUTDOWN=PASS

NEXT_ACTION=EVALUATE_F7_OUTCOME
```

---

## 7. Strategic Conclusions & Next Steps

1. **Definitive Elimination of DCS Order as Active Root Cause**:
   - Sony LK executes `TEON → DISPON → SLPOUT + 120ms` (`BINARY_PROVEN`).
   - However, implementing this sequence in XNU under identical hardware-safe power/reset conditions **did not restore post-kick TE** (`DID_DCS_ORDER_RESTORE_POST_KICK_TE = NO_HW_PROVEN`).
   - Therefore, while the DCS order divergence was authentic, **it is NOT the root cause preventing panel scanout**. DCS order is eliminated as an active blocker.

2. **Downstream Pipeline Shift**:
   - The hardware boundary remains firmly localized at:
     ```text
     CTL_START written (0x1)
             ↓
     CTL_FLUSH consumed (0x00020048 → 0x00000000)
             ↓
     RGB0_CURRENT_SRC0_ADDR latched (0x98000000)
             ↓
     PP0 WR_PTR asserted (0x00010000)
             ↓
     [ WAITING FOR READ POINTER / TE SYNC / DSI HANDSHAKE ]
     ```
   - Future work must focus on whether DDIC active scanout requires specific panel initialization parameters in DCS, or whether the tearcheck hardware engine admission requires downstream trigger dispatch.
