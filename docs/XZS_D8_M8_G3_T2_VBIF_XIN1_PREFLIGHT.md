# D8-M8 G3 / T2 preflight — VBIF XIN1

Source audit of the pinned Sony MDSS VBIF path. No XNU boot. No VBIF
write. TWRP was not entered.

```text
G3_CLASS=G3-D TWRP_CAPTURE_BLOCKED_XNU_CONSOLE
TWRP_VBIF_READ_METHOD=UNAVAILABLE
RGB0_XIN=1
GOLDEN_QOS_PROVEN=NO
GOLDEN_HALT_STATE_PROVEN=NO
QOS_CAUSALITY_FROM_SOURCE=UNKNOWN
T2_NEW_MAPPING_REQUIRED=NO
T2_READ_SAFE=YES
T2_READY_FOR_HARDWARE=NO
NEXT_ACTION=return the phone to fastboot, then read TWRP VBIF; do not boot XNU and do not write QoS
```

The phone on USB is `XZS USB Console`, vendor `0x1209`, product `0x000a`.
`fastboot devices` and `adb devices` are empty. No `xzs# reboot` was sent.

C1 remains the baseline. `MDP_RCG_CFG=0x00000506` is conformant and is
not the frame-start cause.

```text
U1=CLOSED
MDP_RATE_MISMATCH=CORRECTED
MDP_RATE_CAUSAL_TO_PP_FRAME_START=NO — HW_PROVEN
```

## 1. Repository

```text
BRANCH=xzs-d8-display-m8-resume
HEAD_AT_AUDIT_START=c9432822b4309df830444d21405c146374bf2ed0
C1_COMMIT=1922ac155e634579b170413b6d786c83a49a3c96
WORKTREE=clean before this document
PIN=sonyxperiadev/kernel 5772572ccdfbc16c270d33f8fa6b55d33d27709c
```

## 2. RGB0 is XIN 1

`SOURCE_PROVEN`.

| Item | Value | Where |
|---|---|---|
| Client | `MDSS_MDP_SSPP_RGB0` | `mdss_mdp.h` enum, before RGB1 |
| Pipe type | `MDSS_MDP_PIPE_TYPE_RGB` | `get_pipe_type_from_num` |
| DT list | `qcom,mdss-pipe-rgb-xin-id = <1 5 9 13>` | `msm8996-mdss.dtsi` and Keyaki `0x01 0x05 0x09 0x0d` |
| First RGB offset | `0x00015000` | same dtsi `qcom,mdss-pipe-rgb-off` |
| Stored field | `pipe->xin_id = xin_id[i]` | `mdss_mdp_pipe_addr_setup` |
| Instance | RT VBIF, not rotator VBIF | RGB0 on an interface mixer |

The parser walks SSPP numbers and keeps RGB pipes in enum order, so
the first xin-id entry is RGB0. XNU's RGB0 block `0x00915000` is
`mdp_phys 0x00900000 + 0x15000`, the same first RGB offset.

Rotator VBIF is a different window, `0x009b8000`, name `rot_vbif_phys`.
RGB0 does not use it. `mdss_mdp_is_nrt_vbif_client` is the writeback
path.

```text
RGB0_XIN=1
RGB0_XIN1_SOURCE_PROVEN=YES
```

## 3. VBIF base and the registers that matter

`vbif_phys` in `msm8996-mdss.dtsi`:

```text
reg = <0x00900000 0x90000>, <0x009b0000 0x1040>
reg-names = "mdp_phys", "vbif_phys"
VBIF_BASE=0x009b0000
```

MDP revision 107, 107.1, and 107.2 do not set `MDSS_QOS_REMAPPER`
(`mdss_mdp_hw_rev_caps_init`). The remapper therefore takes the legacy
branch, `MDSS_VBIF_QOS_REMAP_BASE + i*4`, not `0x550` / `0x570`.
C1 read MDP version `0x10070002`, which is that 107 family.

`qcom,mdss-has-fixed-qos-arbiter-enabled` is absent from the MSM8996
MDSS node. `mdss_mdp_fixed_qos_arbiter_setup` returns without a write.

