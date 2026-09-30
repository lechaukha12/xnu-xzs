# D8-M8 G5 — External TE Arrival Proof & Panel TE Lifecycle Audit

Audit type: Source code + existing forensic evidence analysis.
Constraints: No XNU boot, no functional change, no MMIO write.

```text
G5_CLASS=G5-B
TE_DELIVERY_ABSENCE_STRONGLY_SUSPECTED
TE_EDGE_REACHING_PP0_IN_XNU=NO — STRONG_INFERENCE

EXTERNAL_TE_COUNTER_EFFECT=
RESYNCHRONIZE_TO_VSYNC_INIT_VAL_OR_TRIGGER_RD_PTR (INFERENCE / NOT_EXPLICITLY_SPECIFIED_IN_C_SOURCE)

C2_COUNTER_TRACE_CONSISTENT_WITH_RECEIVING_TE=NO

DIRECT_TE_OBSERVABLE_AVAILABLE=YES
DIRECT_TE_OBSERVABLE=MDP_INTR_STATUS bit12 (PP0_RD_PTR) & PP_INT_COUNT_VAL reload discontinuity

NEW_TWRP_CAPTURE_REQUIRED=NO

PANEL_SEQUENCE_MATCH=PARTIAL
ORDER_CAUSALITY=UNKNOWN

TE_PATH_COMPONENTS=
1. Panel DDIC TE output pad
2. FPC physical trace to SoC ball (GPIO10)
3. TLMM GPIO10 configuration (TLMM_GPIO_CFG(10): func=fn1_mdp_vsync, dir=input, pull=down, drv=2mA)
4. Internal dedicated silicon line from TLMM fn1 to MDSS PingPong block
5. MDSS PP0 Tearcheck register block (SYNC_CONFIG_VSYNC bit20=1, BIT19=1, TEAR_CHECK_EN=1)
6. MDSS VSYNC clock branch (MDSS_CLK_MDP_VSYNC / VSYNC_CBCR=1 active)

XNU_CONFIGURES_ALL_REQUIRED_TE_PATH_COMPONENTS=YES
GPIO10_INPUT_STATUS_VALID_IN_MDP_VSYNC_MODE=UNKNOWN

EARLIEST_TE_PATH_DIVERGENCE=
Physical TE pulse arrival at MDSS PP0 input: in TWRP, PP0 generates RD_PTR (bit 12) at 60 Hz and PP_LINE starts (>0); in XNU C2, PP_INT_COUNT_VAL increases monotonically without reload, RD_PTR never fires during 20 ms window, and PP_LINE remains 0.

ONE_CHANGE_CANDIDATE_AVAILABLE=NO
NEXT_ACTION=Design a passive XNU TE observation task to verify whether GPIO10 physical pin pulses high during XNU runtime.
```

---

## 1. Context & Motivation

Phase D8-M8 Task C2 aligned all 10 PingPong 0 (PP0) tearcheck registers with the working Sony Keyaki (TWRP G4) golden image:
- `SYNC_CONFIG_VSYNC = 0x00180093` (`BIT19=1, BIT20=1, vclks=0x93`)
- `SYNC_CONFIG_HEIGHT = 0x0000FFF0` (65520)
- `START_POS = 0x00000004`
- `SYNC_WRCOUNT = 0x00000009`

The hardware readback verified `PASS`. However, upon `CTL_START`, `PP_LINE` remained `0x0` throughout the 20,001 µs observation window (`C2_CLASS = C2-C`).

Task G5 investigates the central open question:
**Does the physical Tearing Effect (TE) pulse emitted by the panel actually reach MDSS PP0 in XNU?**

---

## 2. Question A — Effect of External TE Edge on `PP_INT_COUNT_VAL`

### Source Analysis
From Sony reference kernel (`drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c`, `mdss_mdp_hwio.h`):

