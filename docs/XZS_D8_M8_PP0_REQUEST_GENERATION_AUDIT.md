# D8-M8 PP0 command-request generation audit

Offline forensic audit of what dynamic PP0/CTL state can prove after one `CTL_START`. No build, boot, flash, functional MMIO write, or display-code change was made for this audit. R11C classification is unchanged: the sampled window showed no PP0→DSI command-transfer activity. This document decodes the interrupt values that R11C already captured.

Evidence words used below: `HW_READBACK_PROVEN`, `HW_PROVEN`, `SOURCE_PROVEN`, `TRACE_PROVEN`, `CALCULATED`, `INFERENCE`, `UNKNOWN`.

## A. Repository identity

```text
WORKTREE=/Users/lechaukha12/Desktop/xnu-xzs
PROJECT_GIT_ORIGIN=https://github.com/lechaukha12/xnu-xzs.git
BRANCH=xzs-d8-display-m8-resume
HEAD_FULL_SHA=0d45cab7f2749fbad04550594f5b2bd3a41b89b2
WORKTREE_CLEAN=yes
```

Remotes at audit entry:

| Remote | Fetch | Push |
|---|---|---|
| origin | https://github.com/lechaukha12/xnu-xzs.git | https://github.com/lechaukha12/xnu-xzs.git |

`HEAD_FULL_SHA` is `git rev-parse HEAD` on that tree. Short name `0d45cab` is Retry #11D-A. The tree was clean before this document was added.

## B. Sony downstream identity

```text
URL=https://github.com/sonyxperiadev/kernel
BRANCH=aosp/LA.UM.7.1.r1
PINNED_COMMIT=5772572ccdfbc16c270d33f8fa6b55d33d27709c
```

MDSS sources used here live directly under `drivers/video/fbdev/msm/`, not under `drivers/video/fbdev/msm/mdss/`. The pinned blob of `mdss_mdp_hwio.h` is 32087 bytes, matching the tree listing at that commit. No other Qualcomm tree is used for a conclusion in this audit.

## C. Existing hardware baseline

R11C remains one boot, one `CTL_START`, commit `784bd807781afc41710eb7a635f1fd2e697788cb`. Retry #11D-A (`0d45cab7f2749fbad04550594f5b2bd3a41b89b2`) did not boot and classified the static conformance result as `R11D-A5 — INSUFFICIENT_EXISTING_EVIDENCE`. That classification is not revised here.

Raw rows are in [raw-handshake-snapshots.txt](../artifacts/hw/d8m8/m8-7-retry11c/raw-handshake-snapshots.txt). Unchanged facts:

- `DSI_CTRL=0x000001f5` at every snapshot, including C1150 immediately before `CTL_START`. Command-mode enable is not the open question.
- `DSI_TRIG_CTRL=0x80000004`: TE select 1, stream 0, MDP trigger NONE, DMA trigger SW. The pinned Keyaki path does not write a DSI MDP SW trigger.
- `CTL_FLUSH` `0x00020048` → `0` and `RGB0_CURRENT_SRC0_ADDR` `0` → `0x98000000` on the first sample after the one `CTL_START`. CTL/SSPP state was consumed.
- `PP_LINE_COUNT=0` and `PP_OUT_LINE_COUNT=0` at every snapshot.
- `DSI_STATUS=0` and raw `CMD_MDP_DONE=0` across the snapshots and across the tight DSI poll inside the 0–20 ms wait. `ACK_ERR=0`, `TIMEOUT=0`.
- `PP_INT_COUNT_VAL` runs and wraps. C11B0 is `0x777`. C11C0 is `0x41d`. An instantaneous sample of the counter on `START_POS=0x780` / `SYNC_WRCOUNT=0x785` was not captured. That crossing stays `INFERENCE`.
- `MDP_INTR` is real and dynamic: `0x11000` at C1130/C1140 before kickoff, `0` at C1110/C1120/C1150, `0x10000` from C1160 through C11B0, `0x11000` at C11C0/C11D0.
- `PP_AUTOREFRESH_CONFIG` was not read. Silicon value remains `UNKNOWN`.
- Physical TE was not observed. The internal-counter override (`PP_SYNC_CONFIG_VSYNC=0x00080093`, bit 19 set, bit 20 clear) stays in force. This audit does not reopen it.

`kickoff.txt` records `FIRST_WR_PTR_US=0` and `FIRST_RD_PTR_US=20000` with `PP0_DONE_OBSERVED=no`. Those timestamps match the snapshot edges once the bits are decoded below. `pre-kick.txt` records `MDP_INTR_EN=0x00000000` and `DSI_FIFO_STATUS_PRE=0x11111000`.

## D. Exact `MDP_INTR` identity

```text
physical register=MDSS_MDP_REG_INTR_STATUS
mdp base=0x00901000
offset=0x00014
absolute address=0x00901014
XNU read=xzs_d8m8_r11a_capture() → d8p1_read32(0x00901014u) → mdp_intr_status
XNU print=R11C_SNAPSHOT MDP_INTR=
```

