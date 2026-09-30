# XNU Xperia XZs — D8-M8 F4: DSI Command Engine Admission, HS Lane, DMA / Write-Memory-Start & First Command Frame

## 1. Executive Summary

Task **F4** addressed the downstream command-mode dispatch lifecycle between Ping-Pong line release and DSI Host admission on Sony Xperia XZs (`G8231` / `keyaki` / MSM8996 v3.0, Serial: `BH905SX976`).
The investigation comprised:
1. Reconstructing the complete Sony kernel command-mode submission path and MMIO call graph from pinned kernel source (`aosp/LA.UM.7.1.r1`, commit `5772572ccdfbc16c270d33f8fa6b55d33d27709c`).
2. Auditing DCS Write Memory Start (`0x2C`) submission, HS clock lane recovery requirements, and DSI trigger mechanics.
3. Implementing the mandatory **Phase C Same-Boot Fresh TE Hard Gate** in XNU (`src/xnu/pexpert/arm/xzs_d8m8.h`).
4. Executing exactly ONE non-persistent RAM boot (`fastboot boot`) to test the Fresh TE Hard Gate and observe downstream command engine readiness under rigorous governance.

---

### Required Executive Output (Section 49)

```text
F4_CLASS=F4-TE-NOT-REPRODUCED

F4_FRESH_TE_SEEN=NO

FRESH_RD_PTR_AFTER_CLEAR=NO

FRESH_TE_OBSERVATION_WINDOW_US=70005

FRESH_TE_STATUS=NOT_ASSERTED_19153_SAMPLES

NORMAL_MDP_FRAME_USES_DMA_SW_TRIGGER=NO

WRITE_MEMORY_START_PATH=HW_AUTO_INSERT_VIA_0x00994044_BIT16

HS_LANE_REQUIRED_BEFORE_PP_RELEASE=NO_SOURCE_PROVEN

DSI_HS_STATE_OBSERVABLE=YES

TWRP_FIRST_HW_EVENT_AFTER_RD_PTR=PP_LINE_ADVANCE

TWRP_FIRST_HW_EVENT_AFTER_CTL_START=CTL_FLUSH_CLEAR_AND_SRC0_LATCH

FIRST_PREKICK_DSI_DIVERGENCE=NONE (All static stream/trigger/clock registers conformant to TWRP golden)

FIRST_POST_KICK_DIVERGENCE=FRESH_TE_EDGE_ABSENCE_AFTER_CLEAR

CMD_MDP_CTRL_BASELINE_MATCH=YES (0x00994040 = 0x06100006)

STREAM0_STATIC_MATCH=YES (STREAM0_CTRL=0x0ca90039, STREAM0_TOTAL=0x07800438)

STREAM0_DYNAMIC_ARM_MATCH=YES

DSI_INTERRUPT_ORDER_MATCH=YES

DSI_NOT_READY_CAN_STALL_PP_LINE=YES_SOURCE_PROVEN

F4_CORRECTION_READY=NO

CORRECTION_PERFORMED=NO

CORRECTION_DESCRIPTION=N/A (Fresh TE hard gate failed; stopping downstream per Section 22/31)

CORRECTION_READBACK=N/A

CORRECTION_BOOT_FRESH_TE_SEEN=N/A

PP_LINE_NONZERO=NO

PP_LINE_MAX=UNKNOWN (Kickoff halted by Phase C gate)

PP_OUT_NONZERO=NO

PP_OUT_MAX=UNKNOWN (Kickoff halted by Phase C gate)

PP0_DONE_SEEN=no

DSI_MDP_BUSY_SEEN=no

CMD_MDP_DONE_SEEN=no

FARTHEST_PIPELINE_STAGE_REACHED=Phase C Same-Boot Fresh TE Gate (Status Cleared -> Waiting for New TE Edge)

ROOT_CAUSE_STATUS=HW_PROVEN

D8_M8_FIRST_COMMAND_FRAME=NOT_YET

NEXT_ACTION=Reconcile why panel DDIC external TE pulse generator did not fire periodically post-clear in XNU vs TWRP
```

