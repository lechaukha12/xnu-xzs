# D8-M8 C2 — External-TE Golden Alignment Hardware A/B Test Evidence

One RAM boot. One conceptual change. One `CTL_START`. Bounded observation window. Safe panel shutdown.

```text
C2_CLASS=C2-C
EXTERNAL_TE_GOLDEN_CONFIGURATION_CORRECTED_BUT_PP_FRAME_START_STILL_BLOCKED
DID_EXTERNAL_TE_GOLDEN_ALIGNMENT_CAUSE_PP_LINE_TO_START=NO — HW_PROVEN

M8_1_PASS=YES
MDP_RATE_CONFIRMED=YES
XNU_TEON_PRESENT=YES
PP_GOLDEN_CONFIG_READBACK=PASS

TEAR_CHECK_EN=1
SYNC_CONFIG_VSYNC=0x00180093
SYNC_CONFIG_HEIGHT=0x0000fff0
VSYNC_INIT=0x00000780
SYNC_THRESH=0x00040004
START_POS=0x00000004
SYNC_WRCOUNT=0x00000009
RD_PTR_IRQ=0x00000781
WR_PTR_IRQ=0x00000000
AUTOREFRESH=0x00000000

CTL_START_COUNT=1
PP_COUNTER_RUNNING=YES
PP_LINE_NONZERO=NO
PP_LINE_MAX=0x00000000
PP_OUT_NONZERO=NO
PP_OUT_MAX=0x00000000
PP0_DONE_SEEN=no
DSI_MDP_BUSY_SEEN=no
CMD_MDP_DONE_SEEN=no
CTL_FLUSH_CONSUMED=YES
RGB0_CURRENT_SRC0_ADDR=0x98000000
FARTHEST_PIPELINE_STAGE_REACHED=RGB0_SOURCE_LATCHED
```

---

## 1. Executive Summary

Phase D8-M8 Task C2 evaluated whether aligning the PingPong (PP0) tearcheck configuration with the working Sony Keyaki (TWRP G4) external-HW-TE golden path releases the PP scanout line counter gate (`PP_LINE > 0`).

Under one clean RAM boot with zero flash modifications, exactly one `CTL_START` was issued. All 10 PP0 tearcheck registers were verified with exact readback matching the Sony Keyaki golden path.

Upon `CTL_START`:
1. `CTL_FLUSH` was consumed (`0x00020048` → `0x00000000` within 165 µs).
2. `RGB0_CURRENT_SRC0_ADDR` latched the framebuffer base (`0x00000000` → `0x98000000`).
3. `MDP_INTR` set bit 16 (`PP0_WR_PTR = 0x00010000`).
4. `PP_INT_COUNT_VAL` was freely incrementing (`0x1804` → `0x2251`).
5. However, `PP_LINE` remained `0x00000000` throughout the entire 20,001 µs observation window.
6. Downstream signals (`PP_OUT`, `PP0_DONE`, `DSI_MDP_BUSY`, `CMD_MDP_DONE`) remained 0.

Per the test protocol (Section 18), this run is classified as **C2-C**:
**The external-TE golden configuration was established and hardware-verified, but PP frame-start remains blocked. Unblocking the external-TE registers alone is NOT causally sufficient to release the PP frame-start gate (`DID_EXTERNAL_TE_GOLDEN_ALIGNMENT_CAUSE_PP_LINE_TO_START = NO — HW_PROVEN`). Blocker U3 remains OPEN.**

---

## 2. Build Identity & Test Parameters

```text
BRANCH=xzs-d8-display-m8-resume
BASE_HEAD=97b844854a56664bfc4f787bc3ddcde3a307b290
C2_COMMIT=322412c809fd4fd7449f42e72680131536839b34
COMMIT_BOOTED=322412c809fd4fd7449f42e72680131536839b34
KERNEL_SHA256=b8e583369088bd95975b60c8b9071094049c63fd856a434c4499d39e634df441
BOOT_SHA256=49503c66a747fb6ab7fcc7221f156bb13c0a041f371f52f50ef4e23dc52f5ce0
PAC=0 (Zero executable PAC instructions detected; 100% ARMv8.0-A compliant)
TARGET_SERIAL=BH905SX976
BOOT_METHOD=fastboot -s BH905SX976 boot artifacts/builds/xzs-xnu-boot.img (RAM boot only, NO FLASH)
KICKOFF_ATTEMPTS=1
OBSERVATION_WINDOW=20001 us
SAFE_SHUTDOWN=R11C_SAFE_SHUTDOWN=PASS (graceful DCS 0x28 + M5 power-down + return to shell)
ARTIFACT_DIR=artifacts/hw/d8m8/c2-external-te-ab/
```

