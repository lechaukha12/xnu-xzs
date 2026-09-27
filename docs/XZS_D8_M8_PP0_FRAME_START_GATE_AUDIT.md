# D8-M8 PP0 command frame-start gate audit

Offline audit. No build, boot, flash, or display-register change.

## A. Repository identity

```text
WORKTREE=/Users/lechaukha12/Desktop/xnu-xzs
PROJECT_GIT_ORIGIN=https://github.com/lechaukha12/xnu-xzs.git
BRANCH=xzs-d8-display-m8-resume
HEAD_AT_AUDIT_START=bb3f1176db443c82780bb195c2fa11236a79aecd
WORKTREE_CLEAN=yes
```

Remote: `origin` fetch and push `https://github.com/lechaukha12/xnu-xzs.git`.

Retry #11D-B booted `5600ef88f9d0e17474d87c9f92810424b3ad5e9f`. Its evidence commit is the HEAD above.

## B. Sony source identity

```text
URL=https://github.com/sonyxperiadev/kernel
BRANCH=aosp/LA.UM.7.1.r1
PINNED_COMMIT=5772572ccdfbc16c270d33f8fa6b55d33d27709c
TREE=drivers/video/fbdev/msm/
```

No other tree is used for a conclusion.

## C. Retry #11D-B hardware baseline

`R11D-B2 — AUTOREFRESH_DISABLED_PP_OUTPUT_STILL_NOT_STARTED`. One RAM boot, one `CTL_START`, shutdown PASS, `xzs#` returned.

`HW_READBACK_PROVEN`: `PP_AUTOREFRESH_CONFIG=0x00000000` before and after `CTL_START`, bit 31 clear. Counter directly sampled `0x780` and `0x785`, then wrapped. `CTL_FLUSH` `0x00020048` → `0`. RGB0 current source `0` → `0x98000000`. `PP_LINE=0`, `PP_OUT=0`, `PP0_DONE` bit 8 = 0, autorefresh-done bit 20 = 0. Stored DSI busy and raw `CMD_MDP_DONE` stayed 0. No ACK error, no timeout.

## D. `PP_SYNC_CONFIG_VSYNC` bit semantics

Register: `MDSS_MDP_REG_PP_SYNC_CONFIG_VSYNC`, PP+`0x004` (`mdss_mdp_hwio.h:710`).

Construction in `mdss_mdp_cmd_tearcheck_cfg` (`mdss_mdp_intf_cmd.c:306-319`):

```text
cfg = BIT(19)
if (hw_vsync_mode) cfg |= BIT(20)
cfg |= vclks_line
```

`vclks_line` is `vsync_clk / (vtotal * frame_rate)`, then multiplied by `frame_rate * 100 / refx100`. Missing `qcom,mdss-tear-check-frame-rate` makes `refx100` default to 6000 (`mdss_dsi_panel.c:1764-1765`). Keyaki's sharp command node does not set that property. At a 19.2 MHz vsync clock, vtotal 2163 and 60 Hz, that formula is 147 (`0x93`). That clock rate is `CALCULATED` from the numeric match, not a rate printed by this DTS. XNU writes `0x00080093`, which is bit 19 plus `0x93` and bit 20 clear.

What the pinned comments actually say about the bits:

| Bit | What the C source states | Label |
|---|---|---|
| 19 | Always set by `tearcheck_cfg`. No comment names the bit. The low 19 bits are the vclks-per-line field because they are ORed in after the bit is set. | Field construction `SOURCE_PROVEN`. The sentence "bit 19 enables the internal counter" is `UNKNOWN` in this tree. |
| 20 | Cleared by `__disable_rd_ptr_from_te` and by the autorefresh-disable sequence. The comment at `mdss_mdp_intf_cmd.c:2860-2869` says clearing it instructs MDP to ignore the panel TE, and the hardware frame count then advances on internal-counter wrap instead of on each external TE. Setting it again is commented "enable MDP to listen to the TE" (`2892-2897`). | `SOURCE_PROVEN` for TE listen versus internal-counter wrap. |
| 20 as a one-shot pixel gate | `__mdss_mdp_kickoff` does not read bit 20 before `CTL_START`. The "transfer whenever external TE is received" sentence is inside the autorefresh-enabled disable procedure, not the one-shot branch. | Whether bit 20 also releases a one-shot command frame is `UNKNOWN`. |

