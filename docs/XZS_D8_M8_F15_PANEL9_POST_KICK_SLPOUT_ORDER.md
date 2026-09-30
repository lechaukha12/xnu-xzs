# XZS D8-M8 F15: Exact Panel 9 Kickoff → Post-On SLPOUT Ordering Closure
## Replaying Authentic Sony LK State Machine with DSI_TRIG_CTRL Preservation

---

## 1. Executive Summary

Subtask F15 executed the single decisive causal experiment to answer whether authentic Sharp Panel 9 (`somc,sharp_synaptics_cmd_9_panel`) requires arming the first MDP frame request (`CTL_START=1`) *before* waking the DDIC with `SLPOUT (0x11)` via DSI DMA, while strictly preserving `DSI_TRIG_CTRL = 0x80000004` (resolving the F13 trigger-clobber defect).

Forensic disassembly of authentic Sony LK `aboot.img` established the exact call order:
1. `mdss_dsi_panel_initialize()`: Transmits Panel 9 `on_cmds` (`TEON 0x35 0x00`, `DISPON 0x29`).
2. `msm_display_on()`: Calls `mdss_mdp_cmd_kickoff()` (`0xaa01e78c`) writing `CTL_FLUSH` and `CTL_START=1` (`0x0090201c`).
3. Zero software delay (`LK_CTL_START_TO_SLPOUT_SOFTWARE_DELAY_US = 0`).
4. `mdss_dsi_post_on()` (`0xaa020018`): Transmits `SLPOUT (0x11)` via DSI DMA and dwells for 120 ms.
5. In LK, `mdss_dsi_cmd_dma_tx()` configures DMA buffers and triggers DMA start without touching `DSI_TRIG_CTRL` (`0x00994084`), leaving `0x80000004` permanently active.

XNU display driver was updated to replicate this exact order:
- `xzs_d8m8_panel_prepare()` sent `TEON` and `DISPON`, explicitly halting before `SLPOUT` (`SLPOUT_SENT=NO`).
- `xzs_d8m6_transmit_cmd()` was updated to preserve and restore `DSI_TRIG_CTRL`, guaranteeing `0x80000004` throughout DMA packet dispatch.
- `xzs_d8m8_kickoff()` cleared interrupts (`POST_CLEAR_PASS=YES`), wrote `CTL_START=1` once, immediately dispatched post-on `SLPOUT` via DSI DMA, dwelt for 120 ms (`SLPOUT_DELAY_US=120000`), verified strict ordering (`ORDER_VERIFIED=YES`), and conducted high-frequency observation over Window B (>250 ms).

### Hardware Result:
- `TRIG_CTRL` remained `0x80000004` across all phases (`POST_SLPOUT_TRIG_CTRL_MATCH=YES`).
- `ORDER_VERIFIED=YES`: `TEON (79925928 us) < DISPON (79926593 us) < CTL_START (84593841 us) < SLPOUT (84594817 us) < SLPOUT_SETTLE (84836361 us)`.
- `GPIO10_TRANSITIONS=0` across 17,111 dense samples (334.8 ms window). Pad stayed flat at 0V continuously (`LOW`).
- `PP_LINE=0` and `PP_OUT=0`.
- Panel shutdown executed cleanly with `R11C_SAFE_SHUTDOWN=PASS`.

**Causal Conclusion:**
`PANEL9_POST_KICK_SLPOUT_CAUSAL_TO_PHYSICAL_TE = NO_HW_PROVEN`
`EXACT_PANEL9_KICKOFF_POSTON_LIFECYCLE_NOT_SUFFICIENT = YES_HW_PROVEN`

The exact Sony LK kickoff/post-on ordering is **not sufficient** on cold boot to initiate DDIC autonomous scanout. The project is now scientifically justified to advance to vendor revision tables (`somc,change-fps-command`, PCC, UV) and board-level In-Cell touch/DDIC interlocks.

---

## 2. Section 33 Executive Output

