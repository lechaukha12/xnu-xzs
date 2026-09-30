# xnu-xzs

> Native Apple XNU Kernel on Sony Xperia XZs (Qualcomm MSM8996)

---

## Mission

**xnu-xzs** is primarily a hardware and platform porting project.

Its long-term research goal is to determine how far an older authentic **Apple iOS userland** can be brought up on **Sony Xperia XZs** hardware by combining:
- **Native XNU**: Canonical Darwin/XNU kernel running bare-metal on Qualcomm silicon.
- **Xperia/MSM8996 Platform Support**: Clocks, power management, interrupts, MMIO, and bus infrastructure.
- **Native Hardware Drivers**: IOKit drivers for storage, display, touch, USB, and power subsystems.
- **Apple-Facing Compatibility Services (`XZSAppleCompat`)**: IOKit contracts, platform properties, and device topology expected by Apple userland.
- **Darwin/iOS Userland Compatibility**: Mach traps, BSD syscalls, Mach IPC, dyld, launchd, and core frameworks.

> [!IMPORTANT]
> **Mission Clarity**:
> This project is explicitly **NOT** attempting to build an unrelated new mobile operating system.
> The custom userspace components (PID1 skeleton, `/bin/sh` interactive shell) exist strictly as:
> - Bring-up infrastructure
> - Low-level debugging environment
> - Driver-development environment
> - Hardware-validation environment
>
> The ultimate research objective is authentic old-iOS userland execution on Xperia hardware.

---

## Target Hardware

| Component | Hardware Specification |
| :--- | :--- |
| **Device** | Sony Xperia XZs (Model G8231, Platform Tone, Board Keyaki) |
| **SoC** | Qualcomm Snapdragon 820 (MSM8996SG / MSM8996 Pro) |
| **CPU Cores** | Quad-core Qualcomm Kryo ARMv8.0-A (2x Silver @ 1.59 GHz + 2x Gold @ 2.15 GHz) |
| **DRAM** | 4 GB LPDDR4 (Base physical address `0x80000000`) |
| **Interrupt Controller** | ARM GICv3 (Distributor `0x09bc0000`, Redistributors `0x09c00000`) |
| **Hardware Timer** | ARM Generic Timer (19.200 MHz, PPI 27 virtual / PPI 30 physical) |
| **Serial Console** | Qualcomm BLSP2 UARTDM UART2 (`0x075b0000`, 115200 8N1) |
| **Internal Storage** | Samsung BJNB4R 32GB eMMC 5.1 on Qualcomm SDCC1 / SDHCI (`0x07464900`) |
| **Display** | 5.2" 1080x1920 IPS LCD (DSI / MDP5) |
| **Boot Mechanism** | Sony S1 ABOOT Fastboot (`fastboot boot boot.img`) |

---

## Safety constraints

```text
NO FLASH
fastboot boot only
manual Sony force shutdown for recovery
do not use xzs# reboot
no invasive hardware modification
```

Recovery is power-off by holding the Sony keys, then fastboot, then `fastboot boot`. The shell command `xzs# reboot` is not a recovery path.

## Current milestones

```text
Phase      Area                              Status
-------------------------------------------------------
Phase A    Native XNU entry                  COMPLETE
Phase B    Platform bring-up                 COMPLETE
Phase C    SMP                               COMPLETE
D1         BSD/VFS                           COMPLETE
D2         eMMC                              COMPLETE
D3         GPT                               COMPLETE
D4         block/bdev                        COMPLETE
D5         XZSFS                             COMPLETE
D6         PID1 / EL0                        COMPLETE
D7         Interactive shell / external exec COMPLETE

D8-M1      Display topology/MMIO             COMPLETE
D8-M2      Display power/core clocks         COMPLETE
D8-A0..A3  Display audit/tooling             COMPLETE
D8-M3      DSI PLL/clocks/14nm PHY           COMPLETE
D8-M4      DSI host/controller               COMPLETE
D8-P1      TLMM GPIO prerequisite            COMPLETE
D8-P2      SPMI + LAB/IBB power rails        COMPLETE
D8-M5      Panel power/reset sequence        COMPLETE
D8-M6      Panel vendor/DCS init sequence    COMPLETE
D8-M8      MDP scanout / command transport   COMPLETE (AUTONOMOUS VSYNC SCANOUT)
D8-M8.5    First Visible Display (WLED/DCS)  COMPLETE (PHYSICAL PIXELS HARDWARE PROVEN)
```

