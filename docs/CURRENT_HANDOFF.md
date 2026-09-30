# XNU-XZS Current Session Handoff

```text
CURRENT_PHASE=D8-M10
CURRENT_MILESTONE=FRAMEBUFFER_TEXT_CONSOLE_SHELL_DISPLAY
DISPLAY_VISIBLE_PIXELS=HW_PROVEN
D8_M8=CLOSED_SUPERSEDED_BY_M8_5
D8_M8_5=SEALED_HW_PROVEN
D8_M9=SATISFIED_BY_D8_M8_5
NEXT_OBJECTIVE=render interactive xzs# shell to physical framebuffer/LCD
```

Current accepted milestones:
- **D7-T1 (USB Console Transport)**: COMPLETE & SEALED, tag `xzs-d7t1-complete`.
- **D7-T2 (Native Generic Mach-O Execution)**: COMPLETE & SEALED, tag `xzs-d7t2-full-complete`. DEBT-001 is RESOLVED. Generic external Mach-O execution (`/bin/hello`, `/bin/args` with multiple argv layouts) is hardware-verified across 14 external exec cycles without panic or reset.
- **D8-M1..M6, D8-P1, D8-P2**: COMPLETE & SEALED on physical hardware.
- **D8-M8 (MDP Framebuffer Scanout Investigation)**: CLOSED / SUPERSEDED BY D8-M8.5. Historical campaign was EXHAUSTED_NOT_SEALED; acceptance requirement is satisfied by D8-M8.5 hardware proof.
- **D8-M8.5 (First Visible Display Closure Campaign)**: COMPLETE & SEALED, tag `xzs-d8-m8.5-complete`. Physical Sharp 1080p IPS LCD panel illuminated with 8 color bars, center badge, and border; verified across 3/3 fresh cold boots.
- **D8-M9 (Physical First Pixels on Screen)**: SATISFIED BY D8-M8.5.
- **D8-M10 (Framebuffer Text Console / Interactive Shell Display)**: NEXT / ACTIVE.

Technical debt items:
- `DISPLAY-DEBT-001`: DSI DCS RX / Bus Turnaround (BTA) Readback Timeout (non-blocking).
- `DEBT-02`: 88MB safe low-DRAM allocation constraint.
- `DEBT-03`: Timer-seeded software PRNG.
- `DEBT-08`: devfs_getattr pointer-hardening bypass (to be re-audited in D9 XZSPlatform).

Entry point: [`README.md`](../README.md). Display reconciliation: [`docs/XZS_D8_M8_FINAL_RECONCILIATION.md`](XZS_D8_M8_FINAL_RECONCILIATION.md). Display first light report: [`docs/XZS_D8_M8_5_FIRST_VISIBLE_DISPLAY.md`](XZS_D8_M8_5_FIRST_VISIBLE_DISPLAY.md).

The record below is the older D6/D7 handoff and is kept as history.


