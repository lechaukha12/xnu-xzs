#!/usr/bin/env python3
"""D8-M8 F3 Hardware Runner: DSI Command-Mode Dispatch Root-Cause Investigation & Single Conditional Correction Boot.

Phase F3: Tests the hypothesis that restoring DSI_COMMAND_MODE_MDP_CTRL (0x00994040)
from 0x00000008 to the working TWRP golden value 0x06100006 enables Ping-Pong buffer
line advancement (PP_LINE > 0) upon external TE synchronization.
Executes exactly ONE RAM boot, no flash, exactly one CTL_START kickoff,
captures 100 ms passive telemetry, and records comprehensive evidence in
artifacts/hw/d8m8/f3-dispatch-trace/.
"""

import importlib.util
import os
import re
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/f3-dispatch-trace"
SERIAL = "BH905SX976"
BOOT = ROOT / "artifacts/builds/xzs-xnu-boot.img"
KERNEL = ROOT / "src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple"

spec = importlib.util.spec_from_file_location(
    "r11a_helpers", ROOT / "scripts/display/d8m8_7_retry11a_runner.py"
)
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)


def sha256_of(path):
    out = subprocess.run(["shasum", "-a", "256", str(path)], capture_output=True, text=True)
    return out.stdout.split()[0] if out.returncode == 0 else "UNKNOWN"


def word_of(text, key):
    needle = key + "=0x"
    for line in text.splitlines():
        if needle in line:
            raw = line.split(needle, 1)[1].split()[0].strip()
            try:
                return int(raw, 16)
            except ValueError:
                return None
    return None


def field_of(text, key):
    needle = key + "="
    for line in text.splitlines():
        if needle in line:
            return line.split(needle, 1)[1].strip().split()[0]
    return None


def hex_or_unknown(value):
    if value is None:
        return "UNKNOWN"
    return "0x%08x" % value


def rows_from(prefix, text):
    lines = [line.strip() for line in text.splitlines() if line.startswith(prefix)]
    return lines, [dict(re.findall(r"([A-Z0-9_]+)=([^ ]+)", line)) for line in lines]


