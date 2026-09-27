# D8-M8 C1 preflight — MDP core rate 171428571 Hz

Source audit only. No build, no boot, no MMIO write, no clock change.
The rate is not programmed by this document.

```text
C1_READY_FOR_HARDWARE=YES
TARGET_CFG=0x00000506
TARGET_RATE_HZ=171428571
RCG_UPDATE_BIT=0x00000001 (CMD bit 0, CMD_UPDATE)
RCG_UPDATE_SEQUENCE=pre-read gates; RMW CFG mask 0x0010371F value 0x00000506; RMW CMD bit 0; poll bit 0 clear; read CMD, CFG, MDP CBCR
UPDATE_COMPLETION_TEST=(CMD & 0x00000001) == 0
GPLL0_READY=YES
BRANCH_CAN_REMAIN_ON_DURING_UPDATE=YES
NEW_MAPPING_REQUIRED=NO
SAFE_INSERTION_POINT=after display m8-fb-init prints M8_1 = PASS and returns to xzs#, before display m8-rgb0-config
EXPECTED_POSTWRITE_CMD=0x00000000
EXPECTED_POSTWRITE_CFG=0x00000506
ROLLBACK_REQUIRED=NO
NEXT_ACTION=STOP. Do not build, boot, or program the rate.
```

Causality remains unproven. A later hardware run is what can promote
`MDP_CORE_RATE_CAUSAL_TO_FRAME_START`. This preflight only fixes the
register procedure.

## 1. Repository

Canonical tree `/Users/lechaukha12/Desktop/xnu-xzs`, branch
`xzs-d8-display-m8-resume`.

```text
HEAD=0db057cc378605e30b577fbdf79143564f20b1cf
WORKTREE=clean
T1 evidence=1f8b4291e8e5a6f8d9073d6127e820f8acb43303
G1=4aa05dd755176ce3d25be30c6ab933d8106bfbcf
G2=0db057cc378605e30b577fbdf79143564f20b1cf
```

Clock source pin:

```text
https://github.com/sonyxperiadev/kernel
branch aosp/LA.UM.7.1.r1
commit 5772572ccdfbc16c270d33f8fa6b55d33d27709c
```

`mdp_clk_src` uses `clk_rcg2_ops`, not `clk_rcg2_shared_ops` and not
`clk_rcg2_floor_ops` (`mmcc-msm8996.c` `mdp_clk_src`).

## 2. Why this one row

| State | CMD `0x008c2040` | CFG `0x008c2044` | MDP CBCR `0x008c231c` | Class |
|---|---|---|---|---|
| XNU T1 after `M8_1` | `0x00000000` | `0x00000000` | `0x00006221` | `HW_READBACK_PROVEN`, T1-D, BI_TCXO hid 0 |
| TWRP active frame | `0x00000000` | `0x00000506` | `0x00006221` | `HW_PROVEN`, G2-A |
| Sony Keyaki ceiling | — | src 5, hid 6 | — | `SOURCE_PROVEN` + `CALCULATED`, 171428571 Hz |

`0x00000506` is the only target. No other `ftbl_mdp_clk_src` row.
No new divider.

The conceptual change is one rate move: BI_TCXO ~19.2 MHz to
GPLL0 / hid 6 / 171428571 Hz. The RCG update below is several stores
and a poll. They are that one change. Bus, VBIF, TE, PP, and DSI
stay untouched.

## 3. Ten programming answers

All ten are from `drivers/clk/qcom/clk-rcg2.c`, `clk-rcg.h`,
`mmcc-msm8996.c`, `drivers/clk/clk.c`, `drivers/base/regmap/regmap-mmio.c`,
and `arch/arm64/include/asm/io.h` at `5772572`.

### 1. Which register is written first?

`SOURCE_PROVEN`. `CFG_RCGR` at `cmd_rcgr + 0x4` = `0x008c2044`.

`clk_rcg2_configure` reads the old CFG, skips M/N/D for this clock,
then `regmap_update_bits` on `CFG_REG`. `update_config` runs only
after that returns. M and N and D are not written.

### 2. Which CFG fields are changed?

`SOURCE_PROVEN`. The mask is:

```text
(BIT(hid_width) - 1) | CFG_SRC_SEL_MASK | CFG_MODE_MASK | CFG_HW_CLK_CTRL_MASK
hid_width = 5
= 0x0000001F | 0x00000700 | 0x00003000 | 0x00100000
= 0x0010371F
```