`hw_vsync_mode=false` does not add another PP register. `tearcheck_cfg` simply omits bit 20. `mdss_panel_override_te_params` (`mdss_panel.c:535-544`) changes height, init, start, and read-pointer to `vtotal`, `yres`, `yres`, and `yres+1`. It does not write hardware itself.

Can internal timing alone release a command frame on MSM8996? The driver will still execute the one-shot `CTL_START` branch with bit 20 clear. The source does not say the hardware then emits pixels. Retry #11D-B is the hardware result for the current programming: the counter crossed `0x780` and `0x785` and `PP_LINE` stayed 0. That is `HW_READBACK_PROVEN` for this image, not a source sentence that internal mode can never release a frame.

## E. `hw_vsync_mode` trace

| Function | file:line | Condition | Register / state effect | Meaning |
|---|---|---|---|---|
| `mdss_panel_parse_dt` | `mdss_dsi_panel.c:2814-2821` | `sim_panel_mode == SIM_SW_TE_MODE` | `hw_vsync_mode = false` | Comment: simulator command panels generate SW TE (`mdss_panel.h:134-136`) |
| same | `2819-2821` | otherwise | `hw_vsync_mode =` presence of `qcom,mdss-dsi-te-using-te-pin` | Keyaki sharp node has the property (`keyaki.dts:1781`), so unmodified parse sets true |
| `mdss_dsi_set_override_cfg` | `mdss_dsi.c:2972-2973` | boot override token `sim-swte` | `sim_panel_mode = SIM_SW_TE_MODE` | Not a DTS default |
| `mdss_panel_override_te_params` | `mdss_panel.c:535-544` | called for SW TE | software TE geometry only | height = vtotal, init = start = yres, rd = yres+1 |
| `mdss_mdp_cmd_tearcheck_cfg` | `intf_cmd.c:306-308` | `hw_vsync_mode` true | OR bit 20 into `PP_SYNC_CONFIG_VSYNC` | external TE selected in the sync word |
| same | `306` | always | OR bit 19 and `vclks_line` | no separate false-mode register |
| `mdss_mdp_cmd_panel_disable_cfg` | `intf_cmd.c:2395-2441` | sysfs disable, and not already sim with `hw_vsync_mode==0` | sets `hw_vsync_mode=0`, calls the TE override, then `tearcheck_setup` | comment: "remove dependency to external te" and "use sim mode" |
| `mdss_mdp_cmd_set_autorefresh_mode` | `intf_cmd.c:2487-2489` | `hw_vsync_mode` false | returns `-ENODEV` | autorefresh is refused without hardware vsync. One-shot `CTL_START` is a different function and does not make this check |
| `__mdss_mdp_kickoff` | `intf_cmd.c:2942-2985` | autorefresh state, not `hw_vsync_mode` | either autorefresh config or `CTL_START=1` | internal versus external TE does not change this branch |

`hw_vsync_mode=false` is a real driver path: simulator SW TE and the sysfs "remove external TE" reconfiguration. It is not the Keyaki DTS path. The one-shot kickoff is not refused. Autorefresh is refused. No production MSM8996 command panel in the Keyaki DTS was found with the TE-pin property absent. The same-generation example of internal timing is the `sim-swte` override plus `mdss_panel_override_te_params`, not a phone panel DTS.

## F. External versus internal comparison

Keyaki literal parse, properties absent so defaults apply (`mdss_dsi_panel.c:1750-1772`): `tear_check_en` true because `qcom,mdss-dsi-te-check-enable` is present; height default `0xfff0`; init and start default `yres` `0x780`; thresholds default 4 and 4; `rd_ptr` default `0x781`; `wr_ptr` forced 0; bit 20 set.

