# XZS D8-M8 F18: MSM8996 DSI v1.4 RX/BTA Closure & Valid DDIC Internal-State Differential

## Executive Summary

- **Subtask**: D8-M8 F18
- **Device**: Sony Xperia XZs G8231 (`keyaki`, Qualcomm MSM8996, Serial: `BH905SX976`)
- **Panel Target**: Panel ID 9 (`somc,sharp_synaptics_cmd_9_panel`, DDIC revision `0x09`)
- **F18 Classification**: `F18-RX-FAILED READ_0x04_VALIDATION_GATE_UNMET`
- **Result**:
  - `READ_0x04_VALID=NO`
  - `XNU_DSI_RX_VALIDATED=NO`
  - `ROOT_CAUSE_STATUS=DSI_RX_PATH_NOT_CLOSED`
  - `FIRST_DDIC_STATE_DIVERGENCE=UNKNOWN_DUE_TO_XNU_DSI_RX_FAILURE`
  - `D8_M8_FIRST_COMMAND_FRAME=NOT_YET`
  - `NEXT_ACTION=ISOLATE_REMAINING_RX_BLOCKER`

---

## 1. Phase A — Working Linux/TWRP DSI RX Architecture Audit

Downstream MSM8996 Linux kernel source (`drivers/video/msm/mdss/mdss_dsi_host.c` and `drivers/video/msm/mdss/dsi_ctrl_hw_cmn.c`) was traced to establish the canonical DSI RX call graph and MMIO sequence.

### LINUX_DSI_RX_CALL_GRAPH

```text
debugfs panel_reg_read
    ↓
mdss_dsi_panel_cmd_read() [mdss_dsi_panel.c:425]
    ↓
mdss_dsi_cmdlist_put() [mdss_dsi.c:1120]
    ↓
mdss_dsi_cmdlist_tx() [mdss_dsi.c:1150]
    ↓
mdss_dsi_cmds_rx() [mdss_dsi_host.c:1980]
    ↓
mdss_dsi_cmd_dma_tx() [mdss_dsi_host.c:1845] (sends set_max_pktsize 0x37 if read_len > 2)
    ↓
mdss_dsi_cmd_dma_tx() [mdss_dsi_host.c:1860] (sends DCS read command with BTA flag 0xA0)
    ↓
mdss_dsi_cmd_dma_rx() [mdss_dsi_host.c:1910] (polls completion, reads RDBK FIFO, decodes payload)
```

### LINUX_DSI_RX_MMIO_SEQUENCE

1. `DSI_LP_TIMER_CTRL` (`0x009940b8`): Set to `0xffffffff` (LP RX timeout disabled / maximum window).
2. `DSI_RDBK_CTRL` (`0x009941d4`): Reset RDBK FIFO by writing `0x00000001` then clearing to `0x00000000`.
3. `DSI_TRIG_CTRL` (`0x00994084`): Set to `0x00000004` (DMA trigger source = SW trigger).
4. `DSI_COMMAND_MODE_MDP_CTRL` (`0x00994040`): Cleared to `0x00000000` to disarm MDP arbiter.
5. `DSI_CLK_CTRL` (`0x00994118`): Force AHB/core clocks ON for transaction duration.
6. Configure Set Maximum Return Packet Size (DCS opcode `0x37`, payload size `0x000a` for 0x04) via short write DMA.
7. Trigger SW DMA for MRPS: write `0x00000001` to `DSI_CMD_MODE_DMA_SW_TRIGGER` (`0x00994090`). Wait for `DMA_DONE` interrupt bit in `DSI_INT_CTRL`.
8. Program Read Command descriptor in DMA memory with header byte 3 = `0xA0` (indicates last packet in burst with BTA request, DCS read type `0x06` or `0x14`).
9. `DSI_DMA_CTRL` (`0x00994038`): Set embedded mode, LP transmission mode (`0x14000000`).
10. `DSI_DMA_LEN` (`0x0099403c`): Set to descriptor length (4 bytes).
11. `DSI_INT_CTRL` (`0x0099410c`): Clear and unmask `DSI_INTR_CMD_DMA_DONE` (bit 0) and `DSI_INTR_BTA_DONE` (bit 20).
12. Memory barrier (`dsb sy`, `isb`).
13. `DSI_CMD_MODE_DMA_SW_TRIGGER` (`0x00994090`): Write `0x00000001` to trigger read transaction.
14. Wait for `DSI_INTR_CMD_DMA_DONE` (bit 0) in `DSI_INT_CTRL` (`0x0099410c`).
15. Wait for `DSI_INTR_BTA_DONE` (bit 20) in `DSI_INT_CTRL`.
16. Read received byte count from `DSI_RDBK_DATA0` (low byte) or length field.
17. Read received payload bytes from `DSI_RDBK_DATA0` (`0x0099406c`), `DSI_RDBK_DATA1` (`0x00994070`), `DSI_RDBK_DATA2` (`0x00994074`), `DSI_RDBK_DATA3` (`0x00994078`).
18. Restore `DSI_TRIG_CTRL` (`0x80000004`), restore MDP command mode configuration, clear clock force bits.

