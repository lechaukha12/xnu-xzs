# D8-M8 G2 — TWRP active-frame MDP clock

No XNU boot. No display or MMCC write. No rate program.

```text
G2_CLASS=G2-A ACTIVE_RATE_171428571
TRACE_METHOD=A mdss:mdp_commit clk_rate plus debugfs CMD/CFG/CBCR
ACTIVE_FRAME_TRIGGER=eight clearpad swipes on /dev/input/event8
KICKOFF_OBSERVED=YES
ACTIVE_TWRP_MDP_RATE_HZ=171428571
ACTIVE_TWRP_SOURCE=GPLL0
ACTIVE_TWRP_HID=6
ACTIVE_TWRP_TABLE_MATCH=YES
SONY_EXPECTED_MDP_RATE_HZ=171428571
TWRP_ACTIVE_EQUALS_SONY_EXPECTED=YES
CLOCK_POLICY=PER_FRAME
MDP_RATE_CORRECTION_CONFIDENCE=HIGH
NEXT_ACTION=a later one-change XNU test may program the 171428571 Hz row; do not do it in G2
```

## 1. Kernel

Same RAM image as G1, `artifacts/builds/twrp-kagura.img`, serial `BH905SX976`.

```text
uid=0(root)
Linux version 3.18.20-v01+ (androplus@sonymobile.com)
#2 SMP PREEMPT Mon Oct 3 23:07:41 JST 2016
```

`fb0` is `mdssfb_90000`, state 0.

## 2. What tracing exists

`available_tracers` is only `nop`. There is no `set_ftrace_filter`. Function tracing is not available.

These `mdss` events are present. `mdp_commit` carries `clk_rate`:

```text
mdss:mdp_commit            clk_rate, play_cnt, bandwidth
mdss:mdp_cmd_kickoff       ctl, kickoff count
mdss:mdp_cmd_pingpong_done
mdss:mdp_cmd_wait_pingpong
mdss:mdp_cmd_release_bw
mdss:mdp_perf_update_bus
mdss:mdp_mixer_update
```

No `clk_set_rate` event. Before the run, `current_tracer` was `nop`, `tracing_on` was 1, and `set_event` was empty. After the capture those three were put back. `set_event` was confirmed empty.

## 3. Redraw

`/dev/input/event8` is `clearpad`. Eight swipes were written as `EV_ABS` reports only. No framebuffer map, no blanking, no clock enable file write.

## 4. Idle before

Same as G1:

```text
CMD 0x80000000   root off
CFG 0x0000050d   GPLL0 hid 13   85714286 Hz
MDP CBCR 0x80006220   branch off
```

## 5. During the swipes

Samples 0 through 7 stayed on that idle row. Sample 8 changed and was still running on sample 9:

```text
CMD  0x00000000          root on
CFG  0x00000506          GPLL0 hid 6
rate 171428571
MDP CBCR 0x00006221      bit0=1 bit31=0
```

`0x00000506` is source 5, hid 6, mode 0. That is `F(171428571, P_GPLL0, 3.5, 0, 0)`.

Later samples return to root off while CFG stays `0x00000506`. Two more samples, 15 and 17, caught the root and the MDP branch on again at the same row. Idle-after is root off, CFG still `0x00000506`, CBCR `0x80005220`.

The trace buffer held 12 events and did not wrap:

```text
159.872388  mdp_perf_update_bus   client 0  ab=16777216
159.872426  mdp_mixer_update      mixer 0
159.872552  mdp_cmd_wait_pingpong ctl 0 cnt 0
160.006238  mdp_cmd_kickoff       ctl 0 cnt 1
160.007844  mdp_commit            play_cnt 1  clk_rate=166246433  bw=16777216
160.031182  mdp_cmd_pingpong_done
160.055894  mdp_perf_update_bus   ab=598026240 ib=956841984
160.056214  mdp_mixer_update
160.056257  mdp_cmd_wait_pingpong
160.056269  mdp_cmd_kickoff       cnt 1
160.056693  mdp_commit            play_cnt 2  clk_rate=166246433
160.080968  mdp_cmd_pingpong_done
```

Order for each frame: bus update, mixer update, wait-pingpong, kickoff, commit with the clock rate, pingpong done about 25 ms after kickoff. The second frame starts its own bus update 25 ms after the first done. The root is off again on the idle-after read. That is per-frame arming, not one clock left on for the whole burst.

`mdp_commit` prints `clk_rate=166246433`. That number is not a table row. It sits between 150,000,000 and 171,428,571, so `qcom_find_freq` selects 171,428,571. The CFG word written for those frames is that row. The pinned Keyaki arithmetic in G1 produced 166,860,000 Hz and the same ceiling. The two unrounded requests differ by about 0.4 percent. The row that reaches the RCG is the same.

## 6. Classification

```text
G2-A
TWRP_ACTIVE_EQUALS_SONY_EXPECTED=YES
MDP_RATE_CORRECTION_CONFIDENCE=HIGH
```

XNU T1 remains the non-table XO bypass: CMD `0x00000000`, CFG `0x00000000`, branch `0x00006221`. The active TWRP branch word matches that `0x00006221`. The RCG does not. Active TWRP is GPLL0 hid 6. Idle TWRP before the swipe was GPLL0 hid 13 with the root off. The idle 85.7 MHz row is not the active rate.

A later XNU task can program `mdp_clk_src` to this one row, 171,428,571 Hz, and stop. That task is not this one.

Raw samples: `artifacts/hw/d8m8/g2-twrp-mdp-clock/during.txt`.
