# D8-M8 G1 — TWRP golden MDP clock and Sony rate selection

No XNU boot. No MMIO write. No rate program.

```text
G1_CLASS=G1-B RUNTIME_DVFS_OR_CONTEXT_DIFFERENCE
TWRP_KERNEL=Linux 3.18.20-v01+ #2 SMP PREEMPT Mon Oct 3 23:07:41 JST 2016
TWRP_MMIO_READ_METHOD=DEBUGFS
TWRP_MDP_RCG_CMD=0x80000000
TWRP_MDP_RCG_CFG=0x0000050d
TWRP_MDP_SOURCE=GPLL0
TWRP_MDP_HID=13
TWRP_MDP_TABLE_MATCH=CFG_YES_ROOT_OFF
TWRP_MDP_TABLE_RATE_HZ=85714286
TWRP_DECODER_CLASS=ROOT_OFF
SONY_EXPECTED_MDP_RATE_HZ=171428571
TWRP_EQUALS_SONY_EXPECTED=NO
XNU_EQUALS_TWRP=NO
MDP_RATE_CORRECTION_CONFIDENCE=NONE
NEXT_ACTION=do not write a rate
```

## 1. TWRP identity

`fastboot -s BH905SX976 boot artifacts/builds/twrp-kagura.img`. Not flashed.

```text
uid=0(root)
Linux version 3.18.20-v01+ (androplus@sonymobile.com)
#2 SMP PREEMPT Mon Oct 3 23:07:41 JST 2016
androidboot.serialno=BH905SX976
androidboot.hardware=qcom
```

`/sys/class/graphics/fb0` is `mdssfb_90000`, state 0, mode `U:1080x1920p-360`. The boot command line still says `display_status=off`. The MDP, AHB, AXI, and VSYNC branches below are halted, so this snapshot is the idle clock image, not a frame in flight.

## 2. Read method

`/sbin/devmem` is present and refuses to open `/dev/mem` (`No such file or directory`). No device node was created. debugfs was already mounted. The clock `print` nodes dump the live CMD, CFG, and CBCR. Those files were only read. `measure` was not read.

Three reads, about a minute apart, returned the same words.

## 3. Raw snapshots

| Register | Address | G1-S0 | G1-S1 | G1-S2 |
|---|---:|---:|---:|---:|
| MDP CMD | `0x008c2040` | `0x80000000` | `0x80000000` | `0x80000000` |
| MDP CFG | `0x008c2044` | `0x0000050d` | `0x0000050d` | `0x0000050d` |
| MDP CBCR | `0x008c231c` | `0x80006220` | `0x80006220` | `0x80006220` |
| AHB CBCR | `0x008c2308` | `0x80008000` | `0x80008000` | `0x80008000` |
| AXI CBCR | `0x008c2310` | `0x80000220` | `0x80000220` | `0x80000220` |
| VSYNC CBCR | `0x008c2328` | `0x80000000` | `0x80000000` | `0x80000000` |

Debugfs, same moments: `mdp_clk_src` rate 85714286, parent `mmsscc_gpll0`, enable 0. `mmsscc_gpll0` itself reads 600000000 and enable 1. The nine `list_rates` values match `ftbl_mdp_clk_src`.

## 4. Decode

CMD bit 31 is set. The preflight decoder returns `ROOT_OFF` before it looks at the table.

CFG `0x0000050d`: source bits `[10:8]` = 5 (`GPLL0`), hid = 13, mode = 0. That is the first table row, `F(85714286, P_GPLL0, 7, 0, 0)`. The declared table rate is 85,714,286 Hz. It is not a PLL measurement. GPLL0's own debugfs rate is 600,000,000 Hz, and `600000000 / 7` is that row.

Branch bit 0 is clear and bit 31 is set on MDP, AHB, AXI, and VSYNC. All four are `INACTIVE`. Enable counts are 0. The RCG configuration stayed put across the three samples while the branches stayed off.

## 5. Sony in-frame calculation

Unchanged from the source trace, class `CALCULATED`.

Keyaki 1080x1920 command mode, 60 Hz, `v_total` 2163, mixer width 1080, clk-factor 105/100, transfer time default 14000 µs. The command-transfer term is 166,860,000 Hz. `qcom_find_freq` rounds up to 171,428,571 Hz, GPLL0 divided by 3.5, hid 6. No `qcom,mdss-clk-levels` entry changes that. Probe can also set the maximum row, 412,500,000 Hz, before the first perf update.

```text
SONY_EXPECTED_MDP_RATE_HZ=171428571
```

That is a different table row from the CFG captured here.

## 6. Comparison

| State | XNU T1 | TWRP G1 | Match |
|---|---:|---:|---|
| RCG CMD | `0x00000000` | `0x80000000` | no |
| RCG CFG | `0x00000000` | `0x0000050d` | no |
| source | BI_TCXO, hid 0 | GPLL0, hid 13 | no |
| root | on | off | no |
| table row | none | 85714286, not running | no |
| MDP CBCR | `0x00006221` active | `0x80006220` inactive | no |
| AHB CBCR | `0x20008001` active | `0x80008000` inactive | no |
| AXI CBCR | `0x00006221` active | `0x80000220` inactive | no |
| VSYNC CBCR | `0x80000000` before stream config | `0x80000000` | not used as a mismatch |

XNU left the branch on and the RCG at XO bypass. TWRP, in this idle sample, has the lowest table row programmed and the root and branches off. Sony's in-frame rounding picks a third value, 171,428,571 Hz. An older debugfs note in `artifacts/display-audit-a1/linux_golden_clocks.txt` also says 171,428,571 Hz. This boot's register print does not.

## 7. Why the rows differ

The task asked for a steady snapshot and not a kickoff-synchronized one. Command mode in the pinned driver turns the MDP clock bundle on for a frame and can gate it afterward. This TWRP image shows that gated side: enable count 0, CBCR halted, CMD root off, while the framebuffer device still exists. The CFG left behind is the bottom table row, not the calculated in-frame row and not XNU's XO bypass.

So the idle golden state does not choose the rate an XNU one-change test should program. 85,714,286 Hz is what is parked. 171,428,571 Hz is what the pinned perf path requests for a full frame. Neither was observed as a running root-on RCG in this capture.

```text
MDP_RATE_CORRECTION_CONFIDENCE=NONE
NEXT_ACTION=do not write a rate
```

Raw log: `artifacts/hw/d8m8/g1-twrp-mdp-clock/raw-snapshots.txt`.
