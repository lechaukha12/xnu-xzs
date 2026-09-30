#!/usr/bin/env python3
"""R11D-B one-boot passive PP0 gate observation. One CTL_START, no retry."""

import importlib.util
import re
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/m8-7-retry11d-b"
SERIAL = "BH905SX976"
BOOT = ROOT / "artifacts/builds/xzs-xnu-boot.img"

spec = importlib.util.spec_from_file_location(
    "r11a_helpers", ROOT / "scripts/display/d8m8_7_retry11a_runner.py"
)
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)


def rows_from(prefix, text):
    lines = [line.strip() for line in text.splitlines() if line.startswith(prefix)]
    return lines, [dict(re.findall(r"([A-Z0-9_]+)=([^ ]+)", line)) for line in lines]


def decode(kickoff):
    snap_lines, snap_rows = rows_from("R11C_SNAPSHOT=", kickoff)
    (LOG_DIR / "raw-handshake-snapshots.txt").write_text(
        "\n".join(snap_lines) + ("\n" if snap_lines else ""))
    sample_lines, sample_rows = rows_from("R11DB_SAMPLE ", kickoff)
    (LOG_DIR / "raw-pp-gate-samples.txt").write_text(
        "\n".join(sample_lines) + ("\n" if sample_lines else ""))
    columns = [
        "KIND", "TIMESTAMP_US", "PP_COUNT", "PP_AUTOREFRESH", "PP_LINE",
        "PP_OUT", "MDP_INTR", "DONE8", "RD12", "WR16", "AR_DONE20",
        "DSI_BUSY", "DSI_MDP_DONE",
    ]
    table = ["\t".join(columns)]
    for row in sample_rows:
        table.append("\t".join(row.get(column, "UNKNOWN") for column in columns))
    (LOG_DIR / "decoded-pp-gate.tsv").write_text("\n".join(table) + "\n")
    return snap_rows, sample_rows


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        raise RuntimeError("host.txt exists: refusing a second R11D-B XNU boot")
    if not BOOT.is_file():
        raise RuntimeError(f"boot image missing: {BOOT}")

    transcript = []
    boot_attempted = False

    def note(message):
        transcript.append(message)
        print(message, end="", flush=True)

    def step(dev, label, command, timeout, expected):
        note(f"\n--- {label} ---\n> {command}\n")
        output = helpers.send_cmd(dev, command + "\n", wait_sec=timeout,
                                  required_substr=expected)
        transcript.append(output)
        if expected not in output or "xzs#" not in output:
            raise RuntimeError(f"{label}: completion/prompt missing; stop active testing")
        return output

    try:
        detected = subprocess.run(["fastboot", "devices"], capture_output=True,
                                  text=True, check=True, timeout=10).stdout
        note("fastboot_devices_before=\n" + detected)
        if not any(line.split() == [SERIAL, "fastboot"] for line in detected.splitlines()):
            raise RuntimeError("target not in fastboot; no XNU boot attempted")

        note("=== R11D-B ONE XNU FASTBOOT BOOT ===\n")
        boot_attempted = True
        result = subprocess.run(["fastboot", "-s", SERIAL, "boot", str(BOOT)],
                                capture_output=True, text=True, timeout=60)
        note(result.stdout + result.stderr)
        note(f"fastboot_boot_exit={result.returncode}\n")
        if result.returncode != 0:
            raise RuntimeError("fastboot boot failed; no retry")

        dev = helpers.xzs_console.open_stable_device(timeout_sec=60)
        if dev is None:
            raise RuntimeError("XNU USB console did not enumerate")
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
            raise RuntimeError("shell prompt missing; no further commands")

        prerequisites = [
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
            ("M8-1", "display m8-fb-init", 45, "M8_1             = PASS"),
            ("M8-2", "display m8-rgb0-config", 15, "M8_2             = PASS"),
            ("M8-3", "display m8-lm0-config", 15, "M8_3             = PASS"),
            ("M8-4", "display m8-stream-config", 15, "M8_4             = PASS"),
            ("M8-5", "display m8-ctl-config", 15, "M8_5             = PASS"),
            ("M8-6", "display m8-flush-config", 15, "M8_6              = PASS"),
        ]
        for label, command, timeout, expected in prerequisites:
            step(dev, label, command, timeout, expected)

        prekick = step(dev, "PREKICK", "display m8-prekick-status", 20,
                       "PREKICK_READY=YES")
        (LOG_DIR / "pre-kick.txt").write_text(prekick)
        for marker in ("PANEL_READY=yes", "CTL_START_COUNT=0", "MDP_KICKOFF_COUNT=0",
                       "DSI_TRIG_CTRL=0x80000004", "PREKICK_READY=YES"):
            if marker not in prekick:
                raise RuntimeError(f"pre-kick guard missing: {marker}")

        kickoff = step(dev, "ONE KICKOFF", "display m8-kickoff", 60,
                       "R11C_SAFE_SHUTDOWN=PASS")
        (LOG_DIR / "kickoff.txt").write_text(kickoff)
        snap_rows, sample_rows = decode(kickoff)
        if len(snap_rows) != 13:
            raise RuntimeError(f"incomplete handshake snapshots: {len(snap_rows)}/13")
        kinds = [row.get("KIND") for row in sample_rows]
        if "PRE_CLEAR" not in kinds or "POST_CLEAR" not in kinds:
            raise RuntimeError("pre/post clear samples missing")
        for marker in ("CTL_START_WRITE=0x00000001", "CTL_START_COUNT=1",
                       "MDP_KICKOFF_COUNT=1", "M8_7_RETRY11C=OBSERVATION_COMPLETE",
                       "R11DB_AUTOREFRESH_BEFORE=", "R11DB_AUTOREFRESH_AFTER=",
                       "R11DB_POST_CLEAR_BITS8_12_16="):
            if marker not in kickoff:
                raise RuntimeError(f"kickoff guard missing: {marker}")
        note("R11DB_RUN_COMPLETE=yes\nNO_SECOND_KICKOFF=yes\n")
        return 0
    except Exception as exc:
        note(f"R11DB_STOP_REASON={type(exc).__name__}: {exc}\n")
        note("NO_FURTHER_XNU_COMMANDS=yes\n")
        return 1
    finally:
        note(f"XNU_BOOT_ATTEMPTED={'yes' if boot_attempted else 'no'}\n")
        if boot_attempted:
            host_path.write_text("".join(transcript))
        else:
            (LOG_DIR / "not-booted.txt").write_text("".join(transcript))


if __name__ == "__main__":
    sys.exit(main())
