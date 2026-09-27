# D8-M8 Retry 11B — pre-instrumentation source-order audit

Base commit: `33990cf1be8adc2bbc829626a9f458497233335f`.

## Retry 11A terminal fragment

The Retry 11A USB transcript ended during `display m8-fb-init` with
`[PROBE] Word 0 =`. This does **not** establish a fault at the framebuffer
load.

At the base commit, `src/xnu/pexpert/arm/xzs_d8m8.h:680-703` performs, in
source order:

1. `ml_io_map_unmappable(g_m8_fb_pa, g_m8_fb_size, 0x6u)` when `g_m8_fb_va`
   is zero, checks the resulting VA, and prints the PA, VA, and geometry.
2. Emits `Reading word 0 from FB_VA...`.
3. Executes exactly one volatile load into `probe_val`:
   `uint32_t probe_val = *(volatile uint32_t *)g_m8_fb_va;` (line 702).
4. Calls `xzs_diag_emit("  [PROBE] Word 0 = 0x")` (line 703).
5. Calls `xzs_d8p1_hex32(probe_val)` and emits ` (READ PASS)\n` (line 703).
6. Starts the next framebuffer write probe (line 705 onward).

`xzs_d8p1_hex32` (`src/xnu/pexpert/arm/xzs_d8p1.h:27-36`) formats the
value into a local string and calls `xzs_diag_emit`. `xzs_diag_emit`
(`src/xnu/pexpert/arm/xzs_diag.c:32-47`) copies the string into its retain
buffer and calls `xzs_bringup_console_write`, which is the USB/pstore output
path. Thus the visible prefix is output *after* the volatile load. The load
must have completed at the C execution boundary before the prefix call began.

**Answer: YES — SOURCE_PROVEN.** The first framebuffer word had already been
successfully loaded when Retry 11A emitted the visible prefix. That fragment
does not prove the prefix call returned, the formatter ran, the remaining USB
bytes were delivered, or the following write began. It does not prove the
cause of the subsequent return to fastboot.

The existing `xzs_breadcrumb` in `src/xnu/osfmk/arm64/start.s:1435-1492`
writes an IMEM and DRAM checkpoint, then emits a breadcrumb line. Existing
exception capture in the same assembly file records ELR, ESR, FAR, and SPSR.
Retry 11B will use those existing mechanisms without changing mapping,
display programming, watchdog implementation, or exception handling.
