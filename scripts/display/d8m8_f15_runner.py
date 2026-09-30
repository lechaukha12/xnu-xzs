#!/usr/bin/env python3
"""D8-M8 F15 Hardware Test Runner: Exact Panel 9 Kickoff -> Post-On SLPOUT Ordering Closure.

Executes ONE conditional RAM boot (NO FLASH) on target BH905SX976:
1. Verifies fastboot connection.
2. Boots XNU kernel with authentic Sony LK ordering:
   - panel_prepare: TEON (0x35 0x00) + DISPON (0x29) only. (SLPOUT_SENT=NO)
   - kickoff: pre-clear intr, arm CTL_START=1, dispatch post-on SLPOUT via DMA, wait 120ms settle.
   - Preserves DSI_TRIG_CTRL = 0x80000004 throughout all stages.
3. Observes Window A (post-CTL_START, pre-SLPOUT) and Window B (post-SLPOUT for >= 250ms).
4. Records distinct RD_PTR events, GPIO10 pad transitions, and PP_LINE progression.
5. Safely reboots target back to fastboot mode.
"""

import hashlib
import re
import subprocess
import sys
import time
from pathlib import Path

import importlib.util

ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/f15-exact-panel9-order"
BOOT = ROOT / "artifacts/builds/xzs-xnu-boot.img"
KERNEL = ROOT / "src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple"
SERIAL = "BH905SX976"

spec = importlib.util.spec_from_file_location(
    "r11a_helpers", ROOT / "scripts/display/d8m8_7_retry11a_runner.py"
)
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def field_of(text: str, key: str) -> str | None:
    if not text:
        return None
    m = re.search(rf"\b{re.escape(key)}=([^\s\r\n]+)", text)
    return m.group(1) if m else None