Tags that name durable milestones: `xzs-d7t1-complete`, `xzs-d7t2-full-complete`, `xzs-d8-m1-complete`, `xzs-d8-m2-complete`, `xzs-d8m3-display-pll-phy-complete`, `xzs-d8m4-dsi-host-complete`, `xzs-d8p1-gpio-complete`, `xzs-d8p2-power-rails-complete`, `xzs-d8m5-panel-power-reset-complete`, `xzs-d8m6-panel-dcs-complete`, `xzs-d8-m8.5-complete`.

## Generic Native Mach-O Execution (Phase D7-T2)

Phase D7-T2 established and hardware-sealed native file-backed Mach-O execution directly from the XZSFS root filesystem on Sony Xperia XZs:

* **Hardware-Verified Binaries**:
  - `/bin/hello`: Minimal Darwin userspace reference executable (`SYS_write`, `SYS_exit(0)`).
  - `/bin/args`: Generic diagnostic binary validating user stack argument unpacking (`argc`, `argv[0..N]`, NULL termination).
* **Architectural Execution Pipeline**:
  - `VREG` + Unified Buffer Cache (UBC) attachment via `ubc_info_init()`
  - File-backed Mach-O segment mapping via `vm_map_enter_mem_object_control()`
  - Read-only XZSFS `VNOP_PAGEIN` demand paging using `DIRECT_UPL` zero-copy I/O and EOF zero-fill
  - Return to EL0 via `task_wait_to_return` / `thread_bootstrap_return`
  - Canonical Darwin 64-bit syscall path (`svc #0x80`)
  - Asynchronous old exec-thread teardown via `AST_APC` (`thread_terminate_self()`), preventing task-switch hangs and duplicate process exit races
  - Parent PID 1 synchronous child reaping via `wait4()` and clean prompt return
  - Carry flag explicitly cleared on successful syscall returns in both ARM32 and ARM64 paths
  - Interactive shell demand-page faults and generic SVC dispatch permitted in `sleh.c`
* **Hardware Verification Proof**:
  - **14 total external exec cycles** validated on physical silicon across fresh boots
  - **2 independent fresh-boot mixed sequences** (`pwd` → `/bin/hello` → `/bin/args a b c` → `/bin/hello` → `/bin/args x y` → `/bin/hello` → `pwd`)
  - Zero panics (`PANIC=0`), Zero watchdog resets (`RESET=0`)
  - Full reports: [`artifacts/reports/D7_T2N5_SEAL_REPORT.md`](artifacts/reports/D7_T2N5_SEAL_REPORT.md), [`artifacts/reports/D7_T2N6_SEAL_REPORT.md`](artifacts/reports/D7_T2N6_SEAL_REPORT.md)

### What is NOT Supported Yet (Userspace Boundary)

Phase D7-T2 establishes minimal static Mach-O execution. It does **NOT** yet prove or claim:
- `dyld` (dynamic linker)
- Dynamic libraries / shared caches
- `libSystem` / C runtime library
- `launchd` service runtime / daemons
- Full POSIX API compliance
- Multithreaded userspace processes
- GPU acceleration / Metal
- Touchscreen input
- Cellular / Wi-Fi / Bluetooth networking
- Audio subsystem
- Complete platform power management / sleep states
- Apple proprietary frameworks (CoreFoundation, Foundation, UIKit, SpringBoard)

## Display

```text
D8-M1     = PASS
D8-M2     = PASS
D8-A0..A3 = PASS
D8-M3     = PASS (COMPLETE / SEALED / HARDWARE_PROVEN)
D8-M4     = PASS (COMPLETE / SEALED / HARDWARE_PROVEN)
D8-P1     = PASS (COMPLETE / SEALED / HARDWARE_PROVEN)
D8-P2     = PASS (COMPLETE / SEALED / HARDWARE_PROVEN)
D8-M5     = PASS (COMPLETE / SEALED / HARDWARE_PROVEN)
D8-M6     = PASS (COMPLETE / SEALED / HARDWARE_PROVEN)
D8-M8     = DEFERRED / INCOMPLETE / UNSEALED (tag `xzs-d8m8-deferred`)
D8-P3     = PENDING
```

