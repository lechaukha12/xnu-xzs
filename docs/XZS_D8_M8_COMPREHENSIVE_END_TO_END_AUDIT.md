# D8-M8 comprehensive end-to-end command-mode audit

Read-only synthesis of the Sony pin, the XNU tree, and the existing hardware artifacts. No build, no boot, no MMIO change.

## 1. Executive summary

The last hardware boundary that is actually proven is still Retry #11D-B: one `CTL_START`, flush consumed, RGB0 current source latched, tearcheck counter sampled at `0x780` and `0x785` and then wrapped, autorefresh held at 0, and `PP_LINE`, `PP_OUT`, `PP0_DONE`, DSI busy, and `CMD_MDP_DONE` stayed clear in the stored samples. Retry #11E did not move that boundary. It aborted inside the existing framebuffer map, before `M8_1 = PASS`, before any new read, and before `CTL_START`.

```text
CURRENT_BLOCKER=PP_FRAME_START_GATE_STILL_UNKNOWN
LAST_HW_PROVEN_BOUNDARY=CTL flush consumed, RGB0 source latched, tearcheck counter through START_POS and SYNC_WRCOUNT
FIRST_UNPROVEN_BOUNDARY=MDP core source/rate, then actual RGB0 AXI fetch, then PP pixel release
DOWNSTREAM_STATE_NOT_YET_REACHED=PP_LINE, DSI admission, CMD_MDP_DONE, visible pixels
```

Sony and XNU match on the static command-mode datapath that has been read back. They do not match on four runtime resources Sony programs and XNU does not: MDP core rate, interconnect vote, register-bus vote, and probe-time VBIF QoS. None of those four has a proven causal link to `PP_LINE=0`. No correction meets the seven-point rule. The next justified hardware task, when one is authorized, is a passive MMCC read of `mdp_clk_src` after a framebuffer init that is byte-for-byte the #11D-B path. It is not specified as code in this commit.

Machine-readable ledger: `docs/XZS_D8_M8_EVIDENCE_LEDGER.csv`.

## 2. Repository and source identity

```text
WORKTREE=/Users/lechaukha12/Desktop/xnu-xzs
ORIGIN=https://github.com/lechaukha12/xnu-xzs.git
BRANCH=xzs-d8-display-m8-resume
HEAD_AT_AUDIT_START=2e1db4001dd788ffb26b757a7efe25a49429c04c
WORKTREE_AT_START=clean
```

Sony primary source:

```text
URL=https://github.com/sonyxperiadev/kernel
BRANCH=aosp/LA.UM.7.1.r1
COMMIT=5772572ccdfbc16c270d33f8fa6b55d33d27709c
TREE=drivers/video/fbdev/msm/
```

Clock parent and divider facts cited below come from that tree's `drivers/clk/qcom/mmcc-msm8996.c` and `clk-rcg2.c` at the same pin. No newer MDP generation is used.

Panel blob used for the DCS compare is the extracted Keyaki DTS already in the tree at `artifacts/display-audit/keyaki.dts`, node `somc,sharp_synaptics_cmd_9_panel` (line 1740). It is a decompile of the device tree this port already targets, not a second kernel.

Retry #11E identity, booted and then documented:

```text
COMMIT_BOOTED=e869829f7a903619e090a37e0cc00a11292d6cc0
DOC_COMMIT=2e1db4001dd788ffb26b757a7efe25a49429c04c
KERNEL_SHA256=f5dd96e9fdcdff9b80334a63ef59245a8c91753999cda42b705f6d2d44cf98e2
BOOT_SHA256=f9745bb095726381b3295ec78a0d1b8fdc03b0e59b661d588313f02a5f13e2be
PAC=0
```

#11D-B booted code is `5600ef88f9d0e17474d87c9f92810424b3ad5e9f`.

## 3. Canonical D8-M8 history

Retries #1 through #10 are the table in `docs/XZS_D8_M8_DEFERRED.md` section 6. The functional commits on this branch are the ones named below. Section 7.2 of that deferred report concluded that a DSI MDP software trigger was required. Retry #11A's source trace superseded that conclusion: `mdss_dsi_cmd_mdp_start` enables an IRQ and sets a software busy flag. It does not write `0x00994094`.