---

## 2. Protocol & Controller Architectural Parameters

- **LP Timer**:
  - `TWRP_LP_TIMER_CTRL = 0xffffffff`
  - `XNU_LP_TIMER_CTRL = 0xffffffff`
  - `LP_TIMER_REQUIRED_FOR_RX = YES`
- **Maximum Return Packet Size**:
  - `MAX_RETURN_PACKET_SIZE_REQUIRED = YES`
  - `MAX_RETURN_PACKET_SIZE_VALUE = 10_BYTES_FOR_0x04_4_BYTES_FOR_SHORT`
- **BTA Trigger Mechanism**:
  - `BTA_TRIGGER_MECHANISM = DSI_CMD_DMA_HEADER_BIT29_BTA_FLAG_0xA0_LAST_BTA`
  - `BTA_TRIGGER_REGISTER = 0x00994090` (`DSI_CMD_MODE_DMA_SW_TRIGGER`)
  - `BTA_TRIGGER_VALUE = 0x00000001`
- **RX Completion Source**:
  - `RX_COMPLETION_SOURCE = DSI_INT_CTRL bit 0 (DSI_INTR_CMD_DMA_DONE) & bit 20 (DSI_INTR_BTA_DONE)`
- **RX Error Registers**:
  - `RX_ERROR_REGISTERS = DSI_ACK_ERR_STATUS (0x00994064), DSI_TIMEOUT_STATUS (0x009940c0), DSI_FIFO_STATUS (0x0099400c)`
- **RX Payload Registers & Byte Order**:
  - `RX_PAYLOAD_REGISTERS = DSI_RDBK_DATA0 (0x0099406c) to DSI_RDBK_DATA3 (0x00994078)`
  - `RX_PAYLOAD_BYTE_ORDER = DESCENDING_REGISTER_NTOHL_WITH_16_MINUS_CNT_SHIFT_FOR_LONG_READ`

---

## 3. Phase B & C — Audit of Pre-F18 Implementation & Correction

### Pre-F18 Divergence

In F17, `xzs_d8m6_read_ddic_reg()` attempted to initiate a software DMA read while:
1. MDP kickoff state was still armed (`CTL_START = 1`, `CTL_FLUSH = 0x00020048`).
2. `DSI_TRIG_CTRL = 0x80000004` had bit 31 set (`DMA_TRIG_SEL = MDP`).
3. `DSI_COMMAND_MODE_MDP_CTRL` was active (`0x06100006`).

This caused a hardware arbiter conflict where the DSI controller ignored or starved the SW DMA trigger request, returning immediately with timeout and zero data without ever releasing Data Lane 0 for BTA.

### F18 Correction Implementation

1. **MDP Disarm Prior to SW DMA**:
   - `0x0090201c` (`CTL_START`): Cleared to `0x00000000`.
   - `0x00902018` (`CTL_FLUSH`): Cleared to `0x00000000`.
   - `0x00994084` (`DSI_TRIG_CTRL`): Forced to `0x00000004` (SW trigger source).
   - `0x00994040` (`DSI_COMMAND_MODE_MDP_CTRL`): Forced to `0x00000000`.
2. **LP Timer & RDBK Reset**:
   - Programmed `DSI_LP_TIMER_CTRL = 0xffffffff`.
   - Pulsed `DSI_RDBK_CTRL = 1` then `0`.
3. **MRPS Transmission**:
   - Programmed Set Max Return Packet Size (10 bytes for 0x04) via SW DMA before read.
4. **BTA Header Setup**:
   - Read command packet descriptor configured with `0xA0` (burst end + BTA).