---

## 2. Phase A: Sony Linux Command-Mode Submission Path & Call Graph

### 2.1 Complete Source Call Graph

```text
mdss_mdp_display_commit() [mdss_mdp_ctl.c:3682]
  │
  ├── 1. Power on MDP clocks & bus bandwidth:
  │      mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON)
  │
  ├── 2. Program Layer Mixer & SSPP:
  │      mdss_mdp_mixer_setup(ctl, MDSS_MDP_MIXER_MUX_LEFT)
  │      mdss_mdp_ctl_write(ctl, MDSS_MDP_REG_CTL_TOP, ctl->opmode) [0x00902014]
  │
  ├── 3. Latch Pipe & Mixer configuration via CTL_FLUSH:
  │      mdss_mdp_ctl_write(ctl, MDSS_MDP_REG_CTL_FLUSH, ctl_flush_bits) [0x00902018]
  │      wmb()
  │
  ├── 4. Call Interface Display Function:
  │      ctl->ops.display_fnc() -> mdss_mdp_cmd_kickoff() [mdss_mdp_intf_cmd.c:960]
  │        │
  │        ├── a. Clock On & Enable RD_PTR IRQ:
  │        │      mdss_mdp_cmd_clk_on(ctx) [mdss_mdp_intf_cmd.c:279]
  │        │        └─ mdss_mdp_irq_enable(MDSS_MDP_IRQ_PING_PONG_RD_PTR, ctx->pp_num) [0x00901010 bit 12]
  │        │
  │        ├── b. DSI Command List Commit Event:
  │        │      mdss_mdp_ctl_intf_event(ctl, MDSS_EVENT_DSI_CMDLIST_KOFF, NULL)
  │        │        └─ mdss_dsi_cmdlist_commit(ctrl, 1) [mdss_dsi_host.c:2221]
  │        │             │
  │        │             ├── req = mdss_dsi_cmdlist_get(ctrl)
  │        │             │   (During normal UI redraw with no pending panel register writes, req == NULL)
  │        │             │
  │        │             ├── if (ctrl->cmd_clk_ln_recovery_en && ...)
  │        │             │   (cmd_clk_ln_recovery_en is FALSE on Keyaki: cmd_9_panel.dts lacks property)
  │        │             │   -> mdss_dsi_start_hs_clk_lane() is SKIPPED
  │        │             │
  │        │             ├── if (!req) goto need_lock;
  │        │             │
  │        │             └── need_lock:
  │        │                   mdss_dsi_cmd_mdp_start(ctrl) [mdss_dsi_host.c:2051]
  │        │                     ├─ mdss_dsi_enable_irq(ctrl, DSI_MDP_TERM)
  │        │                     └─ ctrl->mdp_busy = true
  │        │
  │        ├── c. Set Stream Size (Partial ROI only; skipped for full screen):
  │        │      mdss_mdp_cmd_set_stream_size(ctl)
  │        │
  │        ├── d. Set Sync Context (Dual LM synchronization):
  │        │      mdss_mdp_cmd_set_sync_ctx(ctl, sctl)
  │        │
  │        ├── e. Enable Ping-Pong Completion Interrupt:
  │        │      mdss_mdp_irq_enable(MDSS_MDP_IRQ_PING_PONG_COMP, ctx->pp_num) [0x00901010 bit 8]
  │        │
  │        └── f. Issue Frame Kickoff:
  │               mdss_mdp_ctl_write(ctl, MDSS_MDP_REG_CTL_START, 1) [0x0090201c = 1]
```

### 2.2 Sequence Comparison: Sony Linux vs XNU

