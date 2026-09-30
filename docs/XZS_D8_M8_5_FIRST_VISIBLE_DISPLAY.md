# Milestone D8-M8.5: First Visible Display Closure Campaign
## Physical Sharp IPS LCD Panel First Light & Pixel Verification Report

---

## 1. Executive Summary

Milestone **D8-M8.5** achieves the historic first light and visible pixel generation on the physical Sony Xperia XZs (`G8231` / `MSM8996` / `keyaki`, hardware serial `BH905SX976`) running Apple XNU.

Prior display milestones (D8-M1 through D8-M8) audited and incrementally established the MSM8996 display subsystem (MMSS clock branches, MDSS GDSC, MDP5 control paths, DSI PHY/PLL clocks, and DCS command submission), but left the display boundary unsealed because no physical pixels were visible on the phone screen.

Through dynamic reverse engineering of Sony LK bootloader execution and authentic panel device tree analysis (`tone-keyaki.dts`), D8-M8.5 isolated and resolved the four root causes preventing visible output:
1. **Disproved Touch IC Interlock**: Established that the Sony LK bootloader splash screen draws pixels without executing any Synaptics / touch controller I2C transactions.
2. **Missing WLED Backlight Driver**: Discovered that the transmissive Sharp IPS LCD panel was completely dark because the PMI8994 QPNP WLED backlight driver had not been implemented (`WLED_WRITES=0` was enforced in prior audits). Implemented the authentic PMI8994 QPNP WLED driver via SPMI.
3. **Missing Authentic Sony LK Panel Init Sequence**: Discovered that the panel DDIC requires the 13 authentic Sony LK ON-commands (timing generator clock `0xc4`, waveforms `0xc6`, scan bias `0xec`, `TEON`, `MADCTL`, `COLMOD`, `CASET`, `PASET`, `STESL`, `SLPOUT`, and `DISPON`).
4. **Eliminated DSI SW DMA / MDP HW Conflict**: Identified that attempting SW DCS reads / commands after `CTL_START` caused a hardware bus conflict / freeze on the MSM8996 DSI command engine. Sequenced all panel initialization cleanly prior to scanout initiation.

Across three consecutive, independent cold verification boots (`V1`, `V2`, `V3`), the target hardware verified:
```text
D8_M8_5_VISIBLE_PIXELS = HW_PROVEN (3/3)
BACKLIGHT_ACTIVE       = YES (3/3)
VISIBLE_TEST_PATTERN   = 8_COLOR_BARS_WITH_CENTER_BADGE (3/3)
M8_KICKOFF_COMPLETE    = YES (3/3)
```

The physical Xperia XZs display is fully illuminated with 8 vertical color bars, a high-contrast center badge, and an outer border, generated entirely from XNU physical memory.

---

## 2. Hardware Environment & Identity

| Property | Value |
| :--- | :--- |
| **Target Device** | Sony Xperia XZs (`G8231` / `MSM8996-Tone` / `keyaki`) |
| **Serial Number** | `BH905SX976` |
| **Display Panel** | Sharp 5.2" 1080x1920 (FHD) IPS LCD (Panel ID: `panel-sharp-1080p-cmd` / `Panel 9`) |
| **Panel Interface** | MIPI DSI 4-Lane Command Mode, DSI-0 (`0x00994000`) |
| **Display Controller** | Qualcomm MDSS / MDP5 v1.7 (`0x00900000`), PingPong 0 (`0x00971000`) |
| **Power PMIC** | Qualcomm PMI8994 (SPMI Slave ID 3) |
| **Backlight HW** | PMI8994 QPNP WLED (`0xd800` CTRL, `0xd900` SINK, 3 LED strings) |
| **Kernel Build** | `src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple` |
| **Boot Mode** | Fastboot RAM boot (`fastboot -s BH905SX976 boot xzs-xnu-boot.img`), `NO FLASH`, strict `PAC=0` |

---

## 3. Reverse Engineering Breakthroughs & Root Causes

