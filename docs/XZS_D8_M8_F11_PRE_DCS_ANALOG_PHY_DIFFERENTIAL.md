# XZS D8-M8 F11: Pre-Panel Analog State, DSI PHY Lifecycle & Command Transport Differential

## 1. Executive Summary & Classification

- **Investigation**: Complete pre-DCS lifecycle audit comparing authentic Sony LK (`aboot.img`) and Device Tree (`keyaki.dts`) against XNU display driver baseline.
- **Key Discovery**: Uncovered the binary mechanism of `qcom,mdss-dsi-lp11-init` in `aboot.img`:
  - When `lp11-init = 1`, `panel_power_on()` intentionally holds Panel Reset (`GPIO8`) **LOW** during VDDIO, LAB, and IBB power ramp.
  - In `mdss_dsi_panel_initialize()`, the DSI host is initialized (`DSI_CTRL=0x1f5`, `DSI_CLK_CTRL=0x3f`), actively asserting **LP-11** on all clock and data lanes.
  - Reset (`GPIO8`) is released **ONLY AFTER** lanes are established in LP-11 (`RESET_RELEASE_RELATIVE_TO_LP11 = AFTER_LP11_ESTABLISHED`).
  - Command 13 (`SLPOUT 0x11`) and Post-Command (`DISPON 0x29`) use DCS Long Write (`dtype = 0x39`).
  - Full PMIC LAB/IBB configuration registers (`0x1DE40`-`0x1DF40`) set current limits (200mA/800mA), precharge (300µs), soft-start (800µs), and pull-downs.
- **Hardware Run on Target `BH905SX976`**:
  - Exactly one conditional RAM boot executed matching Sony LK LP-11 reset order, full LAB/IBB configuration, and DCS `0x39` packet formats.
  - Result: `GPIO10_HIGH_SAMPLES = 0`, `GPIO10_TRANSITIONS = 0`, `PHYSICAL_TE_RESTORED = NO`.
- **Classification**:
  - `F11_CLASS = F11-A5 LP11_RESET_ORDER_INSUFFICIENT`
  - `F11_CORRECTION_CAUSAL_TO_PHYSICAL_TE = NO_HW_PROVEN`
  - `D8_M8_FIRST_COMMAND_FRAME = NOT_YET`

---

## 2. Evidence Hygiene & Terminology

Per Section 1 requirements:
- `VENDOR_SPECIFIC_DT_COMMANDS_PRESENT = YES`: The panel on-commands include vendor-specific DDIC registers (`0xB0`, `0xD6`, `0xC4`, `0xC6`, `0xEC`).
- `HARDCODED_VENDOR_COMMANDS_IN_LK_BINARY = NO`: LK contains no hardcoded panel commands; commands are parsed at boot from the flattened device tree (`keyaki.dts`).

---

## 3. Sony LK Pre-DCS Call Graph & Binary Disassembly

Disassembly of authentic Sony LK (`aboot.img`, base `0xaa000000`):

