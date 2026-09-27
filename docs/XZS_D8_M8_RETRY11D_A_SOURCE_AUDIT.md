# D8-M8 Retry 11D-A — PP0 to DSI admission conformance audit

## A. Scope and repository identity

- Branch `xzs-d8-display-m8-resume`, audited HEAD `784bd807781afc41710eb7a635f1fd2e697788cb`; worktree was clean at entry.
- Sony source: `sonyxperiadev/kernel`, branch `aosp/LA.UM.7.1.r1`, pinned commit `5772572ccdfbc16c270d33f8fa6b55d33d27709c`. Its MDSS files are directly under `drivers/video/fbdev/msm/`, **not** `drivers/video/fbdev/msm/mdss/`.
- Existing R11C evidence only. No hardware command, MMIO write, build, flash, or new display experiment was performed for this audit. R11A/B/C evidence directories were not changed.
- Evidence labels: `HW_READBACK_PROVEN` means an R11C value read from silicon; `SOURCE_PROVEN` means the pinned downstream source establishes the mapping or path; `TRACE_PROVEN` means the XNU source/transcript establishes an executed step; `INFERENCE` and `UNKNOWN` are deliberately weaker.

## B. R11C raw-state extraction

The complete original rows are in [raw-handshake-snapshots.txt](../artifacts/hw/d8m8/m8-7-retry11c/raw-handshake-snapshots.txt), with [pre-kick.txt](../artifacts/hw/d8m8/m8-7-retry11c/pre-kick.txt), [kickoff.txt](../artifacts/hw/d8m8/m8-7-retry11c/kickoff.txt), and [host.txt](../artifacts/hw/d8m8/m8-7-retry11c/host.txt). The paths in these links are relative to this document's `docs/` directory.

All 13 snapshots read `DSI_CTRL=0x000001f5`, `DSI_MDP_CTRL=0x00000008`, `DSI_DMA_CTRL=0x14000000`, `DSI_STREAM0_CTRL=0x0ca90039`, `DSI_STREAM0_TOTAL=0x07800438`, `DSI_TRIG_CTRL=0x80000004`, `DSI_INT_CTRL=0xa220aa02`, `DSI_STATUS=0`, `ACK_ERR=0`, `TIMEOUT=0`, `PP_LINE=0`, and `PP_OUT_LINE=0`. `DSI_DCS_CMD_CTRL=0x00013c2c` appears in the pre-kick transcript, not in the 13 snapshot rows. The previous summary omitted `DSI_CTRL`, `DSI_DMA_CTRL`, `PP_INT_COUNT`, and `MDP_INTR` from its compact table.

| Checkpoint | Time (µs) | CTL_START count | PP_INT_COUNT_VAL | MDP_INTR | CTL_FLUSH | RGB0 current source |
|---|---:|---:|---:|---:|---:|---:|
| C1130 | 640841540 | 0 | 0x709 | 0x11000 | 0 | 0 |
| C1140 | 640841835 | 0 | 0x730 | 0x11000 | 0x20048 | 0 |
| C1110 | 641858091 | 0 | 0x197 | 0 | 0x20048 | 0 |
| C1120 | 641858126 | 0 | 0x19b | 0 | 0x20048 | 0 |
| C1150 | 641859568 | 0 | 0x258 | 0 | 0x20048 | 0 |
| C1160, immediate | 641859608 | 1 | 0x25d | 0x10000 | 0 | 0x98000000 |
| C1170, +49 µs | 641859657 | 1 | 0x263 | 0x10000 | 0 | 0x98000000 |
| C1180, +248 µs | 641859856 | 1 | 0x27d | 0x10000 | 0 | 0x98000000 |
| C1190, +1 ms | 641860608 | 1 | 0x2e0 | 0x10000 | 0 | 0x98000000 |
| C11A0, +5 ms | 641864606 | 1 | 0x4ea | 0x10000 | 0 | 0x98000000 |
| C11B0, +10 ms | 641869606 | 1 | 0x777 | 0x10000 | 0 | 0x98000000 |
| C11C0, +20 ms | 641879606 | 1 | 0x41d | 0x11000 | 0 | 0x98000000 |
| C11D0, final | 641879641 | 1 | 0x421 | 0x11000 | 0 | 0x98000000 |