| Retry | Commit | Hypothesis | Functional change | Hardware result | Proved | Disproved | Left open |
|---|---|---|---|---|---|---|---|
| 1 | `9154f24` (record); code `8c76a25` | One `CTL_START` completes a frame | First kickoff poll | FAIL_CLEAN, `PP0_DONE=0` at 100 ms | Kickoff can be issued once and shut down | A bare start is not a completed frame | Why done stays clear |
| 2 | `7bc2f68` / `914cde6` | Tearcheck geometry or `DSI_CMD_MDP_CTRL` | Minimum PP timing and MDP ctrl | FAIL_CLEAN, done still times out | Those writes are not sufficient | Done is not fixed by that pair | Same gate |
| 3 | `bfde555` / `b24bf0f` | Free-run / zero tearcheck can scan | PP free-run experiment | FAIL_CLEAN | Command mode needs ping-pong timing | Free-run as a substitute | Timing image |
| 4 | `d39ee25` / `8d4cb9a` | VSYNC clock is the missing engine | 19.2 MHz VSYNC RCG+CBCR | FAIL_CLEAN | VSYNC unhalted; RGB current latched `0x98000000` | Latch equals fetch | Pixel release |
| 5 | `132b132` / `b097f22` | CTL routed to the wrong interface | `DISP_INTF_SEL=0x100`, `CTL_TOP=0x20020` | FAIL_CLEAN | Routing readback | Routing as the cause of done=0 | Pixel release |
| 6 | `43fb036` / `6195895` | Panel was not powered | Real rails and DCS prepare | FAIL_CLEAN, `PANEL_READY=yes` | Power and DCS ACK | "panel off" as the cause | Pixel release |
| 7 | `dd4641f` / `4b0ec84` | Flush mask still had the video INTF bit | Flush `0x00020048` | FAIL_CLEAN | Flush consumed to 0; SSPP readback | Wrong flush mask | Pixel release |
| 8 | `79647b1` | Tearcheck enable is the arm | `PP_TEAR_CHECK_EN=1` | FAIL_CLEAN | WR_PTR status `0x00010000` | "tearcheck disabled" | Line out |
| 9 | `8fb8e2d` | External TE must be synchronized | Keyaki geometry, GPIO10 sample | ABORT_CLEAN, no `CTL_START` | 50 ms, 0 TE edges, line stayed low | Physical TE is present | Whether internal mode can replace it |
| 10 | `0243572` | Qualcomm SW-TE override | `PP_SYNC_CONFIG_VSYNC=0x00080093` | FAIL_CLEAN | WR_PTR at 0 µs, RD_PTR at 10 ms, counter period ~16.56 ms | Counter-not-running | `PP_LINE` stayed 0 |
| 11A | `33990cf` | DSI software trigger or busy/done was missed | Passive snapshots only | Handshake stayed quiet | Source order is flush, DSI software prep, PP IRQ, `CTL_START`. No trigger write in `cmd_mdp_start` | Deferred §7.2 trigger requirement | Request versus admission |
| 11B | `2f17e0b` | Framebuffer word is wrong or unmapped at kickoff | Probe of the first word | Word already valid | Mapping and contents before kickoff | Bad FB as the cause | Downstream gate |
| 11C | `784bd80` | The PP-to-DSI edge is visible in status | Dense passive trace | Flush consumed, source latched, line 0, done 0 | `0x10000` is WR_PTR, `0x11000` is WR+RD. Neither is a request strobe | Using those bits as proof of a request | The generation edge |
| 11D-A | docs `0d45cab` | A missing admission write exists | Source only | No boot | No omitted one-shot DSI trigger write | Another invented DSI poke | PP line-out |
| 11D-B | `5600ef8` | Autorefresh or a missed `0x780`/`0x785` sample | Passive counter trace | R11D-B2 | Autorefresh 0; direct `0x780`, `0x785`, and wrap; line and out 0 | Autorefresh-on and "counter never arrives" | Pixel release |
| 11E | `e869829` | Core rate, VBIF QoS, or fetch is visible | Passive reads added after `M8_1 PASS` | ABORTED_BEFORE_M8_1 / INCONCLUSIVE | The new reads were not executed | Nothing about clocks or VBIF | Reset cause, and every #11E runtime answer |

## 4. Retry #11E reset forensics

`git diff 5600ef88 e869829` changes five files. Three are documentation. The runner is host-side. The only device code change is `src/xnu/pexpert/arm/xzs_d8m8.h`, 214 insertions, 0 deletions in that range.

Classification of that device diff:

| Class | What was added |
|---|---|
| Telemetry-only | `xzs_d8m8_r11e_capture`, `xzs_d8m8_r11e_emit`, `xzs_d8m8_r11e_note_count`, sample printout |
| New mapping code | none. `ml_io_map_unmappable(g_m8_fb_pa, g_m8_fb_size, 0x6)` is unchanged |
| New register addresses | MMCC `0x008c2040/2044/2304/2308/2310/231c/2080/2084/2328`, VBIF `0x009b0000+{0x20,0x24,0x28,0x2c,0xb0,0x200,0x204,0x20c}`, `MDSS_REG_HW_VERSION` `0x00900000` |
| New MMIO reads | those addresses, all `d8p1_read32` |
| New MMIO writes | none. `git diff` shows no added `d8p1_write32` |
| New data | `g_r11e_samples[16]`, about 2 KB of BSS, plus five flag bytes |
| Looping | one-shot notes inside the existing post-start poll. Not on the M8-1 path |
| Logging | `R11E_SAMPLE` lines printed only from `r11e_emit` |
| Timing | no added delay. The capture calls sit on the existing kickoff timeline, which M8-1 never reached |

The only edit inside `xzs_d8m8_fb_init` is one call placed after the PASS prints:

```text
M8_1 = PASS
breadcrumbs 0xB11A1, 0xC1100
xzs_d8m8_r11e_capture(0x1100)    /* new */
```

`host.txt` for `artifacts/hw/d8m8/m8-7-retry11e/` stops at:

```text
[MAP] Mapping physical framebuffer via ml_io_map_unmappable...
```

That line is printed before the map call and before any PASS line. The host waited 45 seconds and received no further byte. `M8_1` does not occur. `R11E_SAMPLE` does not occur. `CTL_START_WRITE` does not occur. A later USB scan showed Sony `S1Boot Fastboot` instead of the XNU gadget `1209:000a`.

So the earliest new executable statement in M8-1 was not reached. The binary that contains the new code did run: MMAGIC, MDSS power, M3, M4, P1, and `PANEL_READY=yes` completed, with the pre-existing counters `SError=0` and `PANIC=0`. Those counters were printed before M8-1 and do not explain the reset.

Stack use of `fb_init` was not measured. The #11D-B kernel image was replaced by the #11E link, and this audit does not rebuild. A larger stack frame from the later call is possible in principle and is not evidenced.

```text
R11E_RESET_CAUSE=UNKNOWN
```

`LIKELY` is not used. The log proves where execution stopped. It does not prove why `io_map` did not return, and it does not prove that releasing the USB interface afterward was the reset.

## 5. Evidence grading model

| Level | Name | Meaning |
|---|---|---|
| 4 | HW_PROVEN | Direct observation plus source semantics that say what the observation means |
| 3 | HW_READBACK_PROVEN | Exact register readback whose field meaning is sourced |
| 2 | SOURCE_PROVEN | The pinned Sony source establishes the behavior |
| 1 | TRACE_PROVEN | Control flow and ordering only |
| 0 | INFERENCE | Reasonable and not demonstrated |
| — | UNKNOWN | Not enough evidence |