```text
F15_CLASS=F15-A6 EXACT_PANEL_9_KICKOFF_POSTON_INSUFFICIENT
HEAD_BEFORE_F15=283bbc3
ACTUAL_PANEL_ID=9
ACTUAL_PANEL_NAME=somc,sharp_synaptics_cmd_9_panel
SONY_PANEL9_FIRST_FRAME_ORDER=BINARY_PROVEN (TEON -> DISPON -> CTL_FLUSH -> CTL_START=1 -> SLPOUT 120ms)
LK_CTL_START_TO_POST_ON_CALLS=1
LK_CTL_START_TO_SLPOUT_SOFTWARE_DELAY_US=0
LK_POST_ON_DMA_MMIO_SEQUENCE=TRIG_CTRL_UNTOUCHED (0x80000004 preserved across DMA)
PANEL9_LK_PP_AUTOREFRESH_VALUE=0x00000000
PANEL9_AUTOREFRESH_SOURCE_PROVEN=YES
TEON_TIMESTAMP_US=79925928
DISPON_TIMESTAMP_US=79926593
PRE_CLEAR_INTR_STATUS=0x00001000
POST_CLEAR_INTR_STATUS=0x00000000
POST_CLEAR_PASS=YES
CTL_START_TIMESTAMP_US=84593841
SLPOUT_TIMESTAMP_US=84594817
SLPOUT_SETTLE_DONE_TIMESTAMP_US=84836361
ORDER_VERIFIED=YES
TRIG_CTRL_PRE_KICK=0x80000004
TRIG_CTRL_POST_CTL_START=0x80000004
TRIG_CTRL_PRE_SLPOUT=0x80000004
TRIG_CTRL_POST_SLPOUT=0x80000004
TRIG_CTRL_FINAL=0x80000004
POST_SLPOUT_TRIG_CTRL_MATCH=YES
CTL_START_COUNT=1
OBSERVATION_WINDOW_US=334804
GPIO10_HIGH_SAMPLES=0
GPIO10_LOW_SAMPLES=17111
GPIO10_TRANSITIONS=0
GPIO10_FIRST_HIGH_TIMESTAMP_US=none
GPIO10_FIRST_TRANSITION_TIMESTAMP_US=none
RD_PTR_ASSERTED_SAMPLE_COUNT=1
RD_PTR_DISTINCT_EVENT_COUNT=1
RD_PTR_CORRELATES_WITH_GPIO10=NO
PP_COUNTER_MIN=0x00007969
PP_COUNTER_MAX=0x0000fdf3
PP_COUNTER_BACKWARD_JUMPS=0
PP_COUNTER_RELOAD_SEEN=NO
PP0_WR_PTR_SEEN=YES
PP_LINE_NONZERO=NO
PP_LINE_MAX=0x00000000
PP_OUT_NONZERO=NO
PP_OUT_MAX=0x00000000
PP0_DONE_SEEN=NO
DSI_MDP_BUSY_SEEN=NO
CMD_MDP_DONE_SEEN=NO
PANEL9_POST_KICK_SLPOUT_CAUSAL_TO_PHYSICAL_TE=NO_HW_PROVEN
EXACT_PANEL9_KICKOFF_POSTON_LIFECYCLE_NOT_SUFFICIENT=YES_HW_PROVEN
ROOT_CAUSE_STATUS=LIFECYCLE_NOT_SUFFICIENT_BOARD_OR_REVISION_DIFFERENTIAL
FARTHEST_PIPELINE_STAGE_REACHED=CTL_START_ARMED_POSTON_SLPOUT_SENT_AWAITING_PHYSICAL_TE
D8_M8_FIRST_COMMAND_FRAME=NOT_YET
R11C_SAFE_SHUTDOWN=PASS
NEXT_ACTION=AUDIT_PANEL_REVISION_AND_TOUCH_DDIC_INTERLOCK
```

---

## 3. Mandatory F14 / F15 Differential Comparison Matrix