| Register | PA | XIN1 field | Meaning | Safe read? | Read-clear? |
|---|---:|---|---|---|---|
| QoS remap 0 | `0x009b0020` | bits `[3:2]` | client level 0 → VBIF priority | `SAFE_PASSIVE_READ` after GDSC on | no source evidence of read-clear |
| QoS remap 1 | `0x009b0024` | bits `[3:2]` | client level 1 | same | same |
| QoS remap 2 | `0x009b0028` | bits `[3:2]` | client level 2 | same | same |
| QoS remap 3 | `0x009b002c` | bits `[3:2]` | client level 3 | same | same |
| `XIN_HALT_CTRL0` | `0x009b0200` | bit 1 | halt request | `SAFE_PASSIVE_READ` after GDSC on | no |
| `XIN_HALT_CTRL1` | `0x009b0204` | bit 1 | halt status polled by `mdss_mdp_wait_for_xin_halt` | same | no |
| `XIN_HALT_CTRL1` | `0x009b0204` | bit 17 | idle bit used by `mdss_mdp_is_pipe_idle` (`xin_id+16`) | same | no |
| `RD_LIM_CONF` | `0x009b00b0` | bits `[15:8]` | XIN1 read outstanding limit | same | no |
| `SRC_ERR` `0x194` | `0x009b0194` | undocumented | name only | `UNKNOWN` | `UNKNOWN` |
| `ERR_INFO` `0x1A0` | `0x009b01a0` | undocumented | name only | `UNKNOWN` | `UNKNOWN` |
| `AXI_HALT_CTRL0/1` | `0x009b0208` / `0x009b020c` | bit 0 is the whole RT AXI port | deep suspend / display off | read is the same class as the other VBIF reads | no |

Field math, legacy path, `mdss_mdp_qos_vbif_remapper_setup`:

```text
mask  = 0x3 << (xin_id * 2)
shift = xin_id * 2
xin_id 1 → bits [3:2]
register = 0x020 + (level * 4)
```

Outstanding math, `mdss_mdp_set_ot_limit`:

```text
reg = (xin_id / 4) * 4 + RD_LIM_CONF
bit = (xin_id % 4) * 8
xin_id 1, read → 0x0B0 bits [15:8]
```

`SRC_ERR` and `ERR_INFO` have offsets and no reader in this driver.
They stay out of T2.

## 4. What `<1 2 2 2>` writes

Keyaki and `msm8996-mdss.dtsi` both have:

```text
qcom,mdss-vbif-qos-rt-setting = <1 2 2 2>
qcom,mdss-vbif-qos-nrt-setting = <1 1 1 1>
```

`mdss_mdp_parse_vbif_qos` keeps the array only when its length is
`MDSS_VBIF_QOS_REMAP_ENTRIES` (4). The realtime pointer is
`vbif_rt_qos`. RGB0 on an interface mixer is realtime
(`is_realtime` in `mdss_mdp_pipe_queue_data`).

All four entries are written into XIN1. There is not one QoS number
for the pipe.

| Level `i` | PA | XIN1 bits | RT value |
|---:|---:|---|---:|
| 0 | `0x009b0020` | `[3:2]` | 1 |
| 1 | `0x009b0024` | `[3:2]` | 2 |
| 2 | `0x009b0028` | `[3:2]` | 2 |
| 3 | `0x009b002c` | `[3:2]` | 2 |

The index is the client priority level. The pipe CREQ LUT, a different
SSPP register, picks which level is in use from fill. The VBIF table
itself is the DT constant.

The write runs from `mdss_mdp_pipe_queue_data` when `params_changed`
is set and programming is not delayed. That is commit/queue time, not
`CTL_START`, and not every vsync after `params_changed` is cleared.
The four numbers do not change per frame.

```text
SONY_XIN1_QOS_EXPECTED=level0:1 level1:2 level2:2 level3:2
```

Read outstanding default is `qcom,mdss-default-ot-rd-limit = <32>`.
`apply_dynamic_ot_limit` returns without a change unless the client is
rotator+YUV or WFD. RGB0 stays at 32 when the limit is applied.
`set_ot_limit` skips the write when the field is already 32.

## 5. QoS is not proven to gate fetch

`QOS_FUNCTIONAL_REQUIREMENT=UNKNOWN`.

The remapper stores a 2-bit priority. This driver never says that a
missing or different value stops an AXI read. It also never says the
field is performance-only. `QOS_CAUSALITY_FROM_SOURCE=UNKNOWN`.

A halt request is a different mechanism and can stop fetch. That does
not make the QoS priority a fetch enable.

## 6. XIN1 halt