Controversial claims in this document are written as `CLAIM` / `EVIDENCE_LEVEL`.

## 6. Hypothesis ledger

The CSV is the permanent list. States used there are only `PROVEN`, `DISPROVEN`, `STILL_OPEN`, `NOT_APPLICABLE`, `SUPERSEDED`, and `INSUFFICIENT_EVIDENCE`. `H19` records the superseded software-trigger claim. Closed rows have `ReopenAllowed=no`.

## 7. Sony full first-frame sequence

From `mdss_mdp_display_commit`, `mdss_mdp_cmd_kickoff`, `__mdss_mdp_clk_control`, and `mdss_dsi_cmd_mdp_start` at the pin. This is the normal one-shot command path, not the autorefresh branch.

```text
panel DT parse (mdp-trigger none, dma-trigger sw, wr-mem 0x2c/0x3c)
MDSS power: pm_runtime_get_sync
register-bus vote
SMMU attach (not used by this port's bypass)
msm_bus_scale context vote
clocks, separately: AHB, AXI, MDP core (rate from mdss_mdp_set_clk_rate), VSYNC 19.2 MHz if tearcheck already enabled
optional lut/tbu/throttle/mnoc: not in the 8996 MDSS clock-names
VBIF QoS remap at probe for RGB xin 1, legacy 2-bit map, DT <1 2 2 2>
DSI PLL, PHY, host, lanes
panel rails, reset, on-command, post-panel-on command
tearcheck program, vsync clock, PP enable
framebuffer, RGB pipe, LM, CTL_TOP, layer mux
perf update: core rate again and ctl_perf_update_bus when bandwidth was released or the mixer changed
resource control on kickoff, if the command resource state is OFF:
  clocks again, DSI clocks on, VOTE_INDEX_LOW, mdss_bus_bandwidth_ctrl(true)
CTL_FLUSH
MDSS_EVENT_DSI_CMDLIST_KOFF
mdss_dsi_cmdlist_commit: optional queued DCS, then mdss_dsi_cmd_mdp_start
  which arms CMD_MDP_DONE IRQ and software mdp_busy, and writes no trigger
PP completion IRQ enabled
CTL_START = 1
wait until PP_LINE_COUNT >= 1 ("first line is out")
DSI_STATUS bit 2 set while the MDP transfer is ongoing
CMD_MDP_DONE (DSI int bit 8) at the end of that transfer
PP0_DONE (MDP int bit 8) at the end of the ping-pong transfer
later: release bus, optional clock gate. Not a first-frame prerequisite
```

`CLAIM: this order is what the C source calls.` `EVIDENCE_LEVEL: 2`.

## 8. XNU full first-frame sequence

Shell dispatch in `xzs_diag.c` cases 50 through 59, functions in `xzs_d8m8.h`. This order is what the runners actually send. It is not copied from Sony and then assumed.

| Stage | File | Function | What it does | Side effect | Expected hardware result |
|---|---|---|---|---|---|
| MMAGIC branches | `xzs_diag.c` clock commands | branch enable | CBCR bit 0 | AHB fabric to MDSS | branch unhalted. HW_PROVEN on M2 |
| MDSS GDSC | power command | `display power mdss-on` | clear collapse, poll PWR_ON | MDSS domain | on. HW_PROVEN |
| AHB, AXI, MDP branch | clock commands | CBCR bit 0 only | no RCG write for MDP | core branch runs on whatever parent the RCG already has | branch on. Rate UNKNOWN |
| M3 | `xzs_d8m3.h` | `display m3-run full` | PLL, byte, pixel, escape, PHY | link clocks stay on | sealed |
| M4 | `xzs_d8m4.h` | `display m4-run full` | DSI host command mode | `DSI_CTRL=0x1f5` | sealed |
| P1 / P2 / M5 | GPIO and SPMI | rails and reset | panel power | sealed |
| M8 prepare | `xzs_d8m8_panel_prepare` | SLPOUT, TEON, DISPON | DCS in LP | ACK 0. Order differs from the DTS, section 15 |
| VSYNC | `xzs_d8m8_vsync_clock_on` | CFG, CMD, CBCR | 19.2 MHz XO div 1 | unhalted at prekick on #11D-B |
| M8-1 | `xzs_d8m8_fb_init` | map PA `0x98000000`, fill, clean | no MDP write | PASS on every retry except #11E |
| M8-2 | `xzs_d8m8_rgb0_config` | RGB0 format, address, stride | SSPP shadow | readback matched |
| M8-3 | `xzs_d8m8_lm0_config` | `LM0_OUT_SIZE`, border 0 | no blend registers | base stage |
| M8-4 | `xzs_d8m8_stream_config` | PP image `0x00080093` / height `0x873`, DSI stream, DCS `0x3c2c` | tearcheck and stream | prekick matched |
| M8-5 | `xzs_d8m8_ctl_config` | `DISP_INTF_SEL`, `CTL_LAYER_0`, `CTL_TOP` | routing | readback matched |
| M8-6 | `xzs_d8m8_flush_config` | `CTL_FLUSH=0x00020048` | flush consumed | HW_PROVEN |
| prekick | `xzs_d8m8_prekick_status` | reads only | none | `PREKICK_READY=YES` on the good boots |
| kickoff | `xzs_d8m8_kickoff` | clear intr `0x00011100`, one `CTL_START` | no DSI write, `INTR_EN` stays 0 | #11D-B result |
| observe | same function | buffered reads, then existing panel shutdown | no second start | `xzs#` returned on #11D-B |

`CLAIM: XNU never calls the Sony bus vote, `mdss_mdp_set_clk_rate`, or the VBIF QoS writer.` `EVIDENCE_LEVEL: 1` for absence in this tree, `2` for what Sony calls.

## 9. Full conformance matrix

Applicable means the 8996 command path actually has the stage. `NOT_APPLICABLE` rows are listed and are not in the fraction.

