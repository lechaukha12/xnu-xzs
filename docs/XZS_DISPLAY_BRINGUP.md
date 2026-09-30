# D8 display bring-up

This branch is rebuilt from `b18ba07` and does not include the ramoops cache experiments. The MMCC values below were first measured on `93c511c`. A clean image must show the same power and halt state, or an explained difference, before it is tagged.

First pixels, later, are CPU framebuffer to MDP5 to DSI to the panel. No GPU. D8-M1 does not turn anything on.

## Path

```text
XO / GPLL0 / MMPLL / DSI PLL
        ↓
GCC  0x00300000
        ↓
MMCC 0x008c0000
        ↓
MMAGIC_MDSS_GDSC then MDSS_GDSC
        ↓
MDSS 0x00900000
        ↓
MDP5 0x00901000  (intf1 → DSI0, intf2 → DSI1)
        ↓
DSI0 0x00994000
        ↓
PHY 0x00994400 / lanes 0x00994500 / PLL 0x00994800
        ↓
panel
        ↓
backlight
```

Reference for addresses and clock parents is `device/reference/msm8996.dtsi` plus Linux `drivers/clk/qcom/mmcc-msm8996.c`. That is reference evidence, not proof the block is powered on this phone.

`status = "disabled"` on the MDSS and DSI nodes is the Linux node state. It does not say the hardware power domain is off.

## Power and reset

| Block | Register | Role |
|---|---|---|
| MMAGIC_MDSS_GDSC | MMCC `0x247c` | Parent domain. Upstream marks it votable and always-on. |
| MDSS_GDSC | MMCC `0x2304` | Domain that gates MDSS, MDP, DSI, PHY, and PLL slaves. |
| MDSS_BCR | MMCC reset cell named in the DTS | Block reset. Offset is not copied here. No write in M1. |
| MMAGIC_MDSS_BCR | MMCC `0x2470` | Reference only. No write in M1. |

GDSC bit 31 is `PWR_ON`. Bit 0 is `SW_COLLAPSE`. Branch bit 0 is the enable.

MDSS, MDP, DSI, PHY, and PLL registers stay unread. A read there while the GDSC is off can stall the bus.

## Clocks

MMCC branch registers, read allowlist:

| Clock | MMCC offset | Parents (reference) |
|---|---|---|
| mmss_mmagic_cfg_ahb | `0x5054` | ahb_clk_src, marked critical upstream |
| mdss_ahb | `0x2308` | ahb_clk_src (XO, GPLL0, MMPLL0) |
| mdss_axi | `0x2310` | axi_clk_src |
| mdss_mdp | `0x231c` | mdp_clk_src (GPLL0, MMPLL5) |
| mdss_pclk0 | `0x2314` | DSI0/DSI1 PLL |
| mdss_byte0 | `0x233c` | DSI byte PLL |
| mdss_esc0 | `0x2344` | XO or DSI byte PLL |

GCC is `0x00300000`. `gcc_mmss_noc_cfg_ahb_clk` feeds the MMSS config path. Its CBCR offset is not in this tree, so XNU does not read it yet.

## What XNU reads

Only the MMCC allowlist above. Each read prints `MMCC_READ_PRE` before the load and `MMCC_READ_POST` after it. The shell is the record of the read. MDSS `0x00900000`, MDP `0x00901000`, DSI `0x00994000`, PHY, and PLL are not touched.

MMCC sits in the device window `0x00000000–0x01ffffff`, which the bootstrap map already installs for the watchdog. The read uses that identity map.

## Panel

The repo has no panel compatible string, lane count, timing, or DCS sequence. `panel_tvdd` in the Tone dtsi is a fixed regulator on GPIO 50, active-low, wired to the Synaptics touch controller. That is not a panel init sequence.

TWRP's DTB reserves `cont_splash_mem` at `0x83401000`. That is evidence Sony keeps a splash reservation. It is not evidence the panel is scanning when XNU starts. XNU `boot_args` video fields are zero because the boot shim clears them.

## Recovery-kernel observation

On this TWRP boot, debugfs enable counts were:

```text
gcc_mmss_noc_cfg_ahb_clk = 1
mmss_mmagic_cfg_ahb_clk  = 0
mdss_ahb/axi/mdp/byte0/pclk0/esc0 = 0
```

That is the recovery kernel's clock-consumer count, not the Sony bootloader state and not an XNU MMIO read.

## XNU read on 93c511c

