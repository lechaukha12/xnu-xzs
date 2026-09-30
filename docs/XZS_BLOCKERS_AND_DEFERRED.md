# Blockers and deferred work

This register is the list of problems that were deliberately left unfixed. Chat history is not the record. When an entry is fixed, set `STATUS=RESOLVED` and add `FIX_COMMIT` and `HARDWARE_PROOF`. Do not delete an entry.

Status values: `ACTIVE_BLOCKER`, `DEFERRED`, `BYPASSED`, `RESOLVED`, `OBSOLETE`.

## DEBT-001

| Field | Value |
|---|---|
| ID | DEBT-001 |
| AREA | userspace exec |
| TITLE | Generic external Mach-O execution loses VM mappings |
| STATUS | RESOLVED |
| FIRST_SEEN | D7-T2 on `xzs-d7t2-shell` |
| LAST_KNOWN_COMMIT | `3f335e65572592d36d76ee718caf7979b3cfad5d` |
| FIX_COMMITS | `fd5549f` (UBC attachment), `f377b45` (Mach-O mapping), `ace3c19`/`d3478e3` (VNOP_PAGEIN), `09e34f2` (pager data/zero-fill), `ea85c04` (exec thread teardown), `f408d26` (syscall carry clear & EL0 faults), `6321b33` (generic args & repeated exec) |
| HARDWARE_PROOF | Fully resolved in Phase D7-T2N. Native XZSFS regular vnodes attached to UBC; file-backed Mach-O `__TEXT` mapped via `vm_map_enter_mem_object_control`; read-only `VNOP_PAGEIN` demand-paged via `DIRECT_UPL`; old exec-thread cleanly retired via `AST_APC` `thread_terminate_self`; Carry flag cleared on syscall return; EL0 user faults permitted. Verified on hardware: `/bin/hello` sealed, `/bin/args` sealed across multiple argv layouts (`/bin/args`, `/bin/args test`, `/bin/args a bb ccc dddd`, `/bin/args one two three`), 14 total external exec cycles across 2 independent fresh-boot mixed runs, parent `wait4` reap verified, prompt returned, `PANIC=0`, `RESET=0`. See `artifacts/reports/D7_T2N5_SEAL_REPORT.md` and `artifacts/reports/D7_T2N6_SEAL_REPORT.md`. |
| HISTORICAL_EVIDENCE | Fork child path proven. EL0 child `x0=0` proven. SVC 59 proven. `execve` entered. Mach-O loader reached. Entry point `0x1000002f0` identified. `load_machfile()` reports success. Final task `vm_map` has `min=0x100000000`, `max=0x00007ffffe000000`, `nentries=0`. `__TEXT` is missing. `vm_fault(0x1000002f0)` returns `KERN_INVALID_ADDRESS`. Frozen branch `xzs-d7t2-shell`. Tag `xzs-d7t2-deferred`. Notes: `docs/XZS_D7_T2_DEFERRED.md`. |
| IMPACT | A general external executable cannot be started. (RESOLVED: `/bin/hello` and `/bin/args` execute natively in EL0). |
| CURRENT_BYPASS | None required; generic native Mach-O execution is fully functional on hardware. |
| WHY_DEFERRED | (Historical) Display bring-up did not need `/bin/hello`. |
| RESUME_CONDITION | Resumed and completed in milestone D7-T2N. |
| NEXT_INVESTIGATION | None; sealed in D7-T2N-6. |

## DEBT-002

| Field | Value |
|---|---|
| ID | DEBT-002 |
| AREA | persistent telemetry |
| TITLE | Persistent XNU ramoops telemetry not recoverable through TWRP |
| STATUS | DEFERRED |
| FIRST_SEEN | D7-T3-PERSIST, commits `94c1c84` and `7a57645` |
| LAST_KNOWN_COMMIT | `0c108c1` records the failed write-combine pull. Runtime experiments are not active on the clean D8-M1 tree. |
| EVIDENCE | Live XNU sees a DBGC header (`sig 0x43474244`). TWRP maps the expected physical region (`console 0xa7fbe000`). Pstore files change between runs. `MAGIC=XZSP` is not recovered. Changing cacheability did not make the marker recoverable. Raw DRAM cannot currently be independently inspected in this TWRP image (`/dev/mem` is absent). Notes: `docs/XZS_PERSISTENT_TELEMETRY.md`. |
| IMPACT | A disappearance that happens after the last USB line cannot be reconstructed from pstore. |
| CURRENT_BYPASS | The USB shell is the primary debugger. One PRE/APPLY/POST transaction per hardware step localizes the D8 frontier. |
| WHY_DEFERRED | The shell transcript is sufficient while each display step returns to `xzs#`. |
| RESUME_CONDITION | Non-deterministic crashes, crashes after POST, or failures that cannot be localized from the host log. |
| NEXT_INVESTIGATION | Determine whether the marker never reaches the DRAM payload Linux saves, or whether the ramoops driver replaces it before the pull. Do not add new cache-maintenance experiments without a new hypothesis. |

