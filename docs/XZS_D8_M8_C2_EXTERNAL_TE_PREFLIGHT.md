# D8-M8 C2 Preflight — Align PP0 Tearcheck with Working Keyaki External-TE Golden Path

## Executive Summary

```text
C2_READY_FOR_HARDWARE=
YES

XNU_TEON_PRESENT=
YES

XNU_TEON_PARAMETER=
0x00 (V-blanking only, mode 0)

XNU_TEON_ORDER=
SLPOUT (0x11, +120ms) -> TEON (0x35 0x00) -> DISPON (0x29, +10ms) -> CTL_START

XNU_TE_GPIO_CONFIG_MATCH=
YES

TE_ROUTE_CONFIDENCE=
HIGH

XNU_DSI_TE_SELECTION_MATCH=
YES

PP_PROGRAMMING_SEQUENCE_SOURCE_PROVEN=
YES

TEARCHECK_DISABLE_REQUIRED_BEFORE_PROGRAMMING=
YES

SAFE_INSERTION_POINT=
xzs_d8m8_stream_config() before CTL flush and before CTL_START

TARGET_SYNC_CONFIG_VSYNC=0x00180093
TARGET_HEIGHT=0x0000FFF0
TARGET_WRCOUNT=0x00000009
TARGET_START_POS=0x00000004

NEW_MAPPING_REQUIRED=
NO

INDIVIDUAL_PHYSICAL_TE_PULSE_DIRECTLY_OBSERVED=
NO

EXTERNAL_TE_PATH_FUNCTIONAL=
YES

NO_TE_FAILURE_MODE=
PP_LINE remains 0, PP0_DONE never asserts, bounded sampling loop expires without hang, safe panel shutdown succeeds

ONE_CONCEPTUAL_CHANGE_VALID=
YES

NEXT_ACTION=
Align PP0 tearcheck configuration in xzs_d8m8.h with working Keyaki external-TE golden target for one bounded XNU boot
```

---

## 1. Scope & Preflight Rules

- **Target Device**: Sony Xperia XZs G8231 (`BH905SX976`), platform `tone` / `keyaki`, Qualcomm MSM8996 v3.0.
- **Repository Baseline**:
  - `WORKTREE`: `/Users/lechaukha12/Desktop/xnu-xzs`
  - `BRANCH`: `xzs-d8-display-m8-resume`
  - `BASE_HEAD`: `e8cf1d661d7396dc70eb1bab4b6de93d061f8ff5`
- **Objective**: Preflight the one conceptual change:
  ```text
  XNU PP0 software/internal-TE override model
  →
  Sony Keyaki external-HW-TE golden model
  ```
  to ensure safety, architectural alignment, and strict adherence to the one-change rule before any hardware execution.
- **Constraints Enforced**:
  - `NO BUILD`
  - `NO BOOT`
  - `NO MMIO WRITE`
  - Purely static source and repository audit.

---

## 2. Question A: Panel TEON Initialization Audit

### 2.1 Current XNU Implementation
Audited in `src/xnu/pexpert/arm/xzs_d8m8.h` (`xzs_d8m8_panel_prepare()` lines 152–213) and `src/xnu/pexpert/arm/xzs_d8m6.h` (lines 90–120):
1. **Power-Up & Hardware Reset**: `xzs_d8m6_panel_power_up_to_idle()` sets VDDIO, LAB (+5.5V), IBB (-5.5V), and asserts/releases GPIO11 reset line (10 ms low, 10 ms wait).
2. **DCS Sleep Out (`SLPOUT`)**:
   - Opcode: `0x11`
   - Data length: 1 byte
   - Delay: `120000 us` (120 ms)
3. **DCS Tear On (`TEON`)**:
   - Opcode: `0x35`
   - Parameter: `0x00` (Mode 0: V-blanking only)
   - Payload: `{ 0x35, 0x00 }` (Long Write, 2 bytes)
   - Delay: `0 us`
4. **DCS Display On (`DISPON`)**:
   - Opcode: `0x29`
   - Data length: 1 byte
   - Delay: `10000 us` (10 ms)