D8-M1, tag `xzs-d8-m1-complete` at `861032cb7b137096edeb1aa5caa04aef6737533a`, read the clock controller only. MMAGIC_MDSS_GDSC was `0xa0222000` (on). MDSS_GDSC was `0x00222001` (collapsed).

D8-M2, tag `xzs-d8-m2-complete` at `545398f30d8fda592d4ca67ee867a016c2f37092`, completed display power domain and clock bring-up on physical silicon:
- MMAGIC GDSC: `0xa0222000` (ON)
- MDSS GDSC: `0xa0222000` (ON)
- Linux critical MMAGIC interconnect/bridge branches: `mmss_mmagic_ahb`, `mmss_mmagic_cfg_ahb`, `mmagic_mdss_noc_cfg_ahb`, `mmagic_mdss_axi` enabled (`enable=1, halt=0`)
- `mdss_ahb`: enabled and running (`enable=1, halt=0`, readback `0x20008001`)
- `mdss_axi`: enabled and running (`enable=1, halt=0`, readback `0x00006221`)
- `mdss_mdp`: enabled and running (`enable=1, halt=0`, readback `0x00006221`)
- USB console shell remained fully responsive; zero panics, zero resets.

D8-M3, tag `xzs-d8m3-display-pll-phy-complete`, completed DSI0 PLL, MMCC clock tree branches, and Qualcomm 14nm DSI PHY Stage B bring-up on physical Xperia XZs hardware across two independent fresh-boot runs:
- DSI0 PLL locked and ready: `0x009948cc = 0x0000002f` (`pll_locked=1, pll_ready=1`)
- BYTE0 clock branch unhalted: `BYTE0_CFG_RCGR = 0x00000100` (`src_sel=1:DSI0_BYTE`), `BYTE0_CBCR = 0x00000001` (`halt=0, enable=1`)
- PCLK0 clock branch unhalted: `PCLK0_CFG_RCGR = 0x00000100` (`src_sel=1:DSI0_PIXEL`), `PCLK0_CBCR = 0x00000001` (`halt=0, enable=1`)
- ESC0 clock branch unhalted: `ESC0_CFG_RCGR = 0x00000000` (`src_sel=0:XO`), `ESC0_CBCR = 0x00000001` (`halt=0, enable=1`)
- 14nm PHY Stage B: All 5 lane regulator biases verified (`0x1d`), drive strengths calibrated (`0x0ff`), full acceptance readback verified
- Reproducibility: 2/2 independent fresh boots passed 100%
- Critical M3 diff: 0 against Linux golden state
- Safety invariants: 0 panel GPIO writes, 0 LAB/IBB writes, 0 WLED writes, 0 DCS packets sent; shell and USB console remained 100% responsive; 0 bus aborts, 0 panics, 0 resets.

D8-M4, tag `xzs-d8m4-dsi-host-complete`, completed DSI0 host controller staged configuration and master enable (Command Mode, 4 active data lanes) on physical Xperia XZs hardware across two independent fresh-boot runs:
- DSI controller enable: `DSI_CTRL_PRE_ENABLE = 0x000001f4` (command mode, 4 data lanes, clock lane, master disabled) -> `DSI_CTRL_POST_ENABLE = 0x000001f5` (master bit 0 enabled)
- DSI timing & packets: `DSI_CLKOUT_TIMING_CTRL = 0x00001b2b` (Keyaki `t_clk_post=0x1b`, `t_clk_pre=0x2b`), `DSI_EOT_PACKET_CTRL = 0x00000011`
- DSI lane status: `DSI_LANE_STATUS = 0x00001f1f` (all 4 data lanes and clock lane in clean MIPI Stopstate, idle and ready)
- DSI internal clocks: `DSI_CLK_CTRL = 0x0000023f`, `DSI_CLK_STATUS = 0x0000234f` (all core clock gates active)
- DSI FIFO status: `DSI_FIFO_STATUS = 0x11111000` (idle, transmit queues ready)
- Reproducibility: 2/2 independent cold/fresh boots passed 100%
- Critical M4 diff: 0 against updated Linux golden state
- Safety invariants: 0 DCS packets sent, 0 DMA/BTA/MDP SW triggers, 0 panel GPIO writes, 0 LAB/IBB/WLED writes, 0 bus aborts, 0 SError, 0 panics, 0 resets; shell and USB console remained 100% responsive.

