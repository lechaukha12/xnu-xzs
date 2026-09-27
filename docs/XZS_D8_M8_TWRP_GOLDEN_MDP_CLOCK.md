# D8-M8 G1 — TWRP golden MDP clock and Sony rate selection

No XNU boot. No MMIO write. No clock program.

```text
G1_CLASS=G1-D GOLDEN_HW_READBACK_UNAVAILABLE
TWRP_KERNEL=not entered this session
TWRP_MMIO_READ_METHOD=UNAVAILABLE
TWRP_MDP_RCG_CMD=NOT_CAPTURED
TWRP_MDP_RCG_CFG=NOT_CAPTURED
TWRP_MDP_SOURCE=NOT_CAPTURED
TWRP_MDP_HID=NOT_CAPTURED
TWRP_MDP_TABLE_MATCH=UNKNOWN
TWRP_MDP_TABLE_RATE_HZ=UNKNOWN
SONY_EXPECTED_MDP_RATE_HZ=171428571
TWRP_EQUALS_SONY_EXPECTED=UNKNOWN
XNU_EQUALS_TWRP=UNKNOWN
MDP_RATE_CORRECTION_CONFIDENCE=LOW
NEXT_ACTION=do not write a rate; repeat the six raw reads from fastboot TWRP
```

Repository at the start of this audit: `xzs-d8-display-m8-resume`, `1f8b4291e8e5a6f8d9073d6127e820f8acb43303`. T1 hardware commit remains `98845843cdf70eb1d625fab490a075fe0fca710d`. No display code was edited.

## 1. Why TWRP was not entered

The attached USB device was the running XNU gadget: vendor `XNU-XZS`, product `XZS USB Console`, id `1209:000a`. `fastboot devices` and `adb devices` were empty. The known recovery entry for this tree is a non-persistent `fastboot boot artifacts/builds/twrp-kagura.img` (`docs/XZS_DISPLAY_GOLDEN_DIFF_DRYRUN.md`). That command was not sent. Rebooting the live XNU shell is not part of that procedure.

```text
G1-S0, G1-S1, G1-S2 = not taken
```

## 2. Prior TWRP evidence, not this capture

`artifacts/display-audit-a1/linux_golden_clocks.txt` is an older debugfs trace from TWRP `3.18.20-v01+` on this Xperia XZs, with the panel scanning 1080x1920. It records:

```text
GPLL0 (600,000,000 Hz)
  mdp_clk_src (MMCC div = 3.5) = 171,428,571 Hz
  mdss_mdp_clk = 171,428,571 Hz
```

Divider 3.5 on GPLL0 is the table row whose `F()` rate is 171,428,571 Hz, hid 6, source cfg 5. The file does not contain the raw words at `0x008c2040` and `0x008c2044`. A separate M2-era table in `artifacts/display-audit/linux_golden_clocks.txt` shows `mdss_mdp_clk` at `0x008c231c` = `0x00006221`, the same branch word XNU T1 read, and does not show the RCG.

That debugfs rate is corroboration. It is not G1's three raw snapshots.

## 3. Sony rate-selection trace

Pin `5772572ccdfbc16c270d33f8fa6b55d33d27709c`. Class of the arithmetic: `CALCULATED`. The functions and the inputs are `SOURCE_PROVEN`.

Keyaki `somc,sharp_synaptics_cmd_9_panel` (`artifacts/display-audit/keyaki.dts:1740`): 1080 x 1920, 60 Hz, command mode, porches h 8/8/56 and v 8/8/227, borders 0. The node has no `qcom,mdss-mdp-transfer-time-us`, so the driver default is 14000 µs (`mdss_dsi_panel.c:34`, `:2924-2925`). The MDSS node has `qcom,mdss-clk-factor = <105 100>` (`keyaki.dts:219`, parsed as numer/denom in `mdss_mdp.c:4200-4211`). No `qcom,mdss-clk-levels` property is present, so `mdss_mdp_select_clk_lvl` does not change the rate.

`mdss_mdp_perf_calc_mixer` (`mdss_mdp_ctl.c:1315-1341`):

```text
h_total = mixer width = 1080
v_total = 1920 + 8 + 227 + 8 = 2163
fps = 60
pixel term = 1080 * 2163 * 60 = 140162400
after clk-factor 105/100 = 147170520

command transfer = 1080 * 2163 * 1000000 / 14000 = 166860000
mdp_clk_rate = max(147170520, 166860000) = 166860000
```

A 1:1 RGB pipe (`get_pipe_mdp_clk_rate`, `:669-673`) is `dst.w * v_total * fps` and stays below that transfer term, so the mixer value remains the request. `qcom_find_freq` (`drivers/clk/qcom/common.c:37-50`) returns the first table row at or above the request. `166860000` sits between 150,000,000 and 171,428,571, so the selected row is:

```text
F(171428571, P_GPLL0, 3.5, 0, 0)
hid = 2*3.5 - 1 = 6
source cfg = 5
```

`mdss_mdp_get_mdp_clk_rate` (`mdss_mdp_ctl.c:2270-2285`) passes that through `clk_round_rate`. `mdss_mdp_ctl_perf_update` (`:2425-2437`) then calls `mdss_mdp_set_clk_rate`. Probe also sets the max table rate, 412,500,000 Hz, before the first commit. The steady rate after perf update is the rounded perf value, not that probe maximum. The older TWRP debugfs number, 171,428,571 Hz, matches this perf result and does not match the probe maximum.

```text
SONY_EXPECTED_MDP_RATE_HZ=171428571
SONY_EXPECTED_CLASS=CALCULATED
```

## 4. Comparison with XNU T1

| State | XNU T1 | This G1 TWRP read | Prior TWRP debugfs | Match this session |
|---|---:|---:|---:|---|
| RCG CMD | `0x00000000` | not captured | not recorded | UNKNOWN |
| RCG CFG | `0x00000000` | not captured | not recorded | UNKNOWN |
| source | BI_TCXO | not captured | GPLL0 | UNKNOWN |
| HID | 0 | not captured | 6 if div 3.5 | UNKNOWN |
| table rate | none | not captured | 171428571 | UNKNOWN |
| MDP CBCR | `0x00006221` | not captured | `0x00006221` in the M2 table | branch word matches that older table |
| AHB CBCR | `0x20008001` | not captured | `0x20008001` in the M2 table | same |
| AXI CBCR | `0x00006221` | not captured | `0x00006221` in the M2 table | same |
| VSYNC CBCR | `0x80000000` before stream config | not captured | not used | not a mismatch |

XNU's branch-on / RCG-at-XO state is still `HW_READBACK_PROVEN` from T1. It is not the nine-row table. It is also not proven, by a raw word from this session, to differ from the RCG TWRP is using right now.

## 5. Decision

```text
GOLDEN_MDP_RATE_CONFIRMED = no
MDP_RATE_CORRECTION_CONFIDENCE = LOW
```

The calculated Sony row and the older debugfs rate are the same number. That is not the three raw RCG snapshots this task required, so it does not authorize an XNU rate write. The next hardware step, when the phone is in fastboot, is still G1: `fastboot boot` of `artifacts/builds/twrp-kagura.img`, then read-only copies of `0x008c2040`, `0x008c2044`, `0x008c231c`, `0x008c2308`, `0x008c2310`, and `0x008c2328`. No XNU image, no MMCC write, no T2.