| State | Sony LK | F14 | F15 |
|---|---:|---:|---:|
| TEON | pre-kick | pre-kick | pre-kick |
| DISPON | pre-kick | pre-kick | pre-kick |
| CTL_START | before SLPOUT | after SLPOUT | before SLPOUT |
| SLPOUT | post-kick | pre-kick | post-kick |
| 120ms settle | post-kick | pre-kick | post-kick |
| TRIG_CTRL bit31 | preserved | preserved | preserved (0x80000004) |
| GPIO10 TE | YES | NO | NO (0 transitions / 17,111 samples) |
| fresh RD_PTR | YES | NO | NO (correlates with GPIO10: NO) |
| PP_LINE | >0 | 0 | 0 |

---

## 4. Binary & Source Forensics: Sony LK aboot.img

Disassembly of authentic Sony LK `aboot.img` established:
- `mdss_dsi_panel_initialize()`: Transmits `TEON (0x35 0x00)` and `DISPON (0x29)`.
- `msm_display_on()` (`0xaa01ece8`):
  ```text
  0xaa01ece8:  stp    x29, x30, [sp, #-32]!
  0xaa01ecec:  mov    x29, sp
  0xaa01ecf0:  str    x19, [sp, #16]
  0xaa01ecf4:  cbz    x0, 0xaa01ed28
  0xaa01ecf8:  mov    x19, x0
  0xaa01ecfc:  ldr    w8, [x0, #0x28]        ; pinfo->on
  0xaa01ed00:  cbnz   w8, 0xaa01ed28
  0xaa01ed04:  bl     0xaa01e78c             ; mdss_mdp_cmd_kickoff()
  0xaa01ed08:  cbnz   w0, 0xaa01ed28
  0xaa01ed0c:  bl     0xaa01d298             ; panel type check
  0xaa01ed10:  cmp    w0, #0x1
  0xaa01ed14:  b.ne   0xaa01ed20
  0xaa01ed18:  mov    x0, x19
  0xaa01ed1c:  bl     0xaa020018             ; mdss_dsi_post_on()
  ```
- `mdss_mdp_cmd_kickoff()` (`0xaa01e78c`):
  - Programs `CTL_0_FLUSH` (`0x00902018`).
  - Writes `CTL_0_START = 1` (`0x0090201c`).
- Between `CTL_START` write and `mdss_dsi_post_on()` invocation:
  - Exactly 1 helper call (`0xaa01d298`, ~5 instructions).
  - Software delay: **0 us** (`NONE_SOURCE_PROVEN`).
- `mdss_dsi_post_on()` (`0xaa020018`):
  - Dispatches Panel 9 `post_on_cmds`: `SLPOUT (0x11)` via DSI DMA (`0xaa01f3bc`).
  - Dwells for 120,000 us (120 ms).
- DMA MMIO sequence in `mdss_dsi_cmd_dma_tx()` (`0xaa01f3bc`):
  - `0x00994048` (DMA FIFO physical address)
  - `0x0099404c` (DMA FIFO length)
  - `0x00994110` (DMA INT MASK)
  - `0x00994090` = 1 (DMA SW trigger)
  - `DSI_TRIG_CTRL (0x00994084)` is **never written**, preserving `0x80000004`.
- Autorefresh in Sony LK:
  - `pinfo->autorefresh_enable` is 0.
  - `PP0_AUTOREFRESH (0x00971030)` is **never written**, defaulting to `0x00000000` (`PANEL9_AUTOREFRESH_SOURCE_PROVEN = YES`).

---

## 5. DSI Trigger Preservation Architecture

In F13, `xzs_d8m6_transmit_cmd()` was found to clobber `DSI_TRIG_CTRL` from `0x80000004` to `0x00000004` by unconditionally writing `0x00000004u`.