## DEBT-003

| Field | Value |
|---|---|
| ID | DEBT-003 |
| AREA | reboot |
| TITLE | `xzs# reboot` is unreliable |
| STATUS | BYPASSED |
| FIRST_SEEN | D7 shell bring-up |
| LAST_KNOWN_COMMIT | shell builtin still calls `SYS_REBOOT`. Not used for recovery. |
| EVIDENCE | The USB gadget can remain present while the shell is dead. CPU0 can keep petting the watchdog after the shell thread stops. |
| IMPACT | A shell reboot is not a way back to fastboot. |
| CURRENT_BYPASS | Sony manual force shutdown, then fastboot, then `fastboot boot`. No flash. |
| WHY_DEFERRED | Manual recovery is reliable enough for single-step display bring-up. |
| RESUME_CONDITION | When a software-controlled reboot is operationally useful. |
| NEXT_INVESTIGATION | Separate gadget teardown from the shell thread and the CPU0 watchdog pet. |

## DEBT-004

| Field | Value |
|---|---|
| ID | DEBT-004 |
| AREA | reboot |
| TITLE | `xzs# reboot` should boot reliably into TWRP |
| STATUS | DEFERRED |
| FIRST_SEEN | D7 shell bring-up |
| LAST_KNOWN_COMMIT | shell `reboot` builtin still calls `SYS_REBOOT` |
| EVIDENCE | A shell reboot does not produce a usable recovery session. |
| IMPACT | There is no software path from the XNU shell into TWRP. |
| CURRENT_BYPASS | Sony manual force shutdown, then fastboot, then boot TWRP with `fastboot boot` when a pull is needed. |
| WHY_DEFERRED | Display bring-up uses the USB shell. Recovery pulls are not the current debugger. |
| RESUME_CONDITION | When a software reboot into TWRP is required for a failed boot that the host log cannot explain. |
| NEXT_INVESTIGATION | Define a reboot target that hands the phone to the recovery image without killing the shell first. |

## DEBT-005

| Field | Value |
|---|---|
| ID | DEBT-005 |
| AREA | recovery |
| TITLE | TWRP to fastboot transition |
| STATUS | DEFERRED |
| FIRST_SEEN | D7-T3 pstore pulls |
| LAST_KNOWN_COMMIT | not implemented in the XNU shell |
| EVIDENCE | Returning from TWRP to fastboot is a manual host step (`adb reboot bootloader` has been used). It is not a sealed XNU workflow. |
| IMPACT | A recovery pull is not yet a closed loop back to the next `fastboot boot`. |
| CURRENT_BYPASS | The host asks for fastboot explicitly. No flash. |
| WHY_DEFERRED | D8 does not need an automated recovery loop while each step returns to `xzs#`. |
| RESUME_CONDITION | When post-mortem pulls become part of the normal hardware loop. |
| NEXT_INVESTIGATION | Record one reliable TWRP-to-fastboot sequence and its failure signs. Do not implement it during D8-M2. |

## ACTIVE-D8-001