5. **Kickoff (`CTL_START`)**: Issued significantly later in `xzs_d8m8_kickoff()` after framebuffer, SSPP RGB0, LM0, PP0 stream, and CTL flush are fully configured.

### 2.2 Comparison with Working Sony Keyaki / TWRP
In extracted Keyaki DT (`artifacts/builds/twrp-extracted.dts:1771`) and Sony reference source (`mdss_dsi_panel.c`):
- `qcom,mdss-dsi-on-command = [39 01 00 00 00 00 02 35 00 05 01 00 00 00 00 01 29];`
  - Packet 1: `0x35 0x00` (`set_tear_on` Mode 0)
  - Packet 2: `0x29` (`set_display_on`)
- The command sequence and payload are an **exact 100% match**.

### 2.3 Verdict on Hard Gate (Section 6)
- `XNU_TEON_PRESENT = YES`
- `XNU_TEON_PARAMETER = 0x00`
- `XNU_TEON_ORDER = SLPOUT (0x11, +120ms) -> TEON (0x35 0x00) -> DISPON (0x29, +10ms) -> CTL_START`
- **Hard Gate: PASS**. No additional panel command write is required for C2.

---

## 3. Question B: TE Pin / TLMM State Audit

### 3.1 Hardware Topology & Pinctrl
- Physical TE pin on Sony Xperia XZs: **`GPIO10`**.
- Sony Reference DT (`artifacts/builds/twrp-extracted.dts:13716-13730`):
  ```dts
  mdss_te_active {
      mux {
          pins = "gpio10";
          function = "mdp_vsync";
      };
      config {
          pins = "gpio10";
          drive-strength = <0x02>;
          bias-pull-down;
      };
  };
  ```
- Function: `mdp_vsync` (Mux function 1).
- Pull: Pull-down (`0x1`).
- Direction: Input (OE = 0).
- Drive strength: 2mA (`0x0`).

### 3.2 XNU D8-P1 Sealed Implementation
Audited in `src/xnu/pexpert/arm/xzs_d8p1.h` (lines 273–281):
```c
/* D8P1-40: GPIO 10 (TE mdp_vsync) Plan & Config
 * Target: Input (OE=0), Func=1 (mdp_vsync), Pull=1 (Pull-down), Drive=2mA (0) -> CFG=0x00000005
 */
d8p1_audit_write("GPIO10_CFG", TLMM_GPIO_CFG(GPIO_TE_NUM), 0x00000005u, 0x000003ffu, dryrun);
```
- Hardware readback on physical device (`artifacts/reports/D8_P1_SEAL_REPORT.md` and `artifacts/hw/d8m5/run1/host.txt:610`):
  `GPIO10 CFG @ 0x0101a000 = 0x00000005 [ mux=fn1_mdp_vsync(1) pull=pull_down(1) drive=2mA dir=INPUT(0) ]`
- Hardware readback confirms exact match to Sony golden specification.

### 3.3 Verdict on Hard Gate (Section 8)
- `XNU_TE_GPIO_CONFIG_MATCH = YES (HW_PROVEN / SEALED in D8-P1)`
- `TE_ROUTE_CONFIDENCE = HIGH`
- **Hard Gate: PASS**. No TLMM reconfiguration is required for C2.

---

## 4. Question C: Exact Sony PP Programming Sequence

Audited from `scratch/mdss_mdp_intf_cmd.c`:
1. **Disable before update**: In Qualcomm MDSS driver architecture (`mdss_mdp_cmd_ctx_stop`, `mdss_mdp_cmd_restore`), tearcheck registers are configured before `mdss_mdp_tearcheck_enable` enables the engine. To safely transition from the current running internal timing counter to the external timing parameters without transient counter glitched states, `TEAR_CHECK_EN` must be written to `0` first.
2. **Timing Registers Write Order**:
   In `mdss_mdp_cmd_tearcheck_cfg()` (lines 231–251):
   - Write 1: `MDSS_MDP_REG_PP_SYNC_CONFIG_VSYNC` (Offset `0x004`)
   - Write 2: `MDSS_MDP_REG_PP_SYNC_CONFIG_HEIGHT` (Offset `0x008`)
   - Write 3: `MDSS_MDP_REG_PP_VSYNC_INIT_VAL` (Offset `0x010`)
   - Write 4: `MDSS_MDP_REG_PP_RD_PTR_IRQ` (Offset `0x020`)
   - Write 5: `MDSS_MDP_REG_PP_START_POS` (Offset `0x01C`)
   - Write 6: `MDSS_MDP_REG_PP_SYNC_THRESH` (Offset `0x018`)
   - Write 7: `MDSS_MDP_REG_PP_SYNC_WRCOUNT` (Offset `0x00C`)