Sony SW-TE reconfiguration (`mdss_panel_override_te_params` plus `hw_vsync_mode=0`): height becomes vtotal `0x873`; init/start/rd stay `0x780` / `0x780` / `0x781`; bit 20 clear; bit 19 still set.

| Item | Keyaki external TE | Sony `hw_vsync_mode=false` path | XNU current | Status |
|---|---|---|---|---|
| `PP_SYNC_CONFIG_VSYNC` bit 20 | 1 | 0 | 0 in `0x00080093` | `INTENTIONAL_DIVERGENCE` from Keyaki; match to the false path |
| bit 19 and vclks field | set, formula | set, same formula | `0x93` with bit 19 | `MATCH` to the formula |
| `PP_SYNC_CONFIG_HEIGHT` | default `0xfff0` | vtotal `0x873` | `0x873` | `INTENTIONAL_DIVERGENCE` from literal Keyaki; match to SW-TE override |
| `PP_VSYNC_INIT_VAL` | `0x780` | `0x780` | `0x780` | `MATCH` |
| `PP_START_POS` | `0x780` | `0x780` | `0x780` | `MATCH` |
| `PP_RD_PTR_IRQ` | `0x781` | `0x781` | `0x781` | `MATCH` |
| `PP_WR_PTR_IRQ` | 0 | 0 | 0 | `MATCH` |
| `PP_SYNC_THRESH` | `0x00040004` | unchanged 4/4 | `0x00040004` | `MATCH` |
| `PP_SYNC_WRCOUNT` | `0x785` | same formula | `0x785` | `MATCH` |
| `PP_TEAR_CHECK_EN` | 1 when enabled | still enabled by `tearcheck_enable` | 1 | `MATCH` |
| `PP_AUTOREFRESH_CONFIG` | not written on one-shot; disable value 0 | autorefresh refused when `hw_vsync_mode` is false | not written; silicon read `0` | `MATCH` to the disabled value, `HW_READBACK_PROVEN` |

## G. Sony PP register writes on the normal command path

Applicable to a non-DSC, non-FBC, non-split command panel. DSC and FBC writes exist in `mdss_mdp_ctl.c` (`PP_DSC_MODE`, `PP_DCE_DATA_OUT_SWAP`, `PP_FBC_*`) and are `NOT_APPLICABLE`: the Keyaki sharp node has no compression-mode property.

| Register | PP offset | Value | Function | file:line | Purpose |
|---|---:|---|---|---|---|
| `PP_SYNC_CONFIG_VSYNC` | `0x004` | bit 19, optional bit 20, vclks | `mdss_mdp_cmd_tearcheck_cfg` | `intf_cmd.c:334-335` | tearcheck clock and TE source |
| `PP_SYNC_CONFIG_HEIGHT` | `0x008` | `sync_cfg_height` | same | `336-338` | counter height |
| `PP_VSYNC_INIT_VAL` | `0x010` | `vsync_init_val` | same | `339-341` | init line |
| `PP_RD_PTR_IRQ` | `0x020` | `rd_ptr_irq` | same | `342-344` | read-pointer interrupt line |
| `PP_WR_PTR_IRQ` | `0x024` | `wr_ptr_irq` | same | `345-347` | write-pointer interrupt line |
| `PP_START_POS` | `0x01C` | `start_pos` | same | `348-350` | start position |
| `PP_SYNC_THRESH` | `0x018` | continue<<16 \| start | same | `351-354` | start and continue thresholds |
| `PP_SYNC_WRCOUNT` | `0x00C` | start + start_thresh + 1 | same | `355-357` | write count |
| `PP_TEAR_CHECK_EN` | `0x000` | `tear_check_en && enable` | `mdss_mdp_tearcheck_enable` | `233-235` | tearcheck block enable |
| `PP_SYNC_CONFIG_VSYNC` bit 20 | `0x004` | clear or set | `__disable_rd_ptr_from_te` / `__enable_rd_ptr_from_te` | `443-467` | ignore or listen to external TE |
| `PP_AUTOREFRESH_CONFIG` | `0x030` | `BIT(31)\|frame_cnt` or `0` | `__mdss_mdp_kickoff` / `__disable_autorefresh` | `2948-2951`, `488-491` | autorefresh kickoff, not the one-shot branch |

