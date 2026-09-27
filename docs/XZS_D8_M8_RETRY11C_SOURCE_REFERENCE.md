# D8-M8 Retry 11C — source reference and safety boundary

Baseline: `bc43fd4bc3c62e960e438bd88ec5c54bcc819b32` on
`xzs-d8-display-m8-resume`. Retry 11A's Sony downstream source audit in
`docs/XZS_D8_M8_RETRY11A_SOURCE_AUDIT.md` remains the authority for the
normal-path sequence and register bit meanings.

## Frozen command-mode model

Source-proven downstream order is CTL_FLUSH, DSI software MDP preparation,
PP IRQ preparation, CTL_START. This minimal XNU port has no Linux completion
object or software `mdp_busy` to mutate; its preparation checkpoint is a
passive barrier and **does not write a DSI trigger**. `DSI_TRIG_CTRL` remains
`0x80000004` (TE select 1, stream 0, MDP trigger NONE, DMA trigger SW).
No PP timing, framebuffer geometry, panel initialization, DSI stream value,
or PLL/PHY setting changes in 11C.

The recommended checkpoint names in the task listed DSI preparation before
CTL_FLUSH; the established audited order is the reverse. The actual execution
order is therefore:

1. `C1100`: M8-1 complete, after its existing R11B breadcrumbs.
2. `C1130` / `C1140`: before and after the existing CTL_FLUSH write in M8-6.
3. `C1110` / `C1120`: before and after the passive DSI-preparation barrier in
   M8-7 (no DSI register write).
4. `C1150`: last pre-CTL_START guard/snapshot.
5. `C1160`: first in-memory snapshot immediately after the sole CTL_START
   write, followed by `C1170` +50 us, `C1180` +250 us, `C1190` +1 ms,
   `C11A0` +5 ms, `C11B0` +10 ms, `C11C0` +20 ms, and `C11D0` final passive
   readback. Actual timestamps, not nominal delay names, govern conclusions.
6. The existing graceful M6/M5 panel OFF path runs once after snapshots and
   summary. No second frame or SW trigger is issued.

Only phase boundaries C1100/C1130/C1140/C1110/C1120/C1150/C11C0/C11E0
call the existing persistent breadcrumb writer. Calling it at +50 us would
perturb the timing; the timed samples are instead buffered in memory and
emitted after the window.

## Critical read-only signals

The buffered snapshots read DSI_STATUS (`0x00994008`, hardware MDP busy bit 2),
DSI_INT_CTRL (`0x00994110`, raw CMD_MDP_DONE bit 8 and mask bit 9), DSI_CTRL,
DSI_TRIG_CTRL, DSI_CMD_MDP_CTRL, DSI_CMD_DMA_CTRL, DSI_STREAM0_CTRL/TOTAL,
CTL_FLUSH/START, PP0 interrupt/line/out-line counts, RGB0 current source,
and DSI ACK/TIMEOUT status. The two critical DSI reads are first after each
snapshot timestamp. Status and error bits are not cleared before snapshots.

The experiment has a 20 ms sparse observation window (0/50/250 us,
1/5/10/20 ms). The existing one-write guard prevents any second CTL_START.
The XNU runner boots exactly once, verifies prerequisites and the canonical
trigger value, sends one kickoff command, then stops.