| Question | Answer | Class |
|---|---|---|
| Halt request bit | `XIN_HALT_CTRL0` bit `xin_id` (bit 1) | `SOURCE_PROVEN` |
| Halt status bit | `XIN_HALT_CTRL1` bit `xin_id` (bit 1), polled until set | `SOURCE_PROVEN` |
| Idle bit | `XIN_HALT_CTRL1` bit `xin_id+16` (bit 17) | `SOURCE_PROVEN` |
| Scanout needs a standing unhalt command | The paths that set the request clear it before return | `SOURCE_PROVEN` |
| Who clears it | `mdss_mdp_pipe_fetch_halt` and `mdss_mdp_set_ot_limit` | `SOURCE_PROVEN` |
| Reset default halted or running | not stated | `UNKNOWN` |
| When halt is used | pipe not idle and not in use, before init, recovery, and the short OT-limit update | `SOURCE_PROVEN` |

`mdss_mdp_pipe_fetch_halt` is for an unstaged or uninitialized pipe, and
for recovery. `mdss_mdp_set_ot_limit` sets the request, waits, then
clears it so the new limit can be applied. Neither leaves XIN1 halted
for the following scanout.

Whole-port `AXI_HALT_CTRL0` bit 0 is `mdss_mdp_vbif_axi_halt`, used for
deep suspend, display off, or debug. It is not the per-frame XIN1
control.

## 7. Read safety and mapping

Future T2 reads, and only these, after MDSS GDSC is already on:

```text
0x009b0020  0x009b0024  0x009b0028  0x009b002c
0x009b0200  0x009b0204
0x009b00b0
```

Class of each: `SAFE_PASSIVE_READ` on the existing identity accessor
once GDSC is on. They are `POWER_DOMAIN_DEPENDENT` before that. Do not
read them with MDSS collapsed. `SRC_ERR` and `ERR_INFO` stay out.

`d8p1_read32` is the accessor. It is an identity physical load plus
`dsb sy`, not `ml_io_map`. Page `0x009b0000` was read on the C1 boot
by the existing R11E capture while GDSC was on. `NEW_MAPPING_REQUIRED=NO`.
`T2_READ_SAFE=YES` for that list. `T2_READY_FOR_HARDWARE=NO` until a
TWRP golden of the same fields exists. A safe read without the golden
does not answer the comparison.

Sample point from the source order: after the existing pre-kick pipe
and flush programming, before `CTL_START`. Sony writes this table in
`pipe_queue_data`, before kickoff, and does not write it from
`CTL_START`. Do not make the only sample post-start. Add a second
sample only if a later TWRP capture shows the words change between
idle and active redraw.

## 8. TWRP

Not booted. Intended read, once the phone is in fastboot:

```text
fastboot -s BH905SX976 boot artifacts/builds/twrp-kagura.img
```

Pinned debugfs, not `/dev/mem`: `/sys/kernel/debug/mdp/vbif_reg` reads
`0x100` bytes from the current window. The default window starts at 0
and covers the QoS block and `RD_LIM`. Halt at `0x200` needs the
debugfs window file `vbif_off` set to that offset. That file stores
the dump window. It is not a `writel` to VBIF. Do not write
`vbif_reg`. That write is `writel_relaxed` to the VBIF.

```text
TWRP_XIN1_QOS_IDLE=UNKNOWN
TWRP_XIN1_QOS_ACTIVE=UNKNOWN
TWRP_XIN1_HALT_IDLE=UNKNOWN
TWRP_XIN1_HALT_ACTIVE=UNKNOWN
G3_QOS_GOLDEN_CONFIRMED=NO
```

## 9. Same addresses already read on C1

This is prior XNU readback of these PAs, not a golden and not a cause.

During the C1 frame the R11E lines included:

```text
XIN_HALT0=0x00000000
XIN_HALT1=0x3fff0000
QOS0=0x00000000
QOS1=0x05555555
QOS2=0x0aaaaaaa
QOS3=0x0fffffff
RD_LIM=0x00202020
```

XIN1 decode of those words: halt request bit 1 clear; QoS levels
`0,1,2,3` against the Sony RT row `1,2,2,2`; read limit bits `[15:8]`
equal 32. One later sample showed `XIN_HALT1=0x3ffd0000`. A USB splice
cut part of the R11E dump. Treat the intact words as the C1 observation
of these registers. They do not authorize a QoS write.

## 10. Stop

No XNU boot. No QoS write. No unhalt. The next capture is TWRP only,
after the phone is back in fastboot.
