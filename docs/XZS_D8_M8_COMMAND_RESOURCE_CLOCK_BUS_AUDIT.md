# D8-M8 command kickoff resource, clock, and bus audit

Offline audit of the pinned Sony command-mode path against the current XNU sequence. No build, boot, flash, or MMIO change.

## A. Repository identity

```text
WORKTREE=/Users/lechaukha12/Desktop/xnu-xzs
PROJECT_GIT_ORIGIN=https://github.com/lechaukha12/xnu-xzs.git
BRANCH=xzs-d8-display-m8-resume
HEAD_AT_AUDIT_START=ba905eb52f50c4748cd4968df9a3758e0555d7c5
WORKTREE_CLEAN=yes
```

Remote `origin` fetch and push are `https://github.com/lechaukha12/xnu-xzs.git`.

## B. Sony source identity

```text
URL=https://github.com/sonyxperiadev/kernel
BRANCH=aosp/LA.UM.7.1.r1
PINNED_COMMIT=5772572ccdfbc16c270d33f8fa6b55d33d27709c
TREE=drivers/video/fbdev/msm/
```

MSM8996 clock names are taken from `arch/arm64/boot/dts/qcom/msm8996-mdss.dtsi` at that commit. No other tree is used.

## C. Current hardware baseline

Retry #11D-B, one `CTL_START`, classification `R11D-B2`. `HW_READBACK_PROVEN`: flush `0x00020048` consumed, RGB0 current source latched to `0x98000000`, PP counter sampled at `0x780` and `0x785` then wrapped, `PP_AUTOREFRESH_CONFIG=0`, `PP_LINE=0`, `PP_OUT=0`, `PP0_DONE` bit 8 clear, autorefresh-done bit 20 clear, stored `DSI_STATUS` bit 2 and raw `CMD_MDP_DONE` clear. `MDP_INTR` at those samples was `0x00011000`, which is PP0 write-pointer plus read-pointer. Bit 26, `MDSS_MDP_INTR_INTF_1_UNDERRUN`, was clear in that same word.

Before that kickoff the XNU shell had already run `display power mdss-on`, `clocks mdss-ahb-on`, `clocks mdss-axi-on`, `clocks mdp-on`, M3 byte/pixel/escape, and the M8 VSYNC enable. Those enables are `TRACE_PROVEN` by the runner order and the clock code. Their CBCR values were not sampled again at `0x780`.

## D. Sony command-mode resource lifecycle

`mdss_mdp_display_commit` (`mdss_mdp_ctl.c:5848`) calls `mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON)` before mixer programming. If bandwidth was released or mixer parameters changed, it calls `mdss_mdp_ctl_perf_update(ctl, 1, false)` (`5909`), which updates the MDP core rate and `mdss_mdp_ctl_perf_update_bus` (`2346`, `2152`). It then writes `CTL_FLUSH` (`6109`) and calls `ctl->ops.display_fnc`, which for a command panel is `mdss_mdp_cmd_kickoff` (`6133`, `3955`).

`mdss_mdp_cmd_kickoff` (`3040`) sets the software perf-transaction flag, turns the panel on if it was off, increments `koff_cnt`, then calls `mdss_mdp_resource_control(ctl, MDP_RSRC_CTL_EVENT_KICKOFF)` (`3110`) before the DSI command-list event and before `__mdss_mdp_kickoff`.

On `MDP_RSRC_CTL_EVENT_KICKOFF`, if the resource state is OFF or GATE (`807-828`):

1. `mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON)`.
2. `MDSS_EVENT_PANEL_CLK_CTRL` with `MDSS_DSI_CLK_ON` and client `DSI_CLK_REQ_MDP_CLIENT`.

If the state is OFF (`837-848`):

3. `mdss_update_reg_bus_vote(..., VOTE_INDEX_LOW)`.
4. `mdss_mdp_cmd_clk_on`, which calls `mdss_bus_bandwidth_ctrl(true)` (`1109`).
5. State becomes ON. The function bugs out if it is not ON (`851-856`).

`mdss_mdp_cmd_clk_on` does not itself enable a pixel clock. The hardware clock set is inside `mdss_mdp_clk_ctrl` → `__mdss_mdp_clk_control`.