D8-P1, tag `xzs-d8p1-gpio-complete`, completed TLMM GPIO configuration for the Keyaki panel across two independent cold/fresh boots:
- TLMM register base: `0x01010000` (64KB block)
- GPIO 8 (`disp_reset_n`): `CFG = 0x00000200` (`mux=0:gpio, pull=0:none, drv=0:2mA, dir=OUTPUT`), `IN_OUT = 0x00000000` (Reset held asserted LOW / safe state)
- GPIO 10 (`mdp_vsync` / TE): `CFG = 0x00000005` (`mux=1:mdp_vsync, pull=1:pull-down, drv=0:2mA, dir=INPUT`), `IN_OUT = 0x00000000` (TE input with pull-down)
- GPIO 51 (`lcd_vddio_en`): `CFG = 0x00000200` (`mux=0:gpio, pull=0:none, drv=0:2mA, dir=OUTPUT`), `IN_OUT = 0x00000000` (VDDIO disabled LOW / safe state)
- M3/M4 display engine health verified post-GPIO configuration (`PLL = 0x2f`, `DSI_CTRL = 0x1f5`, `DSI_LANE_STATUS = 0x1f1f`)
- Safety invariants: 0 LAB/IBB writes, 0 WLED writes, 0 DCS packets sent, 0 bus aborts, 0 SError, 0 panics, 0 resets; USB console and shell remained 100% responsive.

D8-P2, tag `xzs-d8p2-power-rails-complete`, completed SPMI Arbiter v2 communication and PMIC (PMI8996) LAB (+5.60V) / IBB (-5.60V) display panel bias regulator bring-up and safe disable across two independent cold/fresh boots:
- SPMI Transport: Arbiter v2 core `0x0400F000`, write channels `0x04400000`, observer read channels `0x04C00000`. Peripheral APID mapping verified for SID 2 REVID (`0x0100`), SID 3 LAB (`0xDE00`), and SID 3 IBB (`0xDC00`).
- Stage 1 Configuration: LAB programmed to +5.60V (`0x8a`), MODULE_RDY (`0x80`), ENABLE (`0x00`); IBB programmed to -5.60V (`0xaa`), MODULE_RDY (`0x80`), ENABLE (`0x00`). Verified safely inactive.
- Stage 2A LAB Enable: `LAB_ENABLE_CTL = 0x80` written -> `LAB_STATUS1 = 0xa0` (`VREG_OK=1`, positive bias rail active).
- Stage 2B IBB Enable: `IBB_ENABLE_CTL = 0x80` written -> `IBB_STATUS1 = 0x80` (`VREG_OK=1`, negative bias rail active).
- Stage 2C Mandatory Safe Disable: IBB disabled first (`0xDC46 = 0x00`), `STATUS1 = 0x00` (OFF PASS) -> 8ms inter-rail delay -> LAB disabled (`0xDE46 = 0x00`), `STATUS1 = 0x00` (OFF PASS).
- Display Engine Integrity: `PLL = 0x2f`, `DSI_CTRL = 0x000001f5`, `LANE_STATUS = 0x00001f1f` intact.
- Reproducibility: 2/2 independent physical cold/fresh boots passed 100%.
- Safety invariants: 0 DCS packets sent, 0 WLED writes, panel reset held asserted LOW (GPIO 8 = 0), 0 bus aborts, 0 SError, 0 panics, 0 resets; shell and USB console remained 100% responsive.