```c
static inline u32 mdss_mdp_cmd_line_count(struct mdss_mdp_ctl *ctl)
{
    ...
    init = mdss_mdp_pingpong_read(mixer->pingpong_base,
        MDSS_MDP_REG_PP_VSYNC_INIT_VAL) & 0xffff;
    height = mdss_mdp_pingpong_read(mixer->pingpong_base,
        MDSS_MDP_REG_PP_SYNC_CONFIG_HEIGHT) & 0xffff;

    if (height < init) {
        mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF);
        goto exit;
    }

    cnt = mdss_mdp_pingpong_read(mixer->pingpong_base,
        MDSS_MDP_REG_PP_INT_COUNT_VAL) & 0xffff;

    if (cnt < init)        /* wrap around happened at height */
        cnt += (height - init);
    else
        cnt -= init;
    ...
    return cnt;
}
```

### Architectural Behavior
1. In internal tearcheck mode (`BIT20=0`), `height` is set to `vtotal` (`0x873` = 2163). The internal counter counts up to `height` and wraps around to 0 automatically.
2. In external hardware TE mode (`BIT20=1`), `height` is programmed to `0xFFF0` (65520). The counter free-runs at the VSYNC line rate (`19.2 MHz / 147 cycles ≈ 130.6 kHz`).
3. Software C source does not implement the counter logic; the counter is an autonomous hardware register in MDSS silicon.
4. When a physical TE pulse arrives on the external input pin:
   - Hardware synchronizes the tearcheck state machine to the start of the panel frame.
   - The counter either reloads `VSYNC_INIT_VAL` (`0x780` = 1920) or resets to 0 (or re-arms the line comparison window).
   - Once the counter matches `RD_PTR_IRQ` (`0x781` = 1921), interrupt `MDSS_MDP_IRQ_PING_PONG_RD_PTR` (bit 12) is triggered.

```text
EXTERNAL_TE_COUNTER_EFFECT=
RESYNCHRONIZE_TO_VSYNC_INIT_VAL_OR_TRIGGER_RD_PTR
EVIDENCE_CLASS=INFERENCE (hardware RTL behavior not explicitly documented in C driver source)
```

---

## 3. Question B — Existing C2 Telemetry Telemetry Audit

C2 recorded high-frequency passive samples of `PP_INT_COUNT_VAL` and `MDP_INTR_STATUS` over a 20,001 µs window:

| Time | Snapshot | `PP_INT_COUNT_VAL` (hex) | Decimal | Delta from Pre-Kick | `MDP_INTR_STATUS` |
|---|---|---|---|---|---|
| T - 165 µs | C1150 | `0x00001804` | 6148 | 0 | `0x00000000` |
| T + 165 µs | C1160 | `0x0000181a` | 6170 | +22 | `0x00010000` (WR_PTR) |
| T + 214 µs | C1170 | `0x00001820` | 6176 | +28 | `0x00010000` |
| T + 361 µs | C1180 | `0x00001833` | 6195 | +47 | `0x00010000` |
| T + 1114 µs | C1190 | `0x00001895` | 6293 | +145 | `0x00010000` |
| T + 5111 µs | C11A0 | `0x00001a9f` | 6815 | +667 | `0x00010000` |
| T + 10110 µs | C11B0 | `0x00001d2c` | 7468 | +1320 | `0x00010000` |
| T + 20104 µs | C11C0 | `0x00002246` | 8774 | +2626 | `0x00010000` |

### Telemetry Findings
1. **Strict Monotonicity**:
   - `0x1804` → `0x181a` → `0x1820` → `0x1833` → `0x1895` → `0x1a9f` → `0x1d2c` → `0x2246`.
   - Line rate: `2626 lines / 20.104 ms = 130.62 lines/ms = 130,620 Hz`.
   - Exactly matches expected VSYNC clock: `19,200,000 Hz / 147 cycles = 130,612 Hz`.
2. **Missing Frame Boundary Discontinuity**:
   - At 60 Hz, the frame period is `16.667 ms` (16,667 µs).
   - In a 20.1 ms window, at least one physical TE pulse **must** have occurred if the panel were pulsing TE.
   - If a TE pulse had reached PP0, `PP_INT_COUNT_VAL` would have experienced a reset/reload step towards `0x780` (1920) or 0.
   - The trace shows zero discontinuities; it free-ran without interruption.