After that, kickoff sends `MDSS_EVENT_DSI_CMDLIST_KOFF`, enables the PP completion IRQ, and `__mdss_mdp_kickoff` writes `CTL_START` on the one-shot branch. `mdss_dsi_cmd_mdp_start` (`mdss_dsi_host.c:2548-2557`) only sets a software busy flag and enables the `CMD_MDP_DONE` IRQ. It does not write a DSI trigger and does not enable a clock.

Between frames, PP done may schedule gate and delayed-off work (`884-902`). That is command-mode clock gating after completion, not a prerequisite for the first `CTL_START`. Video mode does not use this state machine. Command mode therefore turns the same MDP clock bundle on per kickoff if it was gated, then may gate it again after PP done. XNU never gates those branches off after enabling them, so the first kickoff is not missing an "un-gate" step for the branches it already enabled.

## E. MDP clock lifecycle

`__mdss_mdp_clk_control` (`mdss_mdp.c:1396-1458`) on enable:

- `pm_runtime_get_sync` for the MDSS device.
- a register-bus vote.
- `mdss_iommu_ctrl(1)`.
- `msm_bus_scale_client_update_context` on `bus_hdl`.
- then, as separate `mdss_mdp_clk_update` calls: `MDSS_CLK_MNOC_AHB`, `MDSS_CLK_AHB`, `MDSS_CLK_AXI`, `MDSS_CLK_MDP_CORE`, `MDSS_CLK_MDP_LUT`, `MDSS_CLK_MDP_TBU`, `MDSS_CLK_MDP_TBU_RT`, `MDSS_CLK_THROTTLE_AXI`, and `MDSS_CLK_MDP_VSYNC` only if `vsync_ena` is already set.

`mdss_mdp_clk_update` (`1200-1235`) prepares that one clock. For `MDSS_CLK_MDP_VSYNC` it forces 19.2 MHz. For `MDSS_CLK_MDP_CORE`, a quirk path sets the rate if it was left zero. Probe also calls `mdss_mdp_set_clk_rate(max)` (`1955`).

The MSM8996 MDSS node lists only these clock names (`msm8996-mdss.dtsi:114-120`):

| Name | Binding | XNU |
|---|---|---|
| `iface_clk` | `MDSS_AHB_CLK` | `clocks mdss-ahb-on`, CBCR `0x008c2308` |
| `bus_clk` | `MDSS_AXI_CLK` | `clocks mdss-axi-on`, CBCR `0x008c2310` |
| `core_clk_src` | `MDP_CLK_SRC` | no RCG write found |
| `core_clk` | `MDSS_MDP_VOTE_CLK` | branch CBCR `0x008c231c` via `clocks mdp-on` |
| `vsync_clk` | `MDSS_VSYNC_CLK` | RCG `0x008c2080/0x008c2084` and CBCR `0x008c2328` |

`lut_clk`, `tbu_clk`, `tbu_rt_clk`, `mnoc_clk`, and `throttle_bus_clk` are registered as optional (`mdss_mdp.c:1935-1952`). They are not in this DTS clock list, so `clk_get` fails and `mdss_mdp_clk_update` returns without a clock. They are not 8996 requirements of this node.

`mdss_mdp_vsync_clk_enable` (`1238-1253`) toggles only `MDSS_CLK_MDP_VSYNC`. Tearcheck configuration calls it before computing `vclks_line` from that clock's rate (`mdss_mdp_intf_cmd.c:295-304`). The comment at registration says vsync is optional for non-smart panels (`1944`). Command mode uses it. Video mode does not need it for tearcheck.

## F. VSYNC versus pixel/fetch clock

`SOURCE_PROVEN`: `vsync_clk` and `core_clk` are different clock objects, different `mdss_mdp_clk_update` calls, and different MMCC blocks in XNU (`0x008c2328` versus `0x008c231c`). The tearcheck divider is computed only from `MDSS_CLK_MDP_VSYNC`.

`UNKNOWN`: whether `PP_INT_COUNT_VAL` can increment while `MDSS_CLK_MDP_CORE` is gated. The driver never describes that hardware state. Its off path disables vsync together with the core when `vsync_ena` is set (`1434-1440`). The existence of a separate enable function shows the software can turn vsync on without calling the core update, not that the ping-pong counter is unaffected by the core clock.