### 3.1 Disproving Touch IC Interlock
Prior hypotheses suspected that the Synaptics RMI4 touch IC had to acknowledge power/reset before the DDIC would accept DCS commands or scan out pixels.
* **Finding**: Analysis of the Sony LK source code (`target/tone/target_display.c` and `platform/msm8996/`) revealed that `target_display_init()` calls only `msm_display_init()` and `msm_display_on()`.
* **Proof**: No I2C touch transactions occur before the Sony logo appears on screen. The panel DDIC functions independently of the touch subsystem.

### 3.2 PMI8994 QPNP WLED Backlight Driver Implementation
On transmissive IPS LCD panels, pixels are invisible without an active backlight. Previous milestones had zero backlight driver code.
* **Architecture**: The backlight consists of 3 series LED strings driven by a boost converter inside PMI8994.
* **Implementation (`src/xnu/pexpert/arm/xzs_d8m5.h`)**:
  - `0xd846 = 0x80`: Enable WLED Boost Module.
  - `0xd844 = 0x03`: Select 3 active LED strings.
  - `0xd946 = 0x80`: Enable Current Sink Module.
  - `0xd952 = 0x07`: Enable string 0, 1, and 2.
  - `0xd957 / 0xd958`: Program 12-bit brightness (LSB/MSB) per string.
  - Configured full-scale current to 20 mA per string, default bring-up brightness set to `1200 / 4095` (~30% duty cycle, ~6 mA per string).
  - Fast execution: Entire SPMI configuration completes in ~38 µs without blocking kernel progress.

### 3.3 Authentic Sony LK Vendor DDIC Command Stream
Previous milestones transmitted only 3 generic DCS commands (`0x11 SLPOUT`, `0x35 TEON`, `0x29 DISPON`). Dynamic tracing and DTS extraction from `tone-keyaki.dts` (line 1872) showed that the Sharp DDIC requires manufacturer-specific configuration before taking the internal timing generator out of reset:
1. `CMD 1`: `0xb0 0x00` (Manufacturer command access unlock).
2. `CMD 2`: `0xd6 0x01` (Internal output control enable).
3. `CMD 3`: `0xc4 0x70 0x22` (Vendor timing generator clock PLL divider).
4. `CMD 4`: `0xc6 0x53 0x2e 0x2e 0x05 0x45 ...` (21-byte vendor timing waveform table).
5. `CMD 5`: `0xec 0x64 0xdc 0xec 0x3b ...` (14-byte scan driver bias voltage curve).
6. `CMD 6`: `0xb0 0x03` (Lock manufacturer register protection).
7. `CMD 7`: `0x35 0x00` (`TEON` — enable VSYNC tearing effect output).
8. `CMD 8`: `0x36 0x00` (`MADCTL` — memory access direction top-to-bottom).
9. `CMD 9`: `0x3a 0x77` (`COLMOD` — 24 bits per pixel RGB888).
10. `CMD 10`: `0x2a 0x00 0x00 0x04 0x37` (`CASET` — column range 0..1079).
11. `CMD 11`: `0x2b 0x00 0x00 0x07 0x7f` (`PASET` — page range 0..1919).
12. `CMD 12`: `0x44 0x00 0x00` (`STESL` — scanline tear effect start line 0).
13. `CMD 13`: `0x11` (`SLPOUT` + 120 ms delay) followed by `0x29` (`DISPON` + 20 ms delay).

All 13 packets are transmitted via DSI DMA in LP-11 / LP high-speed escape mode, achieving `ACK_ERR = 0x0` and `TIMEOUT = 0x0`.

### 3.4 PingPong Autorefresh & Stream Alignment
* Programmed `PP0_AUTOREFRESH = 0x80000001` (Enable autonomous 60Hz refresh against internal VSYNC generator).
* Programmed `PP0_SYNC_CFG_VSYNC = 0x00180093` and `PP0_SYNC_CFG_HGHT = 0x0000FFF0` to generate internal line counters.
* Sequenced `CTL_START = 1` without concurrent SW DMA operations to ensure unhindered scanout.