D8-M5, tag `xzs-d8m5-panel-power-reset-complete`, completed Keyaki panel power-on sequence, cycle-accurate hardware reset sequence, powered-idle verification, and source-proven safe power-down sequence on physical hardware across two independent fresh boots:
- Sequence Verification: Fully compliant with authoritative downstream kernel `keyaki.dts` specification (`VDDIO_10MS->LAB_10MS->IBB_0MS->RST_LOW_10MS->RST_HIGH_10MS`).
- Hardware Timed Reset: `RESET_DELTA_US = 10,000 µs` (Run 1) and `10,002 µs` (Run 2) cycle-accurate via `cntvct_el0` (19.2 MHz clock base).
- Powered-Idle Check: GPIO8=HIGH (Reset released), GPIO10=LOW (TE inactive), GPIO51=HIGH (VDDIO 1.8V enabled), LAB=0xa0 (VREG_OK=1), IBB=0x80 (VREG_OK=1), DSI0 host stopstate intact (`LANE=0x00001f1f`).
- Mandatory Safe Power-Down: Reset assert LOW (5ms) -> IBB disable (10ms) -> LAB disable (10ms) -> VDDIO disable -> 300ms settling window passed 100%.
- Reproducibility: 2/2 independent physical cold/fresh boots passed 100%.
- Strict Scope Locks: 0 DCS packets sent, 0 WLED writes, 0 MDP scanouts/DMA triggers, 0 bus aborts, 0 SError, 0 panics, 0 resets.

> Native XNU now deterministically powers on, resets, verifies regulation of, and cleanly shuts down the physical Sharp + Synaptics command-mode panel on Sony Xperia XZs hardware.

D8-M6, tag `xzs-d8m6-panel-dcs-complete`, completed panel vendor/DCS command transmission, TE monitoring, and safe shutdown on physical hardware across two independent fresh boots:
- Canonical DCS Packets: Transmitted `SLPOUT (0x11)`, `TEON (0x35 0x00)`, `DISPON (0x29)`, `DISPOFF (0x28)`, and `SLPIN (0x10)` in command mode via 64-byte aligned DMA buffers.
- Zero Timeouts & ACK Errors: 5/5 DMA triggers and completions with `DMA_TIMEOUTS=0` and `ACK_ERRORS=0` across both runs.
- TE Monitoring: Sampled GPIO10 (TE/mdp_vsync) over ~33 ms (19,921 samples) verifying 0 transitions (stable LOW, scanout inactive).
- Graceful Shutdown: Fully reversed DCS sequence (`DISPOFF -> SLPIN -> M5 physical power-down`) returning to stable zero-power idle state.
- Strict Scope Locks: `WLED_WRITES=0` (backlight strictly off), `MDP_KICKOFFS=0` (MDP scanout untouched).
- Reproducibility: 2/2 independent physical cold/fresh boots passed 100% with identical pre- and post-register snapshots.

D8-M8, tag `xzs-d8m8-deferred`, investigated MDP command-mode framebuffer scanout on physical silicon across 10 controlled single-kickoff attempts. Framebuffer allocation (`0x98000000`), SSPP RGB0 configuration (1080x1920 XRGB8888, format/stride verified, active source address latched `0x98000000`), LM0 BASE routing, CTL0 flush consumption (`0x00020048` -> 0), and Qualcomm software-TE / internal VSYNC override (`PP_SYNC_CONFIG_VSYNC=0x00080093`, WR_PTR at 0 us, RD_PTR at 10 ms, internal period ~16.56 ms) were hardware-proven. However, pixel rasterization never advanced (`PP0_LINE_COUNT=0`, `PP0_DONE=0`, `DSI_STREAM_ACTIVITY=0`) due to an unresolved PingPong0 to DSI command-mode trigger / flow-control handshake. As explicitly directed, D8-M8 debugging is frozen, with no further hardware attempts. Milestone D8-M8 is formally marked DEFERRED / INCOMPLETE / UNSEALED. Phase D8 overall remains INCOMPLETE. Full report: [`docs/XZS_D8_M8_DEFERRED.md`](docs/XZS_D8_M8_DEFERRED.md).

Details: [`docs/XZS_DISPLAY_BRINGUP.md`](docs/XZS_DISPLAY_BRINGUP.md), [`docs/XZS_D8_M3_SEAL_REPORT.md`](docs/XZS_D8_M3_SEAL_REPORT.md), [`docs/XZS_D8_M4_SEAL_REPORT.md`](docs/XZS_D8_M4_SEAL_REPORT.md), [`docs/XZS_D8_P1_SEAL_REPORT.md`](docs/XZS_D8_P1_SEAL_REPORT.md), [`docs/XZS_D8_P2_SEAL_REPORT.md`](docs/XZS_D8_P2_SEAL_REPORT.md), [`docs/XZS_D8_M5_SEAL_REPORT.md`](docs/XZS_D8_M5_SEAL_REPORT.md), [`docs/XZS_D8_M6_SEAL_REPORT.md`](docs/XZS_D8_M6_SEAL_REPORT.md), [`docs/XZS_D8_M8_DEFERRED.md`](docs/XZS_D8_M8_DEFERRED.md). Bypassed work: [`docs/XZS_BLOCKERS_AND_DEFERRED.md`](docs/XZS_BLOCKERS_AND_DEFERRED.md). Status: [`docs/XZS_PORT_STATUS.md`](docs/XZS_PORT_STATUS.md).