def classify_f3(prekick, kickoff, boot_fault):
    if boot_fault:
        return "F3-FAULT"
    if prekick is None or kickoff is None:
        return "F3-FAULT"
    if "C2_PP_GOLDEN_READBACK_FAIL=yes" in kickoff or "PP_GOLDEN_CONFIG_READBACK=FAIL" in kickoff:
        return "F3-READBACK-FAIL"

    bjumps = field_of(kickoff, "F2_BACKWARD_JUMPS") or field_of(kickoff, "F1_BACKWARD_JUMPS")
    rd_cnt = field_of(kickoff, "F2_PP0_RD_PTR_COUNT") or field_of(kickoff, "F1_PP0_RD_PTR_COUNT")
    line_max = word_of(kickoff, "F2_PP_LINE_MAX") or word_of(kickoff, "F1_PP_LINE_MAX") or word_of(kickoff, "MAX_LINE_COUNT_OBSERVED") or 0
    out_max = word_of(kickoff, "F2_PP_OUT_MAX") or word_of(kickoff, "F1_PP_OUT_MAX") or word_of(kickoff, "MAX_OUT_LINE_COUNT_OBSERVED") or 0
    done_seen = field_of(kickoff, "PP0_DONE_OBSERVED") == "yes" or field_of(kickoff, "F2_PP_DONE_SEEN") == "yes"
    dsi_busy = field_of(kickoff, "DSI_MDP_BUSY_SEEN") == "yes" or field_of(kickoff, "F2_DSI_BUSY_SEEN") == "yes"
    cmd_done = field_of(kickoff, "DSI_MDP_DONE_RAW_SEEN") == "yes" or field_of(kickoff, "F2_CMD_MDP_DONE_SEEN") == "yes"

    if line_max > 0 and out_max > 0 and (done_seen or dsi_busy or cmd_done):
        return "F3-FRAME-COMPLETE"
    if line_max > 0:
        return "F3-PP-LINE-STARTED"
    if rd_cnt is not None and rd_cnt != "0":
        return "F3-TE-SYNC-PP-STALLED"
    return "F3-NO-TE"


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        raise RuntimeError("host.txt exists: refusing a duplicate F3 XNU boot")
    if not BOOT.is_file():
        raise RuntimeError(f"boot image missing: {BOOT}")

    transcript = []
    boot_attempted = False
    clock_text = ""
    stream_text = ""
    prekick_text = ""
    kickoff_text = None
    boot_fault = False
    m8_1 = "NO"

    def note(message):
        transcript.append(message)
        print(message, end="", flush=True)

    def step(dev, label, command, timeout, expected):
        nonlocal clock_text, stream_text, prekick_text, kickoff_text
        note(f"\n--- {label} ---\n> {command}\n")
        output = helpers.send_cmd(dev, command + "\n", wait_sec=timeout,
                                  required_substr=expected)
        transcript.append(output)
        if label == "CLOCK RATE":
            clock_text = output
        elif label == "M8-4":
            stream_text = output
        elif label == "PREKICK":
            prekick_text = output
        elif label == "ONE KICKOFF":
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
            git_diff = subprocess.run(["git", "diff", "HEAD"], capture_output=True, text=True).stdout
            (LOG_DIR / "git-diff.txt").write_text(git_diff)

            build_id = [
                f"HEAD={commit_sha}",
                f"BOOT_IMAGE={BOOT}",
                f"BOOT_SHA256={sha256_of(BOOT)}",
                f"KERNEL={KERNEL}",
                f"KERNEL_SHA256={sha256_of(KERNEL)}",
                f"PAC=0",
            ]
            (LOG_DIR / "build-identity.txt").write_text("\n".join(build_id) + "\n")
            note("\n=== BUILD IDENTITY ===\n" + "\n".join(build_id) + "\n\n")

            # 3. Boot XNU (RAM-only, NO FLASH)
            note("=== F3 ONE XNU FASTBOOT BOOT (DSI COMMAND MDP DISPATCH TEST) ===\n")
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
                raise RuntimeError("XZS console did not enumerate after fastboot boot")
        else:
            note("Using active XNU console session from current boot.\n")
            boot_attempted = True
            commit_sha = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
            git_diff = subprocess.run(["git", "diff", "HEAD"], capture_output=True, text=True).stdout
            (LOG_DIR / "git-diff.txt").write_text(git_diff)
            build_id = [
                f"HEAD={commit_sha}",
                f"BOOT_IMAGE={BOOT}",
                f"BOOT_SHA256={sha256_of(BOOT)}",
                f"KERNEL={KERNEL}",
                f"KERNEL_SHA256={sha256_of(KERNEL)}",
                f"PAC=0",
            ]
            (LOG_DIR / "build-identity.txt").write_text("\n".join(build_id) + "\n")
            note("\n=== BUILD IDENTITY ===\n" + "\n".join(build_id) + "\n\n")

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
            ("PANEL_PREPARE", "display m8-status", 30, "PANEL_READY=yes"),
            ("M8-1", "display m8-fb-init", 60, "M8_1             = PASS"),
        ]
        for label, command, timeout, expected in prerequisites:
            step(dev, label, command, timeout, expected)
        m8_1 = "YES"

        # 6. Conformant C1 MDP rate configuration (171428571 Hz / CFG 0x00000506)
        clock_text = step(dev, "CLOCK RATE", "clocks mdss-ahb-debug", 20, "[C1] C1_END")
        (LOG_DIR / "c1-rate-confirm.txt").write_text(clock_text)
        if "[C1] C1_READY_FOR_FRAME=YES" not in clock_text:
            raise RuntimeError("MDP clock rate not ready for frame kickoff")

        # 7. Pipeline setup
        frame = [
            ("M8-2", "display m8-rgb0-config", 15, "M8_2             = PASS"),
            ("M8-3", "display m8-lm0-config", 15, "M8_3             = PASS"),
            ("M8-4", "display m8-stream-config", 15, "M8_4             = PASS"),
            ("M8-5", "display m8-ctl-config", 15, "M8_5             = PASS"),
            ("M8-6", "display m8-flush-config", 15, "M8_6              = PASS"),
        ]
        for label, command, timeout, expected in frame:
            step(dev, label, command, timeout, expected)
        (LOG_DIR / "stream-config.txt").write_text(stream_text)

        # 8. Prekick status check
        prekick_text = step(dev, "PREKICK", "display m8-prekick-status", 20, "PREKICK_READY=YES")
        (LOG_DIR / "pre-kick.txt").write_text(prekick_text)

        for marker in ("PANEL_READY=yes", "CTL_START_COUNT=0", "MDP_KICKOFF_COUNT=0",
                       "DSI_TRIG_CTRL=0x80000004", "PREKICK_READY=YES"):
            if marker not in prekick_text:
                raise RuntimeError(f"pre-kick guard missing: {marker}")

        # 9. Exactly ONE Kickoff with 100 ms observation
        kickoff_text = step(dev, "ONE KICKOFF", "display m8-kickoff", 60, "R11C_SAFE_SHUTDOWN=PASS")
        (LOG_DIR / "kickoff.txt").write_text(kickoff_text)

        # 10. Extract high-frequency samples
        snap_lines, _ = rows_from("R11C_SNAPSHOT=", kickoff_text)
        (LOG_DIR / "raw-handshake-snapshots.txt").write_text("\n".join(snap_lines) + ("\n" if snap_lines else ""))
        sample_lines, _ = rows_from("R11DB_SAMPLE ", kickoff_text)
        (LOG_DIR / "raw-frame-observations.txt").write_text("\n".join(sample_lines) + ("\n" if sample_lines else ""))

        if "CTL_START_COUNT=1" not in kickoff_text or "MDP_KICKOFF_COUNT=1" not in kickoff_text:
            raise RuntimeError("kickoff count was not exactly one")

        note("F3_RUN_COMPLETE=yes\nNO_SECOND_KICKOFF=yes\n")
        return 0

    except Exception as exc:
        note(f"F3_STOP_REASON={type(exc).__name__}: {exc}\n")
        note("NO_FURTHER_XNU_COMMANDS=yes\n")
        if boot_attempted and ("USB" in str(exc) or "enumerate" in str(exc) or "prompt" in str(exc)):
            boot_fault = True
        return 1

    finally:
        note(f"XNU_BOOT_ATTEMPTED={'yes' if boot_attempted else 'no'}\n")
        klass = classify_f3(prekick_text, kickoff_text, boot_fault)

        window_us = field_of(kickoff_text or "", "F2_OBSERVATION_WINDOW_US") or field_of(kickoff_text or "", "F1_OBSERVATION_WINDOW_US") or field_of(kickoff_text or "", "OBSERVATION_ELAPSED_US")
        poll_iters = field_of(kickoff_text or "", "F2_POLL_ITERATIONS") or field_of(kickoff_text or "", "F1_POLL_ITERATIONS")
        min_cnt = hex_or_unknown(word_of(kickoff_text or "", "F2_MIN_PP_INT_COUNT") or word_of(kickoff_text or "", "F1_MIN_PP_INT_COUNT"))
        max_cnt = hex_or_unknown(word_of(kickoff_text or "", "F2_MAX_PP_INT_COUNT") or word_of(kickoff_text or "", "F1_MAX_PP_INT_COUNT"))
        bjumps = field_of(kickoff_text or "", "F2_BACKWARD_JUMPS") or field_of(kickoff_text or "", "F1_BACKWARD_JUMPS")
        largest_neg = field_of(kickoff_text or "", "F2_LARGEST_NEG_DELTA") or field_of(kickoff_text or "", "F1_LARGEST_NEG_DELTA")
        rd_cnt = field_of(kickoff_text or "", "F2_PP0_RD_PTR_COUNT") or field_of(kickoff_text or "", "F1_PP0_RD_PTR_COUNT")
        wr_cnt = field_of(kickoff_text or "", "F2_PP0_WR_PTR_COUNT") or field_of(kickoff_text or "", "F1_PP0_WR_PTR_COUNT")
        pp_line_max = hex_or_unknown(word_of(kickoff_text or "", "F2_PP_LINE_MAX") or word_of(kickoff_text or "", "F1_PP_LINE_MAX") or word_of(kickoff_text or "", "MAX_LINE_COUNT_OBSERVED"))
        pp_out_max = hex_or_unknown(word_of(kickoff_text or "", "F2_PP_OUT_MAX") or word_of(kickoff_text or "", "F1_PP_OUT_MAX") or word_of(kickoff_text or "", "MAX_OUT_LINE_COUNT_OBSERVED"))
        pp_done_seen = field_of(kickoff_text or "", "F2_PP_DONE_SEEN") or field_of(kickoff_text or "", "F1_PP_DONE_SEEN") or field_of(kickoff_text or "", "PP0_DONE_OBSERVED") or "no"
        dsi_busy_seen = field_of(kickoff_text or "", "F2_DSI_BUSY_SEEN") or field_of(kickoff_text or "", "F1_DSI_BUSY_SEEN") or field_of(kickoff_text or "", "DSI_MDP_BUSY_SEEN") or "no"
        cmd_done_seen = field_of(kickoff_text or "", "F2_CMD_MDP_DONE_SEEN") or field_of(kickoff_text or "", "F1_CMD_MDP_DONE_SEEN") or field_of(kickoff_text or "", "DSI_MDP_DONE_RAW_SEEN") or "no"

        te_restored = "YES — HW_PROVEN" if (rd_cnt is not None and rd_cnt != "0") else "NO"
        line_started = "YES — HW_PROVEN" if (word_of(kickoff_text or "", "F2_PP_LINE_MAX") or 0) > 0 else "NO"

        summary = [
            f"F3_CLASS={klass}",
            f"OBSERVATION_WINDOW_US={window_us or 'UNKNOWN'}",
            f"POLL_ITERATIONS={poll_iters or 'UNKNOWN'}",
            f"MIN_PP_INT_COUNT={min_cnt}",
            f"MAX_PP_INT_COUNT={max_cnt}",
            f"BACKWARD_JUMPS={bjumps or 'UNKNOWN'}",
            f"LARGEST_NEG_DELTA={largest_neg or 'UNKNOWN'}",
            f"PP0_RD_PTR_COUNT={rd_cnt or 'UNKNOWN'}",
            f"PP0_WR_PTR_COUNT={wr_cnt or 'UNKNOWN'}",
            f"PP_LINE_MAX={pp_line_max}",
            f"PP_OUT_MAX={pp_out_max}",
            f"PP_DONE_SEEN={pp_done_seen}",
            f"DSI_BUSY_SEEN={dsi_busy_seen}",
            f"CMD_MDP_DONE_SEEN={cmd_done_seen}",
            f"TE_RESTORED={te_restored}",
            f"PP_LINE_NONZERO={line_started}",
        ]
        text = "\n".join(summary) + "\n"
        note("\n=== F3 SUMMARY ===\n" + text)
        if boot_attempted:
            host_path.write_text("".join(transcript))
            (LOG_DIR / "final-evidence.txt").write_text(text)
        else:
            (LOG_DIR / "not-booted.txt").write_text("".join(transcript))


if __name__ == "__main__":
    sys.exit(main())