| Stage | Sony/Linux | XNU | Status | Evidence | Risk |
|---|---|---|---|---|---|
| MDSS GDSC | runtime get | `mdss-on` | MATCH | level 4 | NONE |
| MMAGIC critical branches | hand-off clocks | explicit enables | SOURCE_EQUIVALENT | level 4 | NONE |
| AHB branch | `iface_clk` | CBCR `0x2308` | MATCH | level 4 | NONE |
| AXI branch | `bus_clk` | CBCR `0x2310` | MATCH | level 3 for enable only | LOW |
| MDP branch | `core_clk` | CBCR `0x231c` | MATCH | level 3 for enable only | LOW |
| MDP core rate | `mdss_mdp_set_clk_rate`, table from 85.7 MHz | no RCG write | MISSING_IN_XNU | level 1 | HIGH |
| VSYNC 19.2 MHz | `vsync_clk` | RCG+CBCR | MATCH | level 3 | NONE |
| DSI PLL / PHY / byte / pixel / escape | link clocks | M3 leaves them on | MATCH | level 4 | NONE |
| LUT, TBU, throttle, MNOC | absent from this DTS | not enabled | NOT_APPLICABLE | level 2 | NONE |
| IOMMU attach | `mdss_iommu_ctrl` | SMMU bypass | NOT_APPLICABLE | level 2 | NONE |
| DSI host command mode | host enable | `DSI_CTRL=0x1f5` | MATCH | level 4 | NONE |
| `mdp-trigger none`, `dma-trigger sw` | DT | `DSI_TRIG_CTRL=0x80000004` | MATCH | level 3 | NONE |
| MDP SW trigger poke | not in `cmd_mdp_start` | not written | MATCH | level 2 | NONE |
| Rails, reset, GPIO | panel power | M5/P1/P2 | MATCH | level 4 | NONE |
| On-command bytes | TEON then DISPON, SLPOUT is post-panel-on | SLPOUT then TEON then DISPON | INTENTIONAL_DIVERGENCE | level 2 for the bytes, level 0 for effect | LOW |
| Memory-write opcodes | `wr-mem-start 0x2c`, continue `0x3c` | DCS ctrl low half `0x3c2c` | MATCH | level 2 | NONE |
| RGB0 address, stride, format | pipe program | M8-2 | MATCH | level 3 | NONE |
| LM0 size, border, no blend on base | mixer | M8-3 | MATCH | level 2 | NONE |
| CTL routing and flush `0x00020048` | commit | M8-5/M8-6 | MATCH | level 4 | NONE |
| PP tearcheck image for `hw_vsync_mode=false` | bit 20 clear, height vtotal | `0x00080093`, `0x873` | SOURCE_EQUIVALENT | level 2 for the image, level 0 for sufficiency | HIGH |
| Literal Keyaki bit 20 set and height `0xfff0` | not the override path | not programmed | INTENTIONAL_DIVERGENCE | level 2 | MEDIUM |
| Autorefresh | off on the one-shot branch | register 0 | MATCH | level 3 | NONE |
| Interconnect AB/IB vote | before flush and on resource ON | absent | MISSING_IN_XNU | level 2 for absence, level 0 for effect | MEDIUM |
| Register-bus vote | `VOTE_INDEX_LOW` | absent | MISSING_IN_XNU | level 2 / 0 | MEDIUM |
| VBIF QoS `<1 2 2 2>` for XIN 1 | probe | absent | MISSING_IN_XNU | level 2 / 0 | MEDIUM |
| VBIF halt before start | not on the kickoff path | not written | MATCH | level 2 | NONE |
| `cmd_mdp_start` IRQ arm | software | no DSI ISR | MISSING_IN_XNU | level 1 | LOW |
| PP completion IRQ enable | mask bit 8 | `INTR_EN` required 0 | INTENTIONAL_DIVERGENCE | level 3 | LOW |
| `PP_LINE` advance | driver waits for it | stayed 0 | UNKNOWN | level 4 that it stayed 0 | CRITICAL |
| DSI busy / `CMD_MDP_DONE` | admission and completion | not observed | UNKNOWN | level 3 that the bits stayed 0 | HIGH |

The matrix above is the inventory. The fractions use two explicit subsets, not a percentage of that whole table.

```text
SONY_XNU_STATIC_MATCH=16/18
SONY_XNU_RUNTIME_MATCH=6/18
```

Static denominator, 18. These 16 match: GDSC, MMAGIC, AHB branch, AXI branch enable, MDP branch enable, VSYNC, DSI PLL/PHY/link clocks as one stage, DSI host, trigger mode, rails, RGB0 program, LM0, CTL routing and flush as one stage, PP SW-TE register image, memory-write opcodes, autorefresh left off. The other two are the static misses: MDP core rate, VBIF QoS program.

Runtime denominator, 18. These 6 match: DSI link clocks still on at kickoff, flush before `CTL_START`, interrupt clear before `CTL_START`, exactly one `CTL_START`, no MDP software-trigger write, VBIF halt not used before start. These 12 do not: interconnect vote, register-bus vote, `cmd_mdp_start` arm, PP IRQ left masked, panel command order, `PP_LINE`, `PP_OUT`, `PP0_DONE` observed, DSI busy observed, `CMD_MDP_DONE` observed, AXI fetch proven, internal-TE sufficiency.

## 10. Clock and resource comparison

| Clock | Sony enable | Sony rate | XNU enable | XNU rate | Confidence |
|---|---|---|---|---|---|
| MDP core | `core_clk` branch | `mdp_clk_src` table, GPLL0 or MMPLL5, XO is not an entry | CBCR `0x008c231c` | no write | Branch level 3. Rate UNKNOWN |
| MDP VSYNC | `vsync_clk` | 19.2 MHz XO, div 1 | RCG `0x008c2080/84`, CBCR `0x008c2328` | programmed | level 3 |
| AHB | `iface_clk` | parent `mmss_mmagic_ahb` | CBCR `0x008c2308` | parent not re-read at kickoff | enable level 3, rate UNKNOWN |
| AXI | `bus_clk` | branch, no parent in the clock init | CBCR `0x008c2310` | none | enable level 3 |
| DSI byte | byte RCG from DSI0 PLL byte | M3 programs it | left on | level 4 |
| DSI pixel | pixel RCG from DSI0 PLL pixel | M3 | left on | level 4 |
| DSI escape | XO | M3 | left on | level 4 |
| PLL source | 14 nm DSI PLL | locked `0x2f` | M3 | level 4 |

