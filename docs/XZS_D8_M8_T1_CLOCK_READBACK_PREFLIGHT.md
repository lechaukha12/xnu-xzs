# D8-M8 T1 preflight — MDP core RCG / CBCR readback

Source and repository audit only. No build, no boot, no MMIO write, no T1 code.

```text
T1_READY_FOR_HARDWARE=YES
MDP_RCG=mdp_clk_src CMD 0x008c2040 CFG 0x008c2044
MDP_CBCR=mdss_mdp_clk 0x008c231c
VALID_RATE_TABLE=nine hid rows, 85714286 Hz through 412500000 Hz
NEW_MAPPING_REQUIRED=NO
SAFE_INSERTION_POINT=after the m8-fb-init command has printed M8_1 PASS and returned to xzs#, before any later M8 command
R11E_MAP_FAILURE_RELEVANT_TO_T1=NO
NEXT_ACTION=do not implement T1 in this commit
```

## 1. Identity

```text
WORKTREE=/Users/lechaukha12/Desktop/xnu-xzs
ORIGIN=https://github.com/lechaukha12/xnu-xzs.git
BRANCH=xzs-d8-display-m8-resume
HEAD=af5bc63815bf2006e364a30eb188782de95fd7db
WORKTREE=clean
```

Known-good boot: `5600ef88f9d0e17474d87c9f92810424b3ad5e9f` (Retry #11D-B).
#11E device commit: `e869829f7a903619e090a37e0cc00a11292d6cc0`. Status remains `ABORTED_BEFORE_M8_1` / `INCONCLUSIVE`. It proved no clock, VBIF, or fetch state.

Sony pin: `https://github.com/sonyxperiadev/kernel`, `aosp/LA.UM.7.1.r1`, `5772572ccdfbc16c270d33f8fa6b55d33d27709c`. Clock controller nodes and `drivers/clk/qcom/mmcc-msm8996.c` / `clk-rcg2.c` are from that pin. No newer SoC is used.

## 2. What `mdss_mdp_set_clk_rate` programs

`SOURCE_PROVEN`.

`mdss_mdp_set_clk_rate` (`mdss_mdp.c:1256`) takes `mdss_mdp_get_clk(MDSS_CLK_MDP_CORE)` and calls `clk_round_rate` / `clk_set_rate` on it. That index is registered from the DT name `core_clk` (`mdss_mdp.c:1932`).

On this platform `core_clk` is not the RCG itself:

```text
msm8996-mdss.dtsi:114-120
clocks = <&clock_mmss MDSS_AHB_CLK>,
         <&clock_mmss MDSS_AXI_CLK>,
         <&clock_mmss MDP_CLK_SRC>,
         <&clock_mmss_vote MDSS_MDP_VOTE_CLK>,
         <&clock_mmss MDSS_VSYNC_CLK>
clock-names = "iface_clk", "bus_clk", "core_clk_src", "core_clk", "vsync_clk"
```

| Role | Name | Where |
|---|---|---|
| Logical name Sony sets | `core_clk` | `MDSS_CLK_MDP_CORE` |
| Binding | `MDSS_MDP_VOTE_CLK` | voter `mdss_mdp_vote_clk` |
| Branch | `mdss_mdp_clk` | parent `mdp_clk_src`, `CLK_SET_RATE_PARENT` |
| RCG | `mdp_clk_src` | `cmd_rcgr = 0x2040` |
| Parents | BI_TCXO, MMPLL0, MMPLL5, GPLL0, GPLL0_DIV | map cfg 0, 1, 2, 5, 6 |

`set_rate` on the voter follows `CLK_SET_RATE_PARENT` to `mdss_mdp_clk` and then to `mdp_clk_src`. The RCG is an MMCC clock (`qcom,mmsscc@8c0000`), not a GCC clock. GPLL0 is a GCC PLL that can be chosen as a parent. The binding header is `include/dt-bindings/clock/qcom,mmcc-msm8996.h` (`MDP_CLK_SRC`, `MDSS_MDP_CLK`, `MDSS_MDP_VOTE_CLK`).

Probe calls `mdss_mdp_set_clk_rate(mdata->max_mdp_clk_rate)` (`mdss_mdp.c:1954-1955`). This node's `qcom,max-clk-rate` is `412500000` (`msm8996-mdss.dtsi:62`), the top row of the table. `MDSS_QUIRK_MDP_CLK_SET_RATE` is not set for HW rev 107, so a later `mdss_mdp_clk_update` of the core clock does not call `set_rate` again on 8996. The probe call still does.

## 3. Register map

MMCC physical base is `reg = <0x8c0000 0xb00c>` named `cc_base` (`arch/arm64/boot/dts/qcom/msm8996.dtsi`, node `qcom,mmsscc@8c0000`). `0x2040` and `0x2328` are inside that length. XNU's `XZS_MMCC_BASE` is `0x008c0000`.

`mdp_clk_src` sets `hid_width = 5` and does not set `mnd_width` (`mmcc-msm8996.c:903-908`). `clk_rcg2_recalc_rate` reads M and N only when `mnd_width` is non-zero (`clk-rcg2.c:284-296`). M, N, and D are not part of this clock's decode. `cfg_off` is left 0, so CFG is `cmd_rcgr + 4`.

| Register | Physical | Source | Read-safe | Needed for the class |
|---|---:|---|---|---|
| `mdp_clk_src` CMD | `0x008c2040` | `mmcc-msm8996.c` `cmd_rcgr = 0x2040`; `clk-rcg2.c` CMD at +0 | yes, `regmap_read` | yes |
| `mdp_clk_src` CFG | `0x008c2044` | CFG at cmd+4, `clk-rcg2.c` `CFG_REG 0x4` | yes, `regmap_read` | yes |
| M, N, D | not used | `mnd_width` unset | excluded | no |
| `mdss_mdp_clk` CBCR | `0x008c231c` | halt and enable `0x231c`, enable bit 0 | yes | yes, branch state only |
| `mdss_ahb_clk` CBCR | `0x008c2308` | `0x2308`, hwcg bit 1 | yes | context, same page |
| `mdss_axi_clk` CBCR | `0x008c2310` | `0x2310` | yes | context, same page |
| `mdss_vsync_clk` CBCR | `0x008c2328` | `0x2328`, parent `vsync_clk_src` | yes | context, same page |

CMD fields (`clk-rcg2.c:37-41`): bit 0 update, bit 1 root enable, bit 31 `CMD_ROOT_OFF`.
CFG fields (`clk-rcg2.c:45-49`): divider bits `[4:0]` (`hid_width` 5), source bits `[10:8]`, mode bits `[13:12]`.

CBCR (`clk-branch.c`): bit 0 is the enable mask written by the branch ops. Bit 31 is `BRANCH_CLK_OFF`. AHB also has hardware clock-gating on bit 1; that bit is not the off flag. A read of the halt register is how `clk_branch2_check_halt` polls. It does not clear the register.

These reads do not depend on `MDSS_GDSC`. `xzs_diag.c` treats `0x008c0000` as the clock controller, outside the MDSS slave behind the GDSC. M2 and M3 already read this controller before `m8-fb-init`.

## 4. Frequency table

`ftbl_mdp_clk_src` (`mmcc-msm8996.c:890-900`). `F(f, s, h, m, n)` stores `pre_div = 2*h - 1` (`mmcc-msm8996.c:43`). M and N in every row are 0. Mode in a matching row is 0. Parent cfg comes from the map above.

| Declared Hz | Parent | cfg | Divider h | hid | M | N | CFG check |
|---:|---|---:|---:|---:|---:|---:|---|
| 85714286 | GPLL0 | 5 | 7 | 13 | 0 | 0 | src 5, hid 13, mode 0 |
| 100000000 | GPLL0 | 5 | 6 | 11 | 0 | 0 | src 5, hid 11, mode 0 |
| 150000000 | GPLL0 | 5 | 4 | 7 | 0 | 0 | src 5, hid 7, mode 0 |
| 171428571 | GPLL0 | 5 | 3.5 | 6 | 0 | 0 | src 5, hid 6, mode 0 |
| 200000000 | GPLL0 | 5 | 3 | 5 | 0 | 0 | src 5, hid 5, mode 0 |
| 275000000 | MMPLL5 | 2 | 3 | 5 | 0 | 0 | src 2, hid 5, mode 0 |
| 300000000 | GPLL0 | 5 | 2 | 3 | 0 | 0 | src 5, hid 3, mode 0 |
| 330000000 | MMPLL5 | 2 | 2.5 | 4 | 0 | 0 | src 2, hid 4, mode 0 |
| 412500000 | MMPLL5 | 2 | 2 | 3 | 0 | 0 | src 2, hid 3, mode 0 |

XO (cfg 0), MMPLL0 (cfg 1), and GPLL0_DIV (cfg 6) are legal parents of the mux and are not rows of this table. The declared Hz is the table's `F()` frequency. It is not a measurement of the parent PLL. T1 does not read GPLL0 or MMPLL5.

## 5. Decode algorithm

Inputs are the raw CMD word and the raw CFG word. No write.

```text
src  = (CFG >> 8) & 7
hid  = CFG & 0x1f
mode = (CFG >> 12) & 3
root_off = (CMD >> 31) & 1

if CMD or CFG was not read:
    INSUFFICIENT_READBACK
else if root_off:
    ROOT_OFF
else if src not in {0, 1, 2, 5, 6}:
    INVALID_SOURCE
else if mode == 0 and (src, hid) is one row of the table:
    VALID_TABLE_ENTRY
    declared_hz = that row
else:
    NON_TABLE_CONFIGURATION
```

`NON_TABLE_CONFIGURATION` includes XO, MMPLL0, GPLL0_DIV, a known parent with a divider that is not in the table, and a non-zero mode. For XO only, a numeric rate is still defined: `19200000` if `hid == 0`, else `19200000 * 2 / (hid + 1)`, from `clk_rcg2_calc_rate` and the XO frequency in `ftbl_mdss_vsync_clk`. That number does not promote the row into the MDP table.

CBCR is reported beside the class and does not change it:

```text
branch_enabled = (CBCR bit 0 == 1) and (CBCR bit 31 == 0)
```

A table match with the branch halted is still `VALID_TABLE_ENTRY` plus `branch_enabled=no`. U1 is about the source configuration. The branch bit answers a different question and is already known from earlier boots.

```text
T1_RATE_DECODE_DETERMINISTIC=YES
```

for the five classes above. The declared Hz of a `VALID_TABLE_ENTRY` is deterministic. The parent PLL lock is outside this read set.

## 6. Reset and bootloader

`SOURCE_PROVEN` for the software. `UNKNOWN` for the silicon default.

`mdp_clk_src.enable_safe_config` is true. While that RCG is not prepared, `__clk_rcg2_set_rate` stores the requested rate and returns without writing the hardware (`clk-rcg2.c:573-579`). `clk_rcg2_enable` later moves the mux off CXO onto the stashed table row (`clk-rcg2.c:682-694`). XNU's `clocks mdp-on` sets CBCR bit 0 of `0x008c231c` only. It does not call `clk_set_rate` or `clk_rcg2_enable` on `mdp_clk_src`.

So Sony's probe rate call changes the RCG only when the clock framework actually applies a stashed or immediate `set_rate`. XNU leaves whatever the bootloader or reset already had. That can already be a table row. Absence of an XNU rate write is not, by itself, proof that the running configuration is illegal. That is why T1 is a read.

## 7. Mapping

`xzs_mmcc_read32` (`xzs_diag.c:121-130`) loads `XZS_MMCC_BASE + offset` through the existing `g_xzs_ttbr0` identity map. `d8p1_read32` does the same with a full physical address. Neither function calls `ml_io_map` or `ml_io_map_unmappable`.

| Address | In use before M8-1 on the #11D-B path | Same 4 KB page `0x008c2000` |
|---|---|---|
| `0x008c2040` / `0x008c2044` | not printed as its own line | yes. M3 reads `0x008c2000` / `0x008c2004` before `m8-fb-init` |
| `0x008c2308` / `0x008c2310` / `0x008c231c` | M2 `clocks mdss-ahb-on`, `mdss-axi-on`, `mdp-on` | yes |
| `0x008c2328` | VSYNC CBCR is read on the M8 path; the page is already touched by M2/M3 | yes |

No new mapping, no new page, no access inside `xzs_d8m8_fb_init`.

```text
T1_REQUIRES_NEW_MAPPING=NO
T1_REUSES_11DB_MAPPING=YES
```

## 8. Retry #11E and the map

`git diff 5600ef88 e869829 -- src/xnu/pexpert/arm/xzs_d8m8.h` adds reads and one call. The only edit inside `xzs_d8m8_fb_init` is `xzs_d8m8_r11e_capture(0x1100)` after the `M8_1 = PASS` emits and the `0xC1100` breadcrumb. The map call is unchanged:

```text
ml_io_map_unmappable(g_m8_fb_pa, g_m8_fb_size, 0x6)
```

The #11E log stops on the print that precedes that call. The new capture was not executed. The framebuffer map is not required to read MMCC. A T1 that uses `xzs_mmcc_read32` after the shell has returned from `m8-fb-init` does not enter that call in order to perform the read.

```text
R11E_RESET_CAUSE=UNKNOWN
R11E_MAP_FAILURE_RELEVANT_TO_T1=NO
```

The map failure matters only if a later change puts code on that path or replaces `fb_init` before `M8_1 = PASS`.

## 9. `fb_init` boundary and insertion

Through the `M8_1 = PASS` emits, `xzs_d8m8_fb_init` at `e869829` matches `5600ef88`. The extra call is after those emits. Future T1 must not use that call. It reads VBIF and other blocks that are out of scope, and it sits inside `fb_init` before the function returns.

Identical through PASS means the same map arguments, the same fill, the same cache clean, and the same PASS prints, with no T1 read inserted above them. Compile-time declarations with no runtime effect are allowed. The current `g_r11e_samples` BSS is a declaration of that kind; executing `xzs_d8m8_r11e_capture` is not.

Insertion, and the only one that meets the gate:

```text
display m8-fb-init
    prints M8_1 = PASS
    returns to xzs#
        ↓
separate command, xzs_mmcc_read32 only:
    0x2040, 0x2044, 0x231c
    and, same page, 0x2308, 0x2310, 0x2328
        ↓
print the six words
        ↓
stop
```

Do not continue into `m8-rgb0-config`, flush, or `m8-kickoff`. U1 does not need `CTL_START`. Do not put the reads in `ml_io_map_unmappable`, in page-table code, or before the PASS line.

The reads themselves do not require the framebuffer map. The outcome tree below still requires `M8_1 PASS` because that is the requested T1 gate, using the unchanged #11D-B body. A read taken after `clocks mdp-on` and before `m8-fb-init` would also answer U1 and would not touch the map. It is not a substitute for the tree's PASS check. It is the fallback if a future `m8-fb-init` again fails to return: the clock class from that earlier read would still be valid, and the failed init would make the T1 run invalid as a PASS-gated result.

## 10. Outcome tree

```text
M8_1 PASS printed and the shell returned?
 |
 +-- NO
 |     T1 INVALID
 |     no clock conclusion from this run
 |
 +-- YES
       |
       +-- the six reads return
       |     |
       |     +-- VALID_TABLE_ENTRY
       |     |     U1 closed as a configuration
       |     |     declared_hz is the table row
       |     |     parent PLL was not measured
       |     |
       |     +-- ROOT_OFF, INVALID_SOURCE, or NON_TABLE_CONFIGURATION
       |           mismatch is evidence
       |           no rate write in this run
       |           a later one-change task is a separate decision
       |
       +-- a read faults or the machine resets
             T1 READ PATH UNSAFE
             no clock conclusion
```

No row of this tree writes a clock, a branch, or a display register.

## 11. Decisions

```text
T1_RCG_ADDRESSES_SOURCE_PROVEN=YES
T1_CBCR_ADDRESSES_SOURCE_PROVEN=YES
T1_RATE_DECODE_DETERMINISTIC=YES
T1_READS_SAFE=YES
T1_REQUIRES_NEW_MAPPING=NO
T1_REUSES_11DB_MAPPING=YES
T1_CODE_CAN_BEGIN_AFTER_M8_1=YES
R11E_RESET_CAUSE=UNKNOWN
T1_READY_FOR_HARDWARE=YES
```

`T1_READY_FOR_HARDWARE=YES` means the six gates in the request are satisfied by this register set and this insertion point. It does not authorize implementation in this commit. The current tree still contains the #11E capture after the PASS line. A future T1 must not call it.