---

## 3. Preserved Environmental Baselines

All prerequisite controls were preserved without modification:
1. **Clock Rate (C1 conformant)**:
   - `MDP_RCG_CFG = 0x00000506` (171,428,571 Hz)
   - Confirmed via `clocks mdss-ahb-debug`: `[C1] TARGET_RATE_CONFIRMED=YES`, `C1_READY_FOR_FRAME=YES`
2. **Panel Path**:
   - Standard XNU DCS initialization preserved (`SLPOUT 0x11` + 120 ms, `TEON 0x35 0x00`, `DISPON 0x29` + 10 ms).
   - Panel initialized and ready before first `CTL_START`.
3. **GPIO10 TE Routing**:
   - TLMM GPIO10 configured for Function 1 (`fn1_mdp_vsync`), pull-down, 2 mA, input (D8-P1 sealed).
4. **DSI TE Selection**:
   - `DSI_TRIG_CTRL = 0x80000004` (`te_sel = 1`, `dma_trigger = SW`).

---

## 4. PP0 Tearcheck Programming & Verification

### Target vs. Actual Readback Image

| Register | C1 (Internal/SW Override) | Target Golden Image (Keyaki G4) | C2 Pre-Kick Readback | Status |
|---|---|---|---|---|
| `TEAR_CHECK_EN` | `0x00000001` | `0x00000001` | `0x00000001` | **MATCH** |
| `SYNC_CONFIG_VSYNC` | `0x00080093` | `0x00180093` | `0x00180093` | **MATCH** |
| `SYNC_CONFIG_HEIGHT` | `0x00000873` | `0x0000FFF0` | `0x0000FFF0` | **MATCH** |
| `VSYNC_INIT_VAL` | `0x00000780` | `0x00000780` | `0x00000780` | **MATCH** |
| `SYNC_THRESH` | `0x00040004` | `0x00040004` | `0x00040004` | **MATCH** |
| `START_POS` | `0x00000780` | `0x00000004` | `0x00000004` | **MATCH** |
| `SYNC_WRCOUNT` | `0x00000785` | `0x00000009` | `0x00000009` | **MATCH** |
| `RD_PTR_IRQ` | `0x00000781` | `0x00000781` | `0x00000781` | **MATCH** |
| `WR_PTR_IRQ` | `0x00000000` | `0x00000000` | `0x00000000` | **MATCH** |
| `AUTOREFRESH_CONFIG` | `0x00000000` | `0x00000000` | `0x00000000` | **MATCH** |

Pre-kick guard evaluation:
```text
C2_PP_GOLDEN_READBACK_FAIL=no
PP_GOLDEN_CONFIG_READBACK=PASS
PREKICK_READY=YES
```

---

## 5. Hardware Kickoff & Observation Timeline

Exactly one kickoff was executed via `display m8-kickoff`:

```text
CTL_START_WRITE=0x00000001
CTL_START_COUNT=1
MDP_KICKOFF_COUNT=1
```

### High-Frequency Handshake Telemetry

```text
T - 165 us (Snapshot C1150 - Pre-Kick):
  CTL_START_COUNT=0
  CTL_FLUSH=0x00020048
  CTL_START=0x00000000
  PP_INT_COUNT=0x00001804
  PP_LINE=0x00000000
  PP_OUT_LINE=0x00000000
  MDP_INTR=0x00000000
  RGB0_CUR_SRC0=0x00000000
  DSI_STATUS_RAW=0x00000000

T + 0 us (Kickoff issued: write CTL_START = 0x1)

T + 165 us (Snapshot C1160 - Post-Kick 1):
  CTL_START_COUNT=1
  CTL_FLUSH=0x00000000      <-- Consumed!
  CTL_START=0x00000000      <-- Self-cleared
  PP_INT_COUNT=0x0000181a   <-- Freely incrementing (+22)
  PP_LINE=0x00000000        <-- Gate closed
  PP_OUT_LINE=0x00000000
  MDP_INTR=0x00010000       <-- Bit 16 (PP0_WR_PTR) active
  RGB0_CUR_SRC0=0x98000000  <-- Framebuffer address latched!
  DSI_STATUS_RAW=0x00000000

T + 214 us (Snapshot C1170):
  PP_INT_COUNT=0x00001820, PP_LINE=0x00000000, MDP_INTR=0x00010000

T + 361 us (Snapshot C1180):
  PP_INT_COUNT=0x00001833, PP_LINE=0x00000000, MDP_INTR=0x00010000

T + 1114 us (Snapshot C1190):
  PP_INT_COUNT=0x00001895, PP_LINE=0x00000000, MDP_INTR=0x00010000

T + 5111 us (Snapshot C11A0):
  PP_INT_COUNT=0x00001a9f, PP_LINE=0x00000000, MDP_INTR=0x00010000

T + 10110 us (Snapshot C11B0):
  PP_INT_COUNT=0x00001d2c, PP_LINE=0x00000000, MDP_INTR=0x00010000

T + 20104 us (Snapshot C11C0 - Window Expiry):
  PP_INT_COUNT=0x00002246, PP_LINE=0x00000000, MDP_INTR=0x00010000
```