3. **Absence of RD_PTR Interrupt**:
   - Bit 12 (`PP0_RD_PTR`) was cleared before kickoff (`R11DB_POST_CLEAR_INTR = 0x00000000`).
   - Bit 12 **never set** during the entire post-kick window (`MDP_INTR = 0x00010000`, bit 16 only).
   - Since `height = 0xFFF0` (65520), without external TE reload, the counter would take `65520 / 130.6 kHz ≈ 501 ms` to wrap around and reach `0x781`.

```text
C2_COUNTER_TRACE_CONSISTENT_WITH_RECEIVING_TE=NO
TE_EDGE_REACHING_PP0_IN_C2=NO — STRONG_INFERENCE
```

---

## 4. Question C — Direct TE Observables

Audit of Sony MSM8996 MDSS driver reveals the following observables:

| Observable | Register / Symbol | Meaning | External TE Specific? | Read Side Effects | Usable in TWRP | Usable in XNU |
|---|---|---|---|---|---|---|
| `PP0_RD_PTR` IRQ | `MDP_INTR_STATUS` (0x00901014) bit 12 | Fired when `PP_INT_COUNT_VAL == RD_PTR_IRQ` (`0x781`). In external TE mode (`height=0xFFF0`), only fires at 60 Hz if TE reloads counter. | YES (at 60 Hz rate) | None (RO status) | YES | YES |
| `PP_INT_COUNT_VAL` Discontinuity | `0x00971014` [15:0] | Line counter reload back to `VSYNC_INIT_VAL` (`0x780`) | YES | None (RO) | YES | YES |
| `ctl->vsync_cnt` | Linux kernel variable (`mdss_mdp_cmd_readptr_done`) | Incremented on each `PP0_RD_PTR` interrupt | YES | None | YES | N/A (Linux OS var) |
| `TLMM_GPIO_IN_OUT(10)` bit 0 | `0x0100a004` bit 0 | Direct physical pad logic level of GPIO10 | YES (physical pad) | None | YES | YES (if valid in fn1) |
| `hw_vsync_handler` | Linux GPIO IRQ (`devm_request_irq`) | Falling-edge ISR registered on `disp_te_gpio` for ESD check | YES | None | YES | N/A (requires GIC IRQ) |

```text
DIRECT_TE_OBSERVABLE_AVAILABLE=YES
DIRECT_TE_OBSERVABLE=MDP_INTR_STATUS bit12 (PP0_RD_PTR) & PP_INT_COUNT_VAL reload discontinuity
```

---

## 5. Question D — Sony vs XNU Panel Lifecycle & DCS Sequence

### Working Sony Panel Lifecycle
1. **Bootloader / LK (Cold Boot)**:
   - Powers display PMIC rails (LAB + IBB + VDDIO).
   - Deasserts reset GPIO8 HIGH.
   - Sends DCS `SLPOUT (0x11)` and waits 120 ms.
   - Initializes panel timing and sends DCS `DISPON (0x29)`.
   - Display splash logo active (continuous splash).
2. **Linux Kernel Initialization (`mdss_dsi_panel_on`)**:
   - Sends `qcom,mdss-dsi-on-command`:
     - Packet 1: `TEON (0x35 0x00)` (Set Tear On, mode 0: V-blanking only, delay 0 ms)
     - Packet 2: `DISPON (0x29)` (Set Display On, delay 0 ms)
   - Sends `mdss_dsi_set_tear_on()`: `TEON (0x35 0x00)`
   - Arms command kickoff via `CTL_START`.
   - Sends `qcom,mdss-dsi-post-panel-on-command`:
     - DCS `SLPOUT (0x11)` with 120 ms delay (`<0x5010000 0x78000111>`), executed after image display is active.

### Current XNU Panel Lifecycle
1. **Cold Boot (`m5-init` / `m8-status`)**:
   - Powers display PMIC rails (LAB + IBB + VDDIO).
   - Deasserts reset GPIO8 HIGH + 10 ms settle.
2. **DCS Sequence**:
   - Packet 1: `SLPOUT (0x11)` + 120 ms delay.
   - Packet 2: `TEON (0x35 0x00)` (mode 0).
   - Packet 3: `DISPON (0x29)` + 10 ms delay.
3. **Pipeline Configuration & Kickoff**:
   - Arms RGB0 + LM0 + Stream + CTL + Flush.
   - Issues `CTL_START`.