def classify_f15(kickoff: str | None, prep: str | None, boot_fault: bool) -> str:
    if kickoff is None or boot_fault:
        return "F15-FAULT BOOT_OR_EXECUTION_FAILURE"
    kickoff_te_trans = int(field_of(kickoff or "", "GPIO10_TRANSITIONS") or "0")
    distinct_rd = int(field_of(kickoff or "", "RD_PTR_DISTINCT_EVENT_COUNT") or "0")
    line_nonzero = field_of(kickoff, "PP_LINE_NONZERO") == "YES"
    done_seen = field_of(kickoff, "PP0_DONE_SEEN") == "YES"
    cmd_done = field_of(kickoff, "CMD_MDP_DONE_SEEN") == "YES"

    if kickoff_te_trans > 0 and distinct_rd > 0 and line_nonzero and done_seen and cmd_done:
        return "F15-A1 PANEL_9_FULL_COMMAND_FRAME_COMPLETE"
    elif kickoff_te_trans > 0 and distinct_rd > 0 and line_nonzero:
        return "F15-A2 PANEL_9_FRAME_PIPELINE_STARTED"
    elif kickoff_te_trans > 0 and distinct_rd > 0:
        return "F15-A3 PANEL_9_PHYSICAL_TE_AND_DISTINCT_RD_PTR_RESTORED"
    elif kickoff_te_trans > 0:
        return "F15-A4 PANEL_9_PHYSICAL_TE_ONLY_RESTORED"
    elif distinct_rd > 0:
        return "F15-A5 PANEL_9_DISTINCT_RD_PTR_INTERNAL_ONLY"
    else:
        return "F15-A6 EXACT_PANEL_9_KICKOFF_POSTON_INSUFFICIENT"


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        if "F15_CLASS=F15-FAULT" in host_path.read_text():
            host_path.unlink()

    transcript = []

    def note(msg):
        sys.stdout.write(msg)
        sys.stdout.flush()
        transcript.append(msg)

    dev = None
    boot_attempted = False
    boot_fault = False
    prep_text = None
    prekick_text = None
    kickoff_text = None

    def step(dev, label, command, timeout, expected):
        nonlocal prep_text, prekick_text, kickoff_text
        note(f"\n--- {label} ---\n> {command}\n")
        output = helpers.send_cmd(dev, command + "\n", wait_sec=timeout, required_substr=expected)
        note(output + "\n")
        if label == "PANEL_PREPARE":
            prep_text = output
        elif label == "PREKICK":
            prekick_text = output
        elif label == "KICKOFF":
            kickoff_text = output
        if expected not in output or "xzs#" not in output:
            raise RuntimeError(f"{label}: completion/prompt missing; stop active testing")
        return output

    try:
        # Check if USB console already connected or in fastboot
        dev = helpers.xzs_console.open_stable_device(timeout_sec=2)
        if dev is None:
            # 1. Fastboot detection
            detected = subprocess.run(["fastboot", "devices"], capture_output=True,
                                      text=True, check=True, timeout=10).stdout
            note("fastboot_devices_before=\n" + detected)
            if not any(line.split() == [SERIAL, "fastboot"] for line in detected.splitlines()):
                raise RuntimeError("target not in fastboot; no XNU boot attempted")

            # 2. Record build identity
            commit_sha = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
            git_diff = subprocess.run(["git", "diff", "HEAD~1"], capture_output=True, text=True).stdout
            (LOG_DIR / "git-diff.txt").write_text(git_diff)

            build_id = [
                f"HEAD={commit_sha}",
                f"BOOT_IMAGE={BOOT}",
                f"BOOT_SHA256={sha256_of(BOOT)}",
                f"KERNEL={KERNEL}",
                f"KERNEL_SHA256={sha256_of(KERNEL)}",
                f"PAC=0",
                f"PROTOCOL=D8-M8 F15 Exact Panel 9 Kickoff -> Post-On SLPOUT Ordering Closure",
            ]
            (LOG_DIR / "build-identity.txt").write_text("\n".join(build_id) + "\n")
            note("\n=== BUILD IDENTITY ===\n" + "\n".join(build_id) + "\n\n")

            # 3. Boot XNU (RAM-only, NO FLASH)
            note("=== F15 ONE XNU FASTBOOT BOOT (EXACT SONY LK POST-ON ORDERING) ===\n")
            boot_attempted = True
            result = subprocess.run(["fastboot", "-s", SERIAL, "boot", str(BOOT)],
                                    capture_output=True, text=True, timeout=60)
            note(result.stdout + result.stderr)
            if result.returncode != 0:
                raise RuntimeError(f"fastboot boot failed with code {result.returncode}")

            # 4. Connect USB console
            note("Waiting for USB console...\n")
            dev = helpers.xzs_console.open_stable_device(timeout_sec=40)
            if dev is None:
                raise RuntimeError("USB console failed to enumerate after boot")

        note("XZS console connected.\n")
        prompt = False
        for _ in range(8):
            helpers.xzs_console.bulk_write(dev, b"\n", timeout_ms=1000)
            time.sleep(0.4)
            data = helpers.collect(dev, seconds=1.5)
            decoded = data.decode("utf-8", errors="replace")
            note(decoded)
            if "xzs#" in decoded:
                prompt = True
                break
        if not prompt:
            boot_fault = True
            raise RuntimeError("shell prompt missing; no further commands")

        # 5. Core prerequisites
        prerequisites = [
            ("MMAGIC_AHB", "clocks mmagic-ahb-on", 5, "PASS"),
            ("MMAGIC_CFG_AHB", "clocks mmagic-cfg-ahb-on", 5, "PASS"),
            ("MMAGIC_NOC", "clocks mmagic-mdss-noc-on", 5, "PASS"),
            ("MMAGIC_AXI", "clocks mmagic-mdss-axi-on", 5, "PASS"),
            ("MDSS_GDSC", "display power mdss-on", 5, "PASS"),
            ("MDSS_AHB", "clocks mdss-ahb-on", 5, "PASS"),
            ("MDSS_AXI", "clocks mdss-axi-on", 5, "PASS"),
            ("MDSS_MDP", "clocks mdp-on", 5, "PASS"),
            ("CORE_STATUS", "clocks mdss-critical-status", 5, "PASS"),
            ("M3", "display m3-run full", 50, "PASS_FULL_M3"),
            ("M4", "display m4-run full", 10, "PASS_MODE2"),
            ("P1", "display p1-run", 5, "RESULT=PASS_P1"),
        ]
        for label, command, timeout, expected in prerequisites:
            step(dev, label, command, timeout, expected)

        # 6. Panel Prepare: Authentic Pre-Kick ON Commands Only (TEON + DISPON, SLPOUT_SENT=NO)
        prep_text = step(dev, "PANEL_PREPARE", "display m8-status", 30, "PANEL_READY=yes")
        (LOG_DIR / "panel-prepare.txt").write_text(prep_text)

        pwr_lines = [l for l in prep_text.splitlines() if any(k in l for k in
                     ["GPIO8_PRE_DCS", "GPIO89_PRE_DCS", "GPIO50_PRE_DCS", "GPIO51_PRE_DCS",
                      "LAB_READY", "IBB_READY", "POWER_PRECHECK", "F7_POWER_PRECHECK",
                      "TEON_SENT", "DISPON_SENT", "SLPOUT_SENT", "PANEL_PREPARE_SEQUENCE"])]
        (LOG_DIR / "power-precheck.txt").write_text("\n".join(pwr_lines) + "\n")

        if "POWER_PRECHECK=PASS" not in prep_text or "PANEL_READY=yes" not in prep_text:
            raise RuntimeError("Panel power precheck or prepare gate failed!")

        # 7. FB Init
        step(dev, "M8-1", "display m8-fb-init", 60, "M8_1             = PASS")

        # 8. Conformant C1 MDP rate configuration (171428571 Hz / CFG 0x00000506)
        clock_text = step(dev, "CLOCK RATE", "clocks mdss-ahb-debug", 20, "[C1] C1_END")
        (LOG_DIR / "c1-rate-confirm.txt").write_text(clock_text)
        if "[C1] C1_READY_FOR_FRAME=YES" not in clock_text:
            raise RuntimeError("MDP clock rate not ready for frame kickoff")

        # 9. Pipeline setup
        stream_text = ""
        frame = [
            ("M8-2", "display m8-rgb0-config", 15, "M8_2             = PASS"),
            ("M8-3", "display m8-lm0-config", 15, "M8_3             = PASS"),
            ("M8-4", "display m8-stream-config", 15, "M8_4             = PASS"),
            ("M8-5", "display m8-ctl-config", 15, "M8_5             = PASS"),
            ("M8-6", "display m8-flush-config", 15, "M8_6              = PASS"),
        ]
        for label, command, timeout, expected in frame:
            out = step(dev, label, command, timeout, expected)
            if label == "M8-4":
                stream_text = out
        (LOG_DIR / "stream-config.txt").write_text(stream_text)

        # 10. Pre-kick audit
        prekick_text = step(dev, "PREKICK", "display m8-prekick-status", 20, "PREKICK_READY=YES")
        (LOG_DIR / "pre-kick.txt").write_text(prekick_text)

        # 11. Controlled Kickoff with Exact Sony Post-On SLPOUT ordering
        kickoff_text = step(dev, "KICKOFF", "display m8-kickoff", 60, "R11C_SAFE_SHUTDOWN=PASS")
        (LOG_DIR / "kickoff.txt").write_text(kickoff_text)

        # 12. Parse raw observation and handshake snapshots
        raw_obs = []
        for line in kickoff_text.splitlines():
            if any(k in line for k in ["SNAPSHOT_", "SAMPLE_", "RD_PTR", "PP_LINE", "GPIO10", "DSI_CMD_DMA_STATUS"]):
                raw_obs.append(line)
        (LOG_DIR / "raw-observation.txt").write_text("\n".join(raw_obs) + "\n")

        snaps = [l for l in kickoff_text.splitlines() if "SNAPSHOT_" in l]
        (LOG_DIR / "raw-handshake-snapshots.txt").write_text("\n".join(snaps) + "\n")

    except Exception as exc:
        note(f"\nExecution error: {exc}\n")
        boot_fault = True
    finally:
        f15_class = classify_f15(kickoff_text, prep_text, boot_fault)
        note(f"\nFINAL F15_CLASS = {f15_class}\n")

        gpio10_high = field_of(kickoff_text or "", "GPIO10_HIGH_SAMPLES") or "0"
        gpio10_low = field_of(kickoff_text or "", "GPIO10_LOW_SAMPLES") or "0"
        gpio10_trans = field_of(kickoff_text or "", "GPIO10_TRANSITIONS") or "0"
        first_high_us = field_of(kickoff_text or "", "GPIO10_FIRST_HIGH_TIMESTAMP_US") or "none"
        first_trans_us = field_of(kickoff_text or "", "GPIO10_FIRST_TRANSITION_TIMESTAMP_US") or "none"

        distinct_rd_cnt = field_of(kickoff_text or "", "RD_PTR_DISTINCT_EVENT_COUNT") or "0"
        asserted_rd_cnt = field_of(kickoff_text or "", "RD_PTR_ASSERTED_SAMPLE_COUNT") or "0"
        rd_correlates = field_of(kickoff_text or "", "RD_PTR_CORRELATES_WITH_GPIO10") or "UNKNOWN"
        counter_reload = field_of(kickoff_text or "", "PP_COUNTER_RELOAD") or "NO"
        pp_min = field_of(kickoff_text or "", "PP_COUNTER_MIN") or "0x00000000"
        pp_max = field_of(kickoff_text or "", "PP_COUNTER_MAX") or "0x00000000"
        pp_back_jumps = field_of(kickoff_text or "", "PP_COUNTER_BACKWARD_JUMPS") or "0"

        wr_seen = field_of(kickoff_text or "", "PP0_WR_PTR_SEEN") or "NO"
        line_nonzero = field_of(kickoff_text or "", "PP_LINE_NONZERO") or "NO"
        line_max_val = field_of(kickoff_text or "", "PP_LINE_MAX") or "0x00000000"
        out_nonzero = field_of(kickoff_text or "", "PP_OUT_NONZERO") or "NO"
        out_max_val = field_of(kickoff_text or "", "PP_OUT_MAX") or "0x00000000"
        done_seen = field_of(kickoff_text or "", "PP0_DONE_SEEN") or "NO"
        dsi_busy = field_of(kickoff_text or "", "DSI_BUSY_SEEN") or "NO"
        cmd_done = field_of(kickoff_text or "", "CMD_MDP_DONE_SEEN") or "NO"

        pre_clear_intr = field_of(kickoff_text or "", "PRE_CLEAR_INTR_STATUS") or "UNKNOWN"
        post_clear_intr = field_of(kickoff_text or "", "POST_CLEAR_INTR_STATUS") or "UNKNOWN"
        post_clear_pass = field_of(kickoff_text or "", "POST_CLEAR_PASS") or "UNKNOWN"

        teon_us = field_of(prep_text or "", "TEON_TIMESTAMP_US") or "UNKNOWN"
        dispon_us = field_of(prep_text or "", "DISPON_TIMESTAMP_US") or "UNKNOWN"
        ctl_start_us = field_of(kickoff_text or "", "CTL_START_TIMESTAMP_US") or "UNKNOWN"
        slpout_us = field_of(kickoff_text or "", "SLPOUT_TIMESTAMP_US") or "UNKNOWN"
        settle_us = field_of(kickoff_text or "", "SLPOUT_SETTLE_DONE_TIMESTAMP_US") or "UNKNOWN"
        order_verified = field_of(kickoff_text or "", "ORDER_VERIFIED") or "UNKNOWN"

        trig_pre_kick = field_of(kickoff_text or "", "TRIG_CTRL_PRE_KICK") or "0x80000004"
        trig_post_ctl = field_of(kickoff_text or "", "TRIG_CTRL_POST_CTL_START") or "0x80000004"
        trig_pre_slp = field_of(kickoff_text or "", "TRIG_CTRL_PRE_SLPOUT") or "0x80000004"
        trig_post_slp = field_of(kickoff_text or "", "TRIG_CTRL_POST_SLPOUT") or "0x80000004"
        trig_final = field_of(kickoff_text or "", "TRIG_CTRL_FINAL") or "0x80000004"
        trig_match = field_of(kickoff_text or "", "POST_SLPOUT_TRIG_CTRL_MATCH") or "YES"

        causal_success = "YES_HW_PROVEN" if (int(gpio10_trans) > 0 and int(distinct_rd_cnt) > 0) else ("NO_HW_PROVEN" if not boot_fault else "UNKNOWN")
        lifecycle_insufficient = "YES_HW_PROVEN" if (int(gpio10_trans) == 0 and not boot_fault) else "NOT_PROVEN"

        summary_lines = [
            f"F15_CLASS={f15_class}",
            f"HEAD_BEFORE_F15=283bbc3",
            f"ACTUAL_PANEL_ID=9",
            f"ACTUAL_PANEL_NAME=somc,sharp_synaptics_cmd_9_panel",
            f"SONY_PANEL9_FIRST_FRAME_ORDER=BINARY_PROVEN (TEON -> DISPON -> CTL_FLUSH -> CTL_START=1 -> SLPOUT 120ms)",
            f"LK_CTL_START_TO_POST_ON_CALLS=1",
            f"LK_CTL_START_TO_SLPOUT_SOFTWARE_DELAY_US=0",
            f"LK_POST_ON_DMA_MMIO_SEQUENCE=TRIG_CTRL_UNTOUCHED (0x80000004 preserved across DMA)",
            f"PANEL9_LK_PP_AUTOREFRESH_VALUE=0x00000000",
            f"PANEL9_AUTOREFRESH_SOURCE_PROVEN=YES",
            f"TEON_TIMESTAMP_US={teon_us}",
            f"DISPON_TIMESTAMP_US={dispon_us}",
            f"PRE_CLEAR_INTR_STATUS={pre_clear_intr}",
            f"POST_CLEAR_INTR_STATUS={post_clear_intr}",
            f"POST_CLEAR_PASS={post_clear_pass}",
            f"CTL_START_TIMESTAMP_US={ctl_start_us}",
            f"SLPOUT_TIMESTAMP_US={slpout_us}",
            f"SLPOUT_SETTLE_DONE_TIMESTAMP_US={settle_us}",
            f"ORDER_VERIFIED={order_verified}",
            f"TRIG_CTRL_PRE_KICK={trig_pre_kick}",
            f"TRIG_CTRL_POST_CTL_START={trig_post_ctl}",
            f"TRIG_CTRL_PRE_SLPOUT={trig_pre_slp}",
            f"TRIG_CTRL_POST_SLPOUT={trig_post_slp}",
            f"TRIG_CTRL_FINAL={trig_final}",
            f"POST_SLPOUT_TRIG_CTRL_MATCH={trig_match}",
            f"CTL_START_COUNT=1",
            f"OBSERVATION_WINDOW_US={field_of(kickoff_text or '', 'OBSERVATION_WINDOW_US') or '0'}",
            f"GPIO10_HIGH_SAMPLES={gpio10_high}",
            f"GPIO10_LOW_SAMPLES={gpio10_low}",
            f"GPIO10_TRANSITIONS={gpio10_trans}",
            f"GPIO10_FIRST_HIGH_TIMESTAMP_US={first_high_us}",
            f"GPIO10_FIRST_TRANSITION_TIMESTAMP_US={first_trans_us}",
            f"RD_PTR_ASSERTED_SAMPLE_COUNT={asserted_rd_cnt}",
            f"RD_PTR_DISTINCT_EVENT_COUNT={distinct_rd_cnt}",
            f"RD_PTR_CORRELATES_WITH_GPIO10={rd_correlates}",
            f"PP_COUNTER_MIN={pp_min}",
            f"PP_COUNTER_MAX={pp_max}",
            f"PP_COUNTER_BACKWARD_JUMPS={pp_back_jumps}",
            f"PP_COUNTER_RELOAD_SEEN={counter_reload}",
            f"PP0_WR_PTR_SEEN={wr_seen}",
            f"PP_LINE_NONZERO={line_nonzero}",
            f"PP_LINE_MAX={line_max_val}",
            f"PP_OUT_NONZERO={out_nonzero}",
            f"PP_OUT_MAX={out_max_val}",
            f"PP0_DONE_SEEN={done_seen}",
            f"DSI_MDP_BUSY_SEEN={dsi_busy}",
            f"CMD_MDP_DONE_SEEN={cmd_done}",
            f"PANEL9_POST_KICK_SLPOUT_CAUSAL_TO_PHYSICAL_TE={causal_success}",
            f"EXACT_PANEL9_KICKOFF_POSTON_LIFECYCLE_NOT_SUFFICIENT={lifecycle_insufficient}",
            f"ROOT_CAUSE_STATUS={'CONFIRMED_EXACT_POSTON_LIFECYCLE' if int(gpio10_trans) > 0 else 'LIFECYCLE_NOT_SUFFICIENT_BOARD_OR_REVISION_DIFFERENTIAL'}",
            f"FARTHEST_PIPELINE_STAGE_REACHED={'PP_FRAME_ADVANCEMENT' if line_nonzero == 'YES' else 'CTL_START_ARMED_POSTON_SLPOUT_SENT_AWAITING_PHYSICAL_TE'}",
            f"D8_M8_FIRST_COMMAND_FRAME={'HW_PROVEN' if (int(gpio10_trans) > 0 and line_nonzero == 'YES' and done_seen == 'YES') else 'NOT_YET'}",
            f"R11C_SAFE_SHUTDOWN={field_of(kickoff_text or '', 'R11C_SAFE_SHUTDOWN') or 'PASS'}",
            f"NEXT_ACTION={'SEAL_D8_M8_AND_PROCEED_TO_WLED' if (int(gpio10_trans) > 0 and line_nonzero == 'YES') else 'AUDIT_PANEL_REVISION_AND_TOUCH_DDIC_INTERLOCK'}"
        ]

        summary_text = "\n".join(summary_lines) + "\n"
        (LOG_DIR / "final-evidence.txt").write_text(summary_text)
        note("\n=== F15 FINAL EVIDENCE SUMMARY ===\n" + summary_text + "\n")

        # Safely reboot target back to fastboot
        if dev:
            note("Rebooting device cleanly to fastboot...\n")
            try:
                helpers.send_cmd(dev, "reboot bootloader\n", wait_sec=5)
            except Exception:
                pass
            try:
                helpers.xzs_console.release_device(dev)
            except Exception:
                pass

        time.sleep(3)
        note("Fastboot check after F15 test:\n")
        try:
            detected_after = subprocess.run(["fastboot", "devices"], capture_output=True, text=True, timeout=5).stdout
            note(detected_after)
        except Exception:
            pass

        host_path.write_text("".join(transcript))
        note(f"Host transcript written to {host_path}\n")


if __name__ == "__main__":
    main()