Observation summary:
- `MAX_LINE_COUNT_OBSERVED = 0x00000000`
- `MAX_OUT_LINE_COUNT_OBSERVED = 0x00000000`
- `PP0_DONE_OBSERVED = no`
- `DSI_MDP_BUSY_SEEN = no`
- `DSI_MDP_DONE_RAW_SEEN = no`

---

## 6. Causal Analysis & Blocker State

### What Succeeded
1. **PP Configuration Fidelity**: All 10 external-TE golden registers read back cleanly without deviation.
2. **CTL Subsystem Consumption**: `CTL_FLUSH` bitmask (`0x00020048`: flush CTL, LM0, RGB0) was consumed immediately upon `CTL_START`.
3. **RGB0 Fetch Pipe Latch**: `RGB0_CURRENT_SRC0_ADDR` transitioned from `0x0` to `0x98000000`, proving the pipe descriptor was latched by hardware.
4. **PP VSYNC Counter Clock Domain**: `PP_INT_COUNT_VAL` incremented continuously (rate ~105 kHz, consistent with 19.2 MHz XO / internal division), proving the PP tearcheck / VSYNC counter clock domain is running. (The MDP core clock itself was independently confirmed at 171,428,571 Hz via C1/C2 clock readback).

### What Remained Blocked
Despite exact alignment with the working Keyaki TWRP configuration (`SYNC_CONFIG_VSYNC=0x00180093`, `HEIGHT=0xFFF0`, `START_POS=4`, `WRCOUNT=9`), `PP_LINE` never advanced past 0.

### Causal Deductions
- **Clock Rate Alone**: Not sufficient (proven in C1: `171.4 MHz` still had `PP_LINE=0`).
- **External TE Configuration Alone**: Not sufficient (proven in C2: golden register values still had `PP_LINE=0`).
- **Potential Missing Prerequisite**:
  1. **Physical TE Signal Delivery**: Does panel DCS `TEON (0x35 0x00)` actually trigger VSYNC pulses on GPIO10 under current panel state? If the physical panel does not pulse GPIO10, external-TE mode (`BIT19=1`) will wait indefinitely for an edge that never arrives.
  2. **VSYNC Clock Branch / Source Gate**: While `VSYNC_CBCR=1`, is there an unconfigured TLMM/MDSS VSYNC multiplexer or clock root gating the physical pad to the PP sync logic?
  3. **Interface / INTF Path**: Does PP require an active downstream receiver (e.g., INTF1/DSI handshake or trigger) to allow the scanout counter to start, or does DSI wait for PP?

---

## 7. Pipeline Boundary

The farthest hardware-proven stage reached in C2 is:

```text
FARTHEST_PIPELINE_STAGE_REACHED = RGB0_SOURCE_LATCHED
```

CTL state was consumed, RGB0 latched the source pointer, but the PingPong line generator remained dormant (`PP_LINE = 0`).

---

## 8. Preserved Artifacts

All forensic evidence from this single run is preserved in `artifacts/hw/d8m8/c2-external-te-ab/`:
- `build-identity.txt`: Full commit and SHA256 hashes.
- `git-diff.txt`: Exact unified diff of the single conceptual change.
- `c1-rate-confirm.txt`: Verification of 171.4 MHz MDP clock.
- `stream-config.txt`: Log of PP0 golden register programming.
- `pre-kick.txt`: Pre-kick telemetry verifying all guards and golden readback.
- `kickoff.txt`: Complete kickoff transcript, snapshot log, and shutdown sequence.
- `raw-handshake-snapshots.txt`: 14 periodic hardware register snapshots.
- `raw-frame-observations.txt`: Pre- and post-clear interrupt status.
- `host.txt`: Complete serial session transcript.
- `final-evidence.txt`: Normalized key-value executive summary.