`PP_INT_COUNT_VAL` (`0x014`), `PP_LINE_COUNT` (`0x02C`), and `PP_OUT_LINE_COUNT` (`0x028`) are read, not written, on this path.

`mdss_mdp_cmd_lineptr_ctrl` can rewrite `PP_WR_PTR_IRQ` (`1573-1576`). With `wr_ptr_irq==0` it removes the handler instead of programming a new line. Keyaki's parser forces 0, so the initial write of 0 remains.

No PP register in `mdss_mdp_hwio.h` is named enable, command mode, frame arm, or kickoff enable besides `PP_TEAR_CHECK_EN` and `PP_AUTOREFRESH_CONFIG`.

## H. Sony versus XNU PP writes

XNU programs these in `xzs_d8m8_stream_config` (`xzs_d8m8.h:1275-1292`).

| Sony write | file:line | Offset | Purpose | XNU | Result |
|---|---|---:|---|---|---|
| `PP_TEAR_CHECK_EN=1` | `intf_cmd.c:233-235` | `0x000` | enable tearcheck | `0x00971000=1` | `MATCH` |
| sync word with bit 19 and vclks | `306-335` | `0x004` | clock field | `0x00080093` | `MATCH` to the false-mode word |
| sync word bit 20 | `307-308` | `0x004` | external TE | left clear | `INTENTIONAL_DIVERGENCE` from Keyaki |
| height | `336-338` | `0x008` | counter height | `0x873` | `INTENTIONAL_DIVERGENCE` from Keyaki default `0xfff0`; match to SW-TE override |
| wrcount | `355-357` | `0x00C` | start+thresh+1 | `0x785` | `MATCH` |
| init | `339-341` | `0x010` | init line | `0x780` | `MATCH` |
| thresh | `351-354` | `0x018` | 4/4 | `0x00040004` | `MATCH` |
| start | `348-350` | `0x01C` | start line | `0x780` | `MATCH` |
| rd ptr irq | `342-344` | `0x020` | irq line | `0x781` | `MATCH` |
| wr ptr irq | `345-347` | `0x024` | irq line | `0` | `MATCH` |
| autorefresh enable | `2948-2951` | `0x030` | not on one-shot | not written; read `0` | `NOT_APPLICABLE` as a one-shot write; value `MATCH` |
| DSC / FBC PP writes | `mdss_mdp_ctl.c` | `0x034+`, `0x0A0+` | compression | not written | `NOT_APPLICABLE` |

No `MISSING_IN_XNU` row.

## I. CTL0 → LM0 → PP0 → INTF1 → DSI0

`SOURCE_PROVEN` from the pinned MSM8996 DT and the ctl/mixer code. There is no separate PP-select register write.

1. `qcom,mdss_mdp@900000` reg `mdp_phys` is `0x00900000` (`msm8996-mdss.dtsi:16-19`). `mdss_io.base` is that resource. `mdp_base` adds `qcom,mdss-mdp-reg-offset` `0x1000` (`mdss_mdp.c:3376-3383`), so MDP registers such as `DISP_INTF_SEL` live at `0x00901000`.
2. Ping-pong bases are `mdss_io.base + qcom,mdss-pingpong-off[i]` (`mdss_mdp_ctl.c:5267-5270`). Index 0 is `0x00071000` (`msm8996-mdss.dtsi:98-99`), so PP0 is `0x00971000`. Mixer index 0 is `0x00045000`, absolute `0x00945000` (LM0). CTL index 0 is `0x00002000`, absolute `0x00902000`.
3. DSI controller index 0 sets `pdest = DISPLAY_1` (`mdss_dsi.c:3460-3461`).
4. A command panel with `DISPLAY_1` and `mixer_swap` false gets `ctl->intf_num = MDSS_MDP_INTF1` (`mdss_mdp_ctl.c:4025-4028`). Non-writeback ctl allocation starts at `MDSS_MDP_CTL0` (`3979-3982`).
5. `CTL_TOP` ORs `intf_num << 4` (`4054`). `MDSS_MDP_INTF1` is 2 (`mdss.h:218-222`), so the field is `0x20`, plus command mode bit 17, giving `0x00020020`.
6. `DISP_INTF_SEL` ORs `MDSS_INTF_DSI` (value 1, `mdss_mdp_hwio.h:88`) shifted by `(intf_num - MDSS_MDP_INTF0) * 8` (`mdss_mdp_ctl.c:4408-4415`). That shift is 8, so the byte is `0x100`.
7. `qcom,mdss-has-wb-ad` is absent, so `has_wb_ad` is false (`mdss_mdp.c:4645-4646`) and the first interface mixer allocated is mixer 0, whose `pingpong_base` is ping-pong offset 0.