`CLAIM: CBCR enable is not evidence of a valid MDP core rate.` `EVIDENCE_LEVEL: 2`. The frequency table's first entry is 85.7 MHz from GPLL0. `enable_safe_config` can leave the RCG on XO. XNU never writes `0x008c2040` or `0x008c2044`. Reset contents of that RCG were not read on a good boot. #11E was supposed to read them and did not.

## 11. Bus, VBIF, and fetch

| Operation | Class | Why |
|---|---|---|
| `mdss_mdp_ctl_perf_update_bus` | FRAMEWORK_REQUIRED_WITH_SILICON_SIDE_EFFECT | It submits an interconnect AB/IB request. Failure is logged. The source does not say a missing vote freezes `PP_LINE` |
| `mdss_bus_bandwidth_ctrl(true)` | FRAMEWORK_REQUIRED_WITH_SILICON_SIDE_EFFECT | Reference-counted bus vote when command resources leave OFF |
| `mdss_update_reg_bus_vote(VOTE_INDEX_LOW)` | FRAMEWORK_REQUIRED_WITH_SILICON_SIDE_EFFECT | AHB/register vote on that same transition. No local MDSS register was identified |
| `msm_bus_scale_client_update_context` inside power-on | FRAMEWORK_REQUIRED_WITH_SILICON_SIDE_EFFECT | Same family |
| Runtime power `pm_runtime_get_sync` | SILICON_REQUIRED | GDSC. XNU already turns that domain on |
| Vote bookkeeping structs | BOOKKEEPING_ONLY | The Linux client object itself |

`BUS_BW_VOTE_HW_STATE` stays unobservable. No proxy is defined.

VBIF for RGB0 XIN 1:

| Item | When Sony does it | XNU |
|---|---|---|
| QoS `<1 2 2 2>` in the legacy 2-bit fields at `0x020+4*n`, bits `[3:2]` | probe, if `MDSS_QOS_REMAPPER` is clear. Rev `0x10070000` is `MDSS_MDP_HW_REV_107` and does not set that flag | never written. Current value UNKNOWN |
| Remapper `0x550`/`0x570` | only rev 300 and later | not applicable if version is 107. Version was not re-read on #11E |
| Read-limit default 32 | `mdss_mdp_set_ot_limit` when the dynamic limit is non-zero; early-outs when the limit is 0 | not written. Value UNKNOWN |
| XIN halt | suspend or outstanding-limit change, not before a normal `CTL_START` | not written |
| AXI halt | display off | not written |
| Error `SRC_ERR` / `ERR_INFO` | defined, not read by the driver | correctly not instrumented |

Fetch observables, ranked:

| Signal | Rank | Reason |
|---|---|---|
| A transaction counter | DIRECT | none found in the driver |
| `CURRENT_SRC0_ADDR` | NOT_USEFUL as fetch proof | latched pointer only. Useful as proof the flush was accepted |
| INTF1 underrun bit 26 | INDIRECT_LOW_CONFIDENCE | starvation after the interface is scanning. It was 0 in #11D-B. The source does not require it before `PP_LINE` |
| `XIN_HALT_CTRL1` bit 17 | INDIRECT_LOW_CONFIDENCE | the pipe-idle helper treats it as idle when set. Clear does not mean "fetch started" |
| `PP_LINE_COUNT >= 1` | INDIRECT_HIGH_CONFIDENCE for "a line left PP" | not a bus beat. It stayed 0 |
| DSI busy bit 2 | INDIRECT_HIGH_CONFIDENCE for "DSI sees an ongoing MDP transfer" | stayed 0. Not a fetch beat |

```text
RGB0_FETCH_DIRECT_SIGNAL=NONE_SOURCE_PROVEN
```

## 12. MDP static datapath

RGB0 at `0x00915000` is programmed with PA `0x98000000`, stride `0x1100`, XRGB8888, full 1080x1920. Pre-kick readback matched on the boots that reached kickoff. Do not re-test those fields.

LM0 at `0x00945000` gets output size `0x07800438` and border 0. `docs/XZS_D8_M8_DEFERRED.md` records the source result that stage BASE does not use `LM_BLEND0`. That hypothesis stays closed.

CTL0: `DISP_INTF_SEL=0x00000100`, `CTL_LAYER_0=0x00000200`, `CTL_TOP=0x00020020`, flush `0x00020048` with the command-mode INTF bit omitted. No other command-mode CTL register was found on the one-shot path beyond flush and start. `INTF_DSI_CMD_MODE_TRIGGER_EN` is defined and unused by `mdss_mdp_intf_cmd.c`. It stays unread.

## 13. PP and TE comparison

Three images:

| State | A. Literal Keyaki | B. Sony `hw_vsync_mode=false` / SW-TE | C. XNU |
|---|---|---|---|
| `TEAR_CHECK_EN` | enabled when `te-check-enable` | enabled | 1 |
| `SYNC_CONFIG_VSYNC` bit 20 | set (listen to the pin) | clear (ignore the pin, count on internal wrap) | clear, word `0x00080093` |
| Height | property absent, driver default `0xfff0` | vtotal when the override runs | `0x873` |
| `VSYNC_INIT` / `START_POS` | driver formula | same formula | `0x780` |
| `SYNC_WRCOUNT` | start + thresh + 1 | same | `0x785` |
| `RD_PTR_IRQ` | start + 1 | same | `0x781` |
| `WR_PTR_IRQ` | 0 in this image | 0 | 0 |
| Autorefresh | refused when `hw_vsync_mode` is false | off | register 0 |
| GPIO10 | expected | not required for the counter | sampled, 0 edges |