### Comparison & Divergence
```text
PANEL_SEQUENCE_MATCH=PARTIAL
```
- **Divergence**:
  - In Sony DTS, `on-command` is `TEON` then `DISPON`, while `SLPOUT` is listed under `post-panel-on-command`.
  - In XNU, `SLPOUT` (120 ms) precedes `TEON` and `DISPON`.
- **Standard Conformance**:
  - Under MIPI DCS specification, a DDIC powered up from cold reset **must** receive `SLPOUT` (0x11) followed by a 120 ms wait before its internal charge pumps and logic can accept display commands.
  - In Sony's boot environment, the bootloader (aboot/XBL) already sent `SLPOUT` for the splash screen; Linux receives an awake panel.
  - In XNU, cold boot requires `SLPOUT` first. All commands (`SLPOUT`, `TEON`, `DISPON`) return `ACK_ERR=0, TIMEOUT=0`.
- **Causality**:
  ```text
  ORDER_CAUSALITY=UNKNOWN
  ```
  Whether issuing `TEON` before or after `SLPOUT` impacts the DDIC's internal TE generator is not proven by source alone.

---

## 6. Question E — GPIO10 Hardware Interconnect Path

Trace of the external TE signal path:
```text
Panel DDIC TE Pin
  │
  ▼
FPC / PCB Trace
  │
  ▼
SoC Physical Pad (GPIO10)
  │
  ▼
TLMM Multiplexer: TLMM_GPIO_CFG(10) (0x0100a000)
  ├── GPIO_FUNC_SEL = 1 (fn1_mdp_vsync)
  ├── GPIO_OE = 0 (input)
  ├── GPIO_PULL = 1 (pull-down)
  └── GPIO_DRV_STR = 0 (2 mA)
  │
  ▼
Dedicated Internal Hardwired Line to MDSS
  │
  ▼
MDSS PingPong 0 Tearcheck Logic
  ├── SYNC_CONFIG_VSYNC (0x00971004) bit 20 = 1 (hw_vsync_mode)
  ├── SYNC_CONFIG_VSYNC bit 19 = 1 (tearcheck sync enable)
  └── TEAR_CHECK_EN (0x00971000) bit 0 = 1
  │
  ▼
MDSS VSYNC Clock Domain
  ├── MDSS_CLK_MDP_VSYNC active
  └── VSYNC_CBCR (0x008c2328) bit 0 = 1
```

### Components Audit
- There are **no intermediate MDSS top-level mux registers** for VSYNC input on MSM8996.
- The path from TLMM function 1 (`mdp_vsync`) to the PingPong tearcheck block is a direct internal interconnect.
- XNU configures:
  1. `TLMM_GPIO_CFG(10) = 0x00000005` (`fn1_mdp_vsync`, pull-down, 2mA, input).
  2. `PP0_SYNC_CONFIG_VSYNC = 0x00180093` (`BIT19=1, BIT20=1`).
  3. `PP0_TEAR_CHECK_EN = 0x1`.
  4. `VSYNC_CBCR = 0x1` (unhalted).
  5. `DSI_TRIG_CTRL = 0x80000004` (`te_sel = 1`).

```text
XNU_CONFIGURES_ALL_REQUIRED_TE_PATH_COMPONENTS=YES
```

---

## 7. Question F — GPIO10 Input Readback Validity in `fn1_mdp_vsync` Mode

In MSM8996 TLMM architecture:
- `TLMM_GPIO_IN_OUT(10)` (`0x0100a004`) bit 0 is `GPIO_IN`.
- When `GPIO_FUNC_SEL != 0` (alternate function), the input buffer may or may not remain connected to `GPIO_IN` depending on pad retention/isolation configuration.
- The Linux kernel `pinctrl-msm` driver does not document or guarantee whether reading `GPIO_IN` yields the live pin state when muxed to an alternate function like `fn1_mdp_vsync`.

```text
GPIO10_INPUT_STATUS_VALID_IN_MDP_VSYNC_MODE=UNKNOWN
```
Per protocol Section 13, because validity is not `SOURCE_PROVEN YES`, GPIO input status polling cannot be used as an authoritative TE-edge discriminator without separate hardware proof.

---

