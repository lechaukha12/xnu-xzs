# Sony Xperia XZs — Milestone D8-M8 Final Reconciliation & Closure Report
## Definitive Evidence Reconciliation, Root-Cause Synthesis, and Handoff

---

## 1. Executive Summary

Milestone **D8-M8** was originally chartered to achieve the first hardware-proven command-mode display scanout from Qualcomm Snapdragon 820 (MSM8996) MDP5 to the Sony Xperia XZs (`G8231` / `keyaki`, serial `BH905SX976`) physical Sharp 1080p LCD display.

The original D8-M8 investigation campaign (spanning subtasks F1 through F18 and retries R1 through R6) exhausted its planned bounds without achieving visible pixels on the screen:
```text
D8_M8_HISTORICAL_CAMPAIGN = EXHAUSTED_NOT_SEALED
D8_M8_ORIGINAL_FIRST_COMMAND_FRAME = NOT_PROVEN_DURING_ORIGINAL_CAMPAIGN
```

The closure campaign in **Milestone D8-M8.5** subsequently achieved the historic first light and visible pixel generation on the physical Xperia XZs hardware, proven across 3/3 fresh cold boots (`V1`, `V2`, `V3`).

This document provides the authoritative, permanent reconciliation of all historical technical conflicts, dispels obsolete hypotheses, formalizes the final root-cause model, records technical debt, and cleanly closes Milestone D8-M8:
```text
D8_M8_FINAL_STATUS = CLOSED
D8_M8_CLOSURE_REASON = ACCEPTANCE_SATISFIED_BY_D8_M8_5
D8_M8_SUPERSEDED_BY = D8-M8.5
D8_M8_5_STATUS = COMPLETE_SEALED_HW_PROVEN
D8_M9_STATUS = SATISFIED_BY_D8_M8_5
NEXT_ACTIVE_MILESTONE = D8-M10_FRAMEBUFFER_SHELL_CONSOLE
```

---

## 2. Historical Campaign Reconciliation & Truth Preservation

Historical integrity is strictly maintained. The original D8-M8 campaign was **not** successful on its own, and its status is never retroactively rewritten as "sealed".

### 2.1 Why D8-M8 Originally Remained Unsealed
During the original D8-M8 campaign:
1. **WLED Backlight Was 100% Inactive**: The PMI8994 QPNP WLED backlight driver had not been implemented. On transmissive IPS LCD panels (Sharp FHD), zero backlight means zero visible light, regardless of whether the LCD crystals are modulating pixels.
2. **Sharp DDIC Initialization Was Incomplete**: The panel command stream transmitted only 2 generic DCS commands (`TEON` and `DISPON`), leaving the internal Sharp timing generator clock PLL, scan bias curve, and display waveforms uninitialized.
3. **DSI SW DMA & MDP Bus Contention**: Post-kickoff diagnostic readbacks and SW DMA commands executed concurrently with MDP scanout, triggering hardware bus contention on the DSI command engine.
4. **Touch Interlock Was Misidentified as Primary Blocker**: The lack of physical TE transitions on TLMM GPIO10 led to the hypothesis that the Synaptics RMI4 touch IC had to handshake over I2C before the DDIC would emit TE pulses or accept video scanout.

---

## 3. Technical Reconciliation A: Panel Command Provenance

### 3.1 Historical Conflict
In Milestone F13 (`docs/XZS_D8_M8_F13_PANEL_IDENTITY_COMMAND_STREAM.md`), an earlier audit observed a 13-command vendor sequence and misclassified it as belonging to "Panel ID 0 (Samsung S6E3HA2)", while asserting that the authentic "Panel 9" required only 2 commands (`TEON` and `DISPON`).

Conversely, the successful D8-M8.5 implementation proved that transmitting the 13 vendor commands plus `DISPON` on the physical Xperia XZs produced visible, stable color bars and center badge.

