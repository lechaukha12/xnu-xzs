# D8-M8 C1 — MDP core rate A/B

One RAM boot. One RCG update. One `CTL_START`. No second boot.

```text
C1_CLASS=C1-B
MDP_RATE_CORRECTED_PP_FRAME_START_STILL_BLOCKED
DID_CORRECT_MDP_RATE_CAUSE_PP_LINE_TO_START=NO — HW_PROVEN

M8_1_PASS=YES
GPLL0_GATE=PASS
CMD_PRE=0x00000000
CFG_PRE=0x00000000
MDP_CBCR_PRE=0x00006221
RCG_UPDATE_COMPLETED=YES
CMD_POST=0x00000000
CFG_POST=0x00000506
MDP_CBCR_POST=0x00006221
TARGET_RATE_CONFIRMED=YES

CTL_START_COUNT=1
PP_COUNT_RUNNING=YES
PP_LINE_MAX=0x00000000
PP_OUT_MAX=0x00000000
PP0_DONE_SEEN=no
DSI_MDP_BUSY_SEEN=no
CMD_MDP_DONE_SEEN=no
CTL_FLUSH_CONSUMED=YES
RGB0_CURRENT_SRC0_ADDR=0x98000000
```

The 171428571 Hz row was programmed and held through the frame.
`PP_LINE` stayed 0. The clock mismatch was real and is not sufficient
to start the ping-pong frame.

## 1. Identity

```text
BRANCH=xzs-d8-display-m8-resume
BASE_HEAD=0c3cd73019b955b059c0433f5b3cda46ba5f0b3b
C1_COMMIT=1922ac155e634579b170413b6d786c83a49a3c96
COMMIT_BOOTED=1922ac155e634579b170413b6d786c83a49a3c96
KERNEL_SHA256=c048e8a741274e0abf3b40ecfaa4567ad00f6243dfc42d7b8ad21ef9107b173c
BOOT_SHA256=f8fdf6c39e99c5fb54ab60f52913681b6223615e77fe416b71c6db0f0647f69e
PAC=0
SERIAL=BH905SX976
BOOT=fastboot boot artifacts/builds/xzs-xnu-boot.img
```

`/bin/sh` stays at 16768 bytes, CRC `0xf45be15c`. The host sent the
existing verb `clocks mdss-ahb-debug` once, after `M8_1 = PASS` and
before `display m8-rgb0-config`. That verb writes the RCG only when
`g_m8_fb_initialized` is set. The earlier `clocks mdss-critical-status`
ran before `M8_1` and took the T1-E return. It did not write the RCG.

Artifact directory: `artifacts/hw/d8m8/c1-mdp-rate-ab/`.

## 2. GPLL0 gate

Same-boot read, before any CFG store. Class `HW_READBACK_PROVEN`.

```text
GPLL0_MODE=0xc0118000
GPLL0_LOCK=1
GPLL0_ACTIVE=1
GPLL0_FSM=1
GPLL0_VOTE=0x00000011
GPLL0_VOTE_BIT0=1
GPLL0_GATE=PASS
```

This matches the earlier XNU shell word `0xc0118000`. C1 did not vote
or program GPLL0.

## 3. RCG update

Pre-state matches T1: root on, BI_TCXO hid 0, MDP branch active.

```text
CMD_PRE=0x00000000
CFG_PRE=0x00000000
MDP_CBCR_PRE=0x00006221
CFG_OLD=0x00000000
CFG_NEW=0x00000506
```

The CFG store is the RMW `(old & ~0x0010371F) | 0x00000506`. After that
store, CMD read back as `0x00000010`. Bit 4 is `CMD_DIRTY_CFG`. Software
did not write that bit. The following RMW only forced bit 0:

```text
CMD_BEFORE_UPDATE=0x00000010
CMD_UPDATE_WROTE=0x00000011
RCG_POLL_ITERS=1
RCG_UPDATE_COMPLETED=YES
```

The first poll already saw bit 0 clear. Post-state:

```text
CMD_POST=0x00000000
CMD_POST_UPDATE_BIT=0
CMD_POST_ROOT_OFF=0
CFG_POST=0x00000506
CFG_POST_MASKED=0x00000506
MDP_CBCR_POST=0x00006221
MDP_BRANCH_POST=ACTIVE
TARGET_RATE_CONFIRMED=YES
C1_READY_FOR_FRAME=YES
```

Dirty bit 4 cleared with the update. Root-off stayed clear. The branch
stayed `0x00006221`. No M/N/D write. No second rate write.

## 4. The one frame

The unchanged Retry #11D-B path then ran: RGB0, LM0, stream, CTL, flush,
one `display m8-kickoff`. `CTL_START_COUNT=1`. `MDP_KICKOFF_COUNT=1`.

`PP_INT_COUNT` moved (`0x72`, later `0x780`, then wrapped through 0).
`R11DB_PP_LINE_NONZERO=no`. `R11DB_MAX_PP_LINE=0x00000000`.
`R11DB_MAX_PP_OUT=0x00000000`. `R11DB_PP_DONE_BIT8=no`.
`DONE8=0` on every complete sample.

Flush `0x00020048` is present on the pre-start snapshots, including
`C1150`. The first post-start snapshot `C1160` has `CTL_FLUSH=0x00000000`
and `RGB0_CUR_SRC0=0x98000000`. Later snapshots keep both of those.
`CTL_FLUSH_CONSUMED=YES`. The early `POST_CLEAR` sample still showed
`0x00020048` because it was taken before that consumption.

Every intact post-start snapshot has `PP_LINE=0x00000000`,
`PP_OUT_LINE=0x00000000`, `DSI_MDP_BUSY_BIT=0`, and
`DSI_MDP_DONE_RAW=0`. Complete `R11DB` samples have `DSI_BUSY=0` and
`DSI_MDP_DONE=0`.

R11E samples that were not spliced keep, through the frame:

```text
MDP_CMD=0x00000000
MDP_CFG=0x00000506
MDP_CBCR=0x00006221
PP_LINE=0x00000000
CMD_MDP_DONE=0
DSI_BUSY=0
```

The rate stayed at the programmed row. Shutdown was the existing
display-off, sleep-in, and rail sequence. `R11C_SAFE_SHUTDOWN=PASS`.
The shell prompt returned.

A USB splice cut the prose lines `PP0_DONE_OBSERVED`,
`DSI_MDP_BUSY_SEEN`, `DSI_MDP_DONE_RAW_SEEN`, and
`MAX_LINE_COUNT_OBSERVED`, and it joined two R11E sample lines.
The structured snapshots and the `R11DB_*` summary lines are intact.
Those are the source of the answers above. The splice is not a second
boot and not a reason to repeat the frame.

No visible-pixel observation was taken. Pixels are not the C1 criterion.

## 5. What this closes

```text
MDP_CLOCK_MISMATCH was HW_READBACK_PROVEN before this boot.
The correction to 0x00000506 is now HW_READBACK_PROVEN.
PP_LINE remained 0 on the same first-frame path.
MDP_RATE_CAUSAL_TO_PP_FRAME_START = NO — HW_PROVEN
```

Class `C1-B`. The core rate is necessary conformance and is not the
remaining frame-start blocker. The next unknown is downstream of this
clock. This boot does not choose it.

No bus vote, VBIF, TE, PP timing, DSI trigger, or second `CTL_START`
was added.

## 6. Stop

One correction. One boot. One `CTL_START`. Stop.