```text
INTERNAL_TE_COMPLETE=UNKNOWN
```

Reason: image C matches image B, and image B is what the source writes. The source never says that image releases pixels. #11D-B shows the counter does what image B describes (run, hit `0x780` and `0x785`, wrap) and `PP_LINE` does not move. Bit 20 is sourced as "listen to TE" versus "count on internal wrap", not as the pixel-release gate. Setting bit 20 would return to image A, which has no TE pin activity. That is not a sourced fix.

Tearcheck timing state is the counter, the sync word, and the height. Pixel-transfer state is `PP_LINE_COUNT`, and completion is `PP0_DONE`. The source stops at "wait until line count is non-zero" after `CTL_START`. It names no further PP register between those two events on this path.

`PP_OUT_LINE_COUNT` has no reader in the command, ctl, or MDP ISR sources. Its 0 has no meaning beyond the read.

## 14. DSI runtime comparison

Earliest sourced proof that DSI accepted a PP frame is `DSI_STATUS` bit 2, described as the MDP transfer being ongoing. `CMD_MDP_DONE` bit 8 is completion of that transfer, not acceptance. `mdss_dsi_cmd_mdp_start` does not create either bit. It only unmasks the completion IRQ and sets a software flag XNU does not have.

XNU's kickoff writes no DSI register. Link clocks are already on. `INTR_EN` is intentionally 0, so a completion IRQ would not be delivered; the raw status bit is still readable and was read. It stayed 0 through the #11D-B poll. That is "not observed", not "DSI rejected".

## 15. Panel and DCS comparison

`somc,sharp_synaptics_cmd_9_panel` (`keyaki.dts:1740`):

```text
on-command:        TEON 0x35 0x00, then DISPON 0x29
post-panel-on:     SLPOUT 0x11 with wait 0x78 (120 ms)
wr-mem-start:      0x2c
wr-mem-continue:   0x3c
mdp-trigger:       none
dma-trigger:       trigger_sw
te-using-te-pin:   present
no 0x2A / 0x2B in the on-command
```

XNU panel prepare, from the #11E preamble and the same function used since retry 6, sends SLPOUT with a 120 ms wait, then TEON, then DISPON. All three returned `ACK_ERR=0` and `TIMEOUT=0`. The bytes exist in the DTS. The order does not: Sony's blob displays on before sleep-out, and sleep-out is the post-panel-on command.

`CLAIM: this order difference blocks PP_LINE.` `EVIDENCE_LEVEL: 0`. The failure is upstream of DSI busy, and the timing counter is not waiting on GPIO10. Successful DCS does not prove the panel will accept a memory write. It also does not locate the current stall inside the panel, because no memory-write packet has been shown to leave the MDP.

No column or page command is in this node's on-command, so a missing `0x2A`/`0x2B` is not a Sony-versus-XNU gap for this panel. The stream controller already carries `0x2C` and `0x3C`.

## 16. Interrupt and status map

| Event | Bit | Status | Clear | Meaning | When Sony expects it | Seen on a good XNU kickoff |
|---|---|---|---|---|---|---|
| `PP0_DONE` | 8 | MDP `0x00901014` | W1C `0x00901018` | end of transfer | after the frame | no, and the clear is not repeated, so a latch would have stayed |
| `PP0_RD_PTR` | 12 | same | same | read-pointer | around `RD_PTR_IRQ` | yes, including before start |
| `PP0_WR_PTR` | 16 | same | same | write-pointer | not "request issued" | yes, rises in the `CTL_START` window on #11C/#11D-B |
| `PP0_AUTOREFRESH_DONE` | 20 | same | same | autorefresh frame done | only if autorefresh is on | no, matching autorefresh 0 |
| `INTF1_UNDERRUN` | 26 | same | same | interface starvation | only once the interface is scanning | 0 inside `MDP_INTR` on #11D-B |
| `CMD_MDP_DONE` | 8 of DSI `0x00994110` | that word | mask is bit 9 | DSI finished a command-MDP transfer | after admission | raw bit 0 |
| DSI errors | ACK `0x00994068`, timeout `0x009940c0` | those words | status | host error | on a bad packet | 0 |
| VBIF error | `0x194`, `0x1a0` | defined | driver does not read them | unknown side effect | — | not read, correctly |

Status reads used above are not the clear register. DSI `0x110` is also used with whole-word RMW on other Sony paths; XNU only reads it.

## 17. Remaining unknowns

The ledger has 10 open rows (H24, H27, H29–H34, H39, H40). They are seven questions. H31 sits inside U5, and H33/H34 are the request-versus-admission form of U2 and U3. They stay open because a zero is not a disproof.