Pinned definition: [`mdss_mdp_hwio.h:66-68`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_hwio.h#L66-L68).

| Register | Offset from MDP | Absolute | Role |
|---|---:|---:|---|
| `MDSS_MDP_REG_INTR_EN` | 0x010 | 0x00901010 | mask written by the driver |
| `MDSS_MDP_REG_INTR_STATUS` | 0x014 | 0x00901014 | status word XNU prints as `MDP_INTR` |
| `MDSS_MDP_REG_INTR_CLEAR` | 0x018 | 0x00901018 | write-1-to-clear |

The MDP base is the block that contains `MDSS_MDP_REG_DISP_INTF_SEL` at +0x004. XNU programs that register at `0x00901004`, so the +0x014 status slot is `0x00901014`. This is the address in [`xzs_d8m8.h`](../src/xnu/pexpert/arm/xzs_d8m8.h) at the capture that fills `mdp_intr_status`, not a label inferred from the print string.

XNU reads this word. It does not write `INTR_STATUS`. The pre-kick path writes `INTR_CLEAR` with `0x00011100` before C1110 (`xzs_d8m8.h` kickoff, the second clear). That value is bit 8 | bit 12 | bit 16. C1130/C1140 still show `0x11000` because they are captured in the flush stage, before that clear. C1110 is the first snapshot after the clear and reads `0`.

Clear and mask semantics, from [`mdss_mdp.c`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp.c):

- `mdss_mdp_isr` reads `INTR_STATUS`. A zero word is skipped. A nonzero word is written to `INTR_CLEAR`, then ANDed with `INTR_EN` before any callback runs (`mdss_mdp.c:1103-1128`). Status and enable are different registers. A passive read of status is the same first step the ISR uses and does not itself clear the bit.
- `mdss_mdp_irq_enable` writes the selected bit to `INTR_CLEAR` and then updates `INTR_EN` (`mdss_mdp.c:887-890`). Enabling an interrupt drops a stale status bit.
- `mdss_mdp_irq_clear` / `mdss_mdp_intr_check_and_clear` write the bit to `INTR_CLEAR` only (`mdss_mdp.c:838-858`, `952-978`).

R11C required `MDP_INTR_EN==0` before kickoff (`HW_READBACK_PROVEN` in `pre-kick.txt` / `host.txt`). The kickoff path reads that enable word and does not write it (`TRACE_PROVEN`). Status still changed afterward. The latched bits are therefore raw status, not a reflection of the enable mask, and they were not delivered to a CPU ISR.

## E. `MDP_INTR` bit decode

Pinned names, [`mdss_mdp_hwio.h:96-111`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_hwio.h#L96-L111). The PP0 rows are also the interrupt-map entries in [`mdss_mdp.c:211-222`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp.c#L211-L222).

| Value | Bits set | Macro | Block |
|---|---|---|---|
| `0x00010000` | 16 only | `MDSS_MDP_INTR_PING_PONG_0_WR_PTR` | PP0 write-pointer / lineptr status |
| `0x00011000` | 16 and 12 | bit 16 as above, plus `MDSS_MDP_INTR_PING_PONG_0_RD_PTR` | PP0 write-pointer status and PP0 read-pointer status |

No other bit is set in either observed word. In particular:

| Bit | Value | Macro | In `0x10000` / `0x11000`? |
|---:|---:|---|---|
| 8 | `0x100` | `MDSS_MDP_INTR_PING_PONG_0_DONE` | no |
| 12 | `0x1000` | `MDSS_MDP_INTR_PING_PONG_0_RD_PTR` | only in `0x11000` |
| 16 | `0x10000` | `MDSS_MDP_INTR_PING_PONG_0_WR_PTR` | yes |
| 20 | `0x100000` | `MDSS_MDP_INTR_PING_PONG_0_AUTOREFRESH_DONE` | no |
| 26 / 27 | INTF1 underrun / vsync | `MDSS_MDP_INTR_INTF_1_*` | no |

`SOURCE_PROVEN` for the names and the PP0 map index. The names are not a DSI-request strobe and not `PP_DONE`.

### Bit 16 — `PING_PONG_0_WR_PTR`

- Symbolic name and map: `MDSS_MDP_INTR_PING_PONG_0_WR_PTR`, `MDSS_MDP_IRQ_TYPE_PING_PONG_WR_PTR` interface 0. Source file:line above.
- Programmed line: `MDSS_MDP_REG_PP_WR_PTR_IRQ` at PP+0x024. `mdss_mdp_cmd_tearcheck_cfg` writes `te->wr_ptr_irq` ([`mdss_mdp_intf_cmd.c:345-347`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L345-L347)).
- Panel parser forces `te->wr_ptr_irq = 0` ([`mdss_dsi_panel.c:1772`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_panel.c#L1772)). XNU read this register back as `0` before kickoff.
- Software use: `mdss_mdp_cmd_lineptr_done` ([`mdss_mdp_intf_cmd.c:1264-1284`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L1264-L1284)). A nonzero `wr_ptr_irq` installs that handler; zero removes it ([`1579-1588`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L1579-L1588)). Zero disables the Linux lineptr callback. It does not stop the hardware from latching status. R11C latched the bit while `INTR_EN` was 0.
- Clear: write bit 16 to `INTR_CLEAR`. XNU's `0x00011100` includes it.
- The pinned C source does not contain the sentence "status sets when `PP_INT_COUNT_VAL` equals `PP_WR_PTR_IRQ`". That comparator is the ordinary reading of a line-irq register. It is not separately proved by a comment in this tree.

### Bit 12 — `PING_PONG_0_RD_PTR`

- Symbolic name and map: `MDSS_MDP_INTR_PING_PONG_0_RD_PTR`, `MDSS_MDP_IRQ_TYPE_PING_PONG_RD_PTR` interface 0.
- Programmed line: `MDSS_MDP_REG_PP_RD_PTR_IRQ` at PP+0x020, written from `te->rd_ptr_irq` ([`mdss_mdp_intf_cmd.c:343-344`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L343-L344)).
- Panel property name: `qcom,mdss-tear-check-rd-ptr-trigger-intr`, default `yres + 1` ([`mdss_dsi_panel.c:1769-1771`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_panel.c#L1769-L1771)). The property string calls this a read-pointer trigger interrupt. XNU read back `0x781` (1921), which is `1920 + 1`.
- Software use: `mdss_mdp_cmd_readptr_done` increments `vsync_cnt`, completes `rdptr_done`, and runs vsync handlers ([`mdss_mdp_intf_cmd.c:1150-1184`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L1150-L1184)). The driver treats this interrupt as command-mode vsync / read-pointer notification. Callback install is at interface setup ([`3683-3684`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L3683-L3684)).
- Clear: write bit 12 to `INTR_CLEAR`. Included in XNU's `0x00011100`.
- Autorefresh-done is a different bit. Its comment says that interrupt happens when the read pointer resets to the init value for `autorefresh_frame_cnt` frames ([`mdss_mdp_intf_cmd.c:2748-2753`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L2748-L2753)). That is bit 20, which stayed clear.

### Why `0x10000` appears immediately after `CTL_START`

`HW_READBACK_PROVEN` interval:

| Point | Time | `PP_INT_COUNT_VAL` | `MDP_INTR` | MMIO write since previous row |
|---|---:|---:|---:|---|
| C1150 | 641859568 | `0x258` | `0x00000000` | none in this interval yet |
| C1160 | 641859608 | `0x25d` | `0x00010000` | `CTL_START = 1` only |

The XNU sequence between those two captures is the C1150 snapshot, a cycle-counter read, `d8p1_write32(0x0090201c, 1)`, two DSI reads, then the C1160 snapshot (`TRACE_PROVEN`). Delta is 40 µs and 5 counter steps.

`PP_WR_PTR_IRQ` was read back as `0`. The counter moved `0x258` → `0x25d` and did not pass 0. The new bit 16 edge is therefore not the free-running counter matching the programmed write-pointer line.

The same bit was already 1 at C1130 and C1140, with `CTL_START` count 0, `PP_LINE_COUNT=0`, `PP_OUT_LINE_COUNT=0`, and RGB0 current source still 0. A latched `PING_PONG_0_WR_PTR` bit, by itself, has already occurred with no kickoff and no DSI transfer. It is not a source-defined "request issued" flag.

What is new after `CTL_START` is the rising edge in an interval whose only MMIO write is `CTL_START`. The pinned source never says that `CTL_START` generates `PING_PONG_0_WR_PTR`. Calling that edge "PP armed" or "request issued" is `INFERENCE`, not `SOURCE_PROVEN`.

### Why it later becomes `0x11000`

| Point | `PP_INT_COUNT_VAL` | `MDP_INTR` |
|---|---:|---:|
| C11B0, +10 ms | `0x777` | `0x00010000` (bit 12 clear) |
| C11C0, +20 ms | `0x41d` | `0x00011000` (bit 12 set) |

Bit 16 stays set because nothing writes `INTR_CLEAR` during the sample loop (`TRACE_PROVEN`). Bit 12 was clear at `0x777` (1911) and set at `0x41d` (1053). The programmed read-pointer line is `0x781` (1921). Height is `0x873` (2163).

From C1160 to C11B0 the counter advanced 1306 steps in 9998 µs, which is 7.655 µs/step (`CALCULATED`). One revolution of 2163 steps is 16.56 ms, the same timing model already established for this counter. On that rate, `0x780` is about 69 µs after C11B0, still inside the 10 ms gap before C11C0. The counter values are consistent with passing `0x781` and then wrapping at height. That passage was not sampled. It remains `INFERENCE`.

Bit 12 also does not prove a DSI request. C1130 already showed bit 12 set before any `CTL_START`, with `PP_LINE_COUNT=0`. The post-kickoff rising edge is a new read-pointer status latch in the window that contains the programmed line. It is the tearcheck read-pointer event the driver uses as vsync. It is not `PP_DONE` and not DSI admission.

### What these bits do not prove

Neither bit proves a PP timing-threshold start of pixel transfer, PP completion, CTL kickoff acceptance, frame completion, or command-request generation.

Separate signals already cover some of those ideas:

- CTL kickoff acceptance of the flush and source address is `CTL_FLUSH` falling and `RGB0_CURRENT_SRC0_ADDR` latching. Already `HW_READBACK_PROVEN`. It is not `MDP_INTR`.
- Transfer completion, in this driver, is `PING_PONG_0_DONE` (bit 8). The resource-control comment calls `MDP_RSRC_CTL_EVENT_PP_DONE` the end of the data transfer ([`mdss_mdp_intf_cmd.c:572-577`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L572-L577)). Bit 8 is inside the pre-kick clear mask and is not cleared again before the end of the window. It stayed 0 at every post-start snapshot. Completion did not latch (`HW_READBACK_PROVEN` for the sticky bit).
- "First line is out / transfer ongoing" in this driver is `PP_LINE_COUNT >= 1` ([`mdss_mdp_intf_cmd.c:2798-2805`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L2798-L2805)). Sampled `PP_LINE_COUNT` stayed 0. Those samples are sparse. Unlike `PP_DONE`, `PP_LINE_COUNT` is a live value and a pulse between snapshots is not excluded.

## F. `PP_AUTOREFRESH_CONFIG` audit

```text
register=MDSS_MDP_REG_PP_AUTOREFRESH_CONFIG
offset=PP + 0x030
absolute=0x00971030
current hardware readback=UNKNOWN
```

Definition: [`mdss_mdp_hwio.h:721`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_hwio.h#L721).

Fields the pinned source actually uses:

| Field | Source use |
|---|---|
| bit 31 | Enable. `__mdss_mdp_kickoff` writes `BIT(31) \| frame_cnt` when autorefresh is turned on. `mdss_mdp_cmd_tearcheck_setup` treats `read & BIT(31)` as "enabled in splash" and then disables it. |
| low bits written as `frame_cnt` | `frame_cnt` must satisfy `0 <= frame_cnt < AUTOREFRESH_MAX_FRAME_CNT` (`6`) before the mode setter accepts it ([`mdss_mdp_intf_cmd.c:35`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L35), [`2492`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L2492)). The enable write ORs that integer into the register with no further field mask. |
| whole register `0x0` | `__disable_autorefresh` ([`488-491`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L488-L491)). |

No reset value is defined in this source. A read is a plain `mdss_mdp_pingpong_read`. The driver does not describe a read side effect.

Call sites in the pinned command path:

| Site | File:line | What it does |
|---|---|---|
| Splash/setup test | `mdss_mdp_cmd_tearcheck_setup`, `509-533` | If bit 31 is set, disable external-TE listening, wait for PP done, write `0`, restore TE listening. If bit 31 is clear, the register is not written. |
| Enable | `__mdss_mdp_kickoff`, `2942-2960` | States `ON_REQUESTED` and `ON`: write `BIT(31) \| frame_cnt`. Comment: "Program HW to take care of Kickoff". This branch does **not** write `CTL_START`. |
| One-shot | `__mdss_mdp_kickoff`, `2962-2985` | Every other autorefresh state, including initial `OFF`: optional scanline-window wait, then `CTL_START = 1`. This branch does **not** write `PP_AUTOREFRESH_CONFIG`. |
| Disable | `mdss_mdp_disable_autorefresh`, `2883-2884`, and the setup path above | Write `0`. |
| Init | `mdss_mdp_cmd_ctx_setup`, `3658-3659` | Software state `MDP_AUTOREFRESH_OFF`, frame count 0. This assignment does not touch the register. |

XNU has no write to `0x00971030` (`TRACE_PROVEN` by absence in `xzs_d8m8.h`). That does not prove the silicon word is zero.

### Normal one-shot expectation

For the software state machine, one-shot Keyaki kickoff is the `else` branch: `CTL_START = 1`, and `PP_AUTOREFRESH_CONFIG` is left unchanged. The value that makes that branch legal in the driver is autorefresh state `OFF`, which the driver associates with bit 31 clear. The only value the driver itself writes for "disabled" is `0x0`.

Keyaki's extracted DTS (`artifacts/display-audit/keyaki.dts`) contains no `qcom,mdss-mdp-kickoff-threshold`. The panel parser leaves `mdp_koff_thshold` false ([`mdss_dsi_panel.c:1081-1084`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_panel.c#L1081-L1084)). The scanline-window wait is not taken on that panel. One-shot software kickoff is `CTL_START = 1` with no write to this register.

### Could a non-zero value alter one-shot `CTL_START`?

`SOURCE_PROVEN` for the software branch: if the driver's autorefresh state is `ON` or `ON_REQUESTED`, `__mdss_mdp_kickoff` writes bit 31 and does not write `CTL_START`. That is an alternate kickoff, not a gate in front of a `CTL_START` the driver still performs.

XNU does not use that state machine. It writes `CTL_START` unconditionally once the pre-kick checks pass. The pinned source never writes `CTL_START` while bit 31 is being set, and it never documents what a leftover bit 31 does to a later `CTL_START`. That hardware combination is `UNKNOWN`. Low bits with bit 31 clear are also `UNKNOWN`: the driver only tests bit 31.

Bit 20 of `MDP_INTR` stayed 0 for the whole R11C window. That is the autorefresh-done status, not the config register. It does not prove the config word is zero.

So: the register is relevant to which kickoff the downstream driver selects. It is not a proven cause of the current blocker, and its silicon value must be read rather than assumed.

## G. PP0 request-generation call/state chain

Pinned order for one command-mode frame. Each software condition is necessary only for the branch that contains it. After `CTL_START`, the driver does not poll a "request issued" bit; completion is `PING_PONG_0_DONE`, and "line out" is `PP_LINE_COUNT`.

### Before `CTL_START`

| Order | Function | File:line | Condition | Effect if true | Effect if false |
|---|---|---|---|---|---|
| 1 | `mdss_mdp_display_commit` | `mdss_mdp_ctl.c:6102-6134` | Commit reached the display function | Writes `CTL_FLUSH`, then calls the command display function | No kickoff |
| 2 | `mdss_mdp_cmd_kickoff` | `intf_cmd.c:3050-3059` | `ctx` valid and `intf_stopped` clear | Continues | `-ENODEV` or `-EPERM`, no `CTL_START` |
| 3 | `__mdss_mdp_cmd_is_panel_power_off` | `intf_cmd.c:3085-3086` | Panel power off | `mdss_mdp_cmd_panel_on` first | Continues |
| 4 | `mdss_mdp_resource_control` | `intf_cmd.c:3110` | Kickoff event | Driver contract: power/clocks are ON before return. Comment at `564-570` | Clocks may stay gated; hardware result not defined here |
| 5 | `MDSS_EVENT_DSI_CMDLIST_KOFF` → `mdss_dsi_cmdlist_commit(..., from_mdp=1)` | `intf_cmd.c:3146-3147`, `mdss_dsi_host.c:2724-2884` | Called from MDP | May send a queued DCS command, then always reaches the `from_mdp` tail | DCS-only path; no `mdp_busy` set |
| 6 | `mdss_dsi_cmdlist_commit` tail | `host.c:2874-2884` | `from_mdp` and ROI width or height nonzero, or partial update disabled | `mdss_dsi_cmd_mdp_start` | "false kickoff": does not set software `mdp_busy` |
| 7 | `mdss_dsi_cmd_mdp_start` | `host.c:2548-2558` | Reached | Sets software `mdp_busy`, enables the `CMD_MDP_DONE` IRQ, reinits a completion. No DSI trigger register write | DSI hardware is unchanged by this function |
| 8 | `mdss_mdp_cmd_set_stream_size` / `mdss_mdp_cmd_set_sync_ctx` | `intf_cmd.c:3149-3151` | Kickoff continued | Stream size and PP sync context refreshed | Not skipped on the normal path |
| 9 | autorefresh lock | `intf_cmd.c:3153-3157` | State `OFF_REQUESTED` | `mdss_mdp_disable_autorefresh` writes `PP_AUTOREFRESH_CONFIG = 0` | Other states fall through |
| 10 | PP completion IRQ | `intf_cmd.c:3159-3162` | Master PP | Installs `mdss_mdp_cmd_pingpong_done` and `mdss_mdp_irq_enable(PING_PONG_COMP)`. Enable clears stale `PP_DONE` then sets `INTR_EN` bit 8 | No completion callback |
| 11 | `mdss_mdp_cmd_lineptr_ctrl` | `intf_cmd.c:3177-3181` | `mdss_mdp_is_lineptr_supported` | Full-frame update with `wr_ptr_irq != 0` enables lineptr; `wr_ptr_irq == 0` removes it | Keyaki parser leaves `wr_ptr_irq` at 0, so the handler is not armed |
| 12 | `__mdss_mdp_kickoff` | `intf_cmd.c:2942-2960` | State `ON` or `ON_REQUESTED` | Writes `PP_AUTOREFRESH_CONFIG = BIT(31) \| frame_cnt`. Does not write `CTL_START` | Falls through |
| 13 | `__mdss_mdp_kickoff` | `intf_cmd.c:2962-2985` | State not on, and `mdp_koff_thshold` set | May sleep, then `CTL_START = 1` | Keyaki has no kickoff-threshold property, so no sleep |
| 14 | `__mdss_mdp_kickoff` | `intf_cmd.c:2984-2986` | One-shot branch | `MDSS_MDP_REG_CTL_START = 1` | Autorefresh branch skipped this write |

Tearcheck programming that must already have happened, from `mdss_mdp_cmd_tearcheck_cfg` ([`277-357`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L277-L357)) and `mdss_mdp_tearcheck_enable` ([`210-235`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L210-L235)):

- `PP_SYNC_CONFIG_VSYNC`: bit 19 always set (internal counter). Bit 20 set only when `hw_vsync_mode` is true. Keyaki DTS sets `qcom,mdss-dsi-te-using-te-pin`, so unmodified Sony sets bit 20. XNU leaves bit 20 clear on purpose.
- Height, init, `RD_PTR_IRQ`, `WR_PTR_IRQ`, `START_POS`, `SYNC_THRESH`.
- `PP_SYNC_WRCOUNT = start_pos + sync_threshold_start + 1`. With XNU's readback that is `0x780 + 4 + 1 = 0x785`.
- `PP_TEAR_CHECK_EN` written as the panel enable AND the function enable argument.

The parser comment at [`mdss_dsi_panel.c:1744-1748`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_panel.c#L1744-L1748) says the defaults are "write is faster than read" and "init == start == rdptr". It does not give a register-level predicate for emitting a DSI packet.

`mdss_mdp_cmd_intf_callback` waits until `PP_INT_COUNT_VAL > start_pos + sync_threshold_start` only for ping-pong split ([`intf_cmd.c:1227-1251`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L1227-L1251)). Keyaki is single-DSI, not ping-pong split. That wait is not on this path.

### What the source requires before a command transfer can start

`SOURCE_PROVEN` software prerequisites for the one-shot branch:

1. Command-mode CTL selected and flushed (`CTL_TOP` command bit, `CTL_FLUSH` written before the display function).
2. Tearcheck configured and enabled, with a running line counter (`PP_INT_COUNT_VAL`).
3. DSI command engine left in command mode. `mdss_dsi_cmd_mdp_start` does not write `DSI_CTRL` or a trigger.
4. Autorefresh software state not `ON` / `ON_REQUESTED`, otherwise `CTL_START` is replaced by `PP_AUTOREFRESH_CONFIG`.
5. One `CTL_START = 1`.

`SOURCE_PROVEN` hardware-visible conditions the driver uses **after** that write:

- `PP_LINE_COUNT >= 1`: "first line is out", transfer ongoing.
- `PING_PONG_0_DONE`: end of the data transfer.
- `DSI_STATUS` bit 2: the comment at [`mdss_dsi_host.c:2711-2716`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L2711-L2716) calls bit 2 the MDP_BUSY bit and says "transfer is not on-going in hw yet" when it is clear.

The source does not name an earlier "request pending" or "request issued" register between `CTL_START` and `PP_LINE_COUNT`. The compare that releases pixels is inside the ping-pong tearcheck block, programmed through `START_POS`, `SYNC_THRESH`, and `SYNC_WRCOUNT`, and it is not exposed as a status bit by this driver.

### XNU versus that chain

`TRACE_PROVEN` from `xzs_d8m8_kickoff`: XNU writes `CTL_FLUSH` (`0x00020048`), does not write a DSI register on the kickoff path, clears `INTR_STATUS` bits 8/12/16, requires `INTR_EN==0`, then writes `CTL_START` once. It does not run `mdss_dsi_cmd_mdp_start`, does not set `INTR_EN` bit 8, and does not write `PP_AUTOREFRESH_CONFIG`. R11C showed the flush consumed and the source latched, so the CTL write was not ignored by the CTL/SSPP flush logic.

## H. Candidate dynamic status registers

Safe passive read means the pinned driver reads the register without a side-effect write on that read. "Useful for request issue" means the source uses that bit as evidence a command transfer was emitted or is ongoing.

| Register | Offset | Block | Meaning in pinned source | Safe passive read | Useful for request issue? | Evidence |
|---|---:|---|---|---|---|---|
| `MDSS_MDP_REG_INTR_STATUS` | MDP+0x014 | MDP IRQ | Sticky PP/INTF/WB status. PP0 done, RD_PTR, WR_PTR, autorefresh-done are bits 8, 12, 16, 20 | Yes. Clear is a different register | No dedicated request bit. Bit 8 is completion. Bits 12 and 16 are pointer/lineptr status and both latch with no kickoff | `SOURCE_PROVEN` applicable as status; not as request-issued |
| `MDSS_MDP_REG_INTR_EN` | MDP+0x010 | MDP IRQ | CPU mask only | Yes, plain read | No | `SOURCE_PROVEN`. R11C pre-kick value `0` |
| `MDSS_MDP_REG_PP_INT_COUNT_VAL` | PP+0x014 | PP tearcheck | Low 16 bits are the line count `mdss_mdp_cmd_line_count` normalizes against init and height | Yes | No. Proves the counter runs | `SOURCE_PROVEN` applicable to timing, not to request issue |
| `MDSS_MDP_REG_PP_LINE_COUNT` | PP+0x02C | PP | "line out". `>= 1` means transfer ongoing. `0 < line < roi.h` means wait for PP done. Equal to height is treated as already finished | Yes | Closest source-defined "transfer has produced a line". Not a request strobe that can precede the first line | `SOURCE_PROVEN` applicable. R11C samples stayed 0 |
| `MDSS_MDP_REG_PP_OUT_LINE_COUNT` | PP+0x028 | PP | Defined only. No reader in `mdss_mdp_intf_cmd.c`, `mdss_mdp_ctl.c`, or `mdss_mdp.c` | Read side effect `UNKNOWN` | No source use | `UNKNOWN`. Already 0 in every R11C sample; not a new discriminator |
| `MDSS_MDP_REG_PP_AUTOREFRESH_CONFIG` | PP+0x030 | PP | Bit 31 enable; low bits frame count. Alternate kickoff instead of `CTL_START` | Yes. Setup path reads it | No. It selects the kickoff mechanism. It is not request status | `SOURCE_PROVEN` applicable to the branch. Silicon `UNKNOWN` |
| `MDSS_MDP_REG_CTL_START` | CTL+0x01C | CTL | Write `1` to kick. R11C readback after the write is `0` | Yes | The write is the software kickoff. Readback `0` is a consumed pulse, not busy and not request-issued | `SOURCE_PROVEN` for the write. No CTL_BUSY register exists beside it in `mdss_mdp_hwio.h` |
| `MDSS_MDP_REG_CTL_FLUSH` | CTL+0x018 | CTL | Bits consumed when CTL takes the flushed configuration | Yes | Proves CTL/SSPP took the flush. R11C already showed that. Not PP request issue | `SOURCE_PROVEN` |
| `MDSS_MDP_REG_CTL_TOP` / `CTL_LAYER` | CTL+0x014 / +0x000 | CTL | Static mode and layer mux | Yes | Static only | `SOURCE_PROVEN_NOT_APPLICABLE` to the dynamic question |
| `MDSS_MDP_REG_INTF_DSI_CMD_MODE_TRIGGER_EN` | INTF+0x084 | INTF | Defined at `mdss_mdp_hwio.h:669`. No use in `mdss_mdp_intf_cmd.c` or `mdss_mdp_ctl.c` | Not used by the driver, so side effect of a read is `UNKNOWN` | No | `SOURCE_PROVEN_NOT_APPLICABLE` to the normal command path. Do not add it because of the name |
| `MDSS_MDP_REG_INTF_LINE_COUNT` | INTF+0x0B0 | INTF | Defined. No reader in the command kickoff or CTL sources reviewed | `UNKNOWN` | No | `SOURCE_PROVEN_NOT_APPLICABLE` |
| DSI `0x008` `DSI_STATUS` | DSI+0x008 | DSI | Bit 2 is MDP_BUSY, "transfer is on-going in hw". Bit 1 is CMD DMA busy (`0x02`). Bit 3 is video-engine busy (`0x08`). Bit 31 error path writes the register back | Read is the driver's first access. The only documented write-back is bit 31 contention | Bit 2 proves DSI sees an ongoing MDP transfer. Clear does not prove a request was never generated | `SOURCE_PROVEN` applicable to admission/ongoing, not to PP request generation |
| DSI `0x110` `DSI_INT_CTRL` | DSI+0x110 | DSI | Raw `CMD_MDP_DONE` is bit 8; mask is bit 9 | The command path reads it. Whole-word RMW exists on other paths | Bit 8 proves DSI finished a command-MDP transfer. It does not prove the PP request edge | `SOURCE_PROVEN` |
| DSI `0x00C` FIFO status | DSI+0x00C | DSI | `0x11111000` is the empty mask polled before controller reconfiguration | Yes | No. Pre-kick R11C value `0x11111000` matches that empty mask | `SOURCE_PROVEN_NOT_APPLICABLE` to request issue |

There is no `SOURCE_PROVEN` register in this header set whose set bit means "PP request pending" or "PP request issued" while `PP_LINE_COUNT` is still 0.

## I. PP/CTL interrupt map

Command-mode bits in `MDSS_MDP_REG_INTR_STATUS`. Macros from `mdss_mdp_hwio.h:93-119`. Map from `mdss_mdp.c:202-229`.

| Bit | Value | Macro | Hardware source | Meaning in this driver | Can prove request generation? |
|---:|---:|---|---|---|---|
| 8 | `0x00000100` | `MDSS_MDP_INTR_PING_PONG_0_DONE` | PP0 | End of data transfer (`pingpong_done` / `PP_DONE`) | No. Completion, and only if a transfer ran. R11C post-start samples stayed clear |
| 9 | `0x00000200` | `MDSS_MDP_INTR_PING_PONG_1_DONE` | PP1 | Same for PP1 | No. Keyaki is PP0. Not set in R11C |
| 12 | `0x00001000` | `MDSS_MDP_INTR_PING_PONG_0_RD_PTR` | PP0 | Read-pointer trigger; driver vsync. Line register `PP_RD_PTR_IRQ` | No. Latches before kickoff. R11C sets it in `0x11000` |
| 16 | `0x00010000` | `MDSS_MDP_INTR_PING_PONG_0_WR_PTR` | PP0 | Write-pointer / lineptr. Line register `PP_WR_PTR_IRQ` | No. Latches before kickoff, and the post-`CTL_START` edge is not defined as the request |
| 20 | `0x00100000` | `MDSS_MDP_INTR_PING_PONG_0_AUTOREFRESH_DONE` | PP0 | Read pointer reset for `frame_cnt` autorefresh frames | No. Not set in R11C. Not the config register |
| 26 | `0x04000000` | `MDSS_MDP_INTR_INTF_1_UNDERRUN` | INTF1 | Interface underrun | No. Not set |
| 27 | `0x08000000` | `MDSS_MDP_INTR_INTF_1_VSYNC` | INTF1 | Interface vsync. Video-path MISR uses this bit | No. Command path uses PP RD_PTR, not INTF1 vsync. Not set |

CTL has no interrupt bit of its own in this map. `CTL_START` is a trigger write, not an interrupt source.

DSI command completion is not in `MDP_INTR`. It is `DSI_INTR_CMD_MDP_DONE` bit 8 of DSI+0x110 ([`mdss_dsi.h:207-208`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi.h#L207-L208)). R11C raw bit stayed 0.

`0x10000` is bit 16 alone. `0x11000` is bit 16 plus bit 12. `PP_DONE` would have been `0x100` in the same word and was absent.

## J. Reinterpretation of R11C

Nothing in the raw file is rewritten. The new source mapping changes what those words mean.

`HW_READBACK_PROVEN` additions from values R11C already stored:

1. `MDP_INTR=0x00010000` is `MDSS_MDP_INTR_PING_PONG_0_WR_PTR` and no other bit (`SOURCE_PROVEN` name, `HW_READBACK_PROVEN` value).
2. `MDP_INTR=0x00011000` is that write-pointer bit plus `MDSS_MDP_INTR_PING_PONG_0_RD_PTR` (`SOURCE_PROVEN` names).
3. Both bits were set at C1130/C1140 before `CTL_START`, with line and output counts at 0. Pointer-status latches are not proof of a transfer (`HW_READBACK_PROVEN` context).
4. After the pre-kick clear, status was 0 at C1150 (`count=0x258`) and bit 16 was 1 at C1160 (`count=0x25d`). The only MMIO write in between was `CTL_START=1` (`TRACE_PROVEN` plus `HW_READBACK_PROVEN`). The counter did not pass `PP_WR_PTR_IRQ=0`. The edge is real. Its identification as "request issued" is not supported.
5. Bit 12 rose between the `0x777` sample and the `0x41d` sample. The programmed read-pointer line `0x781` sits in that gap if the counter is monotonic modulo height `0x873` (`CALCULATED` / `INFERENCE`). The bit change itself is `HW_READBACK_PROVEN`.
6. Bit 8 (`PP0_DONE`) never appeared after the clear. Because the bit is sticky until `INTR_CLEAR`, and the sample loop does not clear it, PP completion did not latch in the 0–20 ms window (`HW_READBACK_PROVEN`).
7. Bit 20 (autorefresh done) never appeared. This does not read `PP_AUTOREFRESH_CONFIG`.
8. `PP_LINE_COUNT` stayed 0 at the snapshot times. The driver's "first line is out" predicate was false at those times (`HW_READBACK_PROVEN` for the samples). It was not polled on every DSI iteration, so a sub-sample pulse is not excluded.
9. `DSI_STATUS` bit 2 and raw `CMD_MDP_DONE` were polled through the wait, not only at the seven timestamps, and were not seen (`HW_PROVEN` for that poll, as already reported). That is absence of observed DSI transfer activity. It is not proof that PP never generated a request, and it is not proof that DSI rejected one.

Still `UNKNOWN` after this decode:

- Whether the `CTL_START`-aligned `WR_PTR` edge means the write pointer was loaded.
- Whether PP compared the read pointer against `START_POS` / `SYNC_WRCOUNT` and released a command request.
- `PP_AUTOREFRESH_CONFIG`.
- Any direct request-pending or request-issued bit. None is defined for this path.

Do not promote the `0x780`/`0x785` passage to a captured observation.

## K. Current blocker

```text
PP0 request generation not proven
```

Narrower than "DSI problem", and narrower than "DSI rejected a request".

`HW_PROVEN` / `HW_READBACK_PROVEN`: CTL consumed the flush and latched RGB0 `0x98000000`. The tearcheck counter was already running. `PING_PONG_0_WR_PTR` rose in the `CTL_START` interval without the counter reaching the programmed write-pointer line. `PING_PONG_0_RD_PTR` rose later, in the same window as the inferred start-position passage. `PP_DONE` did not latch. Sampled `PP_LINE_COUNT` and `PP_OUT_LINE_COUNT` stayed 0. DSI MDP busy and `CMD_MDP_DONE` were not observed.

`SOURCE_PROVEN`: none of those interrupt bits is the command-request strobe. The driver has no status bit for "request issued" short of `PP_LINE_COUNT >= 1`, `PP_DONE`, and `DSI_STATUS` bit 2.

So the open boundary is still PP0 turning one consumed `CTL_START` into a command-mode transfer. The new decode removes "unknown `MDP_INTR` value" as a separate mystery. It does not show that the request was issued, and it does not show that DSI refused one.

## L. Proposed smallest next hardware observation

Proposal only. Do not boot it from this audit.

One RAM boot, one `CTL_START`, same static programming as R11C: framebuffer, `CTL_TOP`, `CTL_LAYER_0`, `DISP_INTF_SEL`, `CTL_FLUSH`, `DSI_CTRL`, `DSI_TRIG_CTRL`, internal counter (`PP_SYNC_CONFIG_VSYNC=0x00080093`), the same PP timing, the same 20 ms bound, no WLED, no second frame, no DSI MDP SW trigger, no change to autorefresh, no return to physical TE.

No new programming write. Keep the existing pre-kick `INTR_CLEAR` of `0x00011100`. That write is what made the R11C edges visible; removing it would leave stale bit 12 and bit 16 set and hide a new edge. Do not clear again after `CTL_START`. Do not write `PP_AUTOREFRESH_CONFIG`.

Add only passive reads the pinned source already uses as data, not as triggers:

| When | Reads | Why |
|---|---|---|
| C1150, immediately before `CTL_START` | `PP_AUTOREFRESH_CONFIG` at `0x00971030`; `INTR_EN` at `0x00901010`; existing `INTR_STATUS`, `PP_INT_COUNT_VAL`, `PP_LINE_COUNT`, `DSI_STATUS`, `DSI_INT_CTRL` | Bit 31 says whether silicon is in the autorefresh kickoff the driver uses instead of one-shot. `INTR_EN` confirms the following status bits are unmasked raw status |
| C1160, immediate | Same set | Shows whether bit 31 changes across `CTL_START`, and records the `WR_PTR` edge beside the autorefresh word |
| Spin, still inside 20 ms | Each iteration reads `PP_INT_COUNT_VAL` and, once the low 16 bits are in `[0x770, 0x790]`, one tuple of `PP_INT_COUNT_VAL`, `INTR_STATUS`, `PP_LINE_COUNT`, `PP_OUT_LINE_COUNT`, `DSI_STATUS`, `DSI_INT_CTRL`. Continue until the low 16 bits have wrapped below `0x100`, and record that tuple too. Stop at 20 ms if the window is missed | Turns the `0x780`/`0x785` region from a gap between `0x777` and `0x41d` into a same-sample observation, paired with the driver's line-out and DSI-busy bits |
| Existing +20 ms and final samples | Keep the R11C set, plus `PP_AUTOREFRESH_CONFIG` | Preserves the comparison with R11C |

How to read the result, without treating a name as a new state:

- Bit 31 set at C1150: the one-shot assumption is false on silicon (`HW_READBACK` of the branch the driver actually tests). Do not clear it in that boot.
- Bit 31 clear, `WR_PTR` rises, a sample shows the counter in `[0x780, 0x790]`, and in that same tuple `PP_LINE_COUNT` is 0, bit 8 is 0, and `DSI_STATUS` bit 2 is 0: the programmed start region was reached and the driver's "line out" / completion / DSI-busy signals were not. Request issue is still `UNKNOWN`, because no earlier strobe exists. This is the observation that replaces the current inference.
- `PP_LINE_COUNT >= 1` while `DSI_STATUS` bit 2 is 0 in the same tuple: the closest split this source supports between "PP produced a line" and "DSI did not report MDP busy". Still not a dedicated request-issued bit.
- `DSI_STATUS` bit 2 set, or raw `CMD_MDP_DONE` set: DSI transfer activity, which R11C did not see.

States that this pass cannot separate, and must not claim:

- "PP never armed" versus "PP armed". The source has no armed bit. `WR_PTR` is not that bit.
- "Request issued but DSI did not admit it", unless `PP_LINE_COUNT` or `PP_DONE` moves while DSI busy stays clear. A dedicated request-issued register was not found.

## Required decisions

### 1. What are `MDP_INTR` `0x10000` and `0x11000`?

`SOURCE_PROVEN`.

- `0x00010000` is bit 16, `MDSS_MDP_INTR_PING_PONG_0_WR_PTR`, PP0 write-pointer / lineptr status (`mdss_mdp_hwio.h:104`, `mdss_mdp.c:219`).
- `0x00011000` is bit 16 plus bit 12, `MDSS_MDP_INTR_PING_PONG_0_RD_PTR`, PP0 read-pointer / command-mode vsync status (`mdss_mdp_hwio.h:100`, `mdss_mdp.c:215`).

They are not `PP_DONE`, not autorefresh-done, and not a DSI request-issued flag.

### 2. Does existing R11C evidence prove PP0 was armed?

`UNKNOWN`.

The source does not define an armed state. R11C does prove that `PING_PONG_0_WR_PTR` rose from 0 to 1 across the only write in that interval, `CTL_START=1`, while `PP_INT_COUNT_VAL` did not pass `PP_WR_PTR_IRQ=0`. The same bit had already been set before kickoff. That is not a source-defined arming proof.

### 3. Does existing R11C evidence prove PP0 issued a DSI transfer request?

`UNKNOWN`.

Not `YES — HW_PROVEN`: no source-defined request-issued bit was captured. `WR_PTR` and `RD_PTR` are not that bit.

Not `NO — HW_PROVEN`: `DSI_STATUS` bit 2 and `CMD_MDP_DONE` staying clear show no observed DSI transfer. The driver does not define those clear bits as "PP never emitted the request". Sampled `PP_LINE_COUNT=0` and sticky `PP_DONE=0` show no observed line-out and no completion. They still leave the generation edge itself unobserved.

### 4. Is `PP_AUTOREFRESH_CONFIG` relevant to the one-shot path?

Yes, as the alternate kickoff, with the silicon value `UNKNOWN`.

`__mdss_mdp_kickoff` ([`mdss_mdp_intf_cmd.c:2942-2986`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L2942-L2986)): state `ON` or `ON_REQUESTED` writes `BIT(31) | frame_cnt` and does not write `CTL_START`. The one-shot `else` writes `CTL_START` and does not write this register. Disabled is a write of `0` on the disable paths. Init sets the software state to `OFF` without a register write (`3658-3659`). Setup reads bit 31 and, only if set, writes `0` (`509-533`).

A leftover bit 31 would make the downstream driver skip `CTL_START`. XNU writes `CTL_START` anyway. What bit 31 does in combination with that write is `UNKNOWN`. XNU never writes `0x00971030`, which does not prove the word is zero.

### 5. Narrowest current blocker

`PP0 request generation not proven`.

CTL/SSPP consumed the single kickoff. PP pointer interrupts that the driver uses for lineptr and vsync did latch. The source does not define those latches as the command request, and the signals it does use for line-out, PP completion, and DSI MDP-busy were not observed.

### 6. Passive signals for the next single boot

Only these, all source-backed reads:

- `PP_AUTOREFRESH_CONFIG` at `0x00971030` (bit 31 and the written frame-count bits).
- `MDSS_MDP_REG_INTR_EN` at `0x00901010`.
- `MDSS_MDP_REG_INTR_STATUS` at `0x00901014`, kept sticky after the existing pre-kick clear, decoded as bits 8, 12, 16, and 20.
- `PP_INT_COUNT_VAL` at `0x00971014`, including one tuple while the low 16 bits are in `[0x770, 0x790]` and one tuple after the height wrap.
- `PP_LINE_COUNT` at `0x0097102c` in those same tuples.
- `DSI_STATUS` at `0x00994008` bit 2, and `DSI_INT_CTRL` at `0x00994110` raw `CMD_MDP_DONE` bit 8, including the existing tight poll.

`PP_OUT_LINE_COUNT` may stay in the snapshot because R11C already reads it, but this source does not define it. Do not add `INTF_DSI_CMD_MODE_TRIGGER_EN` or a guessed DSI trigger.

Audit stops here. No Retry #11D-B implementation, no kernel build, no boot, no MMIO write.