| Field | Bits | Mask | Value in `0x00000506` |
|---|---|---|---|
| SRC_DIV / hid | `[4:0]` | `0x0000001F` | 6 |
| SRC_SEL | `[10:8]` | `0x00000700` | 5 = `P_GPLL0` cfg |
| MODE | `[13:12]` | `0x00003000` | 0 |
| HW_CLK_CTRL | bit 20 | `0x00100000` | 0 |

`CFG_MODE_DUAL_EDGE` (`0x00002000`) is set only when `mnd_width && f->n && m != n`.
This row has `m = 0`, `n = 0`, and `mdp_clk_src.mnd_width` is unset (0).
Mode stays 0.

`mdp_clk_src.flags` does not include `HW_CLK_CTRL_MODE`, so bit 20 is cleared.

`F(171428571, P_GPLL0, 3.5, 0, 0)` stores `pre_div = 2 * 3.5 - 1 = 6`
(`mmcc-msm8996.c` macro `F` and `ftbl_mdp_clk_src`). Parent map
`mmss_xo_mmpll0_mmpll5_gpll0_gpll0_div_map` gives `P_GPLL0` the cfg value 5.
The programmed CFG payload is `(6 << 0) | (5 << 8) = 0x00000506`.

T1 read CFG as `0x00000000`, so this RMW produces exactly `0x00000506`.
A future run whose pre-read CFG is not `0x00000000` stops before the RMW.
That keeps the result equal to the proven word.

### 3. Is the CMD update bit required?

`SOURCE_PROVEN`. Yes. `clk_rcg2_configure` always returns through
`update_config`. On `qcom,msm8996`, `clk_rcg2_current_config` returns
false immediately, so this SoC does not skip the update as "already
matching".

### 4. What CMD bit initiates the update?

`SOURCE_PROVEN`. `CMD_UPDATE = BIT(0) = 0x00000001`.

`update_config` does `regmap_update_bits(..., CMD_UPDATE, CMD_UPDATE)`.
It does not set `CMD_ROOT_EN` (`BIT(1)`) and it does not write
`CMD_ROOT_OFF` (`BIT(31)`).

`mdp_clk_src` does not set `FORCE_ENABLE_RCG`. The prepared `set_rate`
path therefore does not call `clk_rcg2_set_force_enable`. Bit 1 stays
as it was. T1 and the active TWRP frame both had CMD `0x00000000`, so
bit 1 stays 0.

### 5. What condition indicates update completion?

`SOURCE_PROVEN`. `(cmd & CMD_UPDATE) == 0`. That is bit 0 clear.

These dirty bits exist and are not the completion test:

```text
CMD_DIRTY_CFG  BIT(4)  0x00000010
CMD_DIRTY_N    BIT(5)  0x00000020
CMD_DIRTY_M    BIT(6)  0x00000040
CMD_DIRTY_D    BIT(7)  0x00000080
```

`CMD_ROOT_OFF` is bit 31. It is a root-status bit. `clk_rcg2_is_enabled`
treats the root as on when bit 31 is clear. It is not the update bit.

### 6. Does software poll the update bit until clear?

`SOURCE_PROVEN`. Yes. `update_config` reads CMD up to 500 times,
`udelay(1)` between reads, and returns 0 when bit 0 is clear. If bit 0
is still set, it warns and returns `-EBUSY`.

A future C1 treats timeout as class C1-C. No kickoff. No second write.

### 7. Is the branch clock expected ON or OFF during the update?

`SOURCE_PROVEN`. OFF is not required. The branch may stay ON.

`mdss_mdp_clk` is the branch at halt/enable `0x231c`, parent
`mdp_clk_src`, flags `CLK_SET_RATE_PARENT | CLK_ENABLE_HAND_OFF`,
ops `clk_branch2_ops`. `mdp_clk_src` does not set `CLK_SET_RATE_GATE`.
`clk_core_set_rate_nolock` rejects a prepared rate change only when
`CLK_SET_RATE_GATE` is set. `clk_rcg2_configure` never reads or writes
`0x008c231c`.

When `prepare_count > 0` and the parent changes, `__clk_set_parent_before`
enables the clock for the switch. It does not turn it off.
`CLK_SET_RATE_UNGATE` is also unset, so the core does not add a separate
ungate/regate around `set_rate`.