Host log `artifacts/hw/d8m1-93c511c/host.txt`. Nine MMCC loads each printed PRE and POST. `pwd` then returned `/`. No MDSS, DSI, PHY, or PLL register was read.

| Register | Value | bit0 | bit31 |
|---|---|---|---|
| MMAGIC_MDSS_GDSC | `0xa0222000` | 0 | 1 |
| MDSS_GDSC | `0x00222001` | 1 | 0 |
| mdss_byte0 / pclk0 / esc0 | `0x80000000` | 0 | 1 |
| mmagic_cfg_ahb / mdss_ahb | `0x80008000` | 0 | 1 |
| mdss_axi / mdss_mdp | `0x80004220` | 0 | 1 |

For a GDSC, bit 31 is power-on and bit 0 is software collapse. The parent domain reports power on. MDSS_GDSC reports collapsed. For a branch, bit 0 is enable and bit 31 set means halted. The display branches are halted.

## D8-M2 commands

The clean image `861032c` reproduced the table above (`artifacts/hw/d8m1-clean-861032c/host.txt`) and is tag `xzs-d8-m1-complete`. Later writes live on `xzs-d8-m2-power`.

Shell commands, each one transaction:

```text
display power status
display power mmagic-on
display power mdss-on
clocks display status
clocks mdss-ahb-on
clocks mdss-axi-on
clocks mdp-on
```

`mmagic-on` writes nothing when GDSCR bit 31 is set and bit 0 is clear. It reports `ALREADY_ON`.

The write sequence is the Linux `gdsc_enable` path for these two domains, not a guess from bit names.

MMAGIC_MDSS (`0x247c`, hw status `0x2480`, flags VOTABLE|ALWAYS_ON, pwrsts OFF_ON): clear `SW_COLLAPSE`, `delay(1)` because `gds_hw_ctrl` is set, poll `PWR_ON` on `0x2480` for at most 2000 µs, `delay(1)`, set `HW_CONTROL` (bit 1). No reset, clamp, or memory-retain registers.

MDSS (`0x2304`, parent mmagic, cxcs `0x2310` and `0x231c`, pwrsts OFF_ON, no HW_CTRL, no SW_RESET, no CLAMP_IO): refuse if the parent is not already on. Clear `SW_COLLAPSE`, poll `PWR_ON` on `0x2304` for at most 2000 µs. On success set `RETAIN_MEM` (bit 14) and `RETAIN_PERIPH` (bit 13) on the two cxc registers, then `delay(1)`. Those bits are not the branch enable.

Branch enable is `clk_branch2`: set CBCR bit 0 only, then poll until bit 31 (`CBCR_CLK_OFF`) clears or the NoC FSM field (bits 30:28) equals 2. Cap 2000 µs. A clock command writes nothing if MDSS_GDSC is not on.

A timeout prints the readback and returns to the prompt. No MDSS, MDP, DSI, PHY, or PLL slave read is added by these commands.

On `0b0e429`, `mdss_ahb` accepted enable bit 0 and stayed halted (`0x80008001`). Repeating that write is not the next step. `clocks mdss-ahb-status` only reads the audited chain:

| Node | Register | Parent | Class |
|---|---|---|---|
| `mdss_ahb` | MMCC `0x2308` | `ahb_clk_src` | branch, bit 0 enable, bit 31 halt |
| `ahb_clk_src` | MMCC CMD `0x5000`, CFG `0x5004` | XO=0, MMPLL0=1, GPLL0=5, GPLL0_DIV=6 | RCG. Root is on when CMD bit 31 is clear. Bit 1 is ROOT_EN. CFG bits 10:8 are the source. |
| `mmss_mmagic_ahb` | MMCC `0x5024` | same RCG | critical branch |
| `mmss_mmagic_cfg_ahb` | MMCC `0x5054` | same RCG | critical branch |
| `mmagic_mdss_noc_cfg_ahb` | MMCC `0x2478` | `gcc_mmss_noc_cfg_ahb` | critical branch |
| `gcc_mmss_noc_cfg_ahb` | GCC `0x00309008` | not named in the branch | GCC branch, `CLK_IGNORE_UNUSED` |

Those addresses are reference evidence from Linux `mmcc-msm8996.c`, `gcc-msm8996.c`, and `clk-rcg2.c`. A value printed by the shell is hardware evidence. GCC and MMCC sit in the existing device window, so these reads do not touch MDSS slaves.

Read-only hardware on `94a2c37` (`artifacts/hw/d8m2-94a2c37/host.txt`), after a fresh boot, with MDSS collapsed again:

```text
ahb_cmd            0x00000000   root_off=0 root_en=0 update=0
ahb_cfg            0x00000513   source field = GPLL0
mdss_ahb           0x80008000   enable=0 halt=1
mmss_mmagic_ahb    0x80000000   enable=0 halt=1
mmss_mmagic_cfg_ahb 0x80008000  enable=0 halt=1
mmagic_mdss_noc    0x80000000   enable=0 halt=1
gcc_mmss_noc       0x20008001   enable=1 halt=0
```

No clock write was issued on that boot.

## Reset and AHB dependency

Reference order used by the Linux clock and reset drivers. This is not a claim that every stage is the one blocking `mdss_ahb`.

```text
MMAGIC_MDSS_GDSC
        ↓
MDSS_GDSC
        ↓
BCR level (bit 0 held, not a status latch)
        ↓
ahb_clk_src
        ↓
mdss_ahb branch
        ↓
mdss_axi / axi_clk_src
        ↓
mdss_mdp / mdp_clk_src
```

`mdss_ahb` is `clk_branch2`. Enable and halt are both MMCC `0x2308`. Enable is bit 0. Halt check is `BRANCH_HALT` because `halt_check` is unset. That mode polls `CBCR_CLK_OFF` (bit 31) clear, or NoC FSM bits 30:28 equal to 2. It is not `BRANCH_HALT_DELAY`, `BRANCH_HALT_SKIP`, or `BRANCH_VOTED`. The parent is `ahb_clk_src` only. The branch is not a voted clock. Other MMSS branches share that RCG, but they do not vote `mdss_ahb` itself.

| Item | Register | Bit | Assert | Deassert | Safe to read | Safe to write |
|---|---|---|---|---|---|---|
| MDSS_BCR | MMCC `0x2300` | 0 | write 1 | write 0 | yes, MMCC | only after a read shows bit 0 set |
| MMAGIC_MDSS_BCR | MMCC `0x2470` | 0 | write 1 | write 0 | yes, MMCC | same rule |
| MMAGICAHB_BCR | MMCC `0x5020` | 0 | write 1 | write 0 | yes, MMCC | same rule |
| MMAGIC_CFG_BCR | MMCC `0x5050` | 0 | write 1 | write 0 | yes, MMCC | same rule |

Source: Linux `mmcc-msm8996.c` reset map and `drivers/clk/qcom/reset.c`. `qcom_reset()` asserts, waits 1 µs when the map delay is zero, then deasserts. The read inside assert is discarded. A zero bit matches the deassert write. It is not a separate status bit.

`clocks mdss-ahb-debug` reads those BCRs, the AHB chain, GPLL0 mode at GCC `0x000000` (`PLL_LOCK_DET` is bit 31), and the GPLL0 vote at GCC `0x052000` bit 0. It writes nothing.

Hardware on `a5f47b5` (`artifacts/hw/d8m2-a5f47b5/host.txt`): all four BCR reads were `0x00000000`. GPLL0 mode was `0xc0118000` with lock set, and the vote enable bit was set. No reset write was issued.

## Linux critical baseline

`CLK_IS_CRITICAL` is handled in `__clk_core_init`. Linux calls `clk_core_prepare` and `clk_core_enable` for that clock. Later unprepare/disable refuse to drop the last count, so the clock stays enabled. Enable also enables the parent. This is a reference fact about the clock core, not proof that a missing critical branch is why `mdss_ahb` stayed halted.

| Clock | Register | Parent | Critical | Linux at MMCC registration |
|---|---|---|---|---|
| `mmss_mmagic_ahb` | MMCC `0x5024` | `ahb_clk_src` | yes | prepared and enabled |
| `mmss_mmagic_cfg_ahb` | MMCC `0x5054` | `ahb_clk_src` | yes | prepared and enabled |
| `mmagic_mdss_noc_cfg_ahb` | MMCC `0x2478` | `gcc_mmss_noc_cfg_ahb` | yes | prepared and enabled |
| `mmagic_mdss_axi` | MMCC `0x2474` | `axi_clk_src` | yes | prepared and enabled |
| `mdss_ahb` | MMCC `0x2308` | `ahb_clk_src` | no | left for the MDSS driver |

`axi_clk_src` CMD is MMCC `0x5040` and CFG is `0x5044`. Its source map is XO=0, MMPLL0=1, MMPLL1=2, GPLL0=5, GPLL0_DIV=6. Root-off is CMD bit 31. These MMCC branches can be read before MDSS_GDSC is on. `clocks mdss-critical-status` only reads them.