## 8. TWRP Existing Evidence Audit

Review of `docs/XZS_D8_M8_TWRP_PP_TE_GOLDEN_G4.md` and associated G4 logs confirms:
1. TWRP uses external TE mode:
   - `SYNC_CONFIG_VSYNC = 0x00180093` (`BIT20=1`).
   - `SYNC_CONFIG_HEIGHT = 0xFFF0` (65520).
   - `START_POS = 4`, `SYNC_WRCOUNT = 9`.
2. Hardware interrupt 115 (`mdss_isr`) handles active `RD_PTR` and `PP0_DONE` events.
3. Active frame scanout is verified: `PP_LINE > 0` and `PP0_DONE` observed.

Existing TWRP G4 evidence already provides the complete working baseline.

```text
NEW_TWRP_CAPTURE_REQUIRED=NO
```

---

## 9. Comprehensive Differential Matrix

| Parameter / Observable | Working TWRP (G4) | Failing XNU C2 | Analysis / Meaning |
|---|---|---|---|
| `TEAR_CHECK_EN` | `1` | `1` | Match |
| `SYNC_CONFIG_VSYNC` | `0x00180093` | `0x00180093` | Match (both set `BIT19=1, BIT20=1`) |
| `SYNC_CONFIG_HEIGHT` | `0x0000FFF0` | `0x0000FFF0` | Match (free-running ceiling 65520) |
| `START_POS` | `4` | `4` | Match |
| `SYNC_WRCOUNT` | `9` | `9` | Match |
| `VSYNC_INIT_VAL` | `0x0780` (1920) | `0x0780` (1920) | Match |
| `SYNC_THRESH` | `0x00040004` | `0x00040004` | Match |
| `RD_PTR_IRQ` | `0x0781` (1921) | `0x0781` (1921) | Match |
| `MDP_RCG_CFG` | `0x00000506` | `0x00000506` | Match (171.4 MHz conformant) |
| `DSI_TRIG_CTRL` | `0x80000004` | `0x80000004` | Match (`te_sel = 1`) |
| GPIO10 Configuration | `fn1_mdp_vsync` | `fn1_mdp_vsync` | Match |
| `PP_INT_COUNT_VAL` Behavior | Reloads / synchronizes at 60 Hz | Monotonically increments (6148→8774) with zero discontinuities | **DIVERGENCE**: Indicates no TE edge received in C2 |
| `MDP_INTR` Bit 12 (`PP0_RD_PTR`) | Active at 60 Hz | 0 (never set post-kick) | **DIVERGENCE**: Counter never reached 1921 via TE reload |
| `PP_LINE` | `> 0` | `0` | **DIVERGENCE**: PP frame start gate blocked |
| `PP0_DONE` | Observed | 0 | **DIVERGENCE**: Command frame not completed |

```text
EARLIEST_TE_PATH_DIVERGENCE=
Physical TE pulse arrival at MDSS PP0 tearcheck input: in TWRP, PP0 generates RD_PTR (bit 12) at 60 Hz and PP_LINE starts (>0); in XNU C2, PP_INT_COUNT_VAL increases monotonically without reload, RD_PTR never fires during 20 ms window, and PP_LINE remains 0.
```

---

## 10. Conclusion & Classification

1. C2 successfully configured and verified all 10 golden external-TE registers on PP0.
2. However, the telemetry proves that `PP_INT_COUNT_VAL` free-ran monotonically across >1.2 frame periods (20.1 ms) with zero reload discontinuities and zero `RD_PTR` interrupts.
3. This provides **strong inference** that the physical panel did not assert a TE pulse on GPIO10, or the pulse was not delivered to PP0.
4. Per protocol Section 19:
   - `G5_CLASS = G5-B TE_DELIVERY_ABSENCE_STRONGLY_SUSPECTED`
   - `TE_EDGE_REACHING_PP0_IN_XNU = NO — STRONG_INFERENCE`
5. No functional pipeline changes or speculative register mutations are permitted in G5.

```text
ONE_CHANGE_CANDIDATE_AVAILABLE=NO
NEXT_ACTION=Design a passive XNU TE observation task to verify whether GPIO10 physical pin pulses high during XNU runtime.
```