XNU `clocks mdp-on` (`xzs_d8m2_branch_on`) writes only CBCR bit 0.
T1 then observed `0x00006221` with the root already on. G2 observed the
same CBCR word while CFG was `0x00000506`. The C1 order is: branch
already enabled, then the RCG update. C1 does not clear the branch to
perform the update, and it does not enable the branch itself.

### 8. Is parent preparation required?

`SOURCE_PROVEN`. The RCG function does not prepare the parent.
The framework does, and only in one case.

`mdp_clk_src` does not set `CLK_SET_RATE_PARENT`, `CLK_OPS_PARENT_ENABLE`,
or `FORCE_ENABLE_RCG`. `__clk_rcg2_set_rate` calls
`prepare_enable_rcg_srcs` only for `FORCE_ENABLE_RCG`. That call is not
taken.

`__clk_set_parent_before` calls `clk_core_prepare_enable` on the new
parent when `prepare_count > 0`, before `set_rate_and_parent`.
`set_rate_and_parent` for this ops table is `__clk_rcg2_set_rate`.
So a prepared parent switch requires the new parent enabled before the
CFG write. `clk_rcg2_configure` itself never touches GPLL0.

C1 meets that precondition by selecting a GPLL0 that is already running.
It does not add a parent prepare or enable write. Section 5 is the gate.

### 9. Does GPLL0 need an explicit enable or a reprogram?

`SOURCE_PROVEN` for the driver. No enable write. No PLL program.
C1 only selects the parent.

`gpll0` in `gcc-msm8996.c` is `clk_alpha_pll` at GCC offset `0x00000`,
`enable_reg = 0x52000`, `enable_mask = BIT(0)`, flags
`SUPPORTS_FSM_VOTE`, ops `clk_alpha_pll_ops`. Parent is `bi_tcxo`.
The MMCC parent string is `"gpll0"`.

`clk_alpha_pll_enable`: if `PLL_MODE` has `PLL_VOTE_FSM_ENA` (bit 20),
the enable path votes the existing PLL and waits for active. It does
not write L or alpha. The non-FSM reconfigure path runs only when L is
0, which is not this path.

C1 must not write GCC `0x00300000` or GCC `0x00352000`.

### 10. Is a memory barrier required between CFG and CMD?

`SOURCE_PROVEN`. `clk_rcg2_configure` has no `mb()` of its own between
the CFG `regmap_update_bits` and `update_config`.

MMCC regmap is 32-bit MMIO (`mmcc_msm8996_regmap_config`, `fast_io`).
`regmap_mmio_write32le` uses `writel`. On this tree:

```text
arch/arm64/include/asm/io.h
  __iowmb() is wmb()
  writel(v, c) is { __iowmb(); writel_relaxed(v, c); }

arch/arm64/include/asm/barrier.h
  wmb() is dsb(st)
```

The barrier that orders CFG before CMD is the `dsb(st)` inside the
`writel` of the CMD update. The RCG file does not add a second barrier.
A relaxed store pair would not match this path.

XNU's existing `d8p1_write32` and `xzs_mmcc_write32` already execute
`dsb sy; isb sy` after each store. A future C1 uses that accessor for
both the CFG store and the CMD store. That `dsb sy` sits between them.

## 4. CMD and CFG masks

`CMD_RCGR` `0x008c2040` = MMCC `0x2040` + base `0x008c0000`.

| Name | Mask | Role in this update |
|---|---|---|
| `CMD_UPDATE` | `0x00000001` | set to start; poll until clear |
| `CMD_ROOT_EN` | `0x00000002` | not written |
| `CMD_DIRTY_CFG` | `0x00000010` | not the completion test |
| `CMD_DIRTY_N` | `0x00000020` | not the completion test |
| `CMD_DIRTY_M` | `0x00000040` | not the completion test |
| `CMD_DIRTY_D` | `0x00000080` | not the completion test |
| `CMD_ROOT_OFF` | `0x80000000` | status; root is on when clear |

`CFG_RCGR` `0x008c2044`.

```text
0x00000506
  [4:0]   = 0x06  hid / SRC_DIV
  [10:8]  = 0x5   SRC_SEL = GPLL0
  [13:12] = 0x0   MODE, not dual-edge
  bit 20  = 0     HW_CLK_CTRL
```

M/N/D proof: `mdp_clk_src.mnd_width` is 0. The configure predicate
`rcg->mnd_width && f->n` is false for `F(..., 0, 0)`. `M_REG` is
`cmd + 0x8`, `N_REG` is `cmd + 0xc`, `D_REG` is `cmd + 0x10`.
`clk_rcg2_list_registers` prints only CMD and CFG when `mnd_width` is 0.
`clk_rcg2_recalc_rate` reads M/N/D only inside `if (rcg->mnd_width)`.
Writing `0x00000506` does not require M, N, or D.

