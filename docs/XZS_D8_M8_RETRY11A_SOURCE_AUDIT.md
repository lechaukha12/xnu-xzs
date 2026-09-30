# D8-M8 Retry #11A — PP0 to DSI command-mode source audit

## Scope and identity

- XNU baseline: `bfd7539c4ba5ee12d97b30f6f27c4e2dd4d92eb4` (`xzs-d8m8-deferred`)
- Sony downstream repository: `https://github.com/sonyxperiadev/kernel.git`
- Sony branch: `aosp/LA.UM.7.1.r1`
- Audited Sony commit: `5772572ccdfbc16c270d33f8fa6b55d33d27709c`
- Target hardware readback: `DSI_HW_VERSION=0x10040001`
- Matching Sony definition: `MDSS_DSI_HW_REV_104_1=0x10040001` (`mdss_dsi.h:61-63`, MSM8996)

Evidence labels in this document are deliberately restricted to `SOURCE_PROVEN`,
`CALCULATED`, and `UNKNOWN`.  Hardware conclusions are deferred to the single
Retry #11A run.

## Exact kickoff ordering

The actual downstream ordering is:

1. `mdss_mdp_display_commit()` writes CTL flush, executes `wmb()`, and then calls
   the interface display function (`mdss_mdp_ctl.c:6102-6118,6130-6134`).
2. That function is `mdss_mdp_cmd_kickoff()` (`mdss_mdp_intf_cmd.c:3040`).
3. It raises `MDSS_EVENT_DSI_CMDLIST_KOFF` (`:3146-3147`).
4. The DSI event handler calls `mdss_dsi_cmdlist_commit(ctrl, 1)`
   (`mdss_dsi.c:2770-2792,2866-2868`).
5. `mdss_dsi_cmdlist_commit()` serializes against earlier DSI work, optionally
   transmits any queued DCS request, then calls `mdss_dsi_cmd_mdp_start()` for a
   real ROI (`mdss_dsi_host.c:2724-2760,2802-2803,2869-2886`).
6. `mdss_dsi_cmd_mdp_start()` enables the `DSI_MDP_TERM` interrupt route, sets
   software `ctrl->mdp_busy=true`, and reinitializes `mdp_comp`; it performs no
   MMIO trigger write (`mdss_dsi_host.c:2548-2558`).
7. Back in `mdss_mdp_cmd_kickoff()`, the PP completion callback and PP interrupt
   are installed/enabled (`mdss_mdp_intf_cmd.c:3159-3167`).
8. `__mdss_mdp_kickoff()` writes `MDSS_MDP_REG_CTL_START=1` on the ordinary
   non-autorefresh path (`mdss_mdp_intf_cmd.c:2930-2991`, especially `:2984-2986`).

Therefore the source-proven sequence is:

```text
CTL_FLUSH -> DSI software MDP-preparation -> PP IRQ setup -> CTL_START
```

The Linux-only DSI preparation state has no direct equivalent in the current
minimal XNU shell: no Linux completion object, DSI ISR, or `ctrl->mdp_busy`
software variable exists. Retry #11A consequently brackets that position with
passive snapshots and does not invent a register write.

## Function trace

| File:line | Function | Caller → callee | Important effects | Confidence |
|---|---|---|---|---|
| `mdss_mdp_ctl.c:5820,6102-6134` | `mdss_mdp_display_commit` | atomic commit → `ctl->ops.display_fnc` | Writes CTL_FLUSH, barrier, invokes DSI0 display function | SOURCE_PROVEN |
| `mdss_mdp_intf_cmd.c:3040,3146-3185` | `mdss_mdp_cmd_kickoff` | display function → event/PP setup/kickoff | Emits DSI command-list event, then enables PP completion IRQ, then calls `__mdss_mdp_kickoff` | SOURCE_PROVEN |
| `mdss_dsi.c:2770,2866-2868` | `mdss_dsi_event_handler` | interface event → `mdss_dsi_cmdlist_commit(ctrl,1)` | Maps the kickoff event to the DSI from-MDP path | SOURCE_PROVEN |
| `mdss_dsi_host.c:2724-2895` | `mdss_dsi_cmdlist_commit` | event handler → `mdss_dsi_cmd_mdp_start` | Waits for prior DSI MDP activity; DCS request is optional; arms MDP state for a real ROI | SOURCE_PROVEN |
| `mdss_dsi_host.c:2548-2558` | `mdss_dsi_cmd_mdp_start` | command-list commit | Enables `DSI_MDP_TERM`, sets `ctrl->mdp_busy`, reinitializes completion; no MMIO trigger | SOURCE_PROVEN |
| `mdss_mdp_intf_cmd.c:2930-2991` | `__mdss_mdp_kickoff` | command kickoff | Writes CTL_START=1 unless autorefresh owns kickoff | SOURCE_PROVEN |

## Trigger audit

Sony constructs DSI_TRIG_CTRL at `mdss_dsi_host.c:420-427`:

```text
bit 31       = te_sel
bits 6:4     = mdp_trigger
bit 8        = stream & 1
bits 2:0     = dma_trigger
```

Trigger constants are defined at `mdss_dsi.h:223-228`: NONE=0, TE=2, SW=4,
SW_SEOF=5 (DMA only), SW_TE=6, over-range=7. The DT parser defaults a trigger to
SW and explicitly maps `"none"` to NONE (`mdss_dsi_panel.c:1115-1131`); MDP and
DMA trigger properties are parsed separately at `:2937-2941`.

