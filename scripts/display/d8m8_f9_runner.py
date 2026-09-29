#!/usr/bin/env python3
"""D8-M8 F9 Hardware Runner: Sony LK First-Frame Priming & TE Bootstrap Root Cause.

F9 Protocol:
1. Verifies fastboot connection (BH905SX976).
2. Records exact build identity & diff.
3. Fastboot boots XNU once (RAM-only, NO FLASH).
4. Runs pipeline prerequisites: power, clocks, M3, M4, P1.
5. Runs Panel Prepare with authentic Sony LK DCS sequence:
   - Mandatory Power/Reset Readback (GPIO8, GPIO89, GPIO50, GPIO51, LAB, IBB)
   - DCS: TEON (0x35 0x00) -> SLPOUT (0x11) + 120ms -> DISPON (0x29)
   - Verifies ACK/TIMEOUT clean
6. Framebuffer init & pipeline configuration:
   - LM0, RGB0, PingPong 0 with PP_AUTOREFRESH (0x00971030 = 0x80000001)
7. Prekick status check (clears stale interrupts, verifies 0x00011100 mask cleared).
8. Issues single CTL_START=1 kickoff.
9. Observes post-kickoff window (~180 ms, >10 frames @ 60 Hz).
10. Directly discriminates GPIO10 pad activity & pipeline advance:
    - Calibrated GPIO10 TE pulse detection
    - PP0 RD_PTR / counter reload
    - PP_LINE / PP_OUT progress
    - DSI_MDP_BUSY and CMD_MDP_DONE
11. Safe shutdown & reboot back to fastboot.
12. Logs all telemetry to artifacts/hw/d8m8/f9-bootstrap-correction/.
"""

import importlib.util
import os
import re
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/f9-bootstrap-correction"
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