`clk_rcg2_calc_rate` with parent 600000000, hid 6, mode 0 is
`600000000 * 2 / 7 = 171428571`. That is the table's declared rate.
C1 does not measure or retune the PLL.

## 5. GPLL0 is already available

`GPLL0_READY=YES`.

XNU display code does not write this PLL. `xzs_d8m2_ahb_debug` only
reads it:

```text
GCC 0x00300000  XZS_GCC_GPLL0_MODE   PLL_LOCK_DET is bit 31
GCC 0x00352000  XZS_GCC_GPLL0_VOTE   bit 0
```

`xzs_phys_read32` accepts both addresses. They are below `0x02000000`
and 4-byte aligned. No new mapping.

Hardware, XNU shell, `artifacts/hw/d8m2-545398f/host.txt`, before any M8
frame. The same words are recorded for `a5f47b5` in
`docs/XZS_DISPLAY_BRINGUP.md`.

```text
gpll0_mode=0xc0118000
gpll0_lock=1
gpll0_vote=0x00000011
gpll0_vote_en=1
```

`0xc0118000` is `PLL_LOCK_DET` (bit 31), `PLL_ACTIVE_FLAG` (bit 30),
and `PLL_VOTE_FSM_ENA` (bit 20). `OUTCTRL`, `BYPASSNL`, and `RESET_N`
are clear, which is the FSM-vote mode, not a PLL that still needs the
non-FSM enable sequence. Vote bit 0 is set. Class `HW_READBACK_PROVEN`
for that shell. Later display commits do not add a GPLL0 writer.

TWRP G1, same phone: `mmsscc_gpll0` rate 600000000, enable 1, while the
MDP mux parent was that PLL. G2 then ran frames from it. Class
`HW_READBACK_PROVEN` for TWRP, corroborating the XNU read.

T1's six-register block did not print GPLL0. The future C1 therefore
re-reads these bits in the same boot, before any CFG store:

```text
MODE bit 31 LOCK  = 1
MODE bit 30 ACTIVE = 1
MODE bit 20 FSM   = 1
VOTE bit 0        = 1
```

Any clear bit: stop. Do not vote, do not program L/alpha, do not write
the RCG. The historical words are why this preflight is ready. The
same-boot read is the abort gate, not a second experiment.

Answers:

```text
Does C1 enable GPLL0?            NO
Does C1 program GPLL0?           NO
Does C1 only select a running parent?  YES
```

## 6. Where the update sits

`clocks mdp-on` is diag case 12. It calls `xzs_d8m2_branch_on` on
`0x231c` and can set only CBCR bit 0. It does not write `0x008c2040`
or `0x008c2044`.

Retry #11D-B order, after the shell is up:

```text
clocks mdp-on
display m3-run full
display m4-run full
display p1-run
display m8-status
display m8-fb-init          M8_1 = PASS, MDP_MMIO_WRITES=0
display m8-rgb0-config      first frame configuration
display m8-lm0-config
display m8-stream-config
display m8-ctl-config
display m8-flush-config
display m8-prekick-status
display m8-kickoff          one CTL_START
```

`xzs_d8m8_fb_init` maps and fills the framebuffer. It does not write
MDP or MMCC. T1, taken after this command returned, still showed root
on, CFG 0, MDP branch active.

Safe insertion point: the shell has returned from `display m8-fb-init`
with `M8_1 = PASS`, and the next command is not yet
`display m8-rgb0-config`.

That is after the branch is on and before frame configuration and
before `CTL_START`. It stays out of `ml_io_map_unmappable`. It is not
inside `clocks mdss-critical-status` (diag case 15), which is the T1
read path and must remain a read.

`NEW_MAPPING_REQUIRED=NO`. `d8p1_read32`, `d8p1_write32`,
`xzs_mmcc_read32`, and `xzs_mmcc_write32` use the identity physical
window and a TTBR0 swap. They do not call `ml_io_map`. Page
`0x008c2000` is already read by T1 and by M2/M3.

A future implementation also has to keep `/bin/sh` at 16768 bytes, CRC
`0xf45be15c`, and the bootshim rootfs SHA
`ac8fc067d145446b9c26fabc85879c000e426e429077006036581c6c2797ca5a`.
A new shell verb is not part of this preflight and would halt boot.
This document does not pick a transport.

