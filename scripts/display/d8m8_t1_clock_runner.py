#!/usr/bin/env python3
"""T1 one-boot passive MDP RCG/CBCR read. No CTL_START and no clock write."""

import importlib.util
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/t1-mdp-clock-readback"
SERIAL = "BH905SX976"
BOOT = ROOT / "artifacts/builds/xzs-xnu-boot.img"

TABLE = (
    (5, 13, 85714286, "GPLL0"),
    (5, 11, 100000000, "GPLL0"),
    (5, 7, 150000000, "GPLL0"),
    (5, 6, 171428571, "GPLL0"),
    (5, 5, 200000000, "GPLL0"),
    (2, 5, 275000000, "MMPLL5"),
    (5, 3, 300000000, "GPLL0"),
    (2, 4, 330000000, "MMPLL5"),
    (2, 3, 412500000, "MMPLL5"),
)
PARENT = {0: "BI_TCXO", 1: "MMPLL0", 2: "MMPLL5", 5: "GPLL0", 6: "GPLL0_DIV"}

spec = importlib.util.spec_from_file_location(
    "r11a_helpers", ROOT / "scripts/display/d8m8_7_retry11a_runner.py"
)
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)


def word_of(text, key):
    needle = key + "=0x"
    for line in text.splitlines():
        if needle in line:
            raw = line.split(needle, 1)[1].split()[0]
            return int(raw, 16)
    return None


def classify(cmd, cfg):
    if cmd is None or cfg is None:
        return "T1-E", "INSUFFICIENT_READBACK", "UNKNOWN", None, None, "UNKNOWN"
    src = (cfg >> 8) & 7
    hid = cfg & 0x1f
    mode = (cfg >> 12) & 3
    root_off = (cmd >> 31) & 1
    parent = PARENT.get(src, "UNMAPPED")
    if root_off:
        return "T1-B", "ROOT_OFF", parent, src, hid, "UNKNOWN"
    if src not in PARENT:
        return "T1-C", "INVALID_SOURCE", parent, src, hid, "UNKNOWN"
    if mode == 0:
        for table_src, table_hid, rate, name in TABLE:
            if table_src == src and table_hid == hid:
                return "T1-A", "VALID_TABLE_ENTRY", name, src, hid, str(rate)
    return "T1-D", "NON_TABLE_CONFIGURATION", parent, src, hid, "UNKNOWN"


def branch(value):
    if value is None:
        return "UNKNOWN"
    bit0 = value & 1
    bit31 = (value >> 31) & 1
    if bit0 and not bit31:
        return "ACTIVE"
    if not bit0 and bit31:
        return "INACTIVE"
    return "INCONSISTENT"


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        raise RuntimeError("host.txt exists: refusing a second T1 XNU boot")
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

        note("=== T1 ONE XNU FASTBOOT BOOT ===\n")
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
            ("M3", "display m3-run full", 50, "PASS_FULL_M3"),
            ("M4", "display m4-run full", 10, "PASS_MODE2"),
            ("P1", "display p1-run", 5, "RESULT=PASS_P1"),
            ("PANEL_PREPARE", "display m8-status", 30, "PANEL_READY=yes"),
            ("M8-1", "display m8-fb-init", 60, "M8_1             = PASS"),
        ]
        for label, command, timeout, expected in prerequisites:
            step(dev, label, command, timeout, expected)

        readout = step(dev, "T1 READ", "clocks mdss-critical-status", 40,
                       "[T1] T1_READ_END")
        (LOG_DIR / "t1-readout.txt").write_text(readout)
        if "CTL_START_WRITE" in readout or "display m8-kickoff" in readout:
            raise RuntimeError("kickoff text appeared in the T1 readout")
        cmd = word_of(readout, "[T1] CMD")
        cfg = word_of(readout, "[T1] CFG")
        mdp = word_of(readout, "[T1] MDP_CBCR")
        ahb = word_of(readout, "[T1] AHB_CBCR")
        axi = word_of(readout, "[T1] AXI_CBCR")
        vsync = word_of(readout, "[T1] VSYNC_CBCR")
        klass, kname, parent, src, hid, rate = classify(cmd, cfg)
        lines = [
            f"T1_CLASS={klass} {kname}",
            f"MDP_RCG_CMD={cmd:#010x}" if cmd is not None else "MDP_RCG_CMD=UNKNOWN",
            f"MDP_RCG_CFG={cfg:#010x}" if cfg is not None else "MDP_RCG_CFG=UNKNOWN",
            f"MDP_ROOT_OFF={'YES' if cmd is not None and (cmd >> 31) & 1 else 'NO' if cmd is not None else 'UNKNOWN'}",
            f"MDP_SOURCE={parent}",
            f"MDP_HID={hid if hid is not None else 'UNKNOWN'}",
            f"MDP_TABLE_MATCH={'YES' if klass == 'T1-A' else 'NO' if cmd is not None else 'UNKNOWN'}",
            f"MDP_TABLE_RATE_HZ={rate}",
            f"MDP_CBCR={mdp:#010x}" if mdp is not None else "MDP_CBCR=UNKNOWN",
            f"MDP_BRANCH={branch(mdp)}",
            f"AHB_CBCR={ahb:#010x}" if ahb is not None else "AHB_CBCR=UNKNOWN",
            f"AHB_BRANCH={branch(ahb)}",
            f"AXI_CBCR={axi:#010x}" if axi is not None else "AXI_CBCR=UNKNOWN",
            f"AXI_BRANCH={branch(axi)}",
            f"VSYNC_CBCR={vsync:#010x}" if vsync is not None else "VSYNC_CBCR=UNKNOWN",
            f"VSYNC_BRANCH={branch(vsync)}",
            f"SAFE_SHUTDOWN={'PASS' if '[T1] SAFE_SHUTDOWN=PASS' in readout or '[D8-M6-SHUTDOWN] SUCCESS' in readout else 'UNKNOWN'}",
            "CTL_START=NOT_RUN",
        ]
        (LOG_DIR / "decoded-t1.txt").write_text("\n".join(lines) + "\n")
        if klass == "T1-E":
            raise RuntimeError("T1 decode is INSUFFICIENT_READBACK")
        note("T1_RUN_COMPLETE=yes\nNO_CTL_START=yes\n")
        return 0
    except Exception as exc:
        note(f"T1_STOP_REASON={type(exc).__name__}: {exc}\n")
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