```text
U1
question: Is mdp_clk_src on a table entry with the root running, or only the branch enabled?
why open: XNU writes the CBCR and not the RCG. No good boot read the RCG. #11E did not either.
depends on: fetch, PP pixel release, everything downstream
evidence: level 1 that the write is absent, level 0 that this causes PP_LINE=0
discriminator: read CMD 0x008c2040 and CFG 0x008c2044 plus CBCR 0x008c231c after M8-1 PASS, GDSC already on. MMCC reads of this class already succeeded for VSYNC.

U2
question: Did RGB0 issue an AXI read of 0x98000000?
why open: no sourced beat counter. The latched pointer is not one.
depends on: whether the stall is before or after fetch
evidence: level 0
discriminator: there is no direct one. Do not invent a proxy. Underrun and XIN idle are low-confidence and must not be called fetch.

U3
question: Does the internal-TE image release pixels on MSM8996?
why open: the image matches the source and the hardware counter matches the image, and line-out does not happen. The source does not name the missing gate.
depends on: PP_LINE
evidence: level 4 for the negative line-out result, level 0 for "bit 20 would fix it"
discriminator: only after U1 is known. A bit-20 write while the core rate is unknown confounds both.

U4
question: Does the missing interconnect vote block fetch?
why open: the vote is a real request. No silicon register represents it. Effect on this panel is not stated.
depends on: fetch
evidence: level 2 for the call, level 0 for causality
discriminator: none that is passive. A vote implementation is a new subsystem, not a one-register correction.

U5
question: Does XIN1 QoS currently equal 1,2,2,2?
why open: never read. Reset defaults are not in the driver.
depends on: fetch priority, not proven to block the first beat
evidence: level 2 for the Sony program, level 0 for causality
discriminator: VBIF reads at 0x009b0020+ only after GDSC PWR_ON and a known-good MDP read. Not inside fb_init. Not on the same boot as a rate write.

U6
question: Why did #11E not return from ml_io_map_unmappable?
why open: the new reads had not been called. The map call is the old one. The SoC later enumerated as fastboot.
depends on: whether the next boot can trust M8-1
evidence: level 1 for the stop point
discriminator: a boot whose fb_init matches #11D-B. If that map passes, #11E's extra call was not required to explain a repeated fault. If it fails again with no new code, the port has a separate map instability. This audit does not schedule that boot.

U7
question: Does TEON/DISPON-before-SLPOUT versus XNU's SLPOUT-first change memory-write readiness?
why open: the DTS order and the XNU order differ. DCS ACK was clean either way that XNU tried.
depends on: panel acceptance, which is downstream of PP_LINE
evidence: level 2 for the byte difference, level 0 for causality
discriminator: not useful until a frame leaves PP.
```

## 18. Dependency graph

```text
U6 map stability          (independent; #11E-only)
U1 MDP core source/rate   (independent passive read)
        |
        +--> U2 fetch       (no direct probe)
        |       |
        |       +--> U5 QoS value (independent read, weak causal edge)
        |       |
        |       +--> U4 bus vote  (not passively observable)
        |
        +--> U3 internal-TE pixel release
                |
                +--> PP_LINE
                        |
                        +--> DSI busy
                                |
                                +--> CMD_MDP_DONE
                                        |
                                        +--> visible pixels
U7 panel command order  (independent of U1, downstream of PP_LINE)
```

U1 can be tested without kickoff if the RCG is read after `clocks mdp-on` and a finished M8-1. U5 can be tested without a second `CTL_START`, but not in `fb_init` and not until the domain is on. U4 cannot be tested by a read. U3 must not be tested by a write until U1 is known.

## 19. Information-gain ranking

Ranked by how cleanly a result splits the search, how safe the observation is, and how many later hypotheses it drops. Likelihood is not the sort key.

| Rank | Unknown | Gain | Risk | Boot needed |
|---|---|---|---|---|
| 1 | U1 | Splits "branch only" from "valid source". If the RCG is XO or root-off, fetch and PP tests below it are confounded | Low. Same MMCC block as VSYNC reads that already worked | One passive boot, after M8-1 PASS |
| 2 | U5 | Splits QoS match from mismatch. Does not by itself prove fetch | Medium. New VBIF aperture, domain-dependent, never completed on this port | One later passive boot, not combined with a write |
| 3 | U3 | Only informative after U1 | A bit-20 write is a behavior change and conflicts with the silent TE pin | No write now |
| 4 | U2 | Would move the boundary | No direct register | No boot whose only purpose is a manufactured fetch bit |
| 5 | U4 | Would matter if a vote could be shown to gate the AXI port | Implementing the vote is a new stack | No boot |
| 6 | U7 | Panel-only | Low, and it does not explain a stall before DSI busy | No boot |
| 7 | U6 | Explains #11E only | Repeating a map that has passed many times has low display value | No dedicated boot |

## 20. Correction candidate register

| Candidate | Evidence | Confidence | Hardware change justified now |
|---|---|---|---|
| Program `mdp_clk_src` to a table row | Sony calls `mdss_mdp_set_clk_rate`. XNU does not. Causal link to `PP_LINE` is not sourced | MEDIUM that the software differs, LOW that it is the cause | NO. Read first |
| Add an interconnect vote | Sony calls it. Effect UNKNOWN. No one-register program | LOW | NO |
| Program VBIF QoS `1,2,2,2` | Sony probe write. Current silicon value unknown, so the mismatch itself is not proven | LOW | NO |
| Set PP bit 20 | Returns to literal Keyaki. GPIO10 had no edges. Source does not say the bit releases pixels | LOW | NO |
| Reorder DCS to TEON, DISPON, post-on SLPOUT | DTS order differs. Stall is before DSI busy | LOW | NO |
| Pulse `0x00994094` | Superseded. `cmd_mdp_start` does not | — | NO |

`SAFE_CORRECTION_AVAILABLE=NO`.

A future correction still has to show all seven: exact source path, exact XNU mismatch, plausible link to this boundary, no contradiction with a closed row, a one-change experiment, a positive and a negative result, and a stop that returns the panel to the existing shutdown. U1's rate write fails the causal-link item today.

## 21. Future decision tree

```text
START
  |
  |-- Did M8-1 PASS on a binary whose fb_init matches #11D-B?
  |       NO  -> stop. That is U6, not a display correction.
  |       YES -> continue
  |
  |-- Read mdp_clk_src CMD/CFG and the MDP/AHB/AXI/VSYNC CBCRs. No other new read.
  |       root off, or source not in the mdp table
  |         -> U1 becomes HW_READBACK. Still no write in that boot.
  |            A later one-change rate test is then the only candidate
  |            that would have both a mismatch and a table to copy.
  |       table row and root running
  |         -> drop "unconfigured core clock" as the cause
  |
  |-- Only then, a separate boot: VBIF QoS and XIN halt, GDSC bit 31 already 1.
  |       mismatch -> evidence only. Do not correct in that boot.
  |       match    -> QoS is not the gap
  |
  |-- PP_LINE still 0 with a valid core rate and matched QoS
  |       -> the gate remains inside PP pixel release (U3)
  |       -> do not set bit 20 without a new source sentence
  |       -> do not add a bus vote just to try something
  |
  |-- PP_LINE becomes non-zero
  |       -> next observation is DSI_STATUS bit 2, then CMD_MDP_DONE
  |       -> visible pixels only after CMD_MDP_DONE
```

