#!/usr/bin/env python3
"""D8-M8 F5 Hardware Runner: Periodic TE Lifecycle, Pre/Post-Kickoff Dependency & Panel Scan-State Recovery.

Phase F5 (Branch E2 Protocol):
1. Verifies TWRP same-state idle proof: pre-kick periodic TE does not run without commit.
2. Removes pre-kick blocking gate: allows single CTL_START=1 kickoff.
3. Observes post-kickoff TE synchronization, RD_PTR, counter reload, and PP_LINE advancement.
4. Executes RAM boot only, NO FLASH.
"""

import importlib.util
import os
import re
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/f5-xnu"
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


def classify_f5(prekick, kickoff, boot_fault):
    if boot_fault:
        return "F5-FAULT"
    if prekick is None or kickoff is None:
        return "F5-FAULT"

    fresh_te_post = field_of(kickoff, "FRESH_TE_AFTER_CTL_START")
    line_max = word_of(kickoff, "PP_LINE_MAX") or word_of(kickoff, "F1_PP_LINE_MAX") or 0
    out_max = word_of(kickoff, "F1_PP_OUT_MAX") or word_of(kickoff, "PP_OUT_MAX") or 0
    done_seen = field_of(kickoff, "PP0_DONE_SEEN") == "YES" or field_of(kickoff, "F1_PP_DONE_SEEN") == "yes"
    dsi_busy = field_of(kickoff, "DSI_MDP_BUSY_SEEN") == "YES" or field_of(kickoff, "F1_DSI_BUSY_SEEN") == "yes"
    cmd_done = field_of(kickoff, "CMD_MDP_DONE_SEEN") == "YES" or field_of(kickoff, "F1_CMD_MDP_DONE_SEEN") == "yes"

    if line_max > 0 and out_max > 0 and (done_seen or dsi_busy or cmd_done):
        return "F5-COMMAND-FRAME-HW-PROVEN"
    if line_max > 0:
        return "F5-PP-LINE-STARTED"
    if fresh_te_post == "YES":
        return "F5-TE-PASS-PP-STALLED"
    return "F5-TE-NOT-REPRODUCED"


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        if "F5_CLASS=F5-FAULT" in host_path.read_text():
            host_path.unlink()

    transcript = []

    def note(msg):
        sys.stdout.write(msg)
        sys.stdout.flush()
        transcript.append(msg)

    dev = None
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
            note("=== F5 ONE XNU FASTBOOT BOOT (BRANCH E2 POST-KICK TE LIFECYCLE) ===\n")
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

        # 9. Frame Kickoff with Branch E2 (single CTL_START=1 execution)
        kickoff_text = step(dev, "KICKOFF", "display m8-kickoff", 60, "R11C_SAFE_SHUTDOWN=PASS")
        (LOG_DIR / "kickoff.txt").write_text(kickoff_text)

        # 10. Extract high-frequency samples
        snap_lines, _ = rows_from("R11C_SNAPSHOT=", kickoff_text)
        (LOG_DIR / "raw-handshake-snapshots.txt").write_text("\n".join(snap_lines) + ("\n" if snap_lines else ""))
        sample_lines, _ = rows_from("R11DB_SAMPLE ", kickoff_text)
        (LOG_DIR / "raw-frame-observations.txt").write_text("\n".join(sample_lines) + ("\n" if sample_lines else ""))

    except Exception as exc:
        note(f"\n[ERROR] Exception during F5 execution: {exc}\n")
        boot_fault = True
    finally:
        f5_class = classify_f5(prekick_text, kickoff_text, boot_fault)
        note(f"\nF5_CLASS={f5_class}\n")

        summary = [
            f"F5_CLASS={f5_class}",
            f"BOOT_ATTEMPTED={'yes' if boot_attempted else 'no'}",
            f"BOOT_FAULT={'yes' if boot_fault else 'no'}",
            f"PREKICK_CAPTURED={'yes' if prekick_text is not None else 'no'}",
            f"KICKOFF_CAPTURED={'yes' if kickoff_text is not None else 'no'}",
            f"F5_BRANCH={field_of(kickoff_text or '', 'F5_BRANCH') or 'E2'}",
            f"FRESH_TE_AFTER_CTL_START={field_of(kickoff_text or '', 'FRESH_TE_AFTER_CTL_START') or 'NO'}",
            f"PREKICK_TE_GATE_WAS_INVALID={field_of(kickoff_text or '', 'PREKICK_TE_GATE_WAS_INVALID') or 'NO'}",
            f"PP_LINE_NONZERO={field_of(kickoff_text or '', 'PP_LINE_NONZERO') or 'NO'}",
            f"PP_LINE_MAX={hex_or_unknown(word_of(kickoff_text or '', 'PP_LINE_MAX'))}",
            f"PP_OUT_NONZERO={field_of(kickoff_text or '', 'PP_OUT_NONZERO') or 'NO'}",
            f"PP0_DONE_SEEN={field_of(kickoff_text or '', 'PP0_DONE_SEEN') or 'NO'}",
            f"DSI_MDP_BUSY_SEEN={field_of(kickoff_text or '', 'DSI_MDP_BUSY_SEEN') or 'NO'}",
            f"CMD_MDP_DONE_SEEN={field_of(kickoff_text or '', 'CMD_MDP_DONE_SEEN') or 'NO'}",
            f"CTL_START_COUNT={field_of(kickoff_text or '', 'CTL_START_COUNT') or '0'}",
            f"SAFE_SHUTDOWN={field_of(kickoff_text or '', 'R11C_SAFE_SHUTDOWN') or 'UNKNOWN'}",
        ]
        (LOG_DIR / "summary.txt").write_text("\n".join(summary) + "\n")
        (LOG_DIR / "host.txt").write_text("".join(transcript))

        # Reboot target cleanly back to fastboot
        if dev is not None:
            note("\nRebooting device cleanly to fastboot...\n")
            try:
                helpers.send_cmd(dev, "reboot bootloader\n", wait_sec=2)
            except Exception:
                pass
            try:
                helpers.xzs_console.close_device(dev)
            except Exception:
                pass

        time.sleep(3)
        fb_after = subprocess.run(["fastboot", "devices"], capture_output=True, text=True).stdout
        note(f"Fastboot check after F5 test:\n{fb_after}\n")

    return 0 if not boot_fault else 1


if __name__ == "__main__":
    sys.exit(main())
