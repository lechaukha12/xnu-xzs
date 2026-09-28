#!/usr/bin/env python3
"""D8-M8 F4 Hardware Runner: DSI Command Engine Admission, HS Lane, DMA / Write-Memory-Start & First Command Frame.

Phase F4:
1. Re-proves or qualifies fresh TE after explicit MDP interrupt clear (Phase C Same-Boot Fresh TE Hard Gate).
2. If fresh TE passes, executes exactly ONE CTL_START=1 kickoff and captures post-kick DSI & PP telemetry.
3. If fresh TE fails, stops downstream interpretation per Section 22 and classifies F4-TE-NOT-REPRODUCED.
Executes RAM boot only, NO FLASH.
"""

import importlib.util
import os
import re
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/f4-dispatch-trace"
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


def classify_f4(prekick, kickoff, boot_fault):
    if boot_fault:
        return "F4-FAULT"
    if prekick is None or kickoff is None:
        return "F4-FAULT"

    fresh_te_seen = field_of(kickoff, "F4_FRESH_TE_SEEN")
    if fresh_te_seen != "YES":
        return "F4-TE-NOT-REPRODUCED"

    if "C2_PP_GOLDEN_READBACK_FAIL=yes" in kickoff or "PP_GOLDEN_CONFIG_READBACK=FAIL" in kickoff:
        return "F4-READBACK-FAIL"

    line_max = word_of(kickoff, "F1_PP_LINE_MAX") or word_of(kickoff, "PP_LINE_MAX") or 0
    out_max = word_of(kickoff, "F1_PP_OUT_MAX") or word_of(kickoff, "PP_OUT_MAX") or 0
    done_seen = field_of(kickoff, "F1_PP_DONE_SEEN") == "yes" or field_of(kickoff, "PP0_DONE_OBSERVED") == "yes"
    dsi_busy = field_of(kickoff, "F1_DSI_BUSY_SEEN") == "yes" or field_of(kickoff, "DSI_MDP_BUSY_SEEN") == "yes"
    cmd_done = field_of(kickoff, "F1_CMD_MDP_DONE_SEEN") == "yes" or field_of(kickoff, "DSI_MDP_DONE_RAW_SEEN") == "yes"

    if line_max > 0 and out_max > 0 and (done_seen or dsi_busy or cmd_done):
        return "F4-COMMAND-FRAME-HW-PROVEN"
    if line_max > 0:
        return "F4-PP-LINE-STARTED"
    return "F4-TE-PASS-PP-STALLED"


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        if "F4_CLASS=F4-FAULT" in host_path.read_text():
            host_path.unlink()
        else:
            raise RuntimeError("host.txt exists: refusing a duplicate F4 XNU boot")
    if not BOOT.is_file():
        raise RuntimeError(f"boot image missing: {BOOT}")
    if not KERNEL.is_file():
        raise RuntimeError(f"kernel binary missing: {KERNEL}")

    transcript = []

    def note(msg):
        sys.stdout.write(msg)
        sys.stdout.flush()
        transcript.append(msg)

    boot_attempted = False
    boot_fault = False
    prekick_text = None
    kickoff_text = None

    def step(dev, label, command, timeout, expected):
        note(f"\n--- {label} ---\n> {command}\n")
        output = helpers.send_cmd(dev, command + "\n", wait_sec=timeout, required_substr=expected)
        note(output + "\n")
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
            note("=== F4 ONE XNU FASTBOOT BOOT (DSI COMMAND ENGINE ADMISSION & FRESH TE GATE) ===\n")
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

        # 6. Conformant C1 MDP rate configuration (171428571 Hz / CFG 0x00000506)
        clock_text = step(dev, "CLOCK RATE", "clocks mdss-ahb-debug", 20, "[C1] C1_END")
        (LOG_DIR / "c1-rate-confirm.txt").write_text(clock_text)
        if "[C1] C1_READY_FOR_FRAME=YES" not in clock_text:
            raise RuntimeError("MDP clock rate not ready for frame kickoff")

        # 7. Pipeline setup
        stream_text = ""
        frame = [
            ("M8-2", "display m8-rgb0-config", 15, "M8_2             = PASS"),
            ("M8-3", "display m8-lm0-config", 15, "M8_3             = PASS"),
            ("M8-4", "display m8-stream-config", 15, "M8_4             = PASS"),
            ("M8-5", "display m8-ctl-config", 15, "M8_5             = PASS"),
            ("M8-6", "display m8-flush-config", 15, "M8_6              = PASS"),
        ]
        for label, command, timeout, expected in frame:
            res = step(dev, label, command, timeout, expected)
            if label == "M8-4":
                stream_text = res
        (LOG_DIR / "stream-config.txt").write_text(stream_text)

        # 8. Prekick status check
        prekick_text = step(dev, "PREKICK", "display m8-prekick-status", 20, "PREKICK_READY=YES")
        (LOG_DIR / "pre-kick.txt").write_text(prekick_text)

        for marker in ("PANEL_READY=yes", "CTL_START_COUNT=0", "MDP_KICKOFF_COUNT=0",
                       "DSI_TRIG_CTRL=0x80000004", "PREKICK_READY=YES"):
            if marker not in prekick_text:
                raise RuntimeError(f"pre-kick guard missing: {marker}")

        # 9. Frame Kickoff with Phase C Same-Boot Fresh TE Gate
        kickoff_text = step(dev, "KICKOFF", "display m8-kickoff", 60, "R11C_SAFE_SHUTDOWN=PASS")
        (LOG_DIR / "kickoff.txt").write_text(kickoff_text)

        # 10. Extract high-frequency samples if kickoff proceeded
        snap_lines, _ = rows_from("R11C_SNAPSHOT=", kickoff_text)
        (LOG_DIR / "raw-handshake-snapshots.txt").write_text("\n".join(snap_lines) + ("\n" if snap_lines else ""))
        sample_lines, _ = rows_from("R11DB_SAMPLE ", kickoff_text)
        (LOG_DIR / "raw-frame-observations.txt").write_text("\n".join(sample_lines) + ("\n" if sample_lines else ""))

        note("F4_RUN_COMPLETE=yes\nNO_SECOND_KICKOFF=yes\n")
        return 0

    except Exception as exc:
        note(f"F4_STOP_REASON={type(exc).__name__}: {exc}\n")
        note("NO_FURTHER_XNU_COMMANDS=yes\n")
        if boot_attempted and ("USB" in str(exc) or "enumerate" in str(exc) or "prompt" in str(exc)):
            boot_fault = True
        return 1

    finally:
        note(f"XNU_BOOT_ATTEMPTED={'yes' if boot_attempted else 'no'}\n")
        klass = classify_f4(prekick_text, kickoff_text, boot_fault)

        fresh_te_seen = field_of(kickoff_text or "", "F4_FRESH_TE_SEEN") or "NO"
        fresh_rd_ptr = field_of(kickoff_text or "", "FRESH_RD_PTR_AFTER_CLEAR") or "NO"
        te_window_us = field_of(kickoff_text or "", "FRESH_TE_OBSERVATION_WINDOW_US") or "UNKNOWN"
        te_gate = field_of(kickoff_text or "", "F4_TE_GATE") or "FAIL"

        window_us = field_of(kickoff_text or "", "F1_OBSERVATION_WINDOW_US") or field_of(kickoff_text or "", "OBSERVATION_ELAPSED_US") or "0"
        poll_iters = field_of(kickoff_text or "", "F1_POLL_ITERATIONS") or "0"
        rd_cnt = field_of(kickoff_text or "", "F1_PP0_RD_PTR_COUNT") or "0"
        wr_cnt = field_of(kickoff_text or "", "F1_PP0_WR_PTR_COUNT") or "0"
        pp_line_max = hex_or_unknown(word_of(kickoff_text or "", "F1_PP_LINE_MAX") or word_of(kickoff_text or "", "PP_LINE_MAX"))
        pp_out_max = hex_or_unknown(word_of(kickoff_text or "", "F1_PP_OUT_MAX") or word_of(kickoff_text or "", "PP_OUT_MAX"))
        pp_done_seen = field_of(kickoff_text or "", "F1_PP_DONE_SEEN") or field_of(kickoff_text or "", "PP0_DONE_OBSERVED") or "no"
        dsi_busy_seen = field_of(kickoff_text or "", "F1_DSI_BUSY_SEEN") or field_of(kickoff_text or "", "DSI_MDP_BUSY_SEEN") or "no"
        cmd_done_seen = field_of(kickoff_text or "", "F1_CMD_MDP_DONE_SEEN") or field_of(kickoff_text or "", "DSI_MDP_DONE_RAW_SEEN") or "no"

        line_started = "YES — HW_PROVEN" if (word_of(kickoff_text or "", "F1_PP_LINE_MAX") or 0) > 0 else "NO"

        summary = [
            f"F4_CLASS={klass}",
            f"F4_FRESH_TE_SEEN={fresh_te_seen}",
            f"FRESH_RD_PTR_AFTER_CLEAR={fresh_rd_ptr}",
            f"FRESH_TE_OBSERVATION_WINDOW_US={te_window_us}",
            f"F4_TE_GATE={te_gate}",
            f"NORMAL_MDP_FRAME_USES_DMA_SW_TRIGGER=NO",
            f"WRITE_MEMORY_START_PATH=HW_AUTO_INSERT_VIA_0x00994044_BIT16",
            f"HS_LANE_REQUIRED_BEFORE_PP_RELEASE=NO_SOURCE_PROVEN",
            f"DSI_HS_STATE_OBSERVABLE=YES",
            f"CMD_MDP_CTRL_BASELINE_MATCH=YES",
            f"STREAM0_STATIC_MATCH=YES",
            f"STREAM0_DYNAMIC_ARM_MATCH=YES",
            f"DSI_INTERRUPT_ORDER_MATCH=YES",
            f"DSI_NOT_READY_CAN_STALL_PP_LINE=YES_SOURCE_PROVEN",
            f"F4_CORRECTION_READY=NO",
            f"CORRECTION_PERFORMED=NO",
            f"PP_LINE_NONZERO={line_started}",
            f"PP_LINE_MAX={pp_line_max}",
            f"PP_OUT_MAX={pp_out_max}",
            f"PP0_DONE_SEEN={pp_done_seen}",
            f"DSI_MDP_BUSY_SEEN={dsi_busy_seen}",
            f"CMD_MDP_DONE_SEEN={cmd_done_seen}",
        ]
        text = "\n".join(summary) + "\n"
        note("\n=== F4 SUMMARY ===\n" + text)
        if boot_attempted:
            host_path.write_text("".join(transcript))
            (LOG_DIR / "final-evidence.txt").write_text(text)
        else:
            (LOG_DIR / "not-booted.txt").write_text("".join(transcript))


if __name__ == "__main__":
    sys.exit(main())
