#!/usr/bin/env python3
"""C1 one-boot MDP rate A/B. One RCG update after M8_1, then one CTL_START.

The shell binary is CRC-pinned, so the host sends the existing verb
`clocks mdss-ahb-debug`. The kernel runs the rate write only after
m8-fb-init has passed. No second boot and no second kickoff.
"""

import importlib.util
import re
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/c1-mdp-rate-ab"
SERIAL = "BH905SX976"
BOOT = ROOT / "artifacts/builds/xzs-xnu-boot.img"

spec = importlib.util.spec_from_file_location(
    "r11a_helpers", ROOT / "scripts/display/d8m8_7_retry11a_runner.py"
)
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)


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


def classify(c1_text, kickoff, boot_fault):
    gate = field_of(c1_text, "[C1] GPLL0_GATE")
    ready = field_of(c1_text, "[C1] C1_READY_FOR_FRAME")
    cfg_post = word_of(c1_text, "[C1] CFG_POST")
    masked = word_of(c1_text, "[C1] CFG_POST_MASKED")
    rate_ok = masked == 0x00000506
    if boot_fault:
        return "C1-D"
    if ready != "YES":
        return "C1-C"
    if kickoff is None:
        return "C1-C"
    line_max = word_of(kickoff, "R11DB_MAX_PP_LINE")
    line_obs = word_of(kickoff, "MAX_LINE_COUNT_OBSERVED")
    nonzero = False
    for value in (line_max, line_obs):
        if value is not None and value > 0:
            nonzero = True
    if "R11DB_PP_LINE_NONZERO=yes" in kickoff:
        nonzero = True
    if rate_ok and cfg_post is not None and nonzero:
        return "C1-A"
    if rate_ok and not nonzero and "R11C_SAFE_SHUTDOWN=" in kickoff:
        return "C1-B"
    if "M8_7_RETRY11C=OBSERVATION_COMPLETE" not in kickoff and "xzs#" not in kickoff:
        return "C1-D"
    if rate_ok and not nonzero:
        return "C1-B"
    return "C1-C"