| Field | Value |
|---|---|
| ID | ACTIVE-D8-001 |
| AREA | display clocks |
| TITLE | `mdss_ahb` branch remains HALT after enable |
| STATUS | RESOLVED |
| FIRST_SEEN | D8-M2 run `d8m2-0b0e429` |
| LAST_KNOWN_COMMIT | `0b0e4293b44285e866b1de26038548adb62330e2` |
| EVIDENCE | On `0b0e429`: before `0x80008000`, write `0x80008001`, after `0x80008001`. Enable bit accepted. Halt bit remains set. Poll timeout 2000 µs. Read-only `94a2c37`: AHB CMD `0x00000000` (`root_off=0`), CFG `0x00000513` (GPLL0), GCC NOC `0x20008001` running. Read-only `a5f47b5`, log `artifacts/hw/d8m2-a5f47b5/host.txt`: MDSS_BCR, MMAGIC_MDSS_BCR, MMAGICAHB_BCR, and MMAGIC_CFG_BCR all `0x00000000` (assert-control bit 0 clear). GPLL0 mode `0xc0118000` with `PLL_LOCK_DET` set. Vote `0x00000011`, enable bit set. AHB root still `root_off=0`. `mdss_ahb` on that fresh boot is `0x80008000` (enable clear). Read-only `059d58a`: 4 Linux critical MMAGIC branches were halted (`0x80000000` / `0x80008000`, `enable=0, halt=1`). Hardware test on candidate `545398f` (`artifacts/hw/d8m2-545398f/host.txt`) sequentially enabled all 4 critical MMAGIC branches, powered MDSS GDSC (`0x00222001` -> `0xa0222000`), and retried `mdss_ahb` enable. `mdss_ahb` transitioned from `0x80008000` -> `0x20008001` (`enable=1, halt=0`), clearing the halt condition immediately. MDSS AXI and MDP branches subsequently enabled with `enable=1, halt=0` (`0x00006221`). |
| IMPACT | Resolved. Display clocks running without wedging or panic. |
| CURRENT_BYPASS | None. Full hardware resolution achieved. |
| WHY_DEFERRED | N/A (Resolved). |
| RESUME_CONDITION | N/A (Resolved). |
| ROOT_CAUSE | Missing Linux critical MMAGIC interconnect/bridge clock initialization (`mmss_mmagic_ahb`, `mmss_mmagic_cfg_ahb`, `mmagic_mdss_noc_cfg_ahb`, `mmagic_mdss_axi`). In Linux, these four branches are marked `CLK_IS_CRITICAL` and enabled at MMCC registration. Without them running, the MMAGIC interconnect/bridge between GCC/MMCC and MDSS remains gated, preventing `mdss_ahb` branch logic from clearing its halt bit. |
| FIX_COMMIT | `545398f30d8fda592d4ca67ee867a016c2f37092` |
| HARDWARE_PROOF | Candidate `545398f` booted via `fastboot boot artifacts/hw/d8m2-545398f/xzs-xnu-boot.img` (SHA256: `5a5185fe9b53da69895cc2a6c1b68e96b0f46409cac8e4ca9fb044f6d0712704`). Host transcript `artifacts/hw/d8m2-545398f/host.txt`: 1. `mmss_mmagic_ahb` enabled: `0x80000000` -> `0x00000001` (enable=1, halt=0). 2. `mmss_mmagic_cfg_ahb` enabled: `0x80008000` -> `0x20008001` (enable=1, halt=0). 3. `mmagic_mdss_noc_cfg_ahb` enabled: `0x80000000` -> `0x00000001` (enable=1, halt=0). 4. `mmagic_mdss_axi` enabled: `0x80000000` -> `0x00000001` (enable=1, halt=0). 5. MDSS GDSC powered on: `0x00222001` -> `0xa0222000`. 6. `mdss_ahb` retry: `old=0x80008000, wrote=0x80008001, new=0x20008001, readback=0x20008001` (enable=1, halt=0). 7. `mdss_axi` enabled: `0x80006220` -> `0x00006221` (enable=1, halt=0). 8. `mdss_mdp` enabled: `0x80006220` -> `0x00006221` (enable=1, halt=0). Final `pwd` responsive (`/`), zero panics, zero resets. |

## ACTIVE-D8-002

| Field | Value |
|---|---|
| ID | ACTIVE-D8-002 |
| AREA | display MDP scanout |
| TITLE | PingPong0 to DSI command-mode transport / handshake boundary |
| STATUS | RESOLVED_IN_D8_M8_5 |
| FIRST_SEEN | D8-M8 Retry #1 (`9154f24`), confirmed through Retry #10 (`0243572`) |
| RESOLUTION_COMMIT | `c84623bb760f34e583839c534e08684b35464fe0` |
| RESOLUTION | Resolved by Milestone D8-M8.5 First Visible Display Closure Campaign: 1. Implemented PMI8994 QPNP WLED backlight driver (3 LED strings, 1200/4095 brightness) via SPMI. 2. Transmitted 13 authentic Sharp vendor initialization commands from `keyaki.dts` line 1872 (`somc,default_cmd_panel`) before scanout initiation. 3. Configured `PP0_AUTOREFRESH = 0x80000001` with internal VSYNC timing (`PP0_SYNC_CFG_VSYNC = 0x00180093`, `PP0_SYNC_CFG_HGHT = 0x0000FFF0`) to autonomously drive 60Hz scanout. 4. Eliminated conflicting post-kickoff DSI SW DMA reads. Verified across 3/3 fresh cold boots (`V1`, `V2`, `V3`) displaying 8 color bars + center badge. See `docs/XZS_D8_M8_5_FIRST_VISIBLE_DISPLAY.md` and `docs/XZS_D8_M8_FINAL_RECONCILIATION.md`. |
| IMPACT | RESOLVED. Native XNU scans out framebuffer pixels to physical Sharp LCD panel. Unblocks D8-M10 (Framebuffer Text Console). |



