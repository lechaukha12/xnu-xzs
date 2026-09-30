# D8-M8 Retry 11D-B — passive PP0 command-gate evidence

One RAM boot of commit `5600ef88f9d0e17474d87c9f92810424b3ad5e9f`. No flash, no second `CTL_START`, no second XNU boot, no write to `PP_AUTOREFRESH_CONFIG`.

```text
BRANCH=xzs-d8-display-m8-resume
COMMIT=5600ef88f9d0e17474d87c9f92810424b3ad5e9f
KERNEL_SHA256=09ec21d4bba4a83c932037d948a70aedca4723367a3d428928b64bbbb7e46dc5
BOOT_SHA256=763b9ab5562e62683ee8fef43cfd4044e85beb3033749a173410b35944a38462
PAC=0
SERIAL=BH905SX976
M8-1=PASS
CTL_START_COUNT=1
MDP_KICKOFF_COUNT=1
R11C_SAFE_SHUTDOWN=PASS
shell=xzs# returned
```

Raw rows: `artifacts/hw/d8m8/m8-7-retry11d-b/kickoff.txt`. The host runner then stopped because `M8_7_RETRY11C=OBSERVATION_COMPLETE` is absent from the transcript. After `MDP_KICKOFF_COUNT=1` the console shows `R11C_` spliced directly onto the M5 shutdown text, so the older summary lines (`DSI_MDP_BUSY_SEEN`, `OBSERVATION_ELAPSED_US`) were not captured. Shutdown still printed `R11C_SAFE_SHUTDOWN=PASS` and the prompt returned. Those missing summary lines are not invented here. The buffered samples were printed before the splice.

## Autorefresh

| Point | `PP_AUTOREFRESH_CONFIG` | bit 31 |
|---|---:|---:|
| PRE_CLEAR, count `0x6c7` | `0x00000000` | 0 |
| POST_CLEAR, count `0x6cb` | `0x00000000` | 0 |
| C1150, immediately before `CTL_START` | `0x00000000` | 0 |
| C1160, immediately after | `0x00000000` | 0 |
| every GATE and WRAP sample | `0x00000000` | 0 |

`R11DB_POST_CLEAR_BITS8_12_16=CLEAR`. `MDP_INTR_EN` stayed `0`. Case AR0: autorefresh enable is excluded for this run. The register did not change across `CTL_START`.

## Gate and wrap

`CTL_START` happened at count `0x853` → `0x85a`, already above `0x790` and below height `0x873`. Flush went `0x00020048` → `0`. RGB0 current source went `0` → `0x98000000`. The counter then wrapped, and the next revolution entered `0x770`–`0x790`.

Directly sampled inside the gate, all with `PP_LINE=0`, `PP_OUT=0`, `DONE8=0`, `AR_DONE20=0`, `DSI_BUSY=0`, `DSI_MDP_DONE=0`:

| Timestamp µs | PP_COUNT | Note |
|---:|---:|---|
| 45718956 | `0x780` | `START_POS`, direct |
| 45718970 | `0x782` | next stored count |
| 45718997 | `0x785` | `SYNC_WRCOUNT`, direct |
| 45719079 | `0x790` | end of stored gate |

`0x781` (`RD_PTR_IRQ`) is not a stored sample. It sits between `0x780` and `0x782`, 14 µs apart, and both neighbors are zero on line, out, done, autorefresh-done, and DSI. That single missed count is `INFERENCE` for the exact line, not a direct sample.

Wrap is direct: timestamp 45704260, `PP_COUNT=0x00000000`, then `0x002`, `0x004`, `0x00b`. Line and out are 0. `PP_DONE` bit 8 and autorefresh-done bit 20 are 0. Nineteen gate rows and four wrap rows were stored. `R11DB_MAX_PP_LINE=0` and `R11DB_MAX_PP_OUT=0` cover the post-start polls and the post-start snapshots.

`WR_PTR` (bit 16) rose from 0 at C1150 to 1 at C1160 while the count moved `0x853` → `0x85a`. That repeats the R11C edge. It is not used as proof of a command request. `RD_PTR` (bit 12) was already 1 at C1150, before `CTL_START`.

## Required answers

1. `PP_AUTOREFRESH_CONFIG` before `CTL_START` is `0x00000000` (`HW_READBACK_PROVEN`, C1150).
2. Autorefresh bit 31 was not set. `NO — HW_READBACK_PROVEN`.
3. The counter crossed the gate. `0x780` and `0x785` were directly sampled. `0x781` was not stored; it is between those two zero samples.
4. `PP_LINE` did not become non-zero. `NO — HW_READBACK_PROVEN` on every post-start snapshot and every gate/wrap sample.
5. `PP_OUT` did not become non-zero. `NO — HW_READBACK_PROVEN` on the same rows.
6. `PP0_DONE` bit 8 did not appear. `NO — HW_READBACK_PROVEN`.
7. Autorefresh-done bit 20 did not appear. `NO — HW_READBACK_PROVEN`.
8. `DSI_STATUS[2]` did not read high on any stored snapshot or gate/wrap sample. The aggregate `DSI_MDP_BUSY_SEEN` line was lost in the console splice, so that one summary flag is `UNKNOWN`.
9. Raw `CMD_MDP_DONE` did not read 1 on those same stored rows. The aggregate `DSI_MDP_DONE_RAW_SEEN` line is likewise `UNKNOWN`.
10. The run does not prove that PP0 issued a command-mode transfer request. `UNKNOWN`. Line, out, PP done, and the stored DSI busy/done bits stayed 0 through a directly sampled start position, write-count, and wrap. There is still no source-defined request-issued bit, so those zeros are not promoted to “request proven absent,” and they are not a DSI rejection.

## Classification

`R11D-B2 — AUTOREFRESH_DISABLED_PP_OUTPUT_STILL_NOT_STARTED`

Bit 31 is 0 before and after the only `CTL_START`. The timing counter passed `START_POS` and `SYNC_WRCOUNT` and wrapped. Pixel/output counts stayed 0. No corrective write was made.

## Gate table

| Timestamp | PP_COUNT | AUTOREFRESH | PP_LINE | PP_OUT | MDP_INTR | DONE8 | RD12 | WR16 | AR_DONE20 | DSI_BUSY | DSI_MDP_DONE |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 45700970 | 0x6c7 | 0x0 | 0 | 0 | 0x1000 | 0 | 1 | 0 | 0 | 0 | 0 |
| 45701000 | 0x6cb | 0x0 | 0 | 0 | 0x0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 45703992 | 0x853 | 0x0 | 0 | 0 | 0x1000 | 0 | 1 | 0 | 0 | 0 | 0 |
| 45704053 | 0x85a | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45704260 | 0x0 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45704274 | 0x2 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45704287 | 0x4 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45704339 | 0xb | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45718834 | 0x770 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45718847 | 0x772 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45718861 | 0x773 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45718875 | 0x775 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45718888 | 0x777 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45718902 | 0x779 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45718915 | 0x77a | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45718929 | 0x77c | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45718943 | 0x77e | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45718956 | 0x780 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45718970 | 0x782 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45718984 | 0x783 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45718997 | 0x785 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45719011 | 0x787 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45719025 | 0x789 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45719039 | 0x78b | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45719052 | 0x78c | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45719066 | 0x78e | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |
| 45719079 | 0x790 | 0x0 | 0 | 0 | 0x11000 | 0 | 1 | 1 | 0 | 0 | 0 |

Rows `0x6c7` and `0x6cb` are pre-clear and post-clear. `0x853` is C1150. `0x85a` is C1160. The `0x0` row is the wrap. The rest are the stored gate counts. Stop. No fix.