XNU had already enabled the MDP branch, AHB, AXI, and VSYNC before the #11D-B counter ran. A "VSYNC on, MDP core branch off" explanation does not match that sequence. What is not shown is a `mdss_mdp_set_clk_rate` equivalent for the MDP core source. Flush consumption still shows the MDP start logic accepted `CTL_START`. That does not identify which parent rate was running.

## G. DSI command clock lifecycle

`DSI_CTRL` bit 8 is the controller's internal clock-enable bit. It is not the MMCC byte, pixel, or escape branch. Sony's runtime path requests `MDSS_DSI_ALL_CLKS` through `MDSS_EVENT_PANEL_CLK_CTRL` when command resources move from OFF or GATE to ON (`intf_cmd.c:815-819`). `mdss_dsi_cmd_mdp_start` does not do that request.

XNU's M3 path enables `mdss_byte0`, `mdss_pclk0`, and `mdss_esc0` and leaves them enabled. `DSI_CTRL=0x1f5` includes bit 8 at kickoff (`HW_READBACK_PROVEN`). Nothing in the XNU kickoff turns those branches off. The per-frame DSI clock-on is therefore already satisfied by clocks that stay on. It is not a missing enable at `CTL_START`.

## H. Bus and bandwidth

Three distinct votes exist:

| Call | file:line | What it does |
|---|---|---|
| `mdss_mdp_ctl_perf_update` → `mdss_mdp_ctl_perf_update_bus` | `ctl.c:5909`, `2152-2184` | Computes a real-time AB/IB vote and submits it before flush when bandwidth was released or the mixer changed |
| `msm_bus_scale_client_update_context` | `mdss_mdp.c:1411-1413` | Applied inside every MDP power-on |
| `mdss_bus_bandwidth_ctrl(true)` | `intf_cmd.c:1109` | Reference-counted bus vote when command resources leave OFF |
| `mdss_update_reg_bus_vote(VOTE_INDEX_LOW)` | `intf_cmd.c:839-841` | AHB/register-bus vote on the same transition |
| `mdss_bus_rt_bw_vote` | `mdss_mdp.c:1313-1358` | Minimum RT vote used with register-access clocks and SMMU attach |

`mdss_mdp_ctl_perf_release_bw` (`2198-2240`) is command-mode only. It may set the vote back to zero after transactions complete. The source does not say a zero vote makes RGB0 refuse to fetch. The calls are interconnect requests, not local bookkeeping: a failed `msm_bus_scale_client_update_request` is logged as "Bus bandwidth vote failed" (`1354-1355`). XNU has no equivalent vote. That absence is real. Its effect on this panel is `UNKNOWN`.

## I. VBIF and RGB0

`msm8996-mdss.dtsi` sets `qcom,mdss-pipe-rgb-xin-id = <1 5 9 13>`, so RGB0 is XIN 1, and `qcom,mdss-vbif-qos-rt-setting = <1 2 2 2>`. `mdss_mdp_parse_vbif_qos` (`mdss_mdp.c:4276`) reads that table at probe. The dynamic OT/limit path writes `MMSS_VBIF_RD_LIM_CONF` / `WR_LIM_CONF` and clears a XIN halt bit when a pipe's outstanding limit changes (`5032-5089`). `mdss_mdp_vbif_axi_halt` (`1507-1516`) is for suspend or display-off, and its comment says a successful halt means the RT AXI ports will not fetch more data. It is not called before a normal `CTL_START`.

XNU has no VBIF write in the D8 display sources. It also does not halt VBIF. So the per-frame path does not require an unhalt that XNU skipped. The probe-time QoS remap is absent. The source does not say a missing remap prevents the first fetch. `UNKNOWN`.

`MDSS_MDP_REG_SSPP_CURRENT_SRC0_ADDR` is `0x0A4` (`mdss_mdp_hwio.h:297`). No comment or reader in this tree says the latch happens only after an AXI data beat. The driver treats the programmed source address as the pointer to fetch and the current register as the latched pointer. `UNKNOWN` whether the latch can precede the first read. Existing evidence proves the latch. It does not prove a framebuffer transaction.

## J. Underflow and error signals