```text
somc_display_init (0xaa03eff4)
  │
  ├──> target_display_init (0xaa03efa8)
  │      └──> msm_display_init (0xaa01eedc)
  │             ├──> [0xaa01ef20] panel_power_on (0xaa03ed3c)
  │             │      ├──> Assert VDDIO: GPIO51=1, GPIO50=1 (mdelay 10)
  │             │      ├──> Enable LAB rail: qpnp_lab_ibb_enable(1, 0) -> +5.6V (mdelay 10)
  │             │      ├──> Enable IBB rail: qpnp_lab_ibb_enable(0, 1) -> -5.6V (mdelay 10)
  │             │      └──> CHECK [pdata, #0xcc] (qcom,mdss-dsi-lp11-init):
  │             │             IF lp11-init == 1:
  │             │                 SKIP panel_set_gpio_seq(1)
  │             │                 (GPIO8 Panel Reset held LOW!)
  │             │
  │             ├──> [0xaa01ef58] clk_func (0xaa01e850: display clocks enabled)
  │             ├──> [0xaa01ef74] power_func (0xaa01e918: DSI PHY regulator enabled)
  │             ├──> [0xaa01ef90] pll_enable_func (0xaa01e878: DSI PLL locked)
  │             ├──> [0xaa01efb0] ctrl_power_func (0xaa01e980: DSI controller power on)
  │             │
  │             └──> [0xaa01f038] msm_display_config (0xaa01eaec)
  │                    └──> mdss_dsi_panel_initialize (0xaa01fe54)
  │                           ├──> [0xaa01fe9c] mdss_dsi_host_init (0xaa01f6cc)
  │                           │      ├──> DSI_SOFT_RESET = 1 -> 0
  │                           │      ├──> DSI_CLK_CTRL = 0x3f
  │                           │      ├──> DSI_TRIG_CTRL = 0x4 (SW trigger)
  │                           │      ├──> DSI_CTRL = 0x1f5 (Enable DSI host)
  │                           │      └──> LANES DRIVEN ACTIVELY INTO LP-11!
  │                           │
  │                           ├──> [0xaa01fecc] pre_init_func (0xaa03c830)
  │                           │      └──> panel_set_gpio_seq (0xaa03c6d4)
  │                           │             ├──> Pulse GPIO8: LOW 10ms -> HIGH 10ms
  │                           │             │    (RESET RELEASED UNDER ACTIVE LP-11!)
  │                           │             └──> Pulse GPIO89: LOW 2ms -> HIGH 5ms (settle 40ms)
  │                           │
  │                           ├──> [0xaa01ffb0] DSI_LANE_CTRL bit 28 = 1 (force_clk_lane_hs)
  │                           └──> [0xaa01ffc8] mdss_dsi_cmds_send (0xaa01f9c8: 13 on-commands)
```

---

## 4. Sony LK Pre-DCS Timeline Ledger

| Step | Function / Address | Subsystem | Action / Target Register | Value / Pulse | Delay | Condition |
|:---:|:---:|:---:|:---|:---:|:---:|:---|
| 1 | `0xaa03ed4c` | TLMM | `GPIO51_CFG` / `IN_OUT` | 1 (VDDIO 1.8V) | 0 | Unconditional |
| 2 | `0xaa03ed60` | TLMM | `GPIO50_CFG` / `IN_OUT` | 1 (VDDIO 1.8V) | 10 ms | Unconditional |
| 3 | `0xaa041320` | SPMI PMIC | `LAB_MODULE_RDY` / `EN` | `0x80` (+5.6V) | 10 ms | Unconditional |
| 4 | `0xaa041344` | SPMI PMIC | `IBB_MODULE_RDY` / `EN` | `0x80` (-5.6V) | 10 ms | Unconditional |
| 5 | `0xaa03ef48` | TLMM | `GPIO8_IN_OUT` (Panel Reset) | Hold 0 (LOW) | 0 | `lp11-init == 1` |
| 6 | `0xaa01e850` | GCC | `MDSS_MDP_CLK` / `AXI` | Turn ON | 0 | Clocks on |
| 7 | `0xaa01e918` | DSI PHY | `DSI_PHY_REGULATOR_CTRL` | 1 | 5 ms | Regulator on |
| 8 | `0xaa01e878` | DSI PLL | `DSI_PLL_GLB_CFG` | Lock PLL | 2 ms | PLL lock wait |
| 9 | `0xaa01f6d8` | DSI Host | `DSI_SOFT_RESET` | 1 -> 0 | 1 ms | Host reset |
| 10 | `0xaa01f700` | DSI Host | `DSI_CLK_CTRL` | `0x3f` | 0 | Clocks enabled |
| 11 | `0xaa01f710` | DSI Host | `DSI_TRIG_CTRL` | `0x4` | 0 | SW Trigger |
| 12 | `0xaa01f720` | DSI Host | `DSI_CTRL` | `0x1f5` | 10 ms | **LP-11 Active** |
| 13 | `0xaa03c888` | TLMM | `GPIO8_IN_OUT` (Panel Reset) | Low 10ms -> High 10ms | 20 ms | In `pre_init_func` |
| 14 | `0xaa03c8a4` | TLMM | `GPIO89_IN_OUT` (Touch Reset)| Low 2ms -> High 5ms | 47 ms | Settle 40ms |
| 15 | `0xaa01ffb0` | DSI Host | `DSI_LANE_CTRL` | `0x10000000` (bit 28) | 0 | Force CLK HS |
| 16 | `0xaa01ffc8` | DSI Host | `DSI_DMA_CMD_OFFSET` | Transmit CMD 1 (`0xB0`) | 0 | First DCS TX |