PP0 is used because it is the ping-pong paired by DT index with LM0, and the primary command panel takes that first mixer. INTF1 is chosen from `DISPLAY_1`, not from the ping-pong index. XNU's bases match this pairing. No unread routing register was found between LM0 and PP0.

## J. `PP_LINE_COUNT`

Offset PP+`0x02C` (`mdss_mdp_hwio.h:720`). The driver masks `0xffff`.

Uses:

- `mdss_mdp_cmd_wait4_autorefresh_pp` (`intf_cmd.c:399-409`): if `0 < line < roi.h`, a transaction is treated as ongoing and the driver waits for ping-pong done.
- `mdss_mdp_cmd_wait4_autorefresh_done` (`2798-2809`): comment "wait until the first line is out to make sure transfer is on-going", polling `(val & 0xffff) >= 1`. Timeout text is "timed out waiting for line out".

So the source's meaning of a non-zero value is "a line has come out / a transfer is ongoing." It is not described as the tearcheck timebase. That timebase is `PP_INT_COUNT_VAL`, which `mdss_mdp_cmd_line_count` reads separately (`165-188`).

A kickoff is issued while this register can still be 0. The driver then waits for it to become non-zero. Staying 0 means that "first line out" check did not succeed. It does not name an earlier request strobe. `PP_LINE=0` through the directly sampled `0x780` / `0x785` window and the wrap is `HW_READBACK_PROVEN` evidence that this line-out count did not advance.

## K. `PP_OUT_LINE_COUNT`

Offset PP+`0x028` (`mdss_mdp_hwio.h:719`). No read or write was found in `mdss_mdp_intf_cmd.c`, `mdss_mdp_ctl.c`, or the MDP ISR in `mdss_mdp.c`. Field width, increment, reset, and relation to DSI are `UNKNOWN`. The R11D-B value 0 is `HW_READBACK_PROVEN` and has no source-backed interpretation beyond that.

## L. `PP0_DONE`

`MDSS_MDP_INTR_PING_PONG_0_DONE` is bit 8 (`mdss_mdp_hwio.h:96`), mapped as `MDSS_MDP_IRQ_TYPE_PING_PONG_COMP` for ping-pong 0 (`mdss_mdp.c:211`).

`mdss_mdp_cmd_kickoff` enables that interrupt before `CTL_START` (`intf_cmd.c:3159-3162`). The handler `mdss_mdp_cmd_pingpong_done` completes the kickoff count. The resource-event comment calls `MDP_RSRC_CTL_EVENT_PP_DONE` the end of the data transfer (`intf_cmd.c:572-577`). The autorefresh-disable path waits for the same interrupt when `PP_LINE_COUNT` is already in progress.

The source does not say the interrupt is DSI `CMD_MDP_DONE`, and it does not say the interrupt fires at the moment a request is issued. It is the completion event. A transfer that has not finished also leaves the bit clear. Absence therefore proves completion did not latch. It does not, by itself, prove the ping-pong never started. Combined with `PP_LINE_COUNT` staying 0, the driver's in-progress test and its completion interrupt were both absent.