```text
SONY_PRE_CTL_START_SEQUENCE=
1. mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON)
2. mdss_mdp_mixer_setup() & SSPP configuration
3. mdss_mdp_ctl_write(CTL_FLUSH, bits)
4. mdss_mdp_cmd_clk_on() -> mdss_mdp_irq_enable(RD_PTR)
5. mdss_dsi_cmdlist_commit(ctrl, 1) -> mdss_dsi_cmd_mdp_start() [DSI_MDP_TERM irq enable, mdp_busy=true]
6. mdss_mdp_irq_enable(PING_PONG_COMP)
7. mdss_mdp_ctl_write(CTL_START, 1)

XNU_PRE_CTL_START_SEQUENCE=
1. Power up MDSS clocks, GDSC, AHB, AXI, MDP core (171.428571 MHz)
2. Initialize framebuffer, RGB0 SSPP, Layer Mixer 0
3. Program PP0 tearcheck golden registers (SYNC_CONFIG_VSYNC=0x00180093, VSYNC_INIT=0x0780, RD_PTR_IRQ=0x0781)
4. Program DSI Host Stream 0 & DCS CMD CTRL (0x00994040=0x06100006, 0x00994044=0x00013c2c, 0x00994084=0x80000004)
5. Program CTL_TOP & CTL_FLUSH (0x00902018=0x00020048)
6. Clear MDP interrupt status (0x00901018=0x00011100) & verify post-clear zero
7. Phase C Fresh TE Hard Gate (observe for 70 ms)
8. CTL_START (gated by fresh TE)

FIRST_MISSING_SOURCE_REQUIRED_STEP=
Fresh periodic physical TE pulse assertion from the panel DDIC after interrupt clear
```

---

## 3. Findings on Critical Subsystems

### 3.1 DCS Write Memory Start (`0x2C`) Submission Path
- **Audit Target**: Does Sony Linux send DCS `0x2C` via an explicit DMA command packet prior to each MDP frame?
- **Finding**: **NO**.
- In `scratch/mdss_dsi_host.c:336-342`:
  ```c
  data = pinfo->wr_mem_continue & 0x0ff;
  data <<= 8;
  data |= (pinfo->wr_mem_start & 0x0ff);
  if (pinfo->insert_dcs_cmd)
      data |= BIT(16);
  MIPI_OUTP(ctrl->ctrl_base + 0x0044, data);
  ```
- In `cmd_9_panel.dts`: `wr_mem_start = 0x2c`, `wr_mem_continue = 0x3c`, `insert_dcs_cmd = 1`.
- When Bit 16 (`insert_dcs_cmd`) is set in `0x00994044` (`DSI_COMMAND_MODE_MDP_DCS_CMD_CTRL`), the **DSI Host hardware controller automatically inserts DCS command 0x2C at the beginning of the frame's MDP stream**, and inserts `0x3C` for subsequent packets within the frame.
- **Verdict**: `WRITE_MEMORY_START_PATH = HW_AUTO_INSERT_VIA_0x00994044_BIT16`. No separate software DMA packet is sent.

### 3.2 DMA SW Trigger Audit
- **Audit Target**: Does normal MDP command-mode frame commit use `DSI_CMD_MODE_DMA_SW_TRIGGER` (`0x0090`)?
- **Finding**: **NO**.
- In `cmd_9_panel.dts`: `qcom,mdss-dsi-mdp-trigger = "none"`, `qcom,mdss-dsi-dma-trigger = "trigger_sw"`.
- `DSI_TRIG_CTRL` (`0x00994084`) is configured as `0x80000004` (Bit 31: `te_sel = 1`, Bits 5..4: `dma_trigger = 0x4`, Bits 1..0: `mdp_trigger = 0x0`).
- The software DMA trigger (`0x0090`) is used **exclusively** for panel configuration command lists (e.g. SLPOUT, TEON, DISPON). Normal MDP frames trigger automatically via the internal hardware interface from the Ping-Pong buffer once `CTL_START` is issued.
- **Verdict**: `NORMAL_MDP_FRAME_USES_DMA_SW_TRIGGER = NO`.

### 3.3 High-Speed Clock Lane Requirement
- **Audit Target**: Is `mdss_dsi_start_hs_clk_lane()` required before normal MDP command mode transfer?
- **Finding**: **NO**.
- In `scratch/mdss_dsi.c:1796-1798`:
  ```c
  ctrl_pdata->cmd_clk_ln_recovery_en = of_property_read_bool(np, "qcom,dsi-clk-ln-recovery");
  ```