`PP_INT_COUNT_VAL` is **not missing**. It advances and wraps; C11B0 is `0x777`, just below `START_POS=0x780`, and C11C0 is `0x41d` after a wrap. Crossing `0x780` between these samples is an `INFERENCE`, not a captured instantaneous gate assertion. The raw summary reports no DSI busy or raw CMD_MDP_DONE during bounded intervening polls. PP line/out remain zero. `CTL_FLUSH` consumption and RGB0 source latching prove some CTL/SSPP state was consumed, **not** PP request generation or DSI rejection.

`PP_AUTOREFRESH_CONFIG` (+0x030) was not read in R11C, pre-kick, or host transcript: `UNKNOWN`. XNU M8 source has no write to this register, which is not a substitute for silicon readback. No direct PP request-pending/issued state was captured.

## C–F. DSI command engine, lifecycle, and DCS side effects

Pinned Sony [`mdss_dsi_host.c` lines 402–469](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L402-L469) constructs `DSI_CTRL` with bit 0 controller enable, bit 2 command-mode engine, bit 8 clock enable, bits 4–7 data lanes 0–3, optional bit 20 ECC and bit 24 CRC. R11C `0x1f5` decodes as 0/2/4/5/6/7/8 set; bit 1 video mode, bit 20 ECC, and bit 24 CRC clear. No Keyaki property was found requiring optional CRC/ECC. This proves the **register-defined** command engine enabled at C1150; separate runtime clock-domain delivery and actual request acceptance are not proved by this word alone.

| Lifecycle point | XNU/R11C value | Evidence and source behavior |
|---|---:|---|
| Pre-M4 reset | 0 | R11C `host.txt:343`, `HW_READBACK_PROVEN` |
| M4 configured with master off | 0x1f4 | `host.txt:406`, `HW_READBACK_PROVEN` |
| M4 enable | 0x1f5 | `host.txt:427`, `HW_READBACK_PROVEN` |
| During/after SLPOUT, TEON, DISPON | UNKNOWN for each immediate point | XNU DCS helper does not touch DSI_CTRL (`TRACE_PROVEN`), but no per-command DSI_CTRL readback was recorded. C1130 after panel prepare is 0x1f5. |
| C1150 immediately before CTL_START | 0x1f5 | R11C raw readback, `HW_READBACK_PROVEN` |
| C1160 through C11D0 after CTL_START | 0x1f5 in every sample | R11C raw readback, `HW_READBACK_PROVEN` |

Exact normal downstream write lifecycle:

| Function and pinned source | Old logical state → written state | Why; persistence |
|---|---|---|
| [`mdss_dsi_host_init`, `mdss_dsi_host.c:402-469`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L402-L469) | reset/previous → `0x1f5` for four-lane command panel with optional checks off | Host configuration; persistent |
| [`mdss_dsi_op_mode_config`, `mdss_dsi_host.c:1076-1104`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L1076-L1104) | prior lower mode bits → lower bits `0x5` for command mode; high lane/clock fields preserved | Called by `mdss_dsi_ctrl_setup` after host init; persistent command-mode selection. For a command panel already at 0x1f5, remains 0x1f5. |
| [`mdss_dsi_sw_reset`, `mdss_dsi_host.c:503-533`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L503-L533) | 0x1f5 → 0x1f4 → saved 0x1f5 | Temporary controller reset with `restore=true` in [`mdss_dsi_on`, `mdss_dsi.c:1597-1609`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi.c#L1597-L1609). |
| [`__mdss_dsi_cmd_mode_config`, `mdss_dsi_host.c:1844-1875`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L1844-L1875) | **video** mode old bit 1 set → add bit 2 temporarily → clear bit 2 afterward | DCS transmission for a video panel only; [`mdss_dsi_cmds_tx:1915-1932`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L1915-L1932) calls disable only when the video-mode temporary change flag was set. For a normal command panel, `mode_changed=false`: command mode remains enabled. |

Sony [`mdss_dsi_on`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi.c#L1527-L1634) initializes and later gates command-panel clocks via clock APIs, not by clearing DSI_CTRL bit 2. [`mdss_dsi_off:1834`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi.c#L1828-L1835) selects command mode to send off commands; that is a panel-off path, not a normal kickoff transition. [`MDSS_EVENT_DSI_CMDLIST_KOFF`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi.c#L2866-L2868) goes to `mdss_dsi_cmdlist_commit`, which does not disable DSI_CTRL command mode on its from-MDP path.

XNU [`xzs_d8m6_transmit_cmd`](../src/xnu/pexpert/arm/xzs_d8m6.h) sends all five named DCS commands through DMA TPG. It writes DSI_TRIG_CTRL=`0x4`, ORs transient force-on bits into DSI_CLK_CTRL, configures TPG/DMA, masks/acks DMA_DONE, pulses **DMA** SW trigger at `+0x090`, then restores DSI_CLK_CTRL. It never saves, writes, or restores DSI_CTRL and never clears command mode. [`xzs_d8m8_panel_prepare`](../src/xnu/pexpert/arm/xzs_d8m8.h) restores DSI_TRIG_CTRL=`0x80000004` after DISPON; R11C reads that value before and after CTL_START. DISPOFF/SLPIN use the same helper only after the observation window for graceful shutdown. `DSI_INT_CTRL` after panel prepare retains the CMD_MDP_DONE mask (bit 9 = 1), but DMA helper RMW/ack makes the unrelated raw bits unsuitable for an exact whole-word comparison.

## E. Conformance matrix

R11C raw values are `HW_READBACK_PROVEN`; expected semantics below are from the pinned Sony source. `MATCH` means the source-defined fields match; it does not certify unseen dynamic state.

| Block | Register/state | R11C raw | Expected downstream | Result | Evidence |
|---|---|---:|---|---|---|
| DSI | DSI_CTRL C1150 | 0x000001f5 | enable + command + clock + lanes, video off | MATCH | `HW_READBACK_PROVEN` + [`mdss_dsi_host.c:402-469`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L402-L469) |
| DSI | CMD_MDP_CTRL | 0x00000008 | RGB888 format 8; no swaps | SOURCE_MATCH | [`mdss_dsi_host.c:379-390`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L379-L390) |
| DSI | MDP_DCS_CMD_CTRL | 0x00013c2c (pre-kick) | insert DCS, continue 0x3c, start 0x2c | SOURCE_MATCH | [`mdss_dsi_host.c:392-398`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L392-L398); no C1150 row read |
| DSI | STREAM0_CTRL | 0x0ca90039 | 3241 bytes/line, VC0, DCS long write | SOURCE_MATCH | [`mdss_dsi_host.c:1430-1479`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L1430-L1479) |
| DSI | STREAM0_TOTAL | 0x07800438 | 1920 × 1080 | SOURCE_MATCH | same source |
| DSI | TRIG_CTRL | 0x80000004 | TE select 1, stream 0, MDP trigger NONE, DMA trigger SW | SOURCE_MATCH | [`mdss_dsi_host.c:420-427`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L420-L427), Keyaki DTS properties |
| DSI | INT_CTRL | 0xa220aa02 | CMD_MDP_DONE raw bit 8 = 0, mask bit 9 = 1 | MATCH for relevant bits; whole-word `SOURCE_AMBIGUOUS` | [`mdss_dsi.h:207-221`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi.h#L207-L221) |
| DSI | DMA_CTRL | 0x14000000 | embedded + LP mode (bits 28,26) | SOURCE_MATCH | [`mdss_dsi_host.c:1096-1103`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L1096-L1103) |
| PP0 | TEAR_CHECK_EN | 1 | enabled for command path | MATCH | `pre-kick.txt`; [`mdss_mdp_intf_cmd.c:210-235`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L210-L235) |
| PP0 | SYNC_CONFIG_VSYNC / HEIGHT | 0x80093 / 0x873 | bit 19 internal counter on; bit 20 external TE off; 2163 lines | **SOURCE_MISMATCH versus literal Keyaki DTS bit 20**, intentional SW-TE override; other fields match | `pre-kick.txt`; [`mdss_mdp_intf_cmd.c:277-357`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L277-L357) |
| PP0 | SYNC_WRCOUNT / VSYNC_INIT | 0x785 / 0x780 | start 0x780 + threshold 4 + 1; init 0x780 | MATCH | same source; `pre-kick.txt` |
| PP0 | SYNC_THRESH / START_POS | 0x40004 / 0x780 | continue/start thresholds 4/4; start 1920 | MATCH | same source; `pre-kick.txt` |
| PP0 | RD_PTR_IRQ / WR_PTR_IRQ | 0x781 / 0 | read pointer 1921, write pointer 0 | MATCH | same source; `pre-kick.txt` |
| PP0 | INT_COUNT_VAL | C1150 0x258 → C11B0 0x777 → C11C0 0x41d | running/wrapping counter | activity MATCH; request `UNKNOWN` | R11C raw snapshots |
| PP0 | AUTOREFRESH_CONFIG | UNKNOWN | disabled / BIT(31)=0 for one software kickoff | UNKNOWN, critical readback absent | [`__mdss_mdp_kickoff:2942-2986`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L2942-L2986) |
| CTL0 | TOP | 0x00020020 | CMD bit 17 + `MDSS_MDP_INTF1` enum value 2 shifted by 4 | MATCH | [`mdss.h:218-224`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss.h#L218-L224), [`mdss_mdp_ctl.c:4025-4054`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_ctl.c#L4025-L4054) |
| CTL0 | LAYER_0 | 0x00000200 | RGB0 at LM0 base stage | MATCH | `pre-kick.txt`; existing [M8 source audit](XZS_D8_M8_SOURCE_AUDIT.md) |
| CTL0 | FLUSH | 0x20048 → 0 | CTL + LM0 + RGB0 flush; consumed after CTL_START | MATCH for CTL/SSPP consumption only | R11C raw snapshots; [`mdss_mdp_ctl.c:6102-6134`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_ctl.c#L6102-L6134) |
| MDSS | DISP_INTF_SEL | 0x00000100 | DSI0 selected for INTF1 | MATCH | `pre-kick.txt`; [`mdss_mdp_ctl.c:4408-4415`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_ctl.c#L4408-L4415) |

The Keyaki panel node at `artifacts/display-audit/keyaki.dts:1781` has `qcom,mdss-dsi-te-using-te-pin`; [`mdss_dsi_panel.c:2818-2821`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_panel.c#L2818-L2821) therefore selects hardware TE in unmodified downstream. XNU's bit-20-clear internal-clock setting is a **known, deliberate Retry #10 divergence**, made because the physical TE line was not observed and independently shown to run the PP counter/RD/WR-pointer timing. This audit records the literal source mismatch without inferring it is the failure cause or proposing a reversal to a missing physical signal.

The old plan text in `docs/XZS_D8_M8_SOURCE_AUDIT.md` and a dry-run print string in `xzs_d8m8.h` show `CTL_TOP=0x20010`; those are stale textual expectations. The **executed** XNU write/readback is `0x20020`, matching the pinned Sony enum (`MDSS_MDP_INTF1=2`). Do not alter the functional register from this audit.

## G. PP request-generation and INTF audit

Normal Sony order is [`mdss_mdp_display_commit` CTL_FLUSH then display function](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_ctl.c#L6102-L6134) → [`mdss_mdp_cmd_kickoff`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L3040-L3185) → panel/power/resource/ROI state and `MDSS_EVENT_DSI_CMDLIST_KOFF` → [`mdss_dsi_cmdlist_commit`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L2724-L2885) → [`mdss_dsi_cmd_mdp_start`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_dsi_host.c#L2548-L2558) (Linux interrupt/software completion only) → PP completion IRQ setup → [`__mdss_mdp_kickoff` CTL_START](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L2930-L2991). The source has no normal-path DSI MDP SW-trigger write. MDP trigger `NONE` in DSI_TRIG_CTRL is therefore not itself a mismatch.

PP0 normal prerequisites include selected PP0/mixer/CTL/DSI topology, panel and MDP resources active, valid full-frame ROI, a configured enabled tearcheck clock/counter, CTL flush, and single CTL_START. [`mdss_mdp_cmd_tearcheck_cfg:295-357`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L295-L357) establishes the counter and threshold programming; [`__mdss_mdp_kickoff:2964-2986`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_intf_cmd.c#L2964-L2986) describes optional scanline-range waiting followed by CTL_START. The precise silicon event that raises a PP-to-DSI request is internal to PP/CTL hardware and not exposed by these C functions. The R11C counter and RD/WR pointer IRQ activity proves tearcheck timing, **not** that this request was emitted. PP0_LINE/OUT=0 and DSI busy/done=0 leave the first unobserved boundary between CTL/SSPP consumption and PP request/DSI admission.

`MDSS_MDP_REG_INTF_DSI_CMD_MODE_TRIGGER_EN` is **defined** at +0x084 in [`mdss_mdp_hwio.h:669`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/mdss_mdp_hwio.h#L669). No use appears in the pinned normal command-mode functions `mdss_mdp_intf_cmd.c`, `mdss_mdp_ctl.c`, or their DSI kickoff chain: `used by exact path=no`, `required on MSM8996 normal path=no evidence / not source-supported`. R11C did not read it; its value is `UNKNOWN`. Its name is not grounds for a write.

The separate v2 [`dsi_host_v2.h:63-65`](https://github.com/sonyxperiadev/kernel/blob/5772572ccdfbc16c270d33f8fa6b55d33d27709c/drivers/video/fbdev/msm/dsi_host_v2.h#L63-L65) defines a named MDP SW trigger at **v2** +0x090. It cannot establish a v1.4 +0x094 definition. In the applicable legacy v1.4 host, +0x090 is the **DMA** SW trigger and no normal command-MDP path writes +0x094. Whether v1.4 hardware implements +0x094 is `UNKNOWN`; whether the exact normal Keyaki path writes an MDP SW-trigger is **NO — SOURCE_PROVEN**.

## H–J. Decisions, classification, smallest next proposal

1. **Was DSI command-MDP engine in a valid enabled state at C1150? YES — SOURCE + HW READBACK PROVEN for the DSI_CTRL-defined enable state.** Bit 0 controller, bit 2 command engine, bit 8 clock-enable and all four data-lane bits were 1; bit 1 video mode was 0. This does not by itself prove dynamic DSI admission or independent clock-domain delivery.
2. **Did PP0 satisfy all source-defined conditions to issue the transfer request? UNKNOWN.** Observed static tearcheck/topology values and a running counter match, but R11C lacks PP_AUTOREFRESH_CONFIG readback and a direct PP request-pending/issued observation. A threshold crossing inferred from sparse counter samples does not close that gap.
3. **Does the normal Keyaki path require an MDP SW-trigger write? NO — SOURCE_PROVEN.** Existence of a similarly named register in the distinct v2 map is not use by v1.4 normal command path.
4. **Classification: R11D-A5 — INSUFFICIENT_EXISTING_EVIDENCE.** The only identified literal Keyaki static divergence is the already-established internal-TE override; no evidence shows it prevents request generation, and reverting it would reintroduce the unobserved physical TE dependency. A1/A2 would overclaim a causal gate fault; A3 as a new corrective finding would also overclaim. A4 would imply the critical static autorefresh state was verified; it was not. No direct PP request-generation signal was captured. Refine R11C-A causality to “no PP0→DSI command-transfer activity observed after CTL_START,” not “DSI rejected a request.”
5. **Proposed Retry 11D-B only; do not implement:** one XNU boot, one CTL_START, same 20 ms bounded passive handshake observation and safe shutdown, with **no corrective MMIO write**. Add pre-kick and post-kick passive readback of `PP_AUTOREFRESH_CONFIG` (+0x030), and timestamped PP_INT_COUNT_VAL/PP_LINE/OUT plus applicable raw PP/CTL status densely across the known `START_POS=0x780` / `SYNC_WRCOUNT=0x785` crossing. Keep DSI_STATUS and raw CMD_MDP_DONE observations. If source inspection cannot identify a direct PP-request status bit, state that explicitly rather than treating the counter or interrupt as proof of issue. Do not try DSI MDP SW-trigger or bundled fixes.

Audit stops here. No #11D-B implementation or hardware action.