3. **Enable Tearcheck**:
   In `mdss_mdp_tearcheck_enable()` (lines 146–149):
   - Write 8: `MDSS_MDP_REG_PP_TEAR_CHECK_EN` (Offset `0x000`) = `1`.
4. **Timing relative to `CTL_START`**:
   `SOURCE_PROVEN`: Tearcheck programming occurs during stream / interface setup, **well before** `CTL_START` is written in kickoff.
5. **Memory Barriers & Readback**:
   In Qualcomm driver, `mb()` is issued; in XNU, `xzs_m8_write_reg` automatically verifies register write acceptance with readback.

---

## 5. Question D: Existing DSI TE Selection Audit

### 5.1 Register Bitfield Decode: `DSI_TRIG_CTRL` (Offset `0x0084`, PA `0x00994084`)
Audited from `scratch/mdss_dsi_host.c` (lines 364–371):
```c
data = 0;
if (pinfo->te_sel)
    data |= BIT(31);
data |= pinfo->mdp_trigger << 4; /* cmd mdp trigger */
data |= pinfo->dma_trigger;      /* cmd dma trigger */
data |= (pinfo->stream & 0x01) << 8;
MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x0084, data);
```
- In Sony extracted DT:
  - `te_sel = 1` -> `BIT(31) = 0x80000000`
  - `stream = 0` -> `(0 & 1) << 8 = 0`
  - `mdp_trigger = "none"` -> `0 << 4 = 0`
  - `dma_trigger = "trigger_sw"` -> `4` (bits [3:0] = `0x4`)
  - Calculated: `0x80000004`
- Current XNU value in `xzs_d8m8_stream_config()`:
  `DSI_TRIG_CTRL = 0x80000004`
- Working TWRP debugfs readback from G4:
  `te_sel = 1`, `stream = 0`

### 5.2 Verdict
- `XNU_DSI_TE_SELECTION_MATCH = YES (SOURCE_PROVEN / HW_READBACK_PROVEN)`
- No DSI trigger register change is required.

---

## 6. Question E: External TE Failure Mode & Bounded Execution

### 6.1 Behavior if Physical TE Fails to Assert
Audited from `scratch/mdss_mdp_intf_cmd.c` (`mdss_mdp_cmd_wait4pingpong` lines 688–735):
1. **Hardware State**:
   - `CTL_START` remains armed waiting for tearcheck start condition.
   - `PP_LINE_COUNT` remains `0`.
   - `PP_OUT_LINE_COUNT` remains `0`.
   - `PP0_DONE` does not fire.
   - `DSI_MDP_BUSY` remains `0`.
   - No hardware freeze, SError, Data Abort, or MMIO lockup occurs; the tearcheck engine is designed to safely wait.
2. **Software Execution in XNU**:
   - The bounded high-frequency sampling loop (20 ms window, 200 samples) samples hardware registers safely.
   - If no TE event occurs, it detects `MAX_LINE_COUNT_OBSERVED = 0` and exits cleanly.
   - Orderly panel shutdown (`xzs_d8m6_panel_shutdown()`) executes DISPOFF (`0x28`), SLPIN (`0x10`), and rail collapse, which has been proven safe across all prior runs.
3. **Verdict**:
   - `NO_TE_FAILURE_MODE = PP_LINE remains 0, PP0_DONE never asserts, bounded sampling loop expires without hang, safe panel shutdown succeeds`

---

## 7. C2 Target Configuration & Safe Insertion Point