```text
FINAL_D6_MAIN_COMMIT=cc297b0e721ea0c002e008a4a330ac1aa3c9a3b3

D6_FINAL_HARDWARE_COMMIT=f5dd7a36bfedfbcf68d7a6bf2a95d4d09dab5ac0
D6_FINAL_SEAL_COMMIT=f57fee97e72d05b6ecee2563b8edb21ba283315a
D6_MAIN_MERGE_COMMIT=cc297b0e721ea0c002e008a4a330ac1aa3c9a3b3
D6_TAG=xzs-d6-userspace-complete

D7_M1_COMMIT=ef71c290913d3743ee32a3dc243d658ae8df80c6
D7_M2_HARDWARE_COMMIT=694446c0af85095e3045fb3d15bd12215aea203e
D7_M2_BOOT_IMAGE_SHA256=a78af05bbd39e2c93a04d3b3384d885a83005e40b25dd6cb5b7140795c71e24e
D7_M2_TAG=xzs-d7m2-complete

D7_M3_BOOT_IMAGE_SHA256=c2b66cea0a6475bc140e729b2827e41d636b855f2a29f7619a56c5d1cccbe68c
D7_M3_TAG=xzs-d7m3-complete

D7_M4_INTERNAL_HARDWARE_COMMIT=639fe1c42478c8ca07aa0019cbe59bfd846d9866
D7_M4_INTERNAL_BOOT_IMAGE_SHA256=329f869a55e7185bda99d14f38575d13c693298b04c7fa0b87f5d3439b34044a
D7_M4_INTERNAL_RAW_LOG_SHA256=49422168deee99ad9c8fa3b5d07adb70edf6c367b185174e44cb8e3ff769d469

D7_M4_EXTERNAL_BOOT_IMAGE_SHA256=019beff1c47a15bbb20979fc8d03c47befa7f7f80dbe27bacd1d91c5d9892909
D7_M4_EXTERNAL_RAW_LOG_SHA256=1dd42ebfd8ac667aa359b68a0ffe5e4d5d99a55bcdc4b083ddfe7ba26b7d8338

LAST_SEALED_MILESTONE=D7-M4

D7_T1_CANDIDATE2A_STATUS=COMPLETE / HARDWARE VERIFIED
D7_T1_CANDIDATE2A_COMMIT=981bd0b5cd16dca5b8bf5c0e5f331e2f798bf7a7
D7_T1_CANDIDATE2A_KERNEL_SHA256=c902fadb254a02396b98866b3473f2b18e445807ad5085dd94e0897e0ec03657
D7_T1_CANDIDATE2A_BOOT_SHA256=6b8df9d45116b7e352007ac61a033ff84d0825766af6041242575bfa6cb92c91
D7_T1_CANDIDATE2A_RAW_CONSOLE_SHA256=9f4ee83975b5e7655c6ce8a2fc1f2ae49bfc6a0d9fb454adb3a0dc2d5df51153
D7_T1_CANDIDATE2A_READ_ONLY=yes
USB_STATE_MUTATED=no

D7_T1_CANDIDATE2B_STATUS=COMPLETE / HARDWARE VERIFIED
D7_T1_CANDIDATE2B_COMMIT=da548da5f22e0c1a8c45156aedab2a2d26072963
D7_T1_CANDIDATE2B_KERNEL_SHA256=7e29f7527d51d7bbb55afdc56907179f9c276b652e5f49ee333f1cec9be5a40d
D7_T1_CANDIDATE2B_BOOT_SHA256=e5fd82e9a4ffec69cde8cbc8520aef85ce107b26edd73e538a7e1ba0db52709f
D7_T1_CANDIDATE2B_RAW_CONSOLE_SHA256=4aa1cf14177b2aad000e9db05663b61062f07e47986338e5d9c0f75cf988f742
D7_T1_CANDIDATE2B_DCFG_BEFORE=0x0008080c
D7_T1_CANDIDATE2B_DCFG_WRITTEN=0x00080800
D7_T1_CANDIDATE2B_DCFG_READBACK=0x00080800
D7_T1_CANDIDATE2B_COMPLETE=yes
D7_T1_COMPLETE=no
D7_T1_SEALED=no

CURRENT_BRANCH=xzs-d7t1-usb-console

NEXT_PHASE=D7
NEXT_MILESTONE=D7-T1 Candidate-2C (XNU-owned DWC3 event-buffer plan only; not authorized for execution)

D7_M1_STATUS=COMPLETE
D7_M2_STATUS=COMPLETE / SEALED
D7_M3_STATUS=COMPLETE / SEALED
D7_M4_STATUS=COMPLETE / SEALED
D7_M4_P0_READINESS_STATUS=COMPLETE
D7_M4_INTERNAL_PIPELINE_STATUS=HARDWARE VERIFIED
D7_M4_EXTERNAL_UART_PIN_TEST=OUT_OF_SCOPE_NON_INVASIVE_PROJECT
D7_M4_COMPLETE=yes
D7_M4_HARDWARE_VERIFIED=yes
D7_M4_SEALED=yes

SHELL_STDIN_KERNEL_PATH_WORKING=yes
SHELL_STDIN_WORKING=yes
HOST_INTERACTIVE_STDIN_TRANSPORT_AVAILABLE=no

NEXT_GOAL=
D7-T1 Candidate-2C: plan (do not execute) an XNU-owned DWC3 event-buffer boundary.
  - Retain Candidate-2B's halted-controller and immutable-domain contracts.
  - Audit event-buffer ownership and initialization before any event or endpoint work.
  - Do not activate RUN_STOP, enumerate, or touch EP0 without separate authorization.

KNOWN_BLOCKER=
None for D7-M4. Host-interactive stdin transport (USB gadget CDC ACM / DWC3 console) is deferred to future non-invasive transport bring-up.

SHELL_BINARY_SHA256=848a10da132fb4482c3cae01a35a73fb6fe4a79bf9e170800489d12f3fbb7bd3

CURRENT_KNOWN_PLATFORM_WORKAROUNDS=
- devfs_getattr pointer-hardening workaround / 3e417bb (bypasses vm_kernel_addrhash SHA-256 hang; returns fsid 0x64657666; to be re-audited in D9 XZSPlatform)
- memorystatus static jetsam buffer / kern_memorystatus.c (pre-allocated static snapshot buffer)
- thread_call zone priming / thread_call.c (pre-allocates 105 elements to avoid early zone lock contention)
- non-Apple silicon CTRR compatibility / machine_routines.c (marks unsafe_kernel_text false without Apple DT CTRR property)
- tcp_tfo_init deferral / tcp_subr.c (defers AES-128 random key generation until crypto provider is available)
- skywalk_init deferral / bsd_init.c (defers packet networking arena allocations)
- dtrace_fbt deferral / fbt.c (defers kernel-wide function boundary tracing instrumentation)

NEXT_EXACT_ACTION=
Review `docs/D7_T1_CANDIDATE2B_REPORT.md`; Candidate-2B is complete but D7-T1 is not complete/sealed. Prepare Candidate-2C only when directed.
```