### 3.2 Authoritative Source & Device Tree Audit
Audit of the extracted platform device tree (`artifacts/builds/twrp-extracted.dts` and `device/tone-keyaki.dtb`) reveals two command-mode panel nodes:

1. `somc,sharp_synaptics_cmd_9_panel` (lines 1740–1839):
   - Configured with `somc,lcd-id-adc = <0x00 0xdea8>`.
   - `qcom,mdss-dsi-on-command`: Contains only 2 packets: `TEON` (`0x35 0x00`) and `DISPON` (`0x29`).
   - `qcom,mdss-dsi-post-panel-on-command`: `SLPOUT` (`0x11`, wait 120 ms).

2. `somc,default_cmd_panel` (lines 1841–1930):
   - Configured with `somc,lcd-id-adc = <0x00 0x7fffffff>` (the platform default fallback panel descriptor).
   - Panel name: `"Default Panel"`, mode: `"dsi_cmd_mode"`, resolution: 1080x1920, 24bpp.
   - `qcom,mdss-dsi-on-command` (line 1872): Contains **13 command descriptors**:
     - `CMD 1` (`0x29`): `B0 00` (Manufacturer unlock)
     - `CMD 2` (`0x29`): `D6 01` (Output control enable)
     - `CMD 3` (`0x29`): `C4 70 22` (Vendor timing generator clock PLL divider)
     - `CMD 4` (`0x29`): `C6 53 2E 2E 05 45 ...` (21-byte vendor timing waveform table)
     - `CMD 5` (`0x29`): `EC 64 DC EC 3B 52 ...` (14-byte scan driver bias curve)
     - `CMD 6` (`0x29`): `B0 03` (Lock manufacturer register protection)
     - `CMD 7` (`0x39`): `35 00` (`TEON` — Set Tear On, V-blanking mode)
     - `CMD 8` (`0x39`): `36 00` (`MADCTL` — Memory Access Control)
     - `CMD 9` (`0x39`): `3A 77` (`COLMOD` — 24 bpp / RGB888)
     - `CMD 10` (`0x39`): `2A 00 00 04 37` (`CASET` — Column Address Set 0..1079)
     - `CMD 11` (`0x39`): `2B 00 00 07 7F` (`PASET` — Page Address Set 0..1919)
     - `CMD 12` (`0x39`): `44 00 00` (`STESL` — Scanline Tear Effect Start Line 0)
     - `CMD 13` (`0x39`): `11` (`SLPOUT` — Sleep Out + 120 ms dwell window)
   - `qcom,mdss-dsi-post-panel-on-command` (line 1873): Contains **1 command descriptor**:
     - `DISPON` (`0x29`, wait 20 ms).

### 3.3 Provenance Resolution & Classification
* **Why F13 Misclassified the Sequence**: F13 assumed that because the first node had `"9"` in its name, any command sequence with vendor registers belonged to an entirely different device (a Samsung panel). In reality, Samsung panels do not exist on the Sony `tone-keyaki` platform. The default command panel node `somc,default_cmd_panel` is the authentic Sharp FHD command-mode panel definition containing the complete DDIC power-on state machine.
* **Exact Command Count**:
  ```text
  PANEL9_ON_COMMAND_NODE = somc,default_cmd_panel (keyaki.dts:1841)
  PANEL9_ON_COMMAND_PROPERTY = qcom,mdss-dsi-on-command
  PANEL9_ON_DESCRIPTOR_COUNT = 13
  PANEL9_DCS_TRANSACTION_COUNT = 14 (13 on-commands + 1 post-on command DISPON)
  PANEL9_COMMAND_SOURCE = SOURCE_PROVEN
  OLD_CLASSIFICATION = PANEL0 (Samsung S6E3HA2) -> SUPERSEDED_BY_SOURCE_AUDIT
  FINAL_CLASSIFICATION = AUTHENTIC_SHARP_DEFAULT_COMMAND_PANEL (keyaki.dts:1841)
  ```