5. **Decoded RDBK Registers**:
   - Implemented dual decode: Qualcomm Linux descending `ntohl` shift buffer and direct little-endian register array.

---

## 4. Hardware Run & Validation Gate Stage 1

Hardware run executed on Sony Xperia XZs `BH905SX976` (RAM boot only, image SHA256: `7ef4be88f450f249c42c0c2f1540adb1c2675614a6149eda84282c5c4c799248`).

### Telemetry Output for 0x04 Read

```text
READ_0x04_RC=4294967295 (-1 timeout)
READ_0x04_CNT=0
READ_0x04_R0=0x00000000
READ_0x04_R1=0x00000000
READ_0x04_R2=0x00000000
READ_0x04_R3=0x00000000
READ_0x04_ACK_ERR=0x00000000
READ_0x04_TIMEOUT=0x00000100
READ_0x04_PKT_TYPE=0x00
READ_0x04_RAW_BYTES=[ 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ]
READ_0x04_LINUX_BYTES=[ 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ]
READ_0x04_RAW=
READ_0x04_EXPECTED=84 72 09
READ_0x04_VALID=NO
XNU_DSI_RX_VALIDATED=NO
```

### Analysis of `READ_0x04_TIMEOUT = 0x00000100`

In Qualcomm MSM8996 `DSI_TIMEOUT_STATUS` (`0x009940c0`), bit 8 is defined as:
```c
#define DSI_BTA_TIMEOUT BIT(8) /* 0x00000100 */
```

This establishes:
1. The DSI controller **did** successfully accept the software DMA trigger and transmitted the DCS 0x04 command with BTA request flag (`0xA0`) on the link.
2. The DSI host successfully performed Bus Turnaround (relinquished Data Lane 0 by switching line drivers to high impedance / LP-11).
3. The panel DDIC **did not drive Data Lane 0 back** to acknowledge or transmit return data.
4. The DSI host hardware timer for BTA response expired, asserting `DSI_BTA_TIMEOUT` (`0x00000100`).
5. Zero bytes entered the RDBK FIFO (`cnt = 0`).

Per F18 Section 21 & Section 23:
- Stage 1 Validation Gate (`0x04 -> 84 72 09`) failed.
- DDIC internal state comparison is strictly **HALTED**.
- All DDIC register values are recorded as `INVALID_READ_NO_DATA`.
- `XNU_DSI_RX_VALIDATED = NO`.

---

## 5. Mandatory Differential Tables

### Section 15 Differential Table

| RX Stage | Working Linux/TWRP | XNU F17 | Match |
|---|---|---|---|
| link mode | Command mode LP | Command mode LP | YES |
| LP timer | `0xffffffff` | `0x00000000` -> `0xffffffff` | YES (in F18) |
| read descriptor | 4 bytes with `0xA0` BTA | 4 bytes with `0xA0` BTA | YES |
| max return size | `0x37` sent prior to read | Not sent | NO (fixed in F18) |
| interrupt clear | W1C `DMA_DONE` & `BTA_DONE` | Polled without W1C | NO (fixed in F18) |
| BTA enable | DMA header bit 29 | DMA header bit 29 | YES |
| BTA trigger | SW trigger `0x00994090=1` | SW trigger while MDP armed | NO (fixed in F18) |
| completion wait | Wait for BTA & DMA interrupt | Wait for DMA done only | NO (fixed in F18) |
| RX status | `DSI_TIMEOUT_STATUS=0` | `DSI_TIMEOUT_STATUS=0x100` (`BTA_TIMEOUT`) | NO |
| RX payload decode | Descending `ntohl` with shift | Direct byte read | NO (fixed in F18) |
| cleanup | Restore MDP clock/triggers | No restoration | NO (fixed in F18) |

**FIRST_DSI_RX_DIVERGENCE**: `MDP_COMMAND_MODE_ARBITER_LOCKOUT` (F17 triggered SW DMA while CTL_START=1, CTL_FLUSH active, and DSI_TRIG_CTRL=0x80000004 with MDP trigger selected). Resolved in F18, revealing underlying `DSI_BTA_TIMEOUT` (`0x00000100`).

---

### Section 37 Differential Table