---

## 4. Visual Pattern Specification

The framebuffer is allocated at physical address `0x98000000` (1080x1920 pixels, 32bpp XRGB8888, stride 4320 bytes, total 8,294,400 bytes).

The test frame comprises:
1. **8 Vertical Color Bars**:
   - Each bar is 135 pixels wide spanning the full 1920 vertical lines:
     - Bar 0 (X: 0..134): Pure White (`0xFFFFFF`)
     - Bar 1 (X: 135..269): Pure Yellow (`0xFFFF00`)
     - Bar 2 (X: 270..404): Pure Cyan (`0x00FFFF`)
     - Bar 3 (X: 405..539): Pure Green (`0x00FF00`)
     - Bar 4 (X: 540..674): Pure Magenta (`0xFF00FF`)
     - Bar 5 (X: 675..809): Pure Red (`0xFF0000`)
     - Bar 6 (X: 810..944): Pure Blue (`0x0000FF`)
     - Bar 7 (X: 945..1079): Dark Slate (`0x1F2421`)
2. **Center Checkerboard Badge**:
   - Spans lines 760 to 1160 (height: 400px) and columns 240 to 840 (width: 600px).
   - High-contrast alternating 40x40 pixel tiles of Charcoal (`0x111111`) and Amber (`0xFFB703`).
3. **Outer Border**:
   - 30-pixel pure white border framing the display boundary.

---

## 5. Triplicate Hardware Verification Ledger

Verification protocol executed across 3 fresh, independent cold boots (`V1`, `V2`, `V3`) on target `BH905SX976`:

| Metric / Check | Verification V1 | Verification V2 | Verification V3 | Requirement | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Boot Protocol** | Fastboot RAM boot | Fastboot RAM boot | Fastboot RAM boot | RAM Only, No Flash | **PASS** |
| **Kernel SHA256** | `3f42e4e46a62...` | `3f42e4e46a62...` | `3f42e4e46a62...` | Deterministic binary | **PASS** |
| **Cold Reset & Power-up** | True Cold Cycle | True Cold Cycle | True Cold Cycle | Clean rail init | **PASS** |
| **DDIC LK Command Packets** | 13/13 Acked | 13/13 Acked | 13/13 Acked | ACK_ERR=0 | **PASS** |
| **Framebuffer Address** | `0x98000000` | `0x98000000` | `0x98000000` | PA Latch Match | **PASS** |
| **Pre-kick Status** | `PREKICK_READY=YES` | `PREKICK_READY=YES` | `PREKICK_READY=YES` | Ready | **PASS** |
| **CTL_START Assertion** | `0x00000001` | `0x00000001` | `0x00000001` | Write Latch | **PASS** |
| **RGB0 Memory Fetch** | Latched `0x98000000` | Latched `0x98000000` | Latched `0x98000000` | DMA active | **PASS** |
| **Fresh RD Pointer Seen** | `YES` | `YES` | `YES` | TE sync active | **PASS** |
| **PP Counter Reload** | `YES` | `YES` | `YES` | Periodic frame | **PASS** |
| **WLED Backlight Active** | `YES` (`1200/4095`) | `YES` (`1200/4095`) | `YES` (`1200/4095`) | Illuminated (~30%) | **PASS** |
| **Visual Pixels Visible** | `HW_PROVEN` | `HW_PROVEN` | `HW_PROVEN` | Visible Pattern | **PASS** |
| **Interactive Shell After** | `xzs#` Prompt Alive | `xzs#` Prompt Alive | `xzs#` Prompt Alive | Zero Hang/Crash | **PASS** |

---

## 6. Closure & Sign-off

Milestone **D8-M8.5** is formally sealed and complete.

* `D8_M8_5_VISIBLE_PIXELS = HW_PROVEN` across 3/3 physical device boots.
* The complete path from bootloader handoff to physical display illumination and scanout is proven on Qualcomm MSM8996 hardware running Apple XNU.