- **`PRE_DCS_LK_WRITE_COUNT`**: 38 register operations
- **`PRE_DCS_XNU_MATCHED_COUNT`**: 38 register operations
- **`PRE_DCS_XNU_UNMATCHED_COUNT`**: 0

---

## 5. DSI PHY Lifecycle & LP-11 Analysis

### 5.1 LP-11 State & Reset Timing
- **Qualcomm Specification**: Some DDIC ASICs latch bus interface mode (DSI vs DPI/DBI) or lane count precisely upon the rising edge of Hardware Reset (`RESET_N`). If data lanes are floating or in `ULPS` (Ultra-Low Power State) rather than `LP-11` (Stop State), the DDIC fails to initialize its command receiver logic.
- **Sony LK Implementation**:
  - `SONY_LK_LP11_TIMING = ESTABLISHED_BEFORE_PANEL_RESET_RELEASE`
  - In LK, `mdss_dsi_host_init()` enables `DSI_CTRL` (`0x1f5`), which forces transceiver LP-11 on CLK and DATA lanes.
  - `pdata->pre_init_func()` executes **after** host init, driving `GPIO8` high while lanes are held firmly at LP-11 (`1.2V`).
- **XNU F1-F10 Implementation**:
  - `XNU_LP11_TIMING (F1-F10) = ESTABLISHED_AFTER_PANEL_RESET_RELEASE`
  - XNU previously released `GPIO8` during `panel_power_on` before DSI host was initialized.
- **F11 Alignment**:
  - XNU F11 deferred `GPIO8` and `GPIO89` reset pulse sequence until after DSI host configuration and LP-11 assertion.
  - `RESET_LP11_ORDER_MATCH = YES`.

---

## 6. DCS Transport Flags & Encoding Matrix

Comparison of all 13 panel on-commands between Sony LK (DT) and XNU:

| # | Command Name | LK Type | LK LP/HS | LK Flags | XNU Type | XNU LP/HS | Match |
|:---:|:---|:---:|:---:|:---:|:---:|:---:|:---:|
| 1 | `0xB0 0x00` (Vendor Pass 1) | `0x29` (Generic Long) | LP | `last=1, vc=0, ack=0` | `0x29` | LP | YES |
| 2 | `0xD6 0x01` (Vendor Pass 2) | `0x29` (Generic Long) | LP | `last=1, vc=0, ack=0` | `0x29` | LP | YES |
| 3 | `0xC4 0x70 0x22` (Vendor) | `0x29` (Generic Long) | LP | `last=1, vc=0, ack=0` | `0x29` | LP | YES |
| 4 | `0xC6 ...` (Vendor 21B) | `0x29` (Generic Long) | LP | `last=1, vc=0, ack=0` | `0x29` | LP | YES |
| 5 | `0xEC ...` (Vendor 14B) | `0x29` (Generic Long) | LP | `last=1, vc=0, ack=0` | `0x29` | LP | YES |
| 6 | `0xB0 0x03` (Vendor Lock) | `0x29` (Generic Long) | LP | `last=1, vc=0, ack=0` | `0x29` | LP | YES |
| 7 | `0x35 0x00` (TEON) | `0x39` (DCS Long) | LP | `last=1, vc=0, ack=0` | `0x39` | LP | YES |
| 8 | `0x36 0x00` (MADCTR) | `0x39` (DCS Long) | LP | `last=1, vc=0, ack=0` | `0x39` | LP | YES |
| 9 | `0x3A 0x77` (COLMOD) | `0x39` (DCS Long) | LP | `last=1, vc=0, ack=0` | `0x39` | LP | YES |
| 10 | `0x2A ...` (CASET 1080) | `0x39` (DCS Long) | LP | `last=1, vc=0, ack=0` | `0x39` | LP | YES |
| 11 | `0x2B ...` (PASET 1920) | `0x39` (DCS Long) | LP | `last=1, vc=0, ack=0` | `0x39` | LP | YES |
| 12 | `0x44 ...` (STESL Scanline 0) | `0x39` (DCS Long) | LP | `last=1, vc=0, ack=0` | `0x39` | LP | YES |
| 13 | `0x11` (SLPOUT) | `0x39` (DCS Long) | LP | `last=1, vc=0, ack=0, w=120ms` | `0x39` | LP | YES |
| Post | `0x29` (DISPON) | `0x39` (DCS Long) | LP | `last=1, vc=0, ack=0` | `0x39` | LP | YES |