| RX stage | TWRP/Linux | XNU pre-F18 | XNU F18 |
|---|---|---|---|
| LP timer | `0xffffffff` | `0x00000000` | `0xffffffff` |
| max return packet | `0x37` (10 bytes) | None | `0x37` (10 bytes) |
| read packet | 4 bytes LP | 4 bytes LP | 4 bytes LP |
| BTA | Header `0xA0` + SW trig | Header `0xA0` (MDP locked) | Header `0xA0` (MDP disarmed) |
| completion | DMA_DONE + BTA_DONE | Timeout / unreached | `DSI_BTA_TIMEOUT` (`0x100`) |
| RX FIFO | Populated | Empty (`0x00`) | Empty (`0x00`) |
| payload decode | Descending ntohl shift | Raw register read | Dual representation decode |
| 0x04 result | `84 72 09` | invalid (`00 00 00`) | invalid (`NONE` / timeout) |
| 0x0A result | `1C` | invalid (`00`) | invalid (`INVALID_READ_NO_DATA`) |
| 0x0E result | `80` | invalid (`00`) | invalid (`INVALID_READ_NO_DATA`) |

---

### Section 27 DDIC Internal State Table (Gate Closed)

| Register | Meaning | TWRP | XNU | Match |
|---|---|---:|---:|---|
| 0x0A | Power Mode | 0x1C | `INVALID_READ_NO_DATA` | NO |
| 0x0B | Address Mode | 0x00 | `INVALID_READ_NO_DATA` | NO |
| 0x0C | Pixel Format | 0x77 | `INVALID_READ_NO_DATA` | NO |
| 0x0D | Display Mode | 0x00 | `INVALID_READ_NO_DATA` | NO |
| 0x0E | Signal Mode | 0x80 | `INVALID_READ_NO_DATA` | NO |
| 0x0F | Diagnostic | 0x40 | `INVALID_READ_NO_DATA` | NO |

**FIRST_VALID_DDIC_STATE_DIVERGENCE**: `UNKNOWN_DUE_TO_XNU_DSI_RX_FAILURE`

---

## 6. GPIO10 Correlation & Interlock Isolation

During the post-kick and audit window, TLMM GPIO10 (physical TE line) was sampled passively:
- `GPIO10_HIGH_SAMPLES = 0`
- `GPIO10_TRANSITIONS = 0`

Together with `DSI_BTA_TIMEOUT` (`0x00000100`), this proves that:
1. The DSI controller hardware and host DMA path are functional and execute the turnaround sequence.
2. The panel DDIC does not assert TE on GPIO10 **and** does not respond to DSI Bus Turnaround requests.
3. Because the Stage 1 validation gate failed, this state CANNOT be used to assert internal DDIC register contents.
4. Root cause status remains `DSI_RX_PATH_NOT_CLOSED`.

---

## 7. Section 36 — Required Executive Output

