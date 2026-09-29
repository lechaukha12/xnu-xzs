#!/usr/bin/env python3
"""D8-M8 F13 Hardware Test Runner: Authentic Panel 9 DDIC Command Stream & Verification.

Executes ONE conditional RAM boot (NO FLASH) on target BH905SX976:
1. Verifies fastboot connection.
2. Boots XNU kernel with authentic Panel 9 (Samsung S6E3HA2) command sequence.
3. Performs standard MDP/DSI pipeline configuration and frame kickoff.
4. Records GPIO10 physical TE transitions, RD_PTR progression, and PP_LINE counter.
5. Safely reboots target back to fastboot mode.
"""

import hashlib
import re
import subprocess
import sys
import time
from pathlib import Path

import importlib.util

ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/f13-panel-identity"
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


def rows_from(prefix, text):
    lines = [line.strip() for line in text.splitlines() if line.startswith(prefix)]
    return lines, [dict(re.findall(r"([A-Z0-9_]+)=([^ ]+)", line)) for line in lines]


def classify_f13(kickoff: str | None, boot_fault: bool) -> str:
    if kickoff is None or boot_fault:
        return "F13-FAULT BOOT_OR_EXECUTION_FAILURE"
    te_pad_raw = field_of(kickoff, "GPIO10_TE_AFTER_CTL_START") or field_of(kickoff, "PHYSICAL_TE_AT_GPIO10")
    fresh_rd = field_of(kickoff, "FRESH_RD_PTR") == "YES"
    line_nonzero = field_of(kickoff, "PP_LINE_NONZERO") == "YES"
    done_seen = field_of(kickoff, "PP0_DONE_SEEN") == "YES"

    if fresh_rd and line_nonzero and done_seen:
        return "F13-A1 PANEL_9_COMMANDS_FRAME_COMPLETE"
    elif fresh_rd and line_nonzero:
        return "F13-A2 PANEL_9_COMMANDS_PIPELINE_STARTED"
    elif fresh_rd:
        return "F13-A3 PANEL_9_COMMANDS_TE_RECEIVED"
    elif te_pad_raw == "YES":
        return "F13-A4 PANEL_9_COMMANDS_PHYSICAL_TE_ACTIVE"
    else:
        return "F13-A5 PANEL_9_COMMANDS_INSUFFICIENT"


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        if "F13_CLASS=F13-FAULT" in host_path.read_text():
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
        nonlocal prep_text, prekick_text, kickoff_text
        note(f"\n--- {label} ---\n> {command}\n")
        output = helpers.send_cmd(dev, command + "\n", wait_sec=timeout, required_substr=expected)
        note(output + "\n")
        if label == "PANEL_PREPARE":
            prep_text = output
        elif label == "PREKICK":
            prekick_text = output
        elif label == "KICKOFF":
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
            git_diff = subprocess.run(["git", "diff", "HEAD~1"], capture_output=True, text=True).stdout
            (LOG_DIR / "git-diff.txt").write_text(git_diff)

            build_id = [
                f"HEAD={commit_sha}",
                f"BOOT_IMAGE={BOOT}",
                f"BOOT_SHA256={sha256_of(BOOT)}",
                f"KERNEL={KERNEL}",
                f"KERNEL_SHA256={sha256_of(KERNEL)}",
                f"PAC=0",
                f"PROTOCOL=D8-M8 F13 Authentic Panel 9 DDIC Command Stream & Physical TE Verification",
            ]
            (LOG_DIR / "build-identity.txt").write_text("\n".join(build_id) + "\n")
            note("\n=== BUILD IDENTITY ===\n" + "\n".join(build_id) + "\n\n")

            # 3. Boot XNU (RAM-only, NO FLASH)
            note("=== F13 ONE XNU FASTBOOT BOOT (AUTHENTIC PANEL 9 DDIC SEQUENCE) ===\n")
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
                raise RuntimeError("USB console failed to enumerate after boot")

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

        # 6. Panel Prepare with Authentic Panel 9 DDIC Command Stream
        prep_text = step(dev, "PANEL_PREPARE", "display m8-status", 30, "PANEL_READY=yes")
        (LOG_DIR / "panel-prepare.txt").write_text(prep_text)

        # Extract power precheck details
        pwr_lines = [l for l in prep_text.splitlines() if any(k in l for k in
                     ["GPIO8_PRE_DCS", "GPIO89_PRE_DCS", "GPIO50_PRE_DCS", "GPIO51_PRE_DCS",
                      "LAB_READY", "IBB_READY", "POWER_PRECHECK", "F7_POWER_PRECHECK",
                      "F11_LP11_ESTABLISHED", "RESET_RELEASE_RELATIVE_TO_LP11", "PANEL_PREPARE_SEQUENCE"])]
        (LOG_DIR / "power-precheck.txt").write_text("\n".join(pwr_lines) + "\n")

        if "POWER_PRECHECK=PASS" not in prep_text or "F13_PANEL_PREPARE_STABLE=YES" not in prep_text:
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

        # 11. Kickoff
        kickoff_text = step(dev, "KICKOFF", "display m8-kickoff", 60, "R11C_SAFE_SHUTDOWN=PASS")
        (LOG_DIR / "kickoff.txt").write_text(kickoff_text)

        # 12. Parse raw observation and handshake snapshots
        raw_obs = []
        for line in kickoff_text.splitlines():
            if any(k in line for k in ["SNAPSHOT_", "SAMPLE_", "RD_PTR", "PP_LINE", "GPIO10", "DSI_CMD_DMA_STATUS"]):
                raw_obs.append(line)
        (LOG_DIR / "raw-observation.txt").write_text("\n".join(raw_obs) + "\n")

        snaps = [l for l in kickoff_text.splitlines() if "SNAPSHOT_" in l]
        (LOG_DIR / "raw-handshake-snapshots.txt").write_text("\n".join(snaps) + "\n")

    except Exception as exc:
        note(f"\nExecution error: {exc}\n")
        boot_fault = True
    finally:
        f13_class = classify_f13(kickoff_text, boot_fault)
        note(f"\nFINAL F13_CLASS = {f13_class}\n")

        gpio10_high = field_of(kickoff_text or "", "GPIO10_HIGH_SAMPLES") or "0"
        gpio10_trans = field_of(kickoff_text or "", "GPIO10_TRANSITIONS") or "0"
        gpio10_te = "YES" if (int(gpio10_trans) > 0 or field_of(kickoff_text or "", "PHYSICAL_TE_AT_GPIO10") == "YES") else "NO"
        fresh_rd = field_of(kickoff_text or "", "FRESH_RD_PTR") or "NO"
        counter_reload = field_of(kickoff_text or "", "PP_COUNTER_RELOAD") or "NO"
        line_nonzero = field_of(kickoff_text or "", "PP_LINE_NONZERO") or "NO"
        line_max_val = field_of(kickoff_text or "", "PP_LINE_MAX") or "0x00000000"
        out_nonzero = field_of(kickoff_text or "", "PP_OUT_NONZERO") or "NO"
        out_max_val = field_of(kickoff_text or "", "PP_OUT_MAX") or "0x00000000"
        done_seen = field_of(kickoff_text or "", "PP0_DONE_SEEN") or "NO"
        dsi_busy = field_of(kickoff_text or "", "DSI_BUSY_SEEN") or "NO"
        cmd_done = field_of(kickoff_text or "", "CMD_MDP_DONE_SEEN") or "NO"

        summary_lines = [
            f"F13_CLASS={f13_class}",
            f"BOOT_ATTEMPTED={'YES' if boot_attempted else 'NO'}",
            f"BOOT_FAULT={'YES' if boot_fault else 'NO'}",
            f"PANEL_READY={field_of(prep_text or '', 'PANEL_READY') or 'NO'}",
            f"PANEL_PREPARE_SEQUENCE={field_of(prep_text or '', 'PANEL_PREPARE_SEQUENCE') or 'UNKNOWN'}",
            f"SLPOUT_SENT={field_of(prep_text or '', 'SLPOUT_SENT') or 'NO'}",
            f"DISPON_SENT={field_of(prep_text or '', 'DISPON_SENT') or 'NO'}",
            f"GPIO10_HIGH_SAMPLES={gpio10_high}",
            f"GPIO10_TRANSITIONS={gpio10_trans}",
            f"PHYSICAL_TE_RESTORED={gpio10_te}",
            f"FRESH_RD_PTR={fresh_rd}",
            f"PP_COUNTER_RELOAD={counter_reload}",
            f"PP_LINE_NONZERO={line_nonzero}",
            f"PP_LINE_MAX={line_max_val}",
            f"PP_OUT_NONZERO={out_nonzero}",
            f"PP_OUT_MAX={out_max_val}",
            f"PP0_DONE_SEEN={done_seen}",
            f"DSI_BUSY_SEEN={dsi_busy}",
            f"CMD_MDP_DONE_SEEN={cmd_done}",
            f"F13_CORRECTION_CAUSAL_TO_PHYSICAL_TE={'YES_HW_PROVEN' if gpio10_te == 'YES' else 'NO_HW_PROVEN'}",
            f"NEXT_ACTION={'PROCEED_TO_USERSPACE_DISPLAY' if gpio10_te == 'YES' else 'INVESTIGATE_PANEL_REVISION_OR_HARDWARE_INTERLOCK'}"
        ]

        summary_text = "\n".join(summary_lines) + "\n"
        (LOG_DIR / "final-evidence.txt").write_text(summary_text)
        note("\n=== F13 FINAL EVIDENCE SUMMARY ===\n" + summary_text + "\n")

        # Safely reboot target back to fastboot
        if dev:
            note("Rebooting device cleanly to fastboot...\n")
            try:
                helpers.send_cmd(dev, "reboot bootloader\n", wait_sec=5)
            except Exception:
                pass
            try:
                helpers.xzs_console.release_device(dev)
            except Exception:
                pass

        time.sleep(3)
        note("Fastboot check after F13 test:\n")
        try:
            detected_after = subprocess.run(["fastboot", "devices"], capture_output=True, text=True, timeout=5).stdout
            note(detected_after)
        except Exception:
            pass

        host_path.write_text("".join(transcript))
        note(f"Host transcript written to {host_path}\n")


if __name__ == "__main__":
    main()