---

## 4. Technical Reconciliation B: `PP0_AUTOREFRESH`

### 4.1 Historical Conflict
Milestone F16 (`docs/XZS_D8_M8_F16_PANEL9_AUTOREFRESH_RD_PTR_SOURCE.md`) established that Sony LK operates with `PP0_AUTOREFRESH = 0x00000000` at initial kickoff. F16 therefore classified `0x80000001` as an XNU divergence.

Yet, D8-M8.5 verified that `PP0_AUTOREFRESH = 0x80000001` is operational and produces visible, stable scanout on the physical LCD.

### 4.2 Architectural Analysis
* **Sony LK Mechanism**: Sony LK relies on an external hardware Tear Effect (TE) interrupt signal wired from the DDIC to the host MDP via TLMM GPIO10 to trigger per-frame scanout (`te-using-te-pin`).
* **XNU Operating Environment**: In XNU, the physical GPIO10 pin is quiescent (`GPIO10_TRANSITIONS = 0`) because the external interrupt handler is not yet registered. However, the MDP5 PingPong 0 block features an internal VSYNC counter generator (`VSYNC_CBCR = 1`, `PP0_SYNC_CFG_VSYNC = 0x00180093`, `PP0_SYNC_CFG_HGHT = 0x0000FFF0`).
* **Autorefresh Operation**: Setting `PP0_AUTOREFRESH = 0x80000001` (bit 31 = enable, frame count = 1) instructs PingPong 0 to autonomously trigger scanout cycles against its internal 60 Hz VSYNC generator. This satisfies the MDP read pointer handshake (`FRESH_RD_PTR = YES`) and continuously refreshes the panel DDIC memory.
* **Classification**:
  ```text
  SONY_LK_AUTOREFRESH_AT_FIRST_KICKOFF = 0x00000000
  LINUX_ACTIVE_AUTOREFRESH_STATE = 0x80000001
  XNU_M8_5_AUTOREFRESH = 0x80000001
  AUTOREFRESH_FINAL_CLASSIFICATION = XNU_SCANOUT_WORKAROUND / IMPLEMENTATION_DEVIATION
  XNU_M8_5_AUTOREFRESH_REQUIRED_FOR_CURRENT_IMPLEMENTATION = YES
  ```
  This is a legitimate, robust scanout mechanism for XNU, avoiding reliance on external GPIO10 interrupt routing.

---

## 5. Technical Reconciliation C: DSI RX / Bus Turnaround (BTA)

### 5.1 Status & Non-Blocking Classification
Milestone F18 investigated DSI v1.4 RX / BTA readback and observed `DSI_BTA_TIMEOUT = 0x00000100`.

Milestone D8-M8.5 proved conclusively that the panel initializes and displays physical frames without requiring software DCS readback:
```text
DSI_FORWARD_WRITE_PATH = HW_PROVEN
DSI_SCANOUT_PATH = HW_PROVEN
DSI_RX_BTA_PATH = UNRESOLVED
DSI_RX_BTA_BLOCKS_VISIBLE_SCANOUT = NO_HW_PROVEN
```

DSI RX/BTA is formal technical debt (`DISPLAY-DEBT-001`). It does not impede display bring-up, frame scanout, or console rendering.

---

## 6. Technical Reconciliation D: Touch-Interlock Hypothesis

### 6.1 Disposition
Milestone D8-M8 Final Closure previously elevated `TOUCH_DDIC_INTERLOCK` as a suspected blocker.

Audit of the Sony LK source code (`target_display.c` and `platform/msm8996/`) proved that `target_display_init()` calls `msm_display_init()` and `msm_display_on()` with zero I2C transactions to the Synaptics RMI4 touch controller.

Furthermore, D8-M8.5 initialized the display and achieved visible scanout with zero touch I2C configuration:
```text
TOUCH_DDIC_INTERLOCK_CAUSAL = DISPROVEN
TOUCH_DDIC_INTERLOCK_FINAL_CLASSIFICATION = DISPROVEN
```