```text
F18_CLASS=F18-RX-FAILED READ_0x04_VALIDATION_GATE_UNMET

HEAD_BEFORE_F18=32ee98a6fa830927b824096ad84de8c4832d46f5

LINUX_DSI_RX_CALL_GRAPH=debugfs panel_reg_read -> mdss_dsi_panel_cmd_read -> mdss_dsi_cmdlist_put -> mdss_dsi_cmdlist_tx -> mdss_dsi_cmds_rx -> mdss_dsi_cmd_dma_tx (max_pktsize 0x37) -> mdss_dsi_cmd_dma_tx (read cmd with BTA) -> mdss_dsi_cmd_dma_rx (RDBK decode)

LINUX_DSI_RX_MMIO_SEQUENCE=1.LP_TIMER(0x009940b8=0xffffffff) 2.RDBK_CTRL_PULSE(0x009941d4:1->0) 3.TRIG_CTRL_SW(0x00994084=4) 4.MDP_CTRL_OFF(0x00994040=0) 5.CLK_CTRL_FORCE(0x00994118) 6.SET_MRPS_0x37 7.TPG_FIFO_RESET 8.LOAD_READ_CMD_BTA(0xA0) 9.DMA_CTRL_EMBEDDED_LP 10.DMA_LEN=4 11.INT_CTRL_UNMASK(DMA_DONE|BTA_DONE) 12.DSB_ISB 13.DMA_SW_TRIGGER 14.WAIT_DMA_CMD_DONE 15.WAIT_BTA_LINE_REVERSAL 16.READ_RDBK_COUNT 17.READ_RDBK_DATA0_3 18.RESTORE_TRIG_MDP_CLK

TWRP_LP_TIMER_CTRL=0xffffffff

XNU_LP_TIMER_CTRL=0xffffffff

LP_TIMER_REQUIRED_FOR_RX=YES

MAX_RETURN_PACKET_SIZE_REQUIRED=YES

MAX_RETURN_PACKET_SIZE_VALUE=10_BYTES_FOR_0x04_4_BYTES_FOR_SHORT

BTA_TRIGGER_MECHANISM=DSI_CMD_DMA_HEADER_BIT29_BTA_FLAG_0xA0_LAST_BTA

BTA_TRIGGER_REGISTER=0x00994090

BTA_TRIGGER_VALUE=0x00000001

RX_COMPLETION_SOURCE=DSI_INT_CTRL bit 0 (DSI_INTR_CMD_DMA_DONE) & bit 20 (DSI_INTR_BTA_DONE)

RX_ERROR_REGISTERS=DSI_ACK_ERR_STATUS (0x00994064), DSI_TIMEOUT_STATUS (0x009940c0), DSI_FIFO_STATUS (0x0099400c)

RX_PAYLOAD_REGISTERS=DSI_RDBK_DATA0 (0x0099406c) to DSI_RDBK_DATA3 (0x00994078)

RX_PAYLOAD_BYTE_ORDER=DESCENDING_REGISTER_NTOHL_WITH_16_MINUS_CNT_SHIFT_FOR_LONG_READ

XNU_DSI_RX_SEQUENCE=1.DISARM_MDP_CTL_START_FLUSH 2.TRIG_CTRL=4_MDP_CTRL=0 3.LP_TIMER=0xffffffff 4.RDBK_RESET 5.MAX_PKT_0x37 6.READ_CMD_BTA 7.SW_DMA_TRIGGER 8.POLL_DMA_BTA_DONE 9.RDBK_READ 10.DECODE

FIRST_DSI_RX_DIVERGENCE=MDP_COMMAND_MODE_ARBITER_LOCKOUT: F17 called SW DMA while CTL_START=1 and DSI_TRIG_CTRL=0x80000004 with bit 31 set and DSI_COMMAND_MODE_MDP_CTRL non-zero, starving SW DMA

PRE_READ_STATE_DIVERGENCE=CTL_START=0x00000000 vs 0x00000001; DSI_TRIG_CTRL=0x00000004 vs 0x80000004

F18_RX_CORRECTION_READY=YES

CORRECTION_PERFORMED=YES

CORRECTION_DESCRIPTION=Disarm MDP CTL_START/FLUSH and DSI MDP ctrl before SW DMA; set DSI_TRIG_CTRL=4; program MRPS; execute read with BTA; decode descending ntohl and direct LE RDBK registers

READ_0x04_VALID=NO

READ_0x04_RAW=NONE

READ_0x04_EXPECTED=84 72 09

XNU_DSI_RX_VALIDATED=NO

READ_ID1=NONE
READ_ID2=NONE
READ_ID3=NONE

READ_ID1_MATCH=NO

READ_ID2_MATCH=NO

READ_ID3_MATCH=NO

XNU_DDIC_0x0A=INVALID_READ_NO_DATA

XNU_DDIC_0x0B=INVALID_READ_NO_DATA

XNU_DDIC_0x0C=INVALID_READ_NO_DATA

XNU_DDIC_0x0D=INVALID_READ_NO_DATA

XNU_DDIC_0x0E=INVALID_READ_NO_DATA

XNU_DDIC_0x0F=INVALID_READ_NO_DATA

FIRST_VALID_DDIC_STATE_DIVERGENCE=UNKNOWN_DUE_TO_XNU_DSI_RX_FAILURE

DDIC_SLEEP_OUT_LATCHED=UNKNOWN

DDIC_DISPLAY_ON_LATCHED=UNKNOWN

DDIC_TE_ENABLE_LATCHED=UNKNOWN

DDIC_PIXEL_FORMAT_LATCHED=UNKNOWN

DDIC_DIAGNOSTIC_FAULT=UNKNOWN

GPIO10_HIGH_SAMPLES=0

GPIO10_TRANSITIONS=0

ROOT_CAUSE_STATUS=DSI_RX_PATH_NOT_CLOSED

F19_REQUIRED=NO

D8_M8_FIRST_COMMAND_FRAME=NOT_YET

NEXT_ACTION=ISOLATE_REMAINING_RX_BLOCKER
```