- **`DCS_PACKET_BYTES_MATCH = YES`**
- **`DCS_PACKET_TYPES_MATCH = YES`** (CMD 13 and DISPON aligned to `0x39` DCS Long Write)
- **`DCS_LP_HS_MODE_MATCH = YES`** (All commands sent in low-power mode)
- **`DCS_DESCRIPTOR_FLAGS_MATCH = YES`**
- **`FIRST_DCS_TRANSPORT_DIVERGENCE = NONE`**

---

## 7. PMIC LAB/IBB Configuration Audit

Device Tree configuration in `keyaki.dts` specifies:
- `somc,qpnp-lab-limit-maximum-current = 0xC8` (200 mA)
- `somc,qpnp-ibb-limit-maximum-current = 0x320` (800 mA)
- `somc,qpnp-lab-soft-start = 0x320` (800 µs)
- `somc,qpnp-lab-max-precharge-time = 0x12C` (300 µs)
- `somc,qpnp-lab-pull-down-enable = True`
- `somc,qpnp-ibb-pull-down-enable = True`

### Comparison Table

| PMIC Register / Parameter | Sony LK Target | XNU F10 | XNU F11 | Match |
|:---|:---:|:---:|:---:|:---:|
| `LAB_CURRENT_LIMIT` (`0x1DE4B`) | `0x00` (200 mA) | Default | `0x00` (200 mA) | YES |
| `IBB_CURRENT_LIMIT` (`0x1DF4B`) | `0x03` (800 mA) | Default | `0x03` (800 mA) | YES |
| `LAB_PRECHARGE_CTRL` (`0x1DE4E`) | `0x83` (300 µs, En) | Default | `0x83` (300 µs, En) | YES |
| `LAB_SOFT_START_CTL` (`0x1DE4F`) | `0x03` (800 µs) | Default | `0x03` (800 µs) | YES |
| `LAB_PD_CTL` (`0x1DE48`) | `0x81` (Pull-down En) | Default | `0x81` (Pull-down En) | YES |
| `IBB_PD_CTL` (`0x1DF48`) | `0x81` (Pull-down En) | Default | `0x81` (Pull-down En) | YES |
| Voltage Setpoints | +5.6V / -5.6V | +5.6V / -5.6V | +5.6V / -5.6V | YES |

- **`LAB_FULL_CONFIG_MATCH = YES`**
- **`IBB_FULL_CONFIG_MATCH = YES`**
- **`ADDITIONAL_DISPLAY_ANALOG_RAILS = NONE`**

---

## 8. Analog / PHY Differential Matrix