### Verified Milestone Capabilities

* **Native Kernel Entry**: Bare-metal EL1 entry via `xzs-bootshim`, Apple Device Tree (ADT) generation, 16KB translation tables, and transition to High KVA (`0xfffffe0000000000`).
* **4-Core SMP Scheduler**: CPU0–CPU3 brought online via standard ARM PSCI `CPU_ON` (`0xC4000003`); Mach `pset0` active with `idle_thread` on all cores, GICv3 SGI 1 reschedule IPIs, and AST urgent preemption.
* **BSD Subsystem & VFS Root**: Full BSD process table, credentials, and read-only XZSFS v1 root filesystem mounted from RAMDisk (`rd=md0`).
* **devfs & Console Device**: Namespace resolution through `/dev` overlay; `/dev/console` created as character device 0:0 (`VCHR`).
* **PID 1 Userspace (EL0)**: BSD `initproc` (PID 1) mapped with PAGEZERO guard, RX `__TEXT`, and RW/NX initial stack; Strategy A2 native AF & UXN permission promotion verified on silicon.
* **Native Console Output**: PID 1 holds native file descriptors 0, 1, and 2 mapped to `/dev/console`; real Darwin `write(1)` syscall verified producing 26 bytes of console output (`[XZS-INIT] launchd entered\n`).
* **Sustained Stable Runtime**: PID 1 executes in EL0 indefinitely; completed 3,009 consecutive successful `getpid` (syscall 20) round-trips without faulting.

---

## Architecture

```text
               Authentic Apple / Darwin Userspace (launchd, dyld, daemons)
                                            │
                                            ▼
                       Standard IOKit / Platform Contracts
                 (IORegistry, AppleARMPERoot, IOPMPowerSource)
                                            │
                                            ▼
                    +──────────────────────────────────────────────+
                    |                XZSAppleCompat                |
                    |  - Translates Apple contracts to platform    |
                    |  - Emulates Apple IORegistry topologies      |
                    |  - Provides device-tree/chosen properties    |
                    |  - Exposes power/battery/display interfaces  |
                    +──────────────────────────────────────────────+
                                            │
                                            ▼
                    +──────────────────────────────────────────────+
                    |                 XZSPlatform                  |
                    |  - Clean OS-neutral Qualcomm hardware API    |
                    |  - MMIO, Clock (GCC), Reset, Regulator (RPM) |
                    |  - GPIO/TLMM pinmux, GICv3 IRQ, SMMU DMA     |
                    +──────────────────────────────────────────────+
                                            │
                                            ▼
                    +──────────────────────────────────────────────+
                    |          Native Qualcomm / Sony Drivers      |
                    |  - BLSP UARTDM, SDCC1 eMMC, MDP5 display,    |
                    |  - Synaptics ClearPad touch, DWC3 USB, etc.  |
                    +──────────────────────────────────────────────+
                                            │
                                            ▼
                                  MSM8996 Hardware
```

---

## Current Development Phase

* **Integrated on `main`**:
  - D7-T2 generic native Mach-O execution complete and sealed (tag `xzs-d7t2-full-complete`, `DEBT-001` RESOLVED).
  - D8-M1 display topology audit complete (tag `xzs-d8-m1-complete`).
  - D8-M2 display power domain & core clocks hardware-verified (tag `xzs-d8-m2-complete`).
  - D8-M3 DSI0 PLL, MMCC clock tree, and 14nm PHY hardware-verified (tag `xzs-d8m3-display-pll-phy-complete`).
* **Next active milestone**: D8-M4 (DSI0 host controller bring-up in command mode).

---

## Roadmap