## 7. Future C1 procedure

One boot, one conceptual change, only after a later task implements
this. Not this commit.

Pre-read. Any miss: print the words, stop, no RCG store, no
`m8-rgb0-config`, no `CTL_START`.

```text
GPLL0 MODE  0x00300000   lock, active, FSM set
GPLL0 VOTE  0x00352000   bit 0 set
MDP CMD     0x008c2040   == 0x00000000
MDP CFG     0x008c2044   == 0x00000000
MDP CBCR    0x008c231c   bit 0 = 1 and bit 31 = 0
```

Then, using the existing identity writer so each store is followed by
`dsb sy`:

```text
1. RMW 0x008c2044  mask 0x0010371F  value 0x00000506
2. RMW 0x008c2040  mask 0x00000001  value 0x00000001
3. Poll 0x008c2040 bit 0, 500 times, 1 microsecond apart
```

Immediate readback:

```text
CMD  bit 0 = 0 and bit 31 = 0, expected word 0x00000000
CFG  == 0x00000506
CBCR bit 0 = 1 and bit 31 = 0
```

Mismatch or poll timeout: class C1-C. Stop. Do not enter `CTL_START`.
Do not write a second rate.

Match: continue the unchanged Retry #11D-B commands from
`display m8-rgb0-config` through one `display m8-kickoff`. No new
instrumentation. Compare with T1 / #11D-B.

Signals already on that path:

```text
CTL_FLUSH
RGB0_CURRENT_SRC0_ADDR
PP_INT_COUNT_VAL
PP_LINE_COUNT
PP_OUT_LINE_COUNT
MDP_INTR_STATUS
DSI_STATUS[2]
CMD_MDP_DONE
```

No VBIF QoS. No new register set.

The first discriminator is `PP_LINE`, not pixels.

| Class | Readback | `PP_LINE` | Result |
|---|---|---|---|
| C1-A | CFG `0x00000506` | `> 0` | `CLOCK_CORRECTION_CAUSAL = HW_PROVEN` |
| C1-B | CFG `0x00000506` | stays 0 | clock conformed; `PP_FRAME_START_BLOCKER_REMAINS` |
| C1-C | program or readback failed | — | `CLOCK_PROGRAMMING_PATH_FAILED`, no kickoff |
| C1-D | reset, SError, Data Abort, USB loss | — | `STOP`, no second boot |

## 8. Shutdown

Sony does gate this clock after a command-mode frame, as a power-off,
not as a second DVFS point that C1 must copy.

`mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF)` runs from the command-mode
resource off path (`mdss_mdp_intf_cmd.c`). That disables `core_clk`.
When the RCG enable count hits zero, `clk_rcg2_disable` sees
`enable_safe_config`, force-enables the root, calls
`clk_rcg2_configure` with `cxo_f` (source 0, `pre_div` 1, so the hid
field becomes 1), then clears `CMD_ROOT_EN`. That is a real second
rate write back toward CXO.

G2's captured post-frame words did not show that park. After the
swipes, CMD was `0x80000000` (`ROOT_OFF`), CFG stayed `0x00000506`,
and the branch was halted (`0x80005220`). The sample shows the root
and branch gated with the last row retained. It does not show a
completed CXO reprogram. Class of that sample: `HW_READBACK_PROVEN`
for "CFG retained, root off". The CXO park remains `SOURCE_PROVEN` as
what `clk_rcg2_disable` would do if the RCG count reaches zero.

C1 does not imitate either transition.

```text
CHOSEN_SHUTDOWN=keep 171428571 for the bounded run
CXO_PARK=NO
SECOND_RATE_WRITE=NO
ROLLBACK_REQUIRED=NO
```

Reason: the experiment's question is whether this one rate lets
`PP_LINE` move. A following write to CXO would be a second rate change
in the same run. `xzs_d8m6_panel_shutdown` already sends DCS `0x28`,
then DCS `0x10`, then the M5 rail shutdown. It does not write
`0x008c2040` or `0x008c2044`. T1 used that helper after the panel was
ready and reached the shell with the branch left on. C1 uses that same
shutdown and leaves the RCG at the programmed row. Holding the proven
in-frame rate until the user returns the phone to fastboot is the
bounded policy. A fault is C1-D: stop, no second boot, no cleanup write.

## 9. Stop

This commit is documentation. It does not build, boot, or program
`0x008c2044`.