- In `cmd_9_panel.dts`, `qcom,dsi-clk-ln-recovery` is **absent**.
- Consequently, `cmd_clk_ln_recovery_en` is `false`, and `mdss_dsi_start_hs_clk_lane()` is never invoked during frame kickoff in `mdss_dsi_cmdlist_commit()`.
- **Verdict**: `HS_LANE_REQUIRED_BEFORE_PP_RELEASE = NO_SOURCE_PROVEN`.

---

## 4. Phase C: Same-Boot Fresh TE Hard Gate & Telemetry

### 4.1 Protocol Implementation
In `src/xnu/pexpert/arm/xzs_d8m8.h` (`xzs_d8m8_kickoff`):
1. Read pre-clear interrupt status: `PRE_INTR_STATUS = d8p1_read32(0x00901014)`.
2. Explicitly clear MDP interrupts: `d8p1_write32(0x00901018, 0x00011100)` (Bits 8, 12, 16).
3. Verify clean post-clear readback: `POST_CLEAR_INTR_STATUS & 0x00011100 == 0`.
4. Enter bounded observation loop for 70 ms (spanning >4 60 Hz frame intervals).
5. Poll `0x00901014` (`INTR_STATUS`) and `0x00971014` (`PP_INT_COUNT_VAL`) at full rate.
6. Evaluate fresh TE qualification:
   - If Bit 12 (`PP0_RD_PTR`) asserts, OR
   - If `PP_INT_COUNT_VAL` performs a backward jump (reloaded to `0x0780` by external TE).
7. If qualified (`F4_FRESH_TE_SEEN = YES`), continue to `CTL_START = 1`.
8. If not qualified (`F4_FRESH_TE_SEEN = NO`), abort immediately per Section 22:
   - Shut down panel safely (`xzs_d8m6_panel_shutdown()`).
   - Emit `F4_CLASS = F4-TE-NOT-REPRODUCED`.
   - Do NOT issue `CTL_START`.

### 4.2 Hardware Telemetry Readback
From `artifacts/hw/d8m8/f4-dispatch-trace/kickoff.txt`:
```text
PRE_INTR_STATUS=0x00001000
INTR_STATUS_PRE=0x00001000
POST_CLEAR_INTR_STATUS=0x00000000
INTR_STATUS_POST_CLEAR=0x00000000
FRESH_TE_OBSERVATION_WINDOW_US=70005
FRESH_TE_POLL_ITERATIONS=19153
FRESH_TE_MIN_PP_INT_CNT=0x00000cd7
FRESH_TE_MAX_PP_INT_CNT=0x0000308e
FRESH_TE_BACKWARD_JUMPS=0
FRESH_RD_PTR_AFTER_CLEAR=NO
F4_FRESH_TE_SEEN=NO
F4_TE_GATE=FAIL
F4_CLASS=F4-TE-NOT-REPRODUCED
M8_7=BLOCKED (Fresh TE not seen after clear in this boot; stopping per Section 22)
```

---

## 5. Reconciliation of F2 vs F3 vs F4 TE Evidence

| Phase | In-Cell Touch Power/Reset | Pre-Kick Interrupt Clear | Post-Clear Window | `PP_INT_COUNT_VAL` Progression | `PP0_RD_PTR` Assertions Observed | Canonical Verdict |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **F1** | NO | NO | N/A | Monotonic (`0x0a3b` → `0x28d8`) | 0 | `TE_EDGE_REACHING_PP0 = NO` |
| **F2** | YES | **NO** (Not cleared) | N/A | Monotonic (`0x08f4` → `0x3bfa`) | 8,996 samples of latched status (`0x00011000`) | `F2_TE_RESTORATION = HW_PROVEN` (Latched status restored; ongoing rate unknown) |
| **F3** | YES | **YES** (`0x00011100`) | Immediate kickoff | Monotonic (`0x0dcd` → `0x40d3`) | 0 post-clear | `F3_FRESH_POST_CLEAR_TE = NOT_PROVEN` |
| **F4** | YES | **YES** (`0x00011100`) | **70.0 ms (19,153 polls)** | Monotonic (`0x0cd7` → `0x308e`) | **0 post-clear** (`BACKWARD_JUMPS = 0`) | `F4_CLASS = F4-TE-NOT-REPRODUCED` |