| Phase | Milestone Description | Hardware Status |
| :--- | :--- | :---: |
| **Phase A** | Native kernel entry (Bootshim, ADT, MMU, High KVA) | **COMPLETE** |
| **Phase B** | Platform bring-up (UARTDM, GICv3, Timer, Pmap, VM) | **COMPLETE** |
| **Phase C** | SMP / Mach scheduler (PSCI, 4 Kryo cores, IPI, AST, Preemption) | **COMPLETE** |
| **Phase D1** | BSD / VFS bootstrap to root-storage boundary | **COMPLETE** |
| **Phase D2** | Physical eMMC storage bring-up (SDCC1, CMD0..CMD17, PIO) | **COMPLETE** |
| **Phase D3** | GUID Partition Table (GPT) discovery & partition enumeration | **COMPLETE** |
| **Phase D4** | Block-storage driver integration (`bdevsw` / `disk0`) | **COMPLETE** |
| **Phase D5** | Real root filesystem mount (RAMDisk XZSFS v1) | **COMPLETE / SEALED** |
| **Phase D6** | PID 1 / First EL0 userspace (`initproc` / launchd) | **COMPLETE / SEALED** |
| **Phase D7** | Interactive USB shell (`/bin/sh`) & Generic Mach-O exec | **COMPLETE / SEALED** |
| **Phase D8** | Display audit, power/clocks, and DSI scanout | **D8-M1..M6 PASS / D8-M8 DEFERRED / INCOMPLETE** |
| **Phase D9** | XZSPlatform hardware/platform compatibility layer | **PLANNED** |
| **Phase D10**| Core native device drivers | **PLANNED** |
| **Phase D11**| System hardware integration | **PLANNED** |
| **Phase D12**| XZSAppleCompat (Apple-facing hardware compatibility layer) | **PLANNED** |
| **Phase D13**| Darwin / iOS userland compatibility | **PLANNED** |
| **Phase D14**| First old-iOS userland boot | **PLANNED** |
| **Phase D15**| iOS service bring-up | **PLANNED** |
| **Phase D16**| Graphical iOS userland / SpringBoard investigation | **PLANNED** |

Full specifications and milestone criteria are detailed in [`docs/XZS_ROADMAP.md`](docs/XZS_ROADMAP.md).


---

## Hardware / Platform Strategy

To maintain engineering rigor and keep generic XNU maintainable:
1. **Upstream Alignment**: Keep generic XNU code as close to canonical Apple/Darwin semantics as practical.
2. **Platform Encapsulation**: All Qualcomm MSM8996 and Sony Xperia specific hardware behaviors must be encapsulated within `XZSPlatform`.
3. **Compatibility Shimming**: Apple-specific expectations (IORegistry planes, property trees, power sources) are isolated within `XZSAppleCompat`.
4. **Audit and Retirement**: Temporary bring-up workarounds (e.g. `devfs_getattr` pointer-hardening bypass `3e417bb`) are tracked as technical debt and slated for systematic retirement under Phase D9.

---

## iOS Userland Research Goal

The project models authentic iOS userland execution through an extracted-image paradigm:
```text
Legally obtained Apple IPSW
        ↓
Extract compatible iOS root/userland
        ↓
Prepare project-specific root filesystem/image
        ↓
Boot using xnu-xzs / target-compatible XNU
        ↓
Mount iOS userland
        ↓
Execute authentic Apple launchd
```

### Version Compatibility Principle
An older iOS userland cannot automatically be assumed compatible with the current macOS Sequoia bring-up XNU kernel. The project audits:
```text
Target iOS Version ↔ Darwin Version ↔ XNU Version ↔ dyld Version ↔ launchd Version ↔ IOKit ABI
```
The modular `XZSPlatform` design ensures that board support and native drivers can be re-targeted to an XNU branch matching the selected iOS version if needed.

---

## Debug / Recovery Workflow

* **Boot Mechanism**: Sony S1 Fastboot, `fastboot boot` only. Do not flash.
* **Primary debugger**: USB shell and the host transcript. Each hardware-changing display step is one PRE / APPLY / readback / POST transaction.
* **Recovery**: manual Sony force shutdown, then fastboot. Do not use `xzs# reboot`.
* **Ramoops**: the reserved region is mapped, but a TWRP pull is not XNU evidence unless the file contains an XNU marker. That path is deferred. See [`docs/XZS_BLOCKERS_AND_DEFERRED.md`](docs/XZS_BLOCKERS_AND_DEFERRED.md).