The Sharp DDIC operates independently of the in-cell touch IC state.

---

## 7. Authoritative Final Root-Cause Synthesis

| Historical Candidate | Original Suspicion | Final Classification | Authoritative Evidence |
| :--- | :--- | :--- | :--- |
| **Touch IC Mandatory Interlock** | Suspected causal | **DISPROVEN** | Sony LK source executes 0 touch I2C calls before splash; XNU D8-M8.5 boots and lights panel with 0 touch code. |
| **Missing Backlight / WLED** | Ignored (`WLED_WRITES=0`) | **HW_PROVEN_BLOCKER** | Transmissive IPS LCD requires backlight. Implementing PMI8994 QPNP WLED driver immediately illuminated panel. |
| **Incomplete Panel 9 Vendor Init** | Discarded as "Panel 0" | **HW_PROVEN_BLOCKER** | Sharp DDIC requires 13-command vendor init (`C4`, `C6`, `EC`, `MADCTL`, `COLMOD`, `CASET`, `PASET`). |
| **DSI SW DMA After CTL_START** | Diagnostic routine | **HW_PROVEN_BLOCKER** | Concurrent DSI SW DMA reads/writes after `CTL_START` caused MDP/DSI NOC collision and freeze. |
| **PP Autorefresh (`0x80000001`)** | Suspected deviation | **XNU_SCANOUT_WORKAROUND** | Autorefresh against internal VSYNC enables continuous scanout without external GPIO10 TE interrupts. |
| **DSI RX / BTA Failure** | Suspected causal | **TECHNICAL_DEBT (NON-BLOCKING)** | DSI RX BTA timeout does not block forward command submission or autonomous MDP frame scanout. |

---

## 8. Authoritative Final Milestone Ledger

```text
D7-T1   = PASS (USB console transport)
D7-T2   = PASS (Generic native Mach-O execution)
D8-M1   = PASS (Display topology audit)
D8-M2   = PASS (Display power & core clocks)
D8-M3   = PASS (DSI PLL / clocks / 14nm PHY Stage B; COMPLETE & SEALED)
D8-M4   = PASS (DSI0 host controller in command mode; COMPLETE & SEALED)
D8-P1   = PASS (TLMM GPIO prerequisite; COMPLETE & SEALED)
D8-P2   = PASS (SPMI + LAB/IBB display bias rails; COMPLETE & SEALED)
D8-M5   = PASS (Panel power & reset lifecycle; COMPLETE & SEALED)
D8-M6   = PASS (Panel vendor/DCS initialization sequence; COMPLETE & SEALED)
D8-M8   = CLOSED / SUPERSEDED BY D8-M8.5 (Original campaign EXHAUSTED_NOT_SEALED)
D8-M8.5 = COMPLETE / SEALED / HW_PROVEN (3/3 fresh cold boots, physical pixels visible)
D8-M9   = SATISFIED BY D8-M8.5 (First physical pixels achieved and verified)
D8-M10  = NEXT / ACTIVE (Framebuffer Text Console / Interactive Shell Display)
```

---

## 9. Next Active Milestone Scope: D8-M10

With visible pixels and backlight proven, the display bring-up path transitions to userspace utility:
* **Milestone**: **D8-M10 — Framebuffer Text Console / Interactive Shell Display**
* **Goal**: Render the interactive `xzs#` shell prompt and command output directly onto the physical 1080x1920 LCD panel using a high-legibility bitmap font, while keeping the USB serial console concurrently active.
* **Scope**:
  - Embedded 8x16 or 16x32 monospace bitmap font engine.
  - Character rendering, cursor tracking, newline, carriage return, screen clear (`cls`), and scrolling.
  - Console write fan-out (`xzs_console_write` broadcasting to both USB bulk-in and framebuffer rasterizer).