| Signal | Where | Meaning in this driver | Safe read | Seen in #11D-B |
|---|---|---|---|---|
| `MDSS_MDP_INTR_INTF_1_UNDERRUN` | bit 26 of MDP+`0x014` (`mdss_mdp_hwio.h:114`, map `mdss_mdp.c:204`) | Interface 1 underrun | Yes. Status read does not clear; clear is MDP+`0x018` | Clear. `0x11000` does not include bit 26 |
| `MDSS_MDP_INTR_PING_PONG_0_DONE` | bit 8 | End of data transfer | Yes | Clear |
| PP underflow IRQ | not in `mdss_mdp_hwio.h` interrupt list | none | — | — |
| SSPP/RGB fetch-complete IRQ | not in that list | none | — | — |
| VBIF AXI halt ack | `MMSS_VBIF_AXI_HALT_CTRL1` | Ack of a halt request, not "fetch happened" | Read is passive; do not write `HALT_CTRL0` | Not read |

The source does not say an INTF underrun is raised before `PP_LINE_COUNT` becomes non-zero. A transfer that never starts can leave underrun clear. Bit 26 staying clear is consistent with no interface scan. It does not locate the stall.

## K. Current XNU resource implementation

| Phase | What XNU enables | Register |
|---|---|---|
| Before M3 | MMAGIC AHB/CFG/NOC/AXI branches, MDSS GDSC | shell clock and power commands |
| Before M3 | MDSS AHB, AXI, MDP branches | `0x008c2308`, `0x008c2310`, `0x008c231c` |
| M3 | DSI PLL, byte, pixel, escape branches | `0x008c233c`, `0x008c2314`, `0x008c2344` |
| M8 stream | VSYNC RCG at 19.2 MHz XO and VSYNC CBCR | `0x008c2080`, `0x008c2084`, `0x008c2328` |
| M8 kickoff | does not gate those clocks off | — |

No MDP core RCG rate write, no `msm_bus` vote, and no VBIF QoS write were found. SMMU stays in bypass. That matches the direct-PA framebuffer and is not reopened here. `mdss_iommu_ctrl(1)` attaches a translation context; with bypass there is no translation side effect to reproduce. TBU clocks are not in the 8996 MDSS clock list.

Internal TE does not change this resource sequence. `hw_vsync_mode` changes bit 20 of `PP_SYNC_CONFIG_VSYNC` and refuses autorefresh. It does not skip `mdss_mdp_clk_ctrl` or the bus vote. `INTERNAL_TE_COMPLETE` stays `UNKNOWN`.

## L. Sony versus XNU

| Resource | Sony | XNU | Result |
|---|---|---|---|
| MDSS GDSC / runtime get | `pm_runtime_get_sync` on MDP power-on | `display power mdss-on` | `MATCH` for the GDSC being on before kickoff |
| AHB, AXI, MDP branch | enabled in `__mdss_mdp_clk_control` | CBCR enables before M8 | `MATCH` for branch enable |
| MDP core rate | `mdss_mdp_set_clk_rate` at probe and perf update | no rate write found | `UNKNOWN`. Branch enable is not a rate |
| VSYNC 19.2 MHz | `mdss_mdp_vsync_clk_enable` | RCG plus CBCR | `MATCH` |
| DSI byte/pixel/escape | `MDSS_DSI_ALL_CLKS` on resource ON | M3 leaves them enabled | `MATCH` for the first frame |
| LUT, TBU, throttle, MNOC clocks | optional, absent from this DTS | not enabled | `NOT_APPLICABLE` on this node |
| Interconnect AB/IB vote | `perf_update_bus` and `mdss_bus_bandwidth_ctrl` | none | `MISSING_IN_XNU`. Effect on fetch `UNKNOWN` |
| Register-bus vote | `VOTE_INDEX_LOW` | none | `MISSING_IN_XNU`. Effect `UNKNOWN` |
| VBIF QoS remap | probe, DT `<1 2 2 2>` | none | `MISSING_IN_XNU` as probe setup. Not a per-frame unhalt. Effect `UNKNOWN` |
| VBIF halt | suspend/off only | not halted | `NOT_APPLICABLE` to the first kickoff |
| IOMMU attach | `mdss_iommu_ctrl(1)` | bypass | `NOT_APPLICABLE` to direct PA |
| PP registers | previous audit | matched except intentional TE | `MISSING_PP_WRITE=NO` |

## M. Existing evidence, reinterpreted