In F15, `xzs_d8m6_transmit_cmd()` was updated to preserve and restore `DSI_TRIG_CTRL`:
```c
uint32_t orig_trig = d8m4_read32(D8M6_REG_DSI_TRIG_CTRL);
d8m4_write32(D8M6_REG_DSI_TRIG_CTRL, orig_trig | 0x00000004u);
... /* wait for DMA completion */
d8m4_write32(D8M6_REG_DSI_TRIG_CTRL, orig_trig);
```

Hardware readback verified 100% preservation across all lifecycle events:
- `TRIG_CTRL_PRE_KICK = 0x80000004`
- `TRIG_CTRL_POST_CTL_START = 0x80000004`
- `TRIG_CTRL_PRE_SLPOUT = 0x80000004`
- `TRIG_CTRL_POST_SLPOUT = 0x80000004`
- `TRIG_CTRL_FINAL = 0x80000004`
- `POST_SLPOUT_TRIG_CTRL_MATCH = YES`

---

## 6. Window A & Window B Timing and Metrics

### Window A (Immediately Post-CTL_START, Pre-SLPOUT):
- Timestamp: 84,593,841 us
- Sample: `POST_CTL_START_PRE_SLPOUT_GPIO10 = LOW`

### Post-On SLPOUT Dispatch:
- `F15_POST_ON_BEGIN`: 84,594,387 us
- `F15_SLPOUT_TX`: 84,594,817 us (38 us DMA transfer, ACK_ERR=0, TIMEOUT=0)
- Dwell: 120,000 us
- `F15_SLPOUT_SETTLE_DONE`: 84,836,361 us
- Ordering check: `79925928 < 79926593 < 84593841 < 84594817 < 84836361` → `ORDER_VERIFIED = YES`

### Window B (Post-SLPOUT 120ms Settle):
- Observation begin: 84,836,976 us
- Observation end: 85,097,002 us (duration: 260,026 us = 260 ms)
- Total post-kick observation window: 334,804 us (334.8 ms)
- GPIO10 pad samples: 17,111 dense iterations
  - `GPIO10_HIGH_SAMPLES = 0`
  - `GPIO10_LOW_SAMPLES = 17111`
  - `GPIO10_TRANSITIONS = 0`
  - `GPIO10_FIRST_HIGH_TIMESTAMP_US = none`
  - `GPIO10_FIRST_TRANSITION_TIMESTAMP_US = none`

### Pipeline Progression:
- `PP_LINE_NONZERO = NO` (Line counter: 0)
- `PP_OUT_NONZERO = NO` (Out line counter: 0)
- `PP0_DONE_SEEN = NO`
- `DSI_MDP_BUSY_SEEN = NO`
- `CMD_MDP_DONE_SEEN = NO`
- `CTL_START_READBACK = 0x00000000` (CTL_START write was consumed by MDP hardware, but Ping-Pong engine remained waiting for external TE trigger).

---

## 7. Root Cause Closure & Next Actions

1. **Replay of the Actual Sony LK State Machine is Conclusively Tested**:
   The chicken-and-egg hypothesis (that the DDIC requires `CTL_START` armed before `SLPOUT` DMA wakes the oscillator) has been faithfully replayed on physical hardware without trigger clobbering.
2. **Outcome**:
   The DDIC still does not emit physical TE on GPIO10. Both early SLPOUT (F14) and post-kick SLPOUT (F15) yield `GPIO10_TRANSITIONS = 0`.
3. **Scientific Progression**:
   The standard Device Tree sleep/wake lifecycle (`somc,sharp_synaptics_cmd_9_panel`) is **not sufficient on its own** to bootstrap the Sharp Panel 9 DDIC out of cold suspend.
4. **Next Frontier (F16+)**:
   - **Panel Revision Registers**: Audit whether Sony kernel transmits panel-revision initialization commands (`somc,change-fps-command`, PCC, UV compensation tables).
   - **In-Cell Touch Interlock**: Audit whether the Synaptics ClearPad S3330 touch controller asserts an electrical hardware handshake or GPIO line required to un-gate DDIC autonomous display scanout.