For Retry #10's raw value `0x80000004`:

| Field | Raw | Meaning | Confidence |
|---|---:|---|---|
| `te_sel` | 1 | TE selection flag asserted | SOURCE_PROVEN |
| stream | 0 | command stream 0 | SOURCE_PROVEN |
| MDP trigger | 0 | `DSI_CMD_TRIGGER_NONE` | SOURCE_PROVEN |
| DMA trigger | 4 | `DSI_CMD_TRIGGER_SW` | SOURCE_PROVEN |

The audited normal MDP frame path never writes a DSI MDP SW-trigger register.
It prepares DSI software state and writes CTL_START. Thus an extra DSI MDP
SW-trigger is **not required by this downstream normal command-mode path**
(`SOURCE_PROVEN`). The DMA DCS path is distinct and explicitly writes `+0x090`
at `mdss_dsi_host.c:2244-2254`.

### MDP SW-trigger offset boundary

The v1.4 Sony host implementation does not define or reference a named
`DSI_CMD_MODE_MDP_SW_TRIGGER` offset. It proves DMA trigger `+0x090` and BTA
trigger `+0x098` (`mdss_dsi_host.c:1121-1128,2252-2254`). The only named MDP
SW-trigger definition in this tree is in the different v2 register header,
where offsets are shifted (`dsi_host_v2.h:63-65`); it is not applicable to the
observed v1.4 hardware. The old XNU dictionary's `+0x094` annotation therefore
remains `UNKNOWN`, not source proof. Retry #11A neither reads nor writes it.

## v1.4 register and interrupt audit

DSI0 base is `0x00994000`; offsets below are for the audited legacy v1.4 host.

| Register | Offset | Bit/field or use | Meaning/source | Confidence |
|---|---:|---|---|---|
| HW_VERSION | `0x000` | full word | observed `0x10040001`; `mdss_dsi.h:61-63` | SOURCE_PROVEN |
| DSI_CTRL | `0x004` | command-mode control | read/modify/write at `mdss_dsi_host.c:1076-1103` | SOURCE_PROVEN |
| DSI_STATUS | `0x008` | bit 2 | hardware MDP busy; explicit condition/comment at `mdss_dsi_host.c:2711-2717` | SOURCE_PROVEN |
| COMMAND_MODE_DMA_CTRL | `0x03c` | full word | configured at `mdss_dsi_host.c:1096-1103` | SOURCE_PROVEN |
| COMMAND_MODE_MDP_CTRL | `0x040` | format/lane fields | configured at `mdss_dsi_host.c:379-390` | SOURCE_PROVEN |
| DCS_CMD_CTRL | `0x044` | start/continue command | configured at `mdss_dsi_host.c:392-398` | SOURCE_PROVEN |
| STREAM0_CTRL | `0x058` | bytes/VC/DCS-long-write | `mdss_dsi.c:2565-2574` | SOURCE_PROVEN |
| STREAM0_TOTAL | `0x05c` | height:width | `mdss_dsi.c:2565-2574` | SOURCE_PROVEN |
| ACK_ERR_STATUS | `0x068` | status | read is passive; writing status then zero clears (`mdss_dsi_host.c:3079-3110`) | SOURCE_PROVEN |
| DSI_TRIG_CTRL | `0x084` | fields above | `mdss_dsi_host.c:420-427` | SOURCE_PROVEN |
| CMD DMA SW trigger | `0x090` | write 1 | DCS/DMA path only, `mdss_dsi_host.c:2252-2254` | SOURCE_PROVEN |
| CMD MDP SW trigger | `UNKNOWN` | — | absent from applicable v1.4 definitions/path | UNKNOWN |
| BTA SW trigger | `0x098` | write 1 | `mdss_dsi_host.c:1121-1128` | SOURCE_PROVEN |
| TIMEOUT_STATUS | `0x0c0` | status | read is passive; write clears (`mdss_dsi_host.c:3113-3129`) | SOURCE_PROVEN |
| DSI_INT_CTRL | `0x110` | bit 8 raw, bit 9 mask | CMD_MDP_DONE raw/mask (`mdss_dsi.h:207-208`) | SOURCE_PROVEN |

`DSI_INT_CTRL` combines raw status and masks. Command-mode setup includes the
CMD_MDP_DONE mask (`mdss_dsi_host.c:1083-1103`). The ISR reads `+0x110`, writes
status back to acknowledge it, and handles CMD_MDP_DONE by disabling the route,
clearing software busy, and completing `mdp_comp`
(`mdss_dsi_host.c:3296-3311,3361-3374`). A passive read does not acknowledge the
raw bit; Retry #11A therefore records raw bit 8 and mask bit 9 separately.

## Instrumentation safety result

- New R11A observations are reads only.
- No read is performed from a trigger register.
- No write is added to DSI_CTRL, DSI_TRIG_CTRL, DSI_INT_CTRL, ACK_ERR_STATUS,
  TIMEOUT_STATUS, or any SW-trigger register.
- Existing PP timing values, CTL_TOP, CTL_FLUSH, and the single CTL_START write
  are unchanged.
- The existing 100 ms completion timeout remains bounded.