def causal(klass):
    if klass == "C1-A":
        return "YES — HW_PROVEN"
    if klass == "C1-B":
        return "NO — HW_PROVEN"
    return "UNKNOWN"


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        raise RuntimeError("host.txt exists: refusing a second C1 XNU boot")
    if not BOOT.is_file():
        raise RuntimeError(f"boot image missing: {BOOT}")

    transcript = []
    boot_attempted = False
    c1_text = ""
    kickoff = None
    boot_fault = False
    m8_1 = "NO"

    def note(message):
        transcript.append(message)
        print(message, end="", flush=True)

    def step(dev, label, command, timeout, expected):
        nonlocal c1_text, kickoff
        note(f"\n--- {label} ---\n> {command}\n")
        output = helpers.send_cmd(dev, command + "\n", wait_sec=timeout,
                                  required_substr=expected)
        transcript.append(output)
        if label == "C1 RATE":
            c1_text = output
        if label == "ONE KICKOFF":
            kickoff = output
        if expected not in output or "xzs#" not in output:
            raise RuntimeError(f"{label}: completion/prompt missing; stop active testing")
        return output

    try:
        detected = subprocess.run(["fastboot", "devices"], capture_output=True,
                                  text=True, check=True, timeout=10).stdout
        note("fastboot_devices_before=\n" + detected)
        if not any(line.split() == [SERIAL, "fastboot"] for line in detected.splitlines()):
            raise RuntimeError("target not in fastboot; no XNU boot attempted")

        note("=== C1 ONE XNU FASTBOOT BOOT ===\n")
        boot_attempted = True
        result = subprocess.run(["fastboot", "-s", SERIAL, "boot", str(BOOT)],
                                capture_output=True, text=True, timeout=60)
        note(result.stdout + result.stderr)
        note(f"fastboot_boot_exit={result.returncode}\n")
        if result.returncode != 0:
            boot_fault = True
            raise RuntimeError("fastboot boot failed; no retry")

        dev = helpers.xzs_console.open_stable_device(timeout_sec=60)
        if dev is None:
            boot_fault = True
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
            boot_fault = True
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
            ("M8-1", "display m8-fb-init", 60, "M8_1             = PASS"),
        ]
        for label, command, timeout, expected in prerequisites:
            step(dev, label, command, timeout, expected)
        m8_1 = "YES"

        c1_text = step(dev, "C1 RATE", "clocks mdss-ahb-debug", 20, "[C1] C1_END")
        (LOG_DIR / "c1-rate.txt").write_text(c1_text)
        if "[C1] C1_READY_FOR_FRAME=YES" not in c1_text:
            note("C1_FRAME_PATH=SKIPPED\n")
            raise RuntimeError("C1 readback or GPLL0 gate did not pass; no CTL_START")

        frame = [
            ("M8-2", "display m8-rgb0-config", 15, "M8_2             = PASS"),
            ("M8-3", "display m8-lm0-config", 15, "M8_3             = PASS"),
            ("M8-4", "display m8-stream-config", 15, "M8_4             = PASS"),
            ("M8-5", "display m8-ctl-config", 15, "M8_5             = PASS"),
            ("M8-6", "display m8-flush-config", 15, "M8_6              = PASS"),
        ]
        for label, command, timeout, expected in frame:
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
        snap_lines, snap_rows = rows_from("R11C_SNAPSHOT=", kickoff)
        (LOG_DIR / "raw-handshake-snapshots.txt").write_text(
            "\n".join(snap_lines) + ("\n" if snap_lines else ""))
        sample_lines, _sample_rows = rows_from("R11DB_SAMPLE ", kickoff)
        (LOG_DIR / "raw-frame-observations.txt").write_text(
            "\n".join(sample_lines) + ("\n" if sample_lines else ""))
        if "CTL_START_COUNT=1" not in kickoff or "MDP_KICKOFF_COUNT=1" not in kickoff:
            raise RuntimeError("kickoff count was not exactly one")
        note("C1_RUN_COMPLETE=yes\nNO_SECOND_KICKOFF=yes\n")
        return 0
    except Exception as exc:
        note(f"C1_STOP_REASON={type(exc).__name__}: {exc}\n")
        note("NO_FURTHER_XNU_COMMANDS=yes\n")
        if boot_attempted and ("USB" in str(exc) or "enumerate" in str(exc) or "prompt" in str(exc)):
            boot_fault = True
        return 1
    finally:
        note(f"XNU_BOOT_ATTEMPTED={'yes' if boot_attempted else 'no'}\n")
        klass = classify(c1_text, kickoff, boot_fault and boot_attempted and m8_1 == "NO" and not c1_text)
        if boot_attempted and boot_fault and not c1_text:
            klass = "C1-D"
        elif c1_text and "[C1] C1_READY_FOR_FRAME=YES" not in c1_text:
            klass = "C1-C"
        elif kickoff and klass not in ("C1-A", "C1-B", "C1-D"):
            klass = classify(c1_text, kickoff, False)
        line_max = word_of(kickoff or "", "R11DB_MAX_PP_LINE")
        line_obs = word_of(kickoff or "", "MAX_LINE_COUNT_OBSERVED")
        out_max = word_of(kickoff or "", "R11DB_MAX_PP_OUT")
        out_obs = word_of(kickoff or "", "MAX_OUT_LINE_COUNT_OBSERVED")
        pp_line = None
        for value in (line_max, line_obs):
            if value is not None and (pp_line is None or value > pp_line):
                pp_line = value
        pp_out = None
        for value in (out_max, out_obs):
            if value is not None and (pp_out is None or value > pp_out):
                pp_out = value
        samples = []
        if kickoff:
            _lines, samples = rows_from("R11DB_SAMPLE ", kickoff)
        counts = []
        for row in samples:
            raw = row.get("PP_COUNT")
            if raw and raw.startswith("0x"):
                try:
                    counts.append(int(raw, 16))
                except ValueError:
                    pass
        if len(set(counts)) >= 2:
            pp_running = "YES"
        elif counts:
            pp_running = "NO"
        else:
            pp_running = "UNKNOWN" if kickoff is None else "NO"
        flush_pre = None
        flush_post = None
        rgb_post = word_of(kickoff or "", "RGB0_CURRENT_SRC0_ADDR_POST")
        for row in samples:
            if row.get("KIND") == "PRE_CLEAR" and "CTL_FLUSH" in row:
                try:
                    flush_pre = int(row["CTL_FLUSH"], 16)
                except ValueError:
                    pass
            if row.get("KIND") == "POST_CLEAR" and "CTL_FLUSH" in row:
                try:
                    flush_post = int(row["CTL_FLUSH"], 16)
                except ValueError:
                    pass
            if rgb_post is None and row.get("KIND") == "POST_CLEAR" and "RGB0_CUR" in row:
                try:
                    rgb_post = int(row["RGB0_CUR"], 16)
                except ValueError:
                    pass
        if flush_pre is not None and flush_post == 0 and flush_pre != 0:
            flush_consumed = "YES"
        elif kickoff is None:
            flush_consumed = "UNKNOWN"
        else:
            flush_consumed = "NO"
        summary = [
            f"C1_CLASS={klass}",
            f"M8_1_PASS={m8_1}",
            f"GPLL0_GATE={field_of(c1_text, '[C1] GPLL0_GATE') or 'UNKNOWN'}",
            f"CMD_PRE={hex_or_unknown(word_of(c1_text, '[C1] CMD_PRE'))}",
            f"CFG_PRE={hex_or_unknown(word_of(c1_text, '[C1] CFG_PRE'))}",
            f"MDP_CBCR_PRE={hex_or_unknown(word_of(c1_text, '[C1] MDP_CBCR_PRE'))}",
            f"RCG_UPDATE_COMPLETED={field_of(c1_text, '[C1] RCG_UPDATE_COMPLETED') or 'NO'}",
            f"CMD_POST={hex_or_unknown(word_of(c1_text, '[C1] CMD_POST'))}",
            f"CFG_POST={hex_or_unknown(word_of(c1_text, '[C1] CFG_POST'))}",
            f"MDP_CBCR_POST={hex_or_unknown(word_of(c1_text, '[C1] MDP_CBCR_POST'))}",
            f"TARGET_RATE_CONFIRMED={field_of(c1_text, '[C1] TARGET_RATE_CONFIRMED') or 'NO'}",
            f"CTL_START_COUNT={field_of(kickoff or '', 'CTL_START_COUNT') or '0'}",
            f"PP_COUNT_RUNNING={pp_running}",
            f"PP_LINE_MAX={hex_or_unknown(pp_line)}",
            f"PP_OUT_MAX={hex_or_unknown(pp_out)}",
            f"PP0_DONE_SEEN={field_of(kickoff or '', 'PP0_DONE_OBSERVED') or 'UNKNOWN'}",
            f"DSI_MDP_BUSY_SEEN={field_of(kickoff or '', 'DSI_MDP_BUSY_SEEN') or 'UNKNOWN'}",
            f"CMD_MDP_DONE_SEEN={field_of(kickoff or '', 'DSI_MDP_DONE_RAW_SEEN') or 'UNKNOWN'}",
            f"CTL_FLUSH_CONSUMED={flush_consumed}",
            f"RGB0_CURRENT_SRC0_ADDR={hex_or_unknown(rgb_post)}",
            f"DID_CORRECT_MDP_RATE_CAUSE_PP_LINE_TO_START={causal(klass)}",
            f"FIRST_PP_LINE={field_of(kickoff or '', 'R11DB_FIRST_PP_LINE_US') or 'none'}",
        ]
        text = "\n".join(summary) + "\n"
        note("\n=== C1 SUMMARY ===\n" + text)
        if boot_attempted:
            host_path.write_text("".join(transcript))
            (LOG_DIR / "final-evidence.txt").write_text(text)
        else:
            (LOG_DIR / "not-booted.txt").write_text("".join(transcript))


if __name__ == "__main__":
    sys.exit(main())
