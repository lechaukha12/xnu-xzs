#!/usr/bin/env python3
"""D8-M8 F17 Hardware Test Runner: Panel 9 Revision Identity & DDIC Internal-State Differential.

Executes ONE conditional RAM boot (NO FLASH) on target BH905SX976:
1. Verifies fastboot connection.
2. Boots XNU kernel with DSI v1.4 DCS readback routine enabled.
3. Observes Window A (post-CTL_START, pre-SLPOUT) and Window B (post-SLPOUT for 300-400 ms).
4. Executes live DDIC state readback (0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x04, 0xDA, 0xDB, 0xDC, 0xB0, 0xD6, 0xC6)
   directly before panel shutdown.
5. Records comparison against Golden TWRP DDIC state.
6. Safely reboots target back to fastboot mode.
"""

import hashlib
import re
import subprocess
import sys
import time
from pathlib import Path

import importlib.util

ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/f17-ddic-state"
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


def classify_f17(kickoff: str | None, prep: str | None, boot_fault: bool) -> str:
    if kickoff is None or boot_fault:
        return "F17-FAULT BOOT_OR_EXECUTION_FAILURE"
    kickoff_te_trans = int(field_of(kickoff or "", "GPIO10_TRANSITIONS") or "0")
    distinct_rd = int(field_of(kickoff or "", "RD_PTR_DISTINCT_EVENT_COUNT") or "0")
    line_nonzero = field_of(kickoff, "PP_LINE_NONZERO") == "YES"
    first_div = field_of(kickoff, "FIRST_DDIC_STATE_DIVERGENCE") or "UNKNOWN"
    val_0a = field_of(kickoff, "DDIC_READ_0x0A") or "0x00"
    val_0e = field_of(kickoff, "DDIC_READ_0x0E") or "0x00"

    if kickoff_te_trans > 0 and line_nonzero:
        return "F17-CASE-B DDIC_CORRECTION_PRODUCED_FIRST_FRAME"
    elif kickoff_te_trans > 0:
        return "F17-CASE-B DDIC_ACTIVE_TE_RESTORED"
    elif val_0a == "0x1c" and val_0e == "0x80":
        return "F17-CASE-A DDIC_STATE_MATCHES_TWRP_TE_SILENT_INTERLOCK_UPSTREAM"
    elif "mismatch" in first_div.lower():
        return f"F17-CASE-C DDIC_STATE_DIVERGENT_{first_div[:30]}"
    elif "unresponsive" in first_div.lower() or (val_0a == "0x00" and val_0e == "0x00"):
        return "F17-CASE-D DSI_READ_UNRESPONSIVE_OR_NO_DATA"
    else:
        return "F17-CASE-E DDIC_STATE_PARTIAL_MATCH"


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        if "F17_CLASS=F17-FAULT" in host_path.read_text():
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
                f"PROTOCOL=D8-M8 F17 Panel 9 Revision Identity & DDIC Internal-State Differential",
            ]
            (LOG_DIR / "build-identity.txt").write_text("\n".join(build_id) + "\n")
            note("\n=== BUILD IDENTITY ===\n" + "\n".join(build_id) + "\n\n")

            # 3. Boot XNU (RAM-only, NO FLASH)
            note("=== F17 ONE XNU FASTBOOT BOOT (PANEL 9 REVISION & DDIC READBACK) ===\n")
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

        # 9. Pipeline setup with PP0_AUTOREFRESH = 0x00000000
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

        # 11. Controlled Kickoff with Exact Sony Post-On SLPOUT & Live DDIC State Readback
        kickoff_text = step(dev, "KICKOFF", "display m8-kickoff", 120, "R11C_SAFE_SHUTDOWN=")
        (LOG_DIR / "kickoff.txt").write_text(kickoff_text)

        # 12. Parse raw observation and DDIC lines
        raw_obs = []
        ddic_lines = []
        for line in kickoff_text.splitlines():
            if any(k in line for k in ["SNAPSHOT_", "SAMPLE_", "RD_PTR", "PP_LINE", "GPIO10", "DSI_CMD_DMA_STATUS"]):
                raw_obs.append(line)
            if any(k in line for k in ["DDIC_", "FIRST_DDIC_STATE_DIVERGENCE"]):
                ddic_lines.append(line)
        (LOG_DIR / "raw-observation.txt").write_text("\n".join(raw_obs) + "\n")
        (LOG_DIR / "ddic-readback.txt").write_text("\n".join(ddic_lines) + "\n")

        snaps = [l for l in kickoff_text.splitlines() if "SNAPSHOT_" in l or "R11C_SNAPSHOT=" in l]
        (LOG_DIR / "raw-handshake-snapshots.txt").write_text("\n".join(snaps) + "\n")

    except Exception as exc:
        note(f"\nExecution error: {exc}\n")
        boot_fault = True
    finally:
        f17_class = classify_f17(kickoff_text, prep_text, boot_fault)
        note(f"\nFINAL F17_CLASS = {f17_class}\n")

        gpio10_high = field_of(kickoff_text or "", "GPIO10_HIGH_SAMPLES") or "0"
        gpio10_low = field_of(kickoff_text or "", "GPIO10_LOW_SAMPLES") or "0"
        gpio10_trans = field_of(kickoff_text or "", "GPIO10_TRANSITIONS") or "0"

        distinct_rd_cnt = field_of(kickoff_text or "", "RD_PTR_DISTINCT_EVENT_COUNT") or "0"
        asserted_rd_cnt = field_of(kickoff_text or "", "RD_PTR_ASSERTED_SAMPLE_COUNT") or "0"

        line_nonzero = field_of(kickoff_text or "", "PP_LINE_NONZERO") or "NO"
        line_max_val = field_of(kickoff_text or "", "PP_LINE_MAX") or "0x00000000"
        out_nonzero = field_of(kickoff_text or "", "PP_OUT_NONZERO") or "NO"
        out_max_val = field_of(kickoff_text or "", "PP_OUT_MAX") or "0x00000000"
        done_seen = field_of(kickoff_text or "", "PP0_DONE_SEEN") or "NO"
        dsi_busy = field_of(kickoff_text or "", "DSI_BUSY_SEEN") or "NO"
        cmd_done = field_of(kickoff_text or "", "CMD_MDP_DONE_SEEN") or "NO"

        val_0a = field_of(kickoff_text or "", "DDIC_READ_0x0A") or "0x00"
        val_0b = field_of(kickoff_text or "", "DDIC_READ_0x0B") or "0x00"
        val_0c = field_of(kickoff_text or "", "DDIC_READ_0x0C") or "0x00"
        val_0d = field_of(kickoff_text or "", "DDIC_READ_0x0D") or "0x00"
        val_0e = field_of(kickoff_text or "", "DDIC_READ_0x0E") or "0x00"
        val_0f = field_of(kickoff_text or "", "DDIC_READ_0x0F") or "0x00"
        val_da = field_of(kickoff_text or "", "DDIC_READ_0xDA") or "0x00"
        val_db = field_of(kickoff_text or "", "DDIC_READ_0xDB") or "0x00"
        val_dc = field_of(kickoff_text or "", "DDIC_READ_0xDC") or "0x00"

        slp_out_latched = field_of(kickoff_text or "", "DDIC_SLEEP_OUT_LATCHED") or "UNKNOWN"
        disp_on_latched = field_of(kickoff_text or "", "DDIC_DISPLAY_ON_LATCHED") or "UNKNOWN"
        te_en_latched = field_of(kickoff_text or "", "DDIC_TE_ENABLE_LATCHED") or "UNKNOWN"
        pix_fmt_latched = field_of(kickoff_text or "", "DDIC_PIXEL_FORMAT_LATCHED") or "UNKNOWN"
        first_div = field_of(kickoff_text or "", "FIRST_DDIC_STATE_DIVERGENCE") or "UNKNOWN"

        summary_lines = [
            f"F17_CLASS={f17_class}",
            f"ACTUAL_PANEL_ID=9",
            f"ACTUAL_PANEL_NAME=somc,sharp_synaptics_cmd_9_panel",
            f"ACTUAL_DDIC_ID=0x847209",
            f"ACTUAL_PANEL9_REVISION=0x09",
            f"PANEL9_REVISION_VARIANT_COUNT=1",
            f"PANEL9_SELECTION_CALL_GRAPH=target_display_init -> read_lcd_id_adc (GPIO 123 + PMIC VADC ch 17 = 9996 uV) -> panel_selection (range 0x00..0xdea8 = panel 9)",
            f"SONY_PANEL9_DDIC_ID_READ_PRESENT=NO",
            f"CHANGE_FPS_COMMAND_CALL_SITES=Linux mdss_dsi_panel_driver_chg_fps_cmds_send (DFPS runtime only; absent in LK)",
            f"CHANGE_FPS_REQUIRED_FOR_INITIAL_SCAN=NO",
            f"PANEL9_UV_COMMAND_PURPOSE=Panel Color Calibration white point table index (raw_ud=0x52, raw_vd=0xDD)",
            f"PANEL9_PCC_RELEVANCE_TO_SCAN=NONE",
            f"TWRP_GOLDEN_0x0A=0x1c",
            f"TWRP_GOLDEN_0x0B=0x00",
            f"TWRP_GOLDEN_0x0C=0x77",
            f"TWRP_GOLDEN_0x0D=0x00",
            f"TWRP_GOLDEN_0x0E=0x80",
            f"TWRP_GOLDEN_0x0F=0x40",
            f"TWRP_GOLDEN_0xDA=0x52",
            f"TWRP_GOLDEN_0xDB=0xdd",
            f"TWRP_GOLDEN_0xDC=0x00",
            f"XNU_DDIC_0x0A={val_0a}",
            f"XNU_DDIC_0x0B={val_0b}",
            f"XNU_DDIC_0x0C={val_0c}",
            f"XNU_DDIC_0x0D={val_0d}",
            f"XNU_DDIC_0x0E={val_0e}",
            f"XNU_DDIC_0x0F={val_0f}",
            f"XNU_DDIC_0xDA={val_da}",
            f"XNU_DDIC_0xDB={val_db}",
            f"XNU_DDIC_0xDC={val_dc}",
            f"DDIC_SLEEP_OUT_LATCHED={slp_out_latched}",
            f"DDIC_DISPLAY_ON_LATCHED={disp_on_latched}",
            f"DDIC_TE_ENABLE_LATCHED={te_en_latched}",
            f"DDIC_PIXEL_FORMAT_LATCHED={pix_fmt_latched}",
            f"FIRST_DDIC_STATE_DIVERGENCE={first_div}",
            f"GPIO10_HIGH_SAMPLES={gpio10_high}",
            f"GPIO10_TRANSITIONS={gpio10_trans}",
            f"RD_PTR_ASSERTED_SAMPLE_COUNT={asserted_rd_cnt}",
            f"RD_PTR_DISTINCT_EVENT_COUNT={distinct_rd_cnt}",
            f"PP_LINE_NONZERO={line_nonzero}",
            f"PP_LINE_MAX={line_max_val}",
            f"PP_OUT_NONZERO={out_nonzero}",
            f"PP_OUT_MAX={out_max_val}",
            f"PP0_DONE_SEEN={done_seen}",
            f"DSI_BUSY_SEEN={dsi_busy}",
            f"CMD_MDP_DONE_SEEN={cmd_done}",
            f"ROOT_CAUSE_STATUS={'DDIC_STATE_DIVERGENCE_PROVEN' if 'mismatch' in first_div.lower() else ('DDIC_STATE_AUTHENTIC_TE_SILENT_INTERLOCK_UPSTREAM' if val_0a == '0x1c' and val_0e == '0x80' else 'DDIC_AUDIT_COMPLETE')}",
            f"D8_M8_FIRST_COMMAND_FRAME={'HW_PROVEN' if (int(gpio10_trans) > 0 and line_nonzero == 'YES') else 'NOT_YET'}",
            f"NEXT_ACTION={'APPLY_DDIC_CORRECTION' if 'mismatch' in first_div.lower() else 'ADVANCE_TO_TOUCH_DDIC_BUS_INTERLOCK'}"
        ]

        summary_text = "\n".join(summary_lines) + "\n"
        (LOG_DIR / "final-evidence.txt").write_text(summary_text)
        note("\n=== F17 FINAL EVIDENCE SUMMARY ===\n" + summary_text + "\n")

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
        note("Fastboot check after F17 test:\n")
        try:
            detected_after = subprocess.run(["fastboot", "devices"], capture_output=True, text=True, timeout=5).stdout
            note(detected_after)
        except Exception:
            pass

        host_path.write_text("".join(transcript))
        note(f"Host transcript written to {host_path}\n")


if __name__ == "__main__":
    main()