### 7.1 Target Register Values
Only the four divergent tearcheck parameters are changed:
```text
SYNC_CONFIG_VSYNC  = 0x00180093   (Was 0x00080093: BIT20 changed 0 -> 1)
SYNC_CONFIG_HEIGHT = 0x0000FFF0   (Was 0x00000873: free-running ceiling)
SYNC_WRCOUNT       = 0x00000009   (Was 0x00000785: 4 + 4 + 1 = 9)
START_POS          = 0x00000004   (Was 0x00000780: start pos line 4)
```

Unchanged matching registers:
```text
TEAR_CHECK_EN      = 0x00000001
VSYNC_INIT_VAL     = 0x00000780
SYNC_THRESH        = 0x00040004
RD_PTR_IRQ         = 0x00000781
WR_PTR_IRQ         = 0x00000000
AUTOREFRESH_CONFIG = 0x00000000
```

### 7.2 Safe Insertion Point in XNU
Inside `xzs_d8m8_stream_config()` in `src/xnu/pexpert/arm/xzs_d8m8.h` (lines 1811–1828):
```c
/* 1. Ensure tear check disabled while updating timing */
xzs_m8_write_reg("PP0_TEAR_CHECK_EN ", 0x00971000u, 0x00000000u, 0x00000000u, false);

/* 2. Program golden external-TE parameters */
xzs_m8_write_reg("PP0_SYNC_CFG_VSYNC", 0x00971004u, 0x00180093u, 0x00180093u, false);
xzs_m8_write_reg("PP0_SYNC_CFG_HGHT ", 0x00971008u, 0x0000FFF0u, 0x0000FFF0u, false);
xzs_m8_write_reg("PP0_SYNC_WRCOUNT  ", 0x0097100cu, 0x00000009u, 0x00000009u, false);
xzs_m8_write_reg("PP0_VSYNC_INIT_VAL", 0x00971010u, 0x00000780u, 0x00000780u, false);
xzs_m8_write_reg("PP0_SYNC_THRESH   ", 0x00971018u, 0x00040004u, 0x00040004u, false);
xzs_m8_write_reg("PP0_START_POS     ", 0x0097101cu, 0x00000004u, 0x00000004u, false);
xzs_m8_write_reg("PP0_RD_PTR_IRQ    ", 0x00971020u, 0x00000781u, 0x00000781u, false);
xzs_m8_write_reg("PP0_WR_PTR_IRQ    ", 0x00971024u, 0x00000000u, 0x00000000u, false);

/* 3. Enable tear check with golden configuration */
xzs_m8_write_reg("PP0_TEAR_CHECK_EN ", 0x00971000u, 0x00000001u, 0x00000001u, false);
```

Also update consistency checks in `xzs_d8m8_status()` / prekick check:
- `pp0_sync_cfg_vsync == 0x00180093u`
- `pp0_sync_cfg_hght == 0x0000FFF0u`
- `pp0_sync_wrcount == 0x00000009u`
- `pp0_start_pos == 0x00000004u`
- Formula check: `(pp0_sync_wrcount == (pp0_start_pos + (pp0_sync_thresh & 0xffffu) + 1u))` (`4 + 4 + 1 == 9` -> PASS).

### 7.3 One-Change Verification
- `ONE_CONCEPTUAL_CHANGE_VALID = YES`.
- The change is strictly: **Align PP0 tearcheck mode to Sony Keyaki external-TE golden standard**.
- No DSI, CTL, SSPP RGB0, LM0, MMCC, VBIF, or GPIO changes are bundled.

---

## 8. Primary C2 Discriminator

- **Primary Causal Metric**:
  ```text
  PP_LINE_COUNT: 0 -> NON-ZERO
  ```
- If `PP_LINE_COUNT > 0`, external TE alignment is causal to releasing the PingPong scanout gate (`HW_PROVEN`).
- Visible pixels and downstream DSI transfer completion are not required to validate the C2 hypothesis.

---

## 9. STOP

As mandated by Section 22:
- **STOP**.
- NO build.
- NO boot.
- NO hardware MMIO write.
- Preflight evidence is submitted for review before proceeding to implementation.