## M. Missing-state findings

The normal one-shot command path writes the tearcheck block, enables it, and writes `CTL_START`. The `hw_vsync_mode=false` variant changes the sync word and, when the SW-TE override runs, the height. It does not write an extra "frame arm" register. XNU's PP image matches that variant. The gap between "counter reached `START_POS` / `SYNC_WRCOUNT`" and "line out" is not represented by another software-visible PP register in this tree.

Retry #10 in XNU is the write of `0x00080093` and height `0x873` in `xzs_d8m8_stream_config` (`xzs_d8m8.h:1269-1292`). The comment calls it the internal-VSYNC override. Relative to literal Keyaki it clears bit 20 and replaces default height `0xfff0` with vtotal. Relative to `mdss_panel_override_te_params` plus `hw_vsync_mode=0`, those are the same two changes. No further internal-TE branch was found in kickoff order, `CTL_START`, autorefresh, or PP enable.

## N. Decisions

1. Is XNU's internal-TE programming a complete source-supported MSM8996 command-mode configuration? `UNKNOWN` as a sufficient frame-start configuration. The PP writes match the source's `hw_vsync_mode=false` / SW-TE tearcheck image, and that image contains no further PP write. The source does not say this image releases pixels, and Retry #11D-B shows `PP_LINE` stayed 0. It is not the literal Keyaki image, which sets bit 20 and leaves height at the default `0xfff0`.

2. What does bit 20 do? `SOURCE_PROVEN`: it selects whether MDP listens to the external panel TE. Clear means ignore that TE and advance the hardware frame count on internal-counter wrap. Set means listen to the TE. Whether that bit also gates one-shot pixel release is `UNKNOWN`. The one-shot kickoff does not test it.

3. Is any normal Sony PP0 command-mode register write missing from XNU? `NO` for the non-DSC, non-FBC, one-shot path. The differences are the intentional bit 20 and height choices above, not an omitted write.

4. How is PP0 selected? DSI0 sets `DISPLAY_1`. Command `DISPLAY_1` uses CTL0 and INTF1. With `qcom,mdss-has-wb-ad` absent, the first interface mixer is LM0, and DT index 0 pairs LM0 with ping-pong offset `0x71000` (`0x00971000`). `SOURCE_PROVEN`. No extra PP routing write.

5. What does `PP_LINE=0` prove? The count Sony polls as "first line is out / transfer ongoing" did not advance. `SOURCE_PROVEN` for that meaning. It does not name a request strobe.

6. What does `PP_OUT=0` prove? The register was read as 0. Its meaning is `UNKNOWN`.

7. Does absence of `PP0_DONE` prove PP never started? `NO — SOURCE_PROVEN`. The interrupt is the end of the data transfer. A transfer that has not completed also leaves it clear. With `PP_LINE` also 0, both the in-progress check and the completion interrupt were absent.

8. Narrowest root-cause candidate: the tearcheck programming matches the source's internal/SW-TE image, the counter reaches the programmed start and write-count, and no further PP register in this source turns that timing into line-out. The remaining gate is inside the ping-pong hardware, not a missing named write.

9. Is there one source-proven correction worth testing next? `NO`. Setting bit 20 would return to the external-TE Keyaki image. This source does not show that the one-shot path requires it, and a physical TE was not observed. No other omitted PP write was found.

## O. Smallest next experiment

No register write is proposed.

A further passive boot is not required to identify a missing PP write; this audit did not find one. If another observation is authorized later, the only new pair worth storing in the same sample as `PP_INT_COUNT_VAL==0x780` is the already-programmed `PP_SYNC_CONFIG_VSYNC` and `PP_TEAR_CHECK_EN`, to show the running image still has bit 20 clear and tearcheck enabled at the gate. `PP_LINE`, `PP_OUT`, `PP0_DONE`, and `DSI_STATUS` bit 2 stay in that tuple. Do not set bit 20, do not change height, and do not write `PP_AUTOREFRESH_CONFIG`.

Audit stops.