## 22. Future task templates

### T1 — MDP core RCG read

```text
Prerequisite: fb_init identical to 5600ef8. M8-1 must print PASS before any new read.
Single question: is mdp_clk_src a table entry with ROOT_OFF clear, and is the MDP branch unhalted?
Evidence input: this audit, U1.
Allowed observation: CMD 0x008c2040, CFG 0x008c2044, CBCR 0x008c231c, 0x008c2308, 0x008c2310, 0x008c2328, GDSC 0x008c2304. One sample at prekick and one immediately after the existing CTL_START.
Allowed change: none.
Forbidden: RCG write, VBIF, bus vote, TE, PP, DSI, a second CTL_START, capture inside fb_init.
PASS: both samples decoded from the source bitfields.
FAIL_CLEAN: shell returns, shutdown runs, samples missing or partial.
STOP: panic, SError, data abort, USB loss, reset.
Proves: U1's hardware value. Does not by itself prove causality.
```

### T2 — VBIF XIN1 read

```text
Prerequisite: T1 completed, GDSC PWR_ON observed, M8-1 PASS.
Single question: do QoS words bits [3:2] of XIN1 equal 1,2,2,2, and is halt-request bit 1 clear?
Allowed observation: 0x009b0020/24/28/2c, 0x009b0200, 0x009b0204, HW version 0x00900000.
Forbidden: any VBIF write, QoS "correction", halt clear.
PASS: raw words plus the 2-bit decode.
STOP: any fault on the first VBIF read. Do not retry that address in the same boot.
Proves: U5's value. A mismatch is evidence, not a fix.
```

### T3 — one-change rate, only if T1 shows a non-table or root-off RCG

```text
Prerequisite: T1 raw words. Still must satisfy the seven-point rule in a later task. Not authorized here.
Single change: one mdss_mdp_set_clk_rate equivalent to a single table row.
Positive result: PP_LINE becomes non-zero on that one start.
Negative result: PP_LINE stays 0 and the RCG reads back as the programmed row.
STOP: any fault, or a second start.
```

T3 is a template, not a recommendation. T1 has not been run.

## 23. Recommended next action

```text
NEXT_ACTION_CLASS=PASSIVE_BOOT
NEXT_ACTION=T1 only, when a boot is explicitly authorized. Do not inherit the #11E capture call in fb_init. Do not read VBIF in that boot. Do not program the rate.
EXPECTED_INFORMATION_GAIN=U1 moves from UNKNOWN to HW_READBACK. Either the core source is a real gap, or it is removed from the cause list.
```

Alternative if no boot is authorized:

```text
NEXT_ACTION_CLASS=SOURCE_ONLY
```

is not useful next. The rate registers are identified. Another source pass will not reveal their silicon value.

```text
SAFE_CORRECTION_AVAILABLE=NO
```

## 24. Closed hypotheses

```text
DO_NOT_REOPEN_WITHOUT_NEW_EVIDENCE
```

Count: 30 rows in the CSV with `ReopenAllowed=no`: H01–H23, H25, H26, H28, H35–H38. H28 is "the vote call is absent", which is closed. "The vote causes the stall" is H29 and stays open.

In words: framebuffer map and contents, RGB0 programming and the source latch, LM0 base stage and blend, CTL routing and flush, tearcheck enable, the running counter, `START_POS`, `SYNC_WRCOUNT`, autorefresh, DSI host and command mode, stream geometry, the MDP software trigger, PP0 ownership, a missing normal PP write, physical TE presence, physical TE as a requirement for the counter, VSYNC being the pixel clock, DSI link clocks being off, LUT/TBU/MNOC/IOMMU on this node, and any use of #11E as evidence that the clock or VBIF is bad.

## 25. Final canonical blocker

```text
CURRENT_BLOCKER=PP_FRAME_START_GATE_STILL_UNKNOWN
LAST_HW_PROVEN_BOUNDARY=flush consumed + RGB0 pointer latched + tearcheck counter through 0x780 and 0x785
FIRST_UNPROVEN_BOUNDARY=MDP core source/rate, then whether an AXI fetch happens, then PP pixel release
DOWNSTREAM_STATE_NOT_YET_REACHED=PP first line, DSI admission, command-MDP completion, visible pixels
```

### Unknown is not failure

```text
DSI_STATUS bit 2 stayed 0     ≠  DSI rejected a frame
PP0_DONE stayed 0             ≠  PP never started
CURRENT_SRC0_ADDR latched     ≠  AXI fetch happened
#11E reset before M8_1 PASS   ≠  core clock or VBIF is invalid
PP_LINE stayed 0              ≠  a named missing register was found
No bus-vote register to read  ≠  the vote is irrelevant
```

## 26. Has the problem become deterministic smaller tasks?

```text
PARTIALLY
```

Ordered chain:

1. T1 passive MDP RCG/CBCR read on an otherwise #11D-B framebuffer path.
2. If that read is a non-table or root-off configuration, stop and write a separate one-change rate task. Do not fold it into T1.
3. If that read is a table row and the root is running, drop U1 and run T2 (VBIF QoS and halt) as its own passive boot.
4. If QoS matches and `PP_LINE` is still 0, the remaining gate is U3. No bit-20 write, no bus-vote port, no invented fetch register.
5. U4 and U7 stay off the boot list until a source sentence ties them to this boundary.

What is not yet deterministic: there is still no passive proof of an AXI beat, and the bus vote cannot be observed. Those two are why the answer is partial rather than yes.