`CTL_FLUSH` falling to 0 and `CURRENT_SRC0_ADDR` becoming `0x98000000` prove the MDP accepted the flushed configuration and latched RGB0's source pointer (`HW_READBACK_PROVEN`). Register access also proves the AHB path to MDP was usable. They do not prove an AXI read, a VBIF data beat, or that `PP_LINE_COUNT` should have moved. The source never defines the current-source register as a completed fetch.

The running PP counter proves the tearcheck timing clock was alive. Because XNU had enabled both VSYNC and the MDP branch earlier, it does not prove the core branch was the clock that toggled the counter, and it does not prove the core source was at a scanout rate.

`PP_LINE=0` still means Sony's "first line is out" check did not succeed. INTF1 underrun did not latch in the captured status word.

## N. Decisions

1. Can the tearcheck counter run while the pixel/fetch clock is gated? `UNKNOWN`. The clocks are separate (`SOURCE_PROVEN`). No sentence says the counter increments with the core clock gated.

2. Does Sony perform a hardware-affecting clock or resource enable that XNU does not? `YES`. The absent operations are the interconnect bandwidth vote (`mdss_mdp_ctl_perf_update_bus`, `mdss_bus_bandwidth_ctrl`), the register-bus vote, the MDP core rate set (`mdss_mdp_set_clk_rate`), and the probe-time VBIF QoS remap. None of these is shown by the source to be the reason `PP_LINE` stayed 0.

3. Does command-mode kickoff require a runtime bus vote, and could its absence block RGB0 fetch? The vote is performed before flush and again when resources leave OFF (`SOURCE_PROVEN`). The source does not say a missing vote stops fetch. That causal claim is `UNKNOWN`.

4. Is RGB0/VBIF fetch proven by existing XNU evidence? `NO`. The current-source latch is not an AXI transaction.

5. Is there a source-backed passive signal that proves a framebuffer fetch? `NO`. No such IRQ or status bit is read by this driver. INTF1 underrun proves interface starvation only after the interface is scanning.

6. Would a stalled fetch have raised an underflow or bus-error signal before `PP_LINE`? `UNKNOWN`. The available signal is INTF1 underrun, bit 26, and it was 0 in the #11D-B gate samples. There is no PP underflow IRQ in this header. The source does not require underrun to precede the first line.

7. Narrowest blocker: `PP_FRAME_START_GATE_STILL_UNKNOWN`. The PP register image and the branch clocks XNU enables do not explain `PP_LINE=0`. Bus vote, core rate, and VBIF QoS are real Sony-versus-XNU differences and are not established as this gate.

8. Is there one source-proven correction worth testing? `NO`. A bus-scale vote or an MDP rate write would be a new program, not a value this C source gives as the missing frame-start bit. Do not invent one.

## O. Next observation

No write is proposed. Another boot is not justified by a single correction.

If a later passive run is authorized, add these reads to the existing `0x780` sample, and do not change any clock or vote:

| Signal | Address | Why |
|---|---|---|
| MDP branch CBCR | `0x008c231c` | Confirm the branch XNU enabled is still unhalted at the gate |
| AXI branch CBCR | `0x008c2310` | Same for the bus clock |
| VSYNC CBCR | `0x008c2328` | Same for the tearcheck clock |
| MDSS GDSC | `0x008c2304` | Power domain still on |
| `MDP_INTR_STATUS` | `0x00901014` bit 26 | INTF1 underrun, already 0 in #11D-B; keep it in the same sample as `PP_LINE` |

Do not write VBIF halt or QoS, do not change the MDP RCG, and do not add a bandwidth vote in that run.

## Timeline diff

Sony, one-shot command frame:

```text
MDP power on: GDSC, AHB, AXI, core, optional clocks not present on 8996
bus context vote
VSYNC already enabled for tearcheck
perf bus vote if bandwidth was released
CTL_TOP / mixer
CTL_FLUSH
resource kickoff: DSI clocks on if they were gated, command bus vote if state was OFF
PP completion IRQ armed
CTL_START
```

XNU:

```text
GDSC, MMAGIC, AHB, AXI, MDP branch, DSI byte/pixel/escape, VSYNC
PP tearcheck with internal TE
DSI static command registers
CTL_FLUSH
one CTL_START
no bus vote, no MDP core rate write, no VBIF QoS, no per-frame DSI clock toggle
```

The highlighted mismatches are the bus vote, the core rate, and the VBIF QoS table. They are not promoted to the cause of `PP_LINE=0`.

Audit stops.