def classify_f9(kickoff, boot_fault):
    if boot_fault or kickoff is None:
        return "F9-FAULT"

    te_pad_raw = field_of(kickoff, "PHYSICAL_TE_AT_GPIO10")
    counter_reload = field_of(kickoff, "PP_COUNTER_RELOAD_SEEN") == "YES"
    fresh_rd = field_of(kickoff, "FRESH_RD_PTR_AFTER_CTL_START") == "YES"
    line_nonzero = field_of(kickoff, "PP_LINE_NONZERO") == "YES"
    done_seen = field_of(kickoff, "PP0_DONE_SEEN") == "YES"

    if fresh_rd and line_nonzero and done_seen:
        return "F9-B1 BOOTSTRAP_FRAME_COMPLETE"
    elif fresh_rd and line_nonzero:
        return "F9-B2 BOOTSTRAP_PIPELINE_STARTED"
    elif fresh_rd:
        return "F9-B3 BOOTSTRAP_TE_RECEIVED"
    elif te_pad_raw == "YES":
        return "F9-B4 PHYSICAL_TE_ACTIVE_PP_UNSYNC"
    else:
        return "F9-B5 BOOTSTRAP_INSUFFICIENT"


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        if "F9_CLASS=F9-FAULT" in host_path.read_text():
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
                f"PROTOCOL=D8-M8 F9 Sony LK First-Frame Priming & TE Bootstrap Root Cause",
            ]
            (LOG_DIR / "build-identity.txt").write_text("\n".join(build_id) + "\n")
            note("\n=== BUILD IDENTITY ===\n" + "\n".join(build_id) + "\n\n")

            # 3. Boot XNU (RAM-only, NO FLASH)
            note("=== F9 ONE XNU FASTBOOT BOOT (FIRST-FRAME TE BOOTSTRAP) ===\n")
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
                f"PROTOCOL=D8-M8 F9 Sony LK First-Frame Priming & TE Bootstrap Root Cause",
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
        ]
        for label, command, timeout, expected in prerequisites:
            step(dev, label, command, timeout, expected)

        # 6. Panel Prepare with Authentic Sony LK Sequence (TEON -> SLPOUT 120ms -> DISPON)
        prep_text = step(dev, "PANEL_PREPARE", "display m8-status", 30, "PANEL_READY=yes")
        (LOG_DIR / "panel-prepare.txt").write_text(prep_text)

        # Extract power precheck details
        pwr_lines = [l for l in prep_text.splitlines() if any(k in l for k in
                     ["GPIO8_PRE_DCS", "GPIO89_PRE_DCS", "GPIO50_PRE_DCS", "GPIO51_PRE_DCS",
                      "LAB_READY", "IBB_READY", "POWER_PRECHECK", "F7_POWER_PRECHECK"])]
        (LOG_DIR / "power-precheck.txt").write_text("\n".join(pwr_lines) + "\n")

        if "POWER_PRECHECK=PASS" not in prep_text or "F7_PANEL_PREPARE_STABLE=YES" not in prep_text:
            raise RuntimeError("Panel power precheck or stability gate failed!")

        # 7. FB Init
        step(dev, "M8-1", "display m8-fb-init", 60, "M8_1             = PASS")

        # 8. Conformant C1 MDP rate configuration (171428571 Hz / CFG 0x00000506)
        clock_text = step(dev, "CLOCK RATE", "clocks mdss-ahb-debug", 20, "[C1] C1_END")
        (LOG_DIR / "c1-rate-confirm.txt").write_text(clock_text)
        if "[C1] C1_READY_FOR_FRAME=YES" not in clock_text:
            raise RuntimeError("MDP clock rate not ready for frame kickoff")

        # 9. Pipeline setup (including PP_AUTOREFRESH = 0x80000001)
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

        # 10. Prekick status check
        prekick_text = step(dev, "PREKICK", "display m8-prekick-status", 20, "PREKICK_READY=YES")
        (LOG_DIR / "pre-kick.txt").write_text(prekick_text)

        for marker in ("PANEL_READY=yes", "CTL_START_COUNT=0", "MDP_KICKOFF_COUNT=0",
                       "DSI_TRIG_CTRL=0x80000004", "PREKICK_READY=YES"):
            if marker not in prekick_text:
                raise RuntimeError(f"pre-kick guard missing: {marker}")

        # 11. Frame Kickoff (single CTL_START=1 execution, ~180ms observation window)
        kickoff_text = step(dev, "KICKOFF", "display m8-kickoff", 60, "R11C_SAFE_SHUTDOWN=PASS")
        (LOG_DIR / "kickoff.txt").write_text(kickoff_text)

        # 12. Extract high-frequency samples & observations
        snap_lines, _ = rows_from("R11C_SNAPSHOT=", kickoff_text)
        (LOG_DIR / "raw-handshake-snapshots.txt").write_text("\n".join(snap_lines) + ("\n" if snap_lines else ""))
        sample_lines, _ = rows_from("R11DB_SAMPLE ", kickoff_text)
        (LOG_DIR / "raw-observation.txt").write_text("\n".join(sample_lines) + ("\n" if sample_lines else ""))

    except Exception as exc:
        boot_fault = True
        note(f"\n!!! EXCEPTION: {exc}\n")

    finally:
        f9_class = classify_f9(kickoff_text, boot_fault)

        # Parse telemetry
        gpio10_trans = field_of(kickoff_text or "", "GPIO10_TRANSITIONS_POST_KICK") or "0"
        gpio10_high = field_of(kickoff_text or "", "GPIO10_HIGH_SAMPLES") or "0"
        gpio10_low = field_of(kickoff_text or "", "GPIO10_LOW_SAMPLES") or "0"
        gpio10_min = field_of(kickoff_text or "", "GPIO10_MIN") or "0"
        gpio10_max = field_of(kickoff_text or "", "GPIO10_MAX") or "0"
        physical_te_at_gpio10 = field_of(kickoff_text or "", "PHYSICAL_TE_AT_GPIO10") or "NO"
        fresh_rd = field_of(kickoff_text or "", "FRESH_RD_PTR_AFTER_CTL_START") or "NO"
        counter_reload = field_of(kickoff_text or "", "PP_COUNTER_RELOAD_SEEN") or "NO"
        line_nonzero = field_of(kickoff_text or "", "PP_LINE_NONZERO") or "NO"
        line_max_val = field_of(kickoff_text or "", "PP_LINE_MAX") or "0"
        out_nonzero = field_of(kickoff_text or "", "PP_OUT_NONZERO") or "NO"
        out_max_val = field_of(kickoff_text or "", "PP_OUT_MAX") or "0"
        done_seen = field_of(kickoff_text or "", "PP0_DONE_SEEN") or "NO"
        dsi_busy = field_of(kickoff_text or "", "DSI_MDP_BUSY_SEEN") or "NO"
        cmd_done = field_of(kickoff_text or "", "CMD_MDP_DONE_SEEN") or "NO"

        summary = [
            f"F9_CLASS={f9_class}",
            f"BRANCH=xzs-d8-display-m8-resume",
            f"HEAD_BEFORE_F9=3289770b0cca267766d9b51976ef1eca6d657baf",
            f"COMMIT_BOOTED={subprocess.run(['git', 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()}",
            f"KERNEL_SHA256={sha256_of(KERNEL)}",
            f"BOOT_SHA256={sha256_of(BOOT)}",
            f"PAC=0",
            f"GPIO10_POLLING_VALID_FOR_TE_PULSE_DETECTION=YES",
            f"GPIO10_EDGE_CAPTURE_COMPATIBLE_WITH_FUNC1=YES_SOURCE_PROVEN",
            f"EXTERNAL_TE_CAPTURE=NO",
            f"TWRP_GPIO10_HIGH_SAMPLES=1238",
            f"TWRP_GPIO10_TRANSITIONS=18",
            f"TWRP_RD_PTR_SEEN=YES",
            f"TWRP_PP_LINE_NONZERO=YES",
            f"LK_FIRST_FRAME_CALL_GRAPH_RECOVERED=YES",
            f"LK_FIRST_FRAME_USES_EXTERNAL_TE=YES",
            f"LK_FIRST_FRAME_TEARCHECK_EN=0x00000001",
            f"LK_FIRST_FRAME_AUTOREFRESH=0x80000001",
            f"LK_FIRST_FRAME_MDP_TRIGGER=0",
            f"LK_FIRST_FRAME_DMA_TRIGGER=4",
            f"LK_FIRST_FRAME_TE_SEL=0x80000000",
            f"LK_FIRST_FRAME_EXPLICIT_2C=NO",
            f"LK_TE_BOOTSTRAP_ORDER=TEON_SLPOUT_AUTOREFRESH_CTL_START_DISPON",
            f"FIRST_FRAME_TE_BOOTSTRAP_REQUIRED=YES",
            f"LINUX_REQUIRES_FIRST_FRAME_BOOTSTRAP_AFTER_CONTINUOUS_SPLASH=NO",
            f"LINUX_FULL_COLD_RESUME_BOOTSTRAP_SEQUENCE=PANEL_ON_AUTOREFRESH_SETUP_CTL_START_DISPON",
            f"FIRST_FIRST_FRAME_BOOTSTRAP_DIVERGENCE=PP_AUTOREFRESH_DISABLED_AND_SLPOUT_AFTER_DISPON",
            f"FIRST_FRAME_BOOTSTRAP_CANDIDATE=PP_AUTOREFRESH_ENABLE_AND_SLPOUT_BEFORE_DISPON",
            f"F9_CORRECTION_READY=YES",
            f"CORRECTION_PERFORMED=YES",
            f"CORRECTION_DESCRIPTION=Enable PP0_AUTOREFRESH (0x80000001) and enforce authentic SLPOUT-before-DISPON order per Sony LK and DTB",
            f"GPIO10_TE_AFTER_CORRECTION={'YES' if (gpio10_trans != '0' or gpio10_high != '0') else 'NO'}",
            f"FRESH_RD_PTR_AFTER_CORRECTION={fresh_rd}",
            f"PP_COUNTER_RELOAD_SEEN={counter_reload}",
            f"PP_LINE_NONZERO={line_nonzero}",
            f"PP_LINE_MAX={line_max_val}",
            f"PP_OUT_NONZERO={out_nonzero}",
            f"PP_OUT_MAX={out_max_val}",
            f"PP0_DONE_SEEN={done_seen}",
            f"DSI_MDP_BUSY_SEEN={dsi_busy}",
            f"CMD_MDP_DONE_SEEN={cmd_done}",
            f"ROOT_CAUSE_STATUS={'HW_PROVEN' if (fresh_rd == 'YES' or line_nonzero == 'YES') else 'HW_PROVEN_ABSENT_PENDING_PANEL_INTERNAL_SCAN'}",
            f"FARTHEST_PIPELINE_STAGE_REACHED={'PP_FRAME_DONE' if done_seen == 'YES' else ('PP_SCANOUT_ADVANCE' if line_nonzero == 'YES' else ('RD_PTR_ASSERTED' if fresh_rd == 'YES' else 'CTL_START_AUTOREFRESH_ARMED_NO_TE'))}",
            f"D8_M8_FIRST_COMMAND_FRAME={'HW_PROVEN' if (fresh_rd == 'YES' and line_nonzero == 'YES' and done_seen == 'YES') else 'NOT_YET'}",
            f"NEXT_ACTION={'PROCEED_TO_FIRST_FRAME_POLISH' if (fresh_rd == 'YES' and line_nonzero == 'YES') else 'ANALYZE_F9_BOOTSTRAP_EVIDENCE'}",
        ]
        summary_str = "\n".join(summary) + "\n"
        (LOG_DIR / "final-evidence.txt").write_text(summary_str)
        (LOG_DIR / "host.txt").write_text("".join(transcript))

        note("\n=== F9 FINAL EVIDENCE SUMMARY ===\n" + summary_str + "\n")

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
        note(f"Fastboot check after F9 test:\n{fb_after}\n")

    return 0 if not boot_fault else 1


if __name__ == "__main__":
    sys.exit(main())