On `059d58a` (`artifacts/hw/d8m2-059d58a/host.txt`), with MDSS still collapsed:

```text
mmss_mmagic_ahb         0x80000000  enable=0 halt=1
mmss_mmagic_cfg_ahb     0x80008000  enable=0 halt=1
mmagic_mdss_noc_cfg_ahb 0x80000000  enable=0 halt=1
mmagic_mdss_axi         0x80000000  enable=0 halt=1
ahb_clk_src             cmd 0x00000000 cfg 0x00000513 root_off=0 source=GPLL0
axi_clk_src             cmd 0x00000000 cfg 0x00000000 root_off=0 source=XO
gcc_mmss_noc_cfg_ahb    0x20008001
```

That differs from the Linux registration baseline. It does not by itself prove the difference causes the `mdss_ahb` halt. The four branch enables are separate commands and each one refuses to write if its own parent root is off. They do not require MDSS_GDSC.

## Hardware proof on candidate 545398f (D8-M2 PASS)

Hardware execution session `artifacts/hw/d8m2-545398f/host.txt` on candidate `545398f` (tag `xzs-d8-m2-complete`, SHA256: `5a5185fe9b53da69895cc2a6c1b68e96b0f46409cac8e4ca9fb044f6d0712704`).

1. Baseline capture:
   - `MMAGIC_MDSS_GDSC` = `0xa0222000` (ON)
   - `MDSS_GDSC` = `0x00222001` (collapsed)
   - All 4 critical MMAGIC branches: `enable=0, halt=1`
   - Both roots running: `ahb_root_off=0` (GPLL0), `axi_root_off=0` (XO)
   - GCC MMSS NOC: `0x20008001` (running)

2. Critical MMAGIC clock enables:
   - `mmss_mmagic_ahb` (MMCC 0x5024): `old=0x80000000, wrote=0x80000001, new=0x00000001` -> PASS (`enable=1, halt=0`)
   - `mmss_mmagic_cfg_ahb` (MMCC 0x5054): `old=0x80008000, wrote=0x80008001, new=0x20008001` -> PASS (`enable=1, halt=0`)
   - `mmagic_mdss_noc_cfg_ahb` (MMCC 0x2478): `old=0x80000000, wrote=0x80000001, new=0x00000001` -> PASS (`enable=1, halt=0`)
   - `mmagic_mdss_axi` (MMCC 0x2474): `old=0x80000000, wrote=0x80000001, new=0x00000001` -> PASS (`enable=1, halt=0`)

3. MDSS GDSC power on:
   - `MDSS_GDSC` (MMCC 0x2304): `old=0x00222001, wrote=0x00222000, new=0xa0222000` -> PASS (`PWR_ON=1, SW_COLLAPSE=0`)

4. Decisive retry: `mdss_ahb`:
   - `mdss_ahb` (MMCC 0x2308): `old=0x80008000, wrote=0x80008001, new=0x20008001, readback=0x20008001` -> **PASS** (`enable=1, halt=0, FSM_ON=1`)
   - Halt cleared immediately within 2000 µs.

5. MDSS AXI & MDP clock bring-up:
   - `mdss_axi` (MMCC 0x2310): `old=0x80006220, wrote=0x80006221, new=0x00006221, readback=0x00006221` -> PASS (`enable=1, halt=0`)
   - `mdss_mdp` (MMCC 0x231c): `old=0x80006220, wrote=0x80006221, new=0x00006221, readback=0x00006221` -> PASS (`enable=1, halt=0`)

6. Shell health:
   - `pwd` returned `/`, shell responsive, zero panics, zero resets.

Conclusion:
D8-M2 acceptance criteria fully satisfied on physical silicon.
Tag `xzs-d8-m2-complete` sealed at `545398f30d8fda592d4ca67ee867a016c2f37092`.
Display bring-up milestones through D8-M8.5 (First Visible Display) are now COMPLETE & SEALED on hardware (tag `xzs-d8-m8.5-complete`).
Physical Sharp 1080p IPS LCD panel is verified illuminated with visible pixels across 3/3 fresh cold boots.
Next active milestone: D8-M10 (Framebuffer Text Console / Interactive Shell Display).
See `docs/XZS_D8_M8_5_FIRST_VISIBLE_DISPLAY.md` and `docs/XZS_D8_M8_FINAL_RECONCILIATION.md`.


