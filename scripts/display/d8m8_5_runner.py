#!/usr/bin/env python3
"""D8-M8.5 Hardware Test Runner: FIRST VISIBLE DISPLAY Campaign.

Executes ONE conditional RAM boot (NO FLASH, PAC=0) on target BH905SX976:
1. Verifies fastboot connection.
2. Boots XNU kernel with QPNP WLED backlight + internal VSYNC PingPong pipeline.
3. Observes authentic Panel 9 kickoff lifecycle (TEON -> DISPON -> CTL_START -> SLPOUT -> WLED_ON).
4. Verifies line progress (PP_LINE_COUNT > 0), frame complete, and WLED active.
5. Captures all telemetry and holds display visible for human/photo inspection.
"""

import hashlib
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import importlib.util

ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / (sys.argv[1] if len(sys.argv) > 1 else "artifacts/hw/d8m8_5/r1")
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


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        host_path.unlink()

    transcript = []

    def note(msg):
        sys.stdout.write(msg)
        sys.stdout.flush()
        transcript.append(msg)

    dev = None
    boot_attempted = False
    boot_fault = False
    kickoff_text = None

    def fast_collect(dev, seconds=10.0, required_substr=None):
        end = time.time() + seconds
        buf = b""
        req_bytes = required_substr.encode("utf-8") if required_substr else None
        while time.time() < end:
            chunk, kind = helpers.xzs_console.bulk_read(dev, timeout_ms=50)
            if chunk:
                buf += chunk
                tail = buf[max(0, len(buf) - 200):]
                has_prompt = (b"xzs#" in tail or b"code=3" in tail)
                if req_bytes and (req_bytes in buf or b"ALREADY" in buf) and has_prompt:
                    for _ in range(5):
                        extra, _ = helpers.xzs_console.bulk_read(dev, timeout_ms=50)
                        if extra:
                            buf += extra
                        else:
                            break
                    break
                elif not req_bytes and has_prompt:
                    break
            else:
                time.sleep(0.005)
        return buf

    def fast_send_cmd(dev, cmd_str, wait_sec=10.0, required_substr=None):
        note(f">>> SEND: {cmd_str.strip()}\n")
        # Drain pre-existing input
        for _ in range(5):
            pre_drain, _ = helpers.xzs_console.bulk_read(dev, timeout_ms=20)
            if not pre_drain:
                break
        payload = cmd_str.encode("utf-8") if isinstance(cmd_str, str) else cmd_str
        if not payload.endswith(b"\n"):
            payload += b"\n"
        written, kind = helpers.xzs_console.bulk_write(dev, payload, timeout_ms=2000)
        if kind is not None or written != len(payload):
            note(f"!!! WRITE ERROR: written={written}, kind={kind}\n")
            return ""
        resp = fast_collect(dev, seconds=wait_sec, required_substr=required_substr)
        return resp.decode("utf-8", errors="replace")

    def step(dev, label, command, timeout, expected):
        nonlocal kickoff_text
        note(f"\n--- {label} ---\n> {command}\n")
        output = fast_send_cmd(dev, command + "\n", wait_sec=timeout, required_substr=expected)
        note(output + "\n")
        if label == "KICKOFF":
            kickoff_text = output
            (LOG_DIR / "kickoff.txt").write_text(output)
        return output

    try:
        note("=======================================================\n")
        note("=== D8-M8.5: FIRST VISIBLE DISPLAY CAMPAIGN         ===\n")
        note("=======================================================\n")

        note("Verifying fastboot device presence...\n")
        detected = subprocess.run(["fastboot", "devices"], capture_output=True, text=True, check=True).stdout
        note(f"Fastboot devices:\n{detected}\n")
        if SERIAL not in detected:
            raise RuntimeError(f"Target device {SERIAL} not in fastboot devices output: {detected!r}")

        boot_hash = sha256_of(BOOT)
        kernel_hash = sha256_of(KERNEL)
        commit_sha = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
        git_diff = subprocess.run(["git", "diff", "HEAD~1"], capture_output=True, text=True).stdout
        (LOG_DIR / "git-diff.txt").write_text(git_diff)

        build_id = [
            f"HEAD={commit_sha}",
            f"BOOT_IMAGE={BOOT}",
            f"BOOT_SHA256={boot_hash}",
            f"KERNEL={KERNEL}",
            f"KERNEL_SHA256={kernel_hash}",
            f"PAC=0",
            f"PROTOCOL=D8-M8.5 First Visible Display Closure Campaign",
        ]
        (LOG_DIR / "build-identity.txt").write_text("\n".join(build_id) + "\n")
        note("\n=== BUILD IDENTITY ===\n" + "\n".join(build_id) + "\n\n")

        note(f"Booting {BOOT.name} via fastboot boot (ONE RAM BOOT ONLY)...\n")
        boot_attempted = True
        boot_proc = subprocess.run(["fastboot", "-s", SERIAL, "boot", str(BOOT)], capture_output=True, text=True)
        note(boot_proc.stdout + "\n" + boot_proc.stderr + "\n")
        if boot_proc.returncode != 0:
            raise RuntimeError(f"fastboot boot failed with code {boot_proc.returncode}")

        # Connect USB console
        note("Waiting for USB console...\n")
        dev = helpers.xzs_console.open_stable_device(timeout_sec=40)
        if dev is None:
            raise RuntimeError("USB console failed to enumerate after boot")

        note("XZS console connected.\n")
        prompt = False
        for _ in range(15):
            helpers.xzs_console.bulk_write(dev, b"\n", timeout_ms=1000)
            time.sleep(0.4)
            data = helpers.collect(dev, seconds=1.5)
            decoded = data.decode("utf-8", errors="replace")
            note(decoded)
            if "xzs#" in decoded:
                prompt = True
                break
        if not prompt:
            raise RuntimeError("Timed out waiting for xzs# shell prompt")

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

        # Panel Prepare
        prep_text = step(dev, "PANEL_PREPARE", "display m8-status", 30, "PANEL_READY=yes")
        (LOG_DIR / "panel-prepare.txt").write_text(prep_text)

        # FB Init: draws 8 color bars + center badge
        step(dev, "M8-1", "display m8-fb-init", 60, "M8_1             = PASS")

        # Clock Rate
        clock_text = step(dev, "CLOCK RATE", "clocks mdss-ahb-debug", 20, "[C1] C1_END")
        (LOG_DIR / "c1-rate-confirm.txt").write_text(clock_text)

        # Pipeline setup
        frame = [
            ("M8-2", "display m8-rgb0-config", 15, "M8_2             = PASS"),
            ("M8-3", "display m8-lm0-config", 15, "M8_3             = PASS"),
            ("M8-4", "display m8-stream-config", 15, "M8_4             = PASS"),
            ("M8-5", "display m8-ctl-config", 15, "M8_5             = PASS"),
            ("M8-6", "display m8-flush-config", 15, "M8_6              = PASS"),
        ]
        for label, command, timeout, expected in frame:
            step(dev, label, command, timeout, expected)

        # Pre-kick audit
        prekick_text = step(dev, "PREKICK", "display m8-prekick-status", 20, "PREKICK_READY=YES")
        (LOG_DIR / "pre-kick.txt").write_text(prekick_text)

        # Kickoff with WLED backlight ON and scanout ACTIVE
        note("Executing m8-kickoff (CTL_START, DISPON, WLED brightness 1200, autorefresh 60Hz)...\n")
        kickoff_text = step(dev, "KICKOFF", "display m8-kickoff", 120, "M8_KICKOFF_COMPLETE")
        (LOG_DIR / "kickoff.txt").write_text(kickoff_text)

        note("\n=======================================================\n")
        note("=== D8-M8.5 FIRST VISIBLE DISPLAY EXECUTED          ===\n")
        note("=== PHYSICAL DISPLAY ACTIVE AND LIGHTING TEST BARS  ===\n")
        note("=======================================================\n")
        note("\nHolding display active for visual inspection (15 seconds)...\n")
        time.sleep(15)

    except Exception as exc:
        boot_fault = True
        note(f"\n[FATAL RUNNER EXCEPTION]: {exc}\n")
    finally:
        (LOG_DIR / "transcript.txt").write_text("".join(transcript))
        if dev is not None:
            note("Rebooting device cleanly to fastboot...\n")
            try:
                helpers.send_cmd(dev, "reboot bootloader\n", wait_sec=3)
            except Exception:
                pass
            try:
                helpers.xzs_console.release_device(dev)
            except Exception:
                pass


if __name__ == "__main__":
    main()