| State Parameter | Sony LK | XNU F10 | Working TWRP | Classification |
|:---|:---:|:---:|:---:|:---:|
| LP-11 Timing | Before Reset Release | After Reset Release | Steady-state HS/LP | High-priority divergence (Closed F11) |
| Reset vs LP11 Order | Reset pulsed in LP-11 | Reset before LP-11 | Steady-state | High-priority divergence (Closed F11) |
| PHY Regulator | Enabled | Enabled | Enabled | MATCH |
| PHY Timing Control | Nominal v2 | Nominal v2 | Nominal v2 | MATCH |
| DSI PLL Lock | Locked (895 MHz) | Locked (895 MHz) | Locked (895 MHz) | MATCH |
| LAB Current / Soft-start | 200 mA / 800 µs | Default (PMIC reset) | 200 mA / 800 µs | Divergence (Closed F11) |
| IBB Current Limit | 800 mA | Default (PMIC reset) | 800 mA | Divergence (Closed F11) |
| DCS LP/HS Mode | LP Mode | LP Mode | LP Mode | MATCH |
| Packet Type (SLPOUT) | `0x39` (DCS Long) | `0x05` (DCS Short) | `0x39` (DCS Long) | Divergence (Closed F11) |
| Packet Type (DISPON) | `0x39` (DCS Long) | `0x05` (DCS Short) | `0x39` (DCS Long) | Divergence (Closed F11) |
| Transport Flags | `last=1, vc=0, ack=0` | `last=1, vc=0, ack=0` | `last=1, vc=0, ack=0` | MATCH |

---

## 9. F11 Hardware Execution Evidence (Target: `BH905SX976`)

### 9.1 Hardware Telemetry
The unified F11 test suite executed cleanly on `BH905SX976`:
- **Build Identity**: `c6ea4ee`
- **Bootloader Mode**: `fastboot boot` (RAM boot only, NO FLASH)
- **Power Precheck**: `LAB=0x80`, `IBB=0x80`, `VREG_OK`
- **Reset Sequence Execution**:
  - DSI host initialized and LP-11 state actively driven.
  - `GPIO8` reset pulse asserted (LOW 10ms -> HIGH 10ms) while in LP-11.
  - `GPIO89` touch reset pulse asserted (LOW 2ms -> HIGH 5ms -> settle 40ms).
- **Command Transmission**:
  - All 13 on-commands transmitted via DMA.
  - CMD 13 (`SLPOUT`) sent via DCS `0x39` with 120 ms hardware sleep.
- **First Frame Bootstrap**:
  - `PP_AUTOREFRESH` armed to `0x80000001`.
  - `CTL_START` committed to Ping-Pong engine.
  - `DISPON` (`0x39`) dispatched after kickoff.
- **Physical TE Sampling**:
  - Dense sampling window: 13,850 samples across ~254 ms.
  - `GPIO10_HIGH_SAMPLES = 0`
  - `GPIO10_TRANSITIONS = 0`
  - `PHYSICAL_TE_RESTORED = NO`
- **Pipeline Progression**:
  - `FRESH_RD_PTR = NO`
  - `PP_COUNTER_RELOAD = NO`
  - `PP_LINE = 0`
  - `PP_OUT = 0`
  - `PP0_DONE = NO`
  - `DSI_BUSY = NO`
  - `CMD_MDP_DONE = NO`

---

## 10. Conclusion & Strategic Impact

1. **Definitive Hardware Proof**:
   - Replicating the authentic Sony LK LP-11 before reset order (`qcom,mdss-dsi-lp11-init`), full PMIC LAB/IBB current/soft-start configuration, and DCS `0x39` packet encoding is **NOT SUFFICIENT** to awaken physical TE pulses on `GPIO10`.
   - `F11_CORRECTION_CAUSAL_TO_PHYSICAL_TE = NO_HW_PROVEN`.
2. **Exclusion of Pre-DCS Transport and Analog Hypotheses**:
   - All external pre-DCS state (rails, clocks, PHY timing, LP-11 reset ordering, packet types, DMA flags) now matches Sony LK byte-for-byte and sequence-for-sequence.
   - The remaining unknown is isolated to DDIC-internal behavior or external hardware line condition (e.g., touch IC interlock on GPIO89 or vendor-specific register configuration).
3. **Next Recommended Action**:
   - `INVESTIGATE_DDIC_INTERNAL_REGISTER_OR_TOUCH_INTERLOCK`: Investigate whether Keyaki DDIC requires a specific vendor sequence register unlock or if touch panel handshake lines hold DDIC TE asserted.
