#!/usr/bin/env python3
"""One-boot Retry 11B M8-1-only diagnostic. Never sends M8-2 or kickoff."""

import importlib.util
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/m8-7-retry11b"
SERIAL = "BH905SX976"
BOOT = ROOT / "artifacts/builds/xzs-xnu-boot.img"

spec = importlib.util.spec_from_file_location(
    "r11a_helpers", ROOT / "scripts/display/d8m8_7_retry11a_runner.py"
)
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        raise RuntimeError("host.txt already exists; refusing a second R11B run")
    if not BOOT.is_file():
        raise RuntimeError(f"missing boot image: {BOOT}")

    transcript = []
    boot_attempted = False

    def record(s):
        transcript.append(s)
        print(s, end="", flush=True)

    def step(dev, label, command, timeout, expected):
        record(f"\n--- {label} ---\n> {command}\n")
        out = helpers.send_cmd(dev, command + "\n", wait_sec=timeout,
                               required_substr=expected)
        record(out)
        if expected not in out or "xzs#" not in out:
            raise RuntimeError(f"{label}: expected completion/prompt missing; stop testing")
        return out

    try:
        detected = subprocess.run(["fastboot", "devices"], capture_output=True,
                                  text=True, check=True, timeout=10).stdout
        record("fastboot_devices_before=\n" + detected)
        if not any(line.split() == [SERIAL, "fastboot"] for line in detected.splitlines()):
            raise RuntimeError("target serial is not in fastboot")

        record("=== ONE XNU FASTBOOT BOOT ===\n")
        boot_attempted = True
        result = subprocess.run(["fastboot", "-s", SERIAL, "boot", str(BOOT)],
                                capture_output=True, text=True, timeout=60)
        record(result.stdout + result.stderr)
        record(f"fastboot_boot_exit={result.returncode}\n")
        if result.returncode != 0:
            raise RuntimeError("fastboot boot failed; no retry")

        dev = helpers.xzs_console.open_stable_device(timeout_sec=60)
        if dev is None:
            raise RuntimeError("USB shell did not enumerate; no further commands")

        prompt = False
        for _ in range(8):
            helpers.xzs_console.bulk_write(dev, b"\n", timeout_ms=1000)
            time.sleep(0.4)
            data = helpers.collect(dev, seconds=1.5)
            decoded = data.decode("utf-8", errors="replace")
            record(decoded)
            if "xzs#" in decoded:
                prompt = True
                break
        if not prompt:
            raise RuntimeError("shell prompt missing; no further commands")

        prereqs = [
            ("MMAGIC_AHB", "clocks mmagic-ahb-on", 2, "PASS"),
            ("MMAGIC_CFG_AHB", "clocks mmagic-cfg-ahb-on", 2, "PASS"),
            ("MMAGIC_NOC", "clocks mmagic-mdss-noc-on", 2, "PASS"),
            ("MMAGIC_AXI", "clocks mmagic-mdss-axi-on", 2, "PASS"),
            ("MDSS_GDSC", "display power mdss-on", 2, "PASS"),
            ("MDSS_AHB", "clocks mdss-ahb-on", 2, "PASS"),
            ("MDSS_AXI", "clocks mdss-axi-on", 2, "PASS"),
            ("MDSS_MDP", "clocks mdp-on", 2, "PASS"),
            ("CORE_STATUS", "clocks mdss-critical-status", 2, "PASS"),
            ("M3", "display m3-run full", 50, "PASS_FULL_M3"),
            ("M4", "display m4-run full", 10, "PASS_MODE2"),
            ("P1", "display p1-run", 5, "RESULT=PASS_P1"),
            ("PANEL_PREPARE", "display m8-status", 30, "PANEL_READY=yes"),
        ]
        for label, command, timeout, expected in prereqs:
            step(dev, label, command, timeout, expected)

        result_text = step(dev, "M8-1 ONLY", "display m8-fb-init", 45,
                           "R11B_DIAGNOSTIC_STOP=yes")
        if "M8_1             = PASS" not in result_text:
            raise RuntimeError("diagnostic stop seen without M8-1 PASS")
        record("R11B_M8_1_PASS=yes\nCTL_START_EXECUTED=no\n")
    except Exception as exc:
        record(f"R11B_STOP_REASON={type(exc).__name__}: {exc}\n")
        record("NO_FURTHER_XNU_COMMANDS=yes\n")
        return 1
    finally:
        record(f"XNU_BOOT_ATTEMPTED={'yes' if boot_attempted else 'no'}\n")
        host_path.write_text("".join(transcript))
    return 0


if __name__ == "__main__":
    sys.exit(main())