---

## Known Limitations

1. **Display is not scanning**: D8-M1 topology audit passed. D8-M2 has powered MDSS and verified clocks on physical hardware. D8-M3 has locked DSI0 PLL (`0x2f`), unhalted MMCC BYTE0/PCLK0/ESC0 branches (`CBCR=1, halt=0`), and verified 14nm PHY lanes. D8-M4 (DSI0 host controller bring-up) is the next milestone. Panel power, backlight, and framebuffer scanout follow in subsequent milestones.
2. **devfs Pointer-Hardening Bypass**: Commit `3e417bb` bypasses `vm_kernel_addrhash` in `devfs_getattr` to prevent a SHA-256 address hashing hang during early devfs open. Classified as `XZS PLATFORM WORKAROUND` to be re-audited under Phase D9.
3. **Deferred Subsystems**: Advanced networking (Skywalk, lo0) and DTrace FBT are temporarily deferred until required drivers are active.

---

## Documentation Index

### Core Architecture & Strategy
* [Project Roadmap](docs/XZS_ROADMAP.md) — Authoritative multi-phase development roadmap (Phases A through D16).
* [Architecture Overview](docs/XZS_ARCHITECTURE.md) — End-to-end boot architecture, execution flow, and platform design.
* [Workarounds & Compatibility Matrix](docs/XZS_WORKAROUNDS.md) — Active shims, workarounds, and classification taxonomy.
* [Technical Debt & Backlog](docs/XZS_TECHNICAL_DEBT.md) — Architectural debt inventory, risks, and remediation plans.
* [Current Session Handoff](docs/CURRENT_HANDOFF.md) — Concise current state, commits, and next actions for AI sessions.
* [Port Status](docs/XZS_PORT_STATUS.md) — Hardware-verified checklist and the current milestone.
* [Display Bring-up](docs/XZS_DISPLAY_BRINGUP.md) — D8 topology, measured registers, and the power/clock sequence.
* [Blockers and Deferred Work](docs/XZS_BLOCKERS_AND_DEFERRED.md) — Issues that were bypassed on purpose, and the condition that resumes each one.

### Hardware & Bring-up Reference
* [Hardware Map](docs/HARDWARE_MAP.md) — Audited MMIO register bases, IRQs, and clock domains.
* [Memory Map](docs/XZS_MEMORY_MAP.md) — Physical memory layout, carveouts, and pstore allocations.
* [Early Debug Architecture](docs/XZS_EARLY_DEBUG.md) — Diagnostic telemetry, breadcrumbs, and ramoops recovery.
* [Hardware Verification Guide](docs/XZS_HARDWARE_VERIFICATION.md) — Flashing, testing, and extraction procedures.

### Sealed Phase Reports & Audits
* [Phase D8-M8 Final Closure Report](docs/XZS_D8_M8_FINAL_CLOSURE.md) — Comprehensive D8-M8 Final Closure Campaign ledger (R1..R6), proven architectural milestones, and in-cell touch interlock isolation.
* [Phase D6 Final Userspace Foundation Report](artifacts/reports/D6_FINAL_USERSPACE_FOUNDATION_REPORT.md) — Comprehensive multi-milestone seal and regression report.
* [Phase D6-M6 Stable PID1 Report](artifacts/reports/D6_M6_STABLE_PID1_REPORT.md) — D6-M6 hardware acceptance report.
* [Phase D6-M6 Source Audit](docs/D6_M6_STABLE_PID1_SOURCE_AUDIT.md) — File descriptor bootstrap, console call path, and telemetry audit.
* [Phase D6-M5 Syscall Round-Trip Report](artifacts/reports/D6_M5_FIRST_SYSCALL_REPORT.md) — First EL0 syscall dispatcher and return proof.
* [Phase D6-M4 First EL0 Transition Report](artifacts/reports/D6_M4_FIRST_EL0_REPORT.md) — First userland instruction execution proof.
* [Phase D5 Rootfs Final Architecture](docs/D5_ROOTFS_FINAL_ARCHITECTURE.md) — RAMDisk transport and XZSFS v1 filesystem architecture.
