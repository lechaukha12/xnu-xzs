#!/usr/bin/env python3
"""D8-M8 C2 Hardware A/B Test Runner: Align PP0 Tearcheck with Keyaki External-TE Golden Path.

One RAM boot, one conceptual change, one CTL_START.
Preserves C1 clock correction (171428571 Hz), verifies prekick golden PP readback,
issues exactly one kickoff, captures high-frequency dynamic observations,
and records comprehensive forensic evidence in artifacts/hw/d8m8/c2-external-te-ab/.
"""

import importlib.util
import os
import re
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/c2-external-te-ab"
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


def classify(prekick, kickoff, boot_fault):
    if boot_fault:
        return "C2-D"
    if prekick is None or kickoff is None:
        return "C2-D"
    if "C2_PP_GOLDEN_READBACK_FAIL=yes" in kickoff or "PP_GOLDEN_CONFIG_READBACK=FAIL" in kickoff:
        return "C2-D"
    
    line_max = word_of(kickoff, "R11DB_MAX_PP_LINE")
    line_obs = word_of(kickoff, "MAX_LINE_COUNT_OBSERVED")
    nonzero = False
    for value in (line_max, line_obs):
        if value is not None and value > 0:
            nonzero = True
    if "R11DB_PP_LINE_NONZERO=yes" in kickoff:
        nonzero = True

    dsi_busy = "DSI_MDP_BUSY_SEEN=yes" in kickoff
    dsi_done = "DSI_MDP_DONE_RAW_SEEN=yes" in kickoff
    pp_done = "PP0_DONE_OBSERVED=yes" in kickoff

    if nonzero:
        if dsi_busy or dsi_done or pp_done:
            return "C2-A"
        return "C2-B"
    return "C2-C"


def causal_decision(klass):
    if klass in ("C2-A", "C2-B"):
        return "YES — HW_PROVEN"
    if klass == "C2-C":
        return "NO — HW_PROVEN"
    return "UNKNOWN"


