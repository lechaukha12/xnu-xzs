# D8-M8 Retry 11E — MDP clock / VBIF / RGB0 fetch

One RAM boot was attempted. It did not reach `CTL_START`, and it did not produce an R11E sample. The phone then enumerated as Sony fastboot (`S1Boot Fastboot`), not the XNU gadget. No second boot was issued. No clock, QoS, halt, rate, bus, PP, TE, or DSI register was written by this retry.

```text
BRANCH=xzs-d8-display-m8-resume
BASE_HEAD=8e0006b8aaad9e1758a47fe17dacf2739a4c8d11
COMMIT_BOOTED=e869829f7a903619e090a37e0cc00a11292d6cc0
KERNEL_SHA256=f5dd96e9fdcdff9b80334a63ef59245a8c91753999cda42b705f6d2d44cf98e2
BOOT_SHA256=f9745bb095726381b3295ec78a0d1b8fdc03b0e59b661d588313f02a5f13e2be
PAC=0
SERIAL=BH905SX976
IMAGE=artifacts/builds/xzs-xnu-boot.img
```

Host transcript: `artifacts/hw/d8m8/m8-7-retry11e/host.txt`.

## What the boot did

`fastboot -s BH905SX976 boot` returned exit 0. The XNU shell prompt came up. The existing display preamble completed through panel prepare:

```text
clocks mmagic-* PASS
display power mdss-on PASS
clocks mdss-ahb/axi/mdp-on PASS
display m3-run full PASS_FULL_M3
display m4-run full PASS_MODE2
display p1-run RESULT=PASS_P1
display m8-status PANEL_READY=yes
```

`SError=0` and `PANIC=0` in the M4/P1 status lines are the existing zero counters, not a fault report.

`display m8-fb-init` printed the framebuffer map line and then sent nothing further:

```text
[MAP] Mapping physical framebuffer via ml_io_map_unmappable...
```

The host waited 45 seconds. `M8_1 = PASS` did not appear. `xzs#` did not return from that command. No `R11E_SAMPLE` line exists. `CTL_START` was not written. The runner stopped and did not send another command.

A later USB scan showed vendor `Sony`, product `S1Boot Fastboot`, instead of the XNU gadget `1209:000a`. That is an unexpected reset to the bootloader during or after the map call. The map function itself is unchanged (`ml_io_map_unmappable` → `io_map`). The new reads sit after `M8_1 = PASS`, so they were not executed.

## Registers that would have been read

The source map is `artifacts/hw/d8m8/m8-7-retry11e/source-read-map.txt`. Every address below was classified `SAFE_PASSIVE_READ=YES` before the boot. None of them was sampled, because the command never returned from the map.

MDP core: CBCR `0x008c231c`, CMD `0x008c2040`, CFG `0x008c2044`. AHB CBCR `0x008c2308`. AXI CBCR `0x008c2310`. VSYNC CBCR `0x008c2328`, CMD `0x008c2080`, CFG `0x008c2084`. MDSS GDSC `0x008c2304` is read first; MDSS and VBIF reads are skipped when power-on bit 31 is clear.

VBIF base `0x009b0000` (`vbif_phys` in `msm8996-mdss.dtsi`). Legacy QoS words `0x020/024/028/02c`, XIN halt `0x200/0x204`, AXI halt ack `0x20c`, read-limit `0x0b0`. Hardware version is `MDSS_REG_HW_VERSION` at `0x00900000`. Rev `0x10070000` is `MDSS_MDP_HW_REV_107` (`MDSS_MDP_REV(1, 7, 0)` in `include/uapi/linux/msm_mdp.h`). That revision does not set `MDSS_QOS_REMAPPER`, so the Sony RGB0/XIN1 compare is the legacy 2-bit field, bits `[3:2]`, expected `1, 2, 2, 2` from `qcom,mdss-vbif-qos-rt-setting`.

No source-backed register was found that counts an RGB0 AXI beat. `RGB0_CURRENT_SRC0_ADDR` remains a latched pointer. `BUS_BW` remains an RPM/interconnect software vote with no identified silicon register. `SRC_ERR` / `ERR_INFO` are defined and not read by the pinned driver, so they were not added.

## Required answers

```text
MDP_CORE_BRANCH_ENABLED=UNKNOWN
MDP_CORE_SOURCE_CONFIG_VALID=UNKNOWN
MDP_CORE_RATE_HZ=UNKNOWN
MDP_AHB_CLOCK_ACTIVE=UNKNOWN
MDP_AXI_CLOCK_ACTIVE=UNKNOWN
VSYNC_CLOCK_ACTIVE=UNKNOWN

VBIF_XIN1_STATE=UNKNOWN
VBIF_QOS_MATCHES_SONY=UNKNOWN
BUS_BW_VOTE_OBSERVABLE=NO
RGB0_FETCH_PROVEN=UNKNOWN
INTF1_UNDERRUN_SEEN=UNKNOWN
PP_LINE_NONZERO=NOT_SAMPLED
PP_OUT_NONZERO=NOT_SAMPLED
PP0_DONE_SEEN=UNKNOWN
DSI_MDP_BUSY_SEEN=UNKNOWN
CMD_MDP_DONE_SEEN=UNKNOWN
```

`BUS_BW_VOTE_OBSERVABLE=NO` is the source finding from the command-resource audit: the vote lives in the interconnect/RPM client, and no passive silicon alias was identified. This boot did not print `BUS_BW_VOTE_HW_STATE` because kickoff did not run.

`PP_LINE` and `PP_OUT` have no sample from this boot. They are not reported as zero.

## Classification

```text
E11-CLOCK-D  INSUFFICIENT_EVIDENCE
E11-VBIF-E   INSUFFICIENT_EVIDENCE
RGB0_FETCH   UNKNOWN
```

Does Retry #11E identify a source-supported runtime resource mismatch?

```text
UNKNOWN
```

The source differences (no `mdss_mdp_set_clk_rate`, no bus vote, no VBIF QoS program) were already known. This boot did not read the RCG, the QoS words, or XIN halt, so it neither confirms nor clears them.

Does the new evidence prove actual framebuffer fetch?

```text
UNKNOWN
```

`CURRENT_SRC0_ADDR` was not read. No fetch counter was sampled. Absence of a sample is not evidence that fetch did not occur.

## Narrowest blocker

```text
PP_FRAME_START_GATE_STILL_UNKNOWN
```

That remains the last sealed hardware result (Retry #11D-B). Retry #11E did not move it. The run-abort is the framebuffer map not returning, followed by a reset into Sony fastboot, before the passive window.

No rate program, bus vote, QoS write, XIN unhalt, TE change, PP change, or DSI change was made.