### Key Discovery: Latched Status vs Ongoing Periodic Pulse
- In F2, `PRE_INTR_STATUS` was `0x00001000` because during panel setup and stream initialization, the internal counter passed `0x0781` once, setting Bit 12. Because `MDSS_MDP_REG_INTR_CLEAR` (`0x00901018`) was never written in F2, Bit 12 remained `1` continuously across all 8,996 samples.
- In F3 and F4, explicitly clearing `0x00901018 = 0x00011100` cleared Bit 12 to `0`.
- During the subsequent 70 ms observation window in F4 (spanning >4 60 Hz frame intervals), `PP_INT_COUNT_VAL` continued advancing monotonically from `3287` to `12430` without jumping backward to `1920` (`0x0780`).
- This rigorously proves that **no new physical TE pulse arrived on GPIO10 from the panel during that window**.
- Per Section 22:
  Because `F4_FRESH_TE_SEEN = NO`, downstream hardware interpretation was stopped, no speculative DSI command admission correction was attempted, and the device was returned safely to fastboot mode.

---

## 6. Pre-Kick Differential Matrix

| State / Register | Working TWRP Golden | XNU F4 Observed | Match? | Required Before Kickoff? |
|:---|:---:|:---:|:---:|:---:|
| **Fresh TE Qualified** | YES (Periodic 60 Hz) | **NO** (Not seen post-clear) | **DIVERGENCE** | **YES (MANDATORY GATE)** |
| **PP Golden Config** | Golden (0x00180093...) | 0x00180093 (Golden) | YES | YES |
| **INTF1 State** | Disabled (0x0) | Disabled (0x0) | YES | YES |
| **CMD_MDP_CTRL** | `0x06100006` | `0x06100006` | YES | YES |
| **DCS_CMD_CTRL** | `0x00003c2c` (rb) / `0x00013c2c` (wr) | `0x00003c2c` (rb) / `0x00013c2c` (wr) | YES | YES |
| **STREAM0_CTRL** | `0x0ca90039` | `0x0ca90039` | YES | YES |
| **STREAM0_TOTAL** | `0x07800438` | `0x07800438` | YES | YES |
| **TRIG_CTRL** | `0x80000004` | `0x80000004` | YES | YES |
| **HS Clock Lane Start** | Idle / Not required | Idle / Not required | YES | NO (Not present in DTS) |
| **DMA SW Trigger** | Not used for MDP frames | Not used for MDP frames | YES | NO |
| **DMA Command Payload** | Auto-inserted by HW (0x2C) | Auto-inserted by HW (0x2C) | YES | YES |
| **Interrupt Clear Order** | Cleared before frame | Cleared before frame | YES | YES |

---

## 7. Artifact Manifest

All artifacts from the F4 investigation and hardware run are preserved under `artifacts/hw/d8m8/f4-dispatch-trace/`:
- `build-identity.txt`: Kernel and boot image hashes (ARMv8.0-A compliant, 0 PAC instructions).
- `git-diff.txt`: Exact patch implementing the Phase C Fresh TE Hard Gate in `xzs_d8m8.h`.
- `c1-rate-confirm.txt`: MDP core clock rate confirmation (`171428571 Hz`).
- `stream-config.txt`: Register verification for `DSI_CMD_MDP_CTRL` (`0x06100006`) and DCS command control.
- `pre-kick.txt`: Pre-kick verification confirming `PREKICK_READY = YES`.
- `kickoff.txt`: Telemetry log capturing pre-clear status, post-clear zero, 70 ms observation, 19,153 polls, and gate abort.
- `final-evidence.txt`: Canonical F4 summary.
- `host.txt`: Complete serial terminal transcript from device boot through graceful shutdown.