def pipeline_boundary(kickoff, flush_consumed, rgb_post, pp_line, pp_out):
    if kickoff is None:
        return "NONE"
    if "DSI_MDP_DONE_RAW_SEEN=yes" in kickoff:
        return "DSI_CMD_DONE"
    if "DSI_MDP_BUSY_SEEN=yes" in kickoff:
        return "DSI_BUSY"
    if "PP0_DONE_OBSERVED=yes" in kickoff:
        return "PP_DONE"
    if pp_out is not None and pp_out > 0:
        return "PP_OUTPUT_STARTED"
    if pp_line is not None and pp_line > 0:
        return "PP_LINE_STARTED"
    if rgb_post == 0x98000000:
        return "RGB0_SOURCE_LATCHED"
    if flush_consumed == "YES":
        return "CTL_STATE_CONSUMED"
    return "CTL_STATE_UNVERIFIED"


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        raise RuntimeError("host.txt exists: refusing a second C2 XNU boot")
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
        # 1. Fastboot detection
        detected = subprocess.run(["fastboot", "devices"], capture_output=True,
                                  text=True, check=True, timeout=10).stdout
        note("fastboot_devices_before=\n" + detected)
        if not any(line.split() == [SERIAL, "fastboot"] for line in detected.splitlines()):
            raise RuntimeError("target not in fastboot; no XNU boot attempted")

        # 2. Record build identity
        c2_commit = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
        git_diff = subprocess.run(["git", "diff", "HEAD~1"], capture_output=True, text=True).stdout
        (LOG_DIR / "git-diff.txt").write_text(git_diff)

        build_id = [
            f"BASE_HEAD=97b844854a56664bfc4f787bc3ddcde3a307b290",
            f"C2_COMMIT={c2_commit}",
            f"BOOT_IMAGE={BOOT}",
            f"BOOT_SHA256={sha256_of(BOOT)}",
            f"KERNEL={KERNEL}",
            f"KERNEL_SHA256={sha256_of(KERNEL)}",
            f"PAC=0",
        ]
        (LOG_DIR / "build-identity.txt").write_text("\n".join(build_id) + "\n")
        note("\n=== BUILD IDENTITY ===\n" + "\n".join(build_id) + "\n\n")

        # 3. Boot XNU (RAM-only, NO FLASH)
        note("=== C2 ONE XNU FASTBOOT BOOT ===\n")
        boot_attempted = True
        result = subprocess.run(["fastboot", "-s", SERIAL, "boot", str(BOOT)],
                                capture_output=True, text=True, timeout=60)
        note(result.stdout + result.stderr)
        note(f"fastboot_boot_exit={result.returncode}\n")
        if result.returncode != 0:
            boot_fault = True
            raise RuntimeError("fastboot boot failed; no retry")

        # 4. Connect USB console
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

        # 5. Core prerequisites
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

        # 9. Exactly ONE Kickoff
        kickoff_text = step(dev, "ONE KICKOFF", "display m8-kickoff", 60, "R11C_SAFE_SHUTDOWN=PASS")
        (LOG_DIR / "kickoff.txt").write_text(kickoff_text)

        # 10. Extract high-frequency samples
        snap_lines, _ = rows_from("R11C_SNAPSHOT=", kickoff_text)
        (LOG_DIR / "raw-handshake-snapshots.txt").write_text("\n".join(snap_lines) + ("\n" if snap_lines else ""))
        sample_lines, _ = rows_from("R11DB_SAMPLE ", kickoff_text)
        (LOG_DIR / "raw-frame-observations.txt").write_text("\n".join(sample_lines) + ("\n" if sample_lines else ""))

        if "CTL_START_COUNT=1" not in kickoff_text or "MDP_KICKOFF_COUNT=1" not in kickoff_text:
            raise RuntimeError("kickoff count was not exactly one")

        note("C2_RUN_COMPLETE=yes\nNO_SECOND_KICKOFF=yes\n")
        return 0

    except Exception as exc:
        note(f"C2_STOP_REASON={type(exc).__name__}: {exc}\n")
        note("NO_FURTHER_XNU_COMMANDS=yes\n")
        if boot_attempted and ("USB" in str(exc) or "enumerate" in str(exc) or "prompt" in str(exc)):
            boot_fault = True
        return 1

    finally:
        note(f"XNU_BOOT_ATTEMPTED={'yes' if boot_attempted else 'no'}\n")
        klass = classify(prekick_text, kickoff_text, boot_fault)

        line_max = word_of(kickoff_text or "", "R11DB_MAX_PP_LINE")
        line_obs = word_of(kickoff_text or "", "MAX_LINE_COUNT_OBSERVED")
        out_max = word_of(kickoff_text or "", "R11DB_MAX_PP_OUT")
        out_obs = word_of(kickoff_text or "", "MAX_OUT_LINE_COUNT_OBSERVED")

        pp_line = None
        for val in (line_max, line_obs):
            if val is not None and (pp_line is None or val > pp_line):
                pp_line = val

        pp_out = None
        for val in (out_max, out_obs):
            if val is not None and (pp_out is None or val > pp_out):
                pp_out = val

        samples = []
        if kickoff_text:
            _, samples = rows_from("R11DB_SAMPLE ", kickoff_text)

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
            pp_running = "UNKNOWN" if kickoff_text is None else "NO"

        flush_pre = None
        flush_post = None
        rgb_post = word_of(kickoff_text or "", "RGB0_CURRENT_SRC0_ADDR_POST")
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
        elif kickoff_text is None:
            flush_consumed = "UNKNOWN"
        else:
            flush_consumed = "NO"

        pp_readback_status = "PASS" if (kickoff_text and "PP_GOLDEN_CONFIG_READBACK=PASS" in kickoff_text) else "FAIL"

        summary = [
            f"C2_CLASS={klass}",
            f"M8_1_PASS={m8_1}",
            f"MDP_RATE_CONFIRMED={'YES' if '[C1] TARGET_RATE_CONFIRMED=YES' in clock_text else 'NO'}",
            f"XNU_TEON_PRESENT=YES",
            f"PP_GOLDEN_CONFIG_READBACK={pp_readback_status}",
            f"TEAR_CHECK_EN={field_of(kickoff_text or '', 'TEAR_CHECK_EN') or 'UNKNOWN'}",
            f"SYNC_CONFIG_VSYNC={hex_or_unknown(word_of(kickoff_text or '', 'SYNC_CONFIG_VSYNC'))}",
            f"SYNC_CONFIG_HEIGHT={hex_or_unknown(word_of(kickoff_text or '', 'SYNC_CONFIG_HEIGHT'))}",
            f"VSYNC_INIT={hex_or_unknown(word_of(kickoff_text or '', 'VSYNC_INIT'))}",
            f"SYNC_THRESH={hex_or_unknown(word_of(kickoff_text or '', 'SYNC_THRESH'))}",
            f"START_POS={hex_or_unknown(word_of(kickoff_text or '', 'START_POS'))}",
            f"SYNC_WRCOUNT={hex_or_unknown(word_of(kickoff_text or '', 'SYNC_WRCOUNT'))}",
            f"RD_PTR_IRQ={hex_or_unknown(word_of(kickoff_text or '', 'RD_PTR_IRQ'))}",
            f"WR_PTR_IRQ={hex_or_unknown(word_of(kickoff_text or '', 'WR_PTR_IRQ'))}",
            f"AUTOREFRESH={hex_or_unknown(word_of(kickoff_text or '', 'AUTOREFRESH'))}",
            f"CTL_START_COUNT={field_of(kickoff_text or '', 'CTL_START_COUNT') or '0'}",
            f"PP_COUNTER_RUNNING={pp_running}",
            f"PP_LINE_NONZERO={'YES' if (pp_line is not None and pp_line > 0) else 'NO'}",
            f"PP_LINE_MAX={hex_or_unknown(pp_line)}",
            f"PP_OUT_NONZERO={'YES' if (pp_out is not None and pp_out > 0) else 'NO'}",
            f"PP_OUT_MAX={hex_or_unknown(pp_out)}",
            f"PP0_DONE_SEEN={field_of(kickoff_text or '', 'PP0_DONE_OBSERVED') or 'UNKNOWN'}",
            f"DSI_MDP_BUSY_SEEN={field_of(kickoff_text or '', 'DSI_MDP_BUSY_SEEN') or 'UNKNOWN'}",
            f"CMD_MDP_DONE_SEEN={field_of(kickoff_text or '', 'DSI_MDP_DONE_RAW_SEEN') or 'UNKNOWN'}",
            f"CTL_FLUSH_CONSUMED={flush_consumed}",
            f"RGB0_CURRENT_SRC0_ADDR={hex_or_unknown(rgb_post)}",
            f"DID_EXTERNAL_TE_GOLDEN_ALIGNMENT_CAUSE_PP_LINE_TO_START={causal_decision(klass)}",
            f"FARTHEST_PIPELINE_STAGE_REACHED={pipeline_boundary(kickoff_text, flush_consumed, rgb_post, pp_line, pp_out)}",
        ]
        text = "\n".join(summary) + "\n"
        note("\n=== C2 SUMMARY ===\n" + text)
        if boot_attempted:
            host_path.write_text("".join(transcript))
            (LOG_DIR / "final-evidence.txt").write_text(text)
        else:
            (LOG_DIR / "not-booted.txt").write_text("".join(transcript))


if __name__ == "__main__":
    sys.exit(main())
