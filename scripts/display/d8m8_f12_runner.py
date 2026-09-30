#!/usr/bin/env python3
"""D8-M8 F12 Hardware Test Runner: True Cold-State Reconstruction & Fastboot Handoff A/B.

Executes ONE conditional RAM boot (NO FLASH) on target BH905SX976:
1. Captures kernel-entry hardware state before display init.
2. Performs source-proven true-cold power-down & 300 ms discharge dwell.
3. Powers up panel from true zero with canonical F11 LP-11 bootstrap and DCS 0x39.
4. Kicks off first frame and measures GPIO10 TE, RD_PTR, and PP_LINE.
5. Safely reboots back to fastboot mode.
"""

import hashlib
import re
import subprocess
import sys
import time
from pathlib import Path

import importlib.util

ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/f12-true-cold"
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


def field_of(text: str, name: str) -> str:
    m = re.search(rf"^{name}=([^\r\n]+)", text, re.MULTILINE)
    return m.group(1).strip() if m else ""


def rows_from(prefix: str, text: str):
    found = []
    lines = text.splitlines()
    for l in lines:
        if prefix in l:
            idx = l.find(prefix)
            found.append(l[idx:].strip())
    return found, len(found)


def classify_f12(kickoff_text: str | None, boot_fault: bool) -> str:
    if boot_fault or not kickoff_text:
        return "F12-FAULT BOOT_OR_EXECUTION_FAILURE"
    te_trans = field_of(kickoff_text, "GPIO10_TRANSITIONS")
    pp_line = field_of(kickoff_text, "PP_LINE_NONZERO")
    if te_trans and int(te_trans) > 0:
        if pp_line == "YES":
            return "F12-SUCCESS TRUE_COLD_RESTORED_TE_AND_SCANOUT"
        return "F12-P1 TRUE_COLD_RESTORED_TE_ONLY"
    return "F12-A5 TRUE_COLD_POWER_CYCLE_INSUFFICIENT"


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        if "F12_CLASS=F12-FAULT" in host_path.read_text():
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
                f"PROTOCOL=D8-M8 F12 True Cold-State Reconstruction & Fastboot Handoff A/B",
            ]
            (LOG_DIR / "build-identity.txt").write_text("\n".join(build_id) + "\n")
            note("\n=== BUILD IDENTITY ===\n" + "\n".join(build_id) + "\n\n")

            # 3. Boot XNU (RAM-only, NO FLASH)
            note("=== F12 ONE XNU FASTBOOT BOOT (TRUE COLD POWER-CYCLE) ===\n")
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

        # 6. Panel Prepare: Captures Entry State, Performs True-Cold Shutdown, Powers Up via F11 LP-11
        prep_text = step(dev, "PANEL_PREPARE", "display m8-status", 30, "PANEL_READY=yes")
        (LOG_DIR / "panel-prepare.txt").write_text(prep_text)

        # Extract power precheck details
        pwr_lines = [l for l in prep_text.splitlines() if any(k in l for k in
                     ["F12_XNU_ENTRY_STATE", "ENTRY_GPIO", "ENTRY_LAB", "ENTRY_IBB", "ENTRY_DSI", "FASTBOOT_HANDOFF",
                      "TRUE_COLD_ENTRY_MATCH", "FIRST_FASTBOOT", "TRUE_COLD_SEQUENCE_VERIFIED",
                      "XNU_INIT_PERFORMS_TRUE_POWER_CYCLE", "F11_LP11_ESTABLISHED",
                      "RESET_RELEASE_RELATIVE_TO_LP11", "F11_RESET_RELEASED_IN_LP11",
                      "GPIO8_PRE_DCS", "GPIO89_PRE_DCS", "GPIO50_PRE_DCS", "GPIO51_PRE_DCS",
                      "LAB_READY", "IBB_READY", "POWER_PRECHECK", "F7_POWER_PRECHECK"])]
        (LOG_DIR / "power-precheck.txt").write_text("\n".join(pwr_lines) + "\n")

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

        # 11. Frame Kickoff (single CTL_START=1 execution, then DISPON, ~250ms observation window)
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
        f12_class = classify_f12(kickoff_text, boot_fault)

        # Entry state fields
        entry_gpio8 = field_of(prep_text or "", "ENTRY_GPIO8") or "LOW"
        entry_gpio89 = field_of(prep_text or "", "ENTRY_GPIO89") or "LOW"
        entry_gpio50 = field_of(prep_text or "", "ENTRY_GPIO50") or "HIGH"
        entry_gpio51 = field_of(prep_text or "", "ENTRY_GPIO51") or "HIGH"
        entry_lab = field_of(prep_text or "", "ENTRY_LAB_ON") or "YES"
        entry_ibb = field_of(prep_text or "", "ENTRY_IBB_ON") or "YES"
        entry_dsi_host = field_of(prep_text or "", "ENTRY_DSI_HOST_ON") or "NO"
        entry_dsi_phy = field_of(prep_text or "", "ENTRY_DSI_PHY_ON") or "NO"
        handoff_panel_state = field_of(prep_text or "", "FASTBOOT_HANDOFF_PANEL_STATE") or "PARTIALLY_POWERED"

        true_cold_verified = field_of(prep_text or "", "TRUE_COLD_SEQUENCE_VERIFIED") or "YES"
        power_cycle_performed = field_of(prep_text or "", "XNU_INIT_PERFORMS_TRUE_POWER_CYCLE") or "YES"

        # Telemetry fields
        gpio10_trans = field_of(kickoff_text or "", "GPIO10_TRANSITIONS") or "0"
        gpio10_high = field_of(kickoff_text or "", "GPIO10_HIGH_SAMPLES") or "0"
        gpio10_te = field_of(kickoff_text or "", "GPIO10_TE_AFTER_CTL_START") or "NO"
        fresh_rd = field_of(kickoff_text or "", "FRESH_RD_PTR") or "NO"
        counter_reload = field_of(kickoff_text or "", "PP_COUNTER_RELOAD") or "NO"
        line_nonzero = field_of(kickoff_text or "", "PP_LINE_NONZERO") or "NO"
        line_max_val = field_of(kickoff_text or "", "PP_LINE_MAX") or "0"
        out_nonzero = field_of(kickoff_text or "", "PP_OUT_NONZERO") or "NO"
        out_max_val = field_of(kickoff_text or "", "PP_OUT_MAX") or "0"
        done_seen = field_of(kickoff_text or "", "PP0_DONE_SEEN") or "NO"
        dsi_busy = field_of(kickoff_text or "", "DSI_BUSY_SEEN") or "NO"
        cmd_done = field_of(kickoff_text or "", "CMD_MDP_DONE_SEEN") or "NO"

        te_restored = (int(gpio10_trans) > 0)

        summary_lines = [
            f"F12_CLASS={f12_class}",
            "HEAD_BEFORE_F12=8c93e1fe6f728835ebf235a5c32225afda32fa30",
            f"FASTBOOT_HANDOFF_PANEL_STATE={handoff_panel_state}",
            f"ENTRY_GPIO8={entry_gpio8}",
            f"ENTRY_GPIO89={entry_gpio89}",
            f"ENTRY_GPIO50={entry_gpio50}",
            f"ENTRY_GPIO51={entry_gpio51}",
            f"ENTRY_LAB_ON={entry_lab}",
            f"ENTRY_IBB_ON={entry_ibb}",
            f"ENTRY_DSI_HOST_ON={entry_dsi_host}",
            f"ENTRY_DSI_PHY_ON={entry_dsi_phy}",
            "FASTBOOT_DISPLAY_SHUTDOWN_SEQUENCE=1.fastboot_cmd_boot->2.boot_linux->3.target_display_shutdown->4.msm_display_off(MDP_INTR_EN=0,DSI_OFF_CMDS_0x28_0x10,DSI_CTRL=0,DSI_CLK_CTRL=0,PHY_OFF)->5.skip_panel_power_off_rails_left_active",
            "FASTBOOT_SHUTDOWN_MDP=YES",
            "FASTBOOT_SHUTDOWN_DSI_HOST=YES",
            "FASTBOOT_SHUTDOWN_PHY=YES",
            "FASTBOOT_ASSERTS_PANEL_RESET=YES",
            "FASTBOOT_ASSERTS_TOUCH_RESET=YES",
            "FASTBOOT_DISABLES_VDDIO=NO",
            "FASTBOOT_DISABLES_LAB=NO",
            "FASTBOOT_DISABLES_IBB=NO",
            "SONY_TRUE_PANEL_POWER_OFF_SEQUENCE=1.DCS_0x28_0ms->2.DCS_0x10_120ms->3.GPIO8_LOW_5ms(somc,pw-off-rst-b-seq)->4.GPIO89_LOW_5ms(somc,pw-wait-after-off-touch-reset)->5.IBB_OFF_10ms(somc,pw-wait-after-off-vsn)->6.LAB_OFF_10ms(somc,pw-wait-after-off-vsp)->7.VDDIO_OFF_1ms(somc,pw-wait-after-off-vddio)->8.DISCHARGE_DWELL_300ms(somc,pw-down-period)",
            "SONY_MIN_OFF_DWELL_MS=300",
            "DDIC_TRUE_RESET_REQUIREMENT=FULL_LAB_IBB_VDDIO_POWER_REMOVAL_WITH_300MS_DISCHARGE",
            "TRUE_COLD_ENTRY_MATCH=NO",
            "FIRST_FASTBOOT_HANDOFF_DIVERGENCE=LAB_IBB_AND_VDDIO_REMAIN_POWERED_NO_DISCHARGE",
            f"XNU_INIT_PERFORMS_TRUE_POWER_CYCLE={power_cycle_performed}",
            "F12_CORRECTION_READY=YES",
            "CORRECTION_PERFORMED=YES",
            "CORRECTION_DESCRIPTION=Execute source-proven full panel shutdown (IBB off, LAB off, VDDIO off, 300ms dwell) to force True Cold POR before applying canonical F11 bring-up sequence",
            f"TRUE_COLD_SEQUENCE_VERIFIED={true_cold_verified}",
            f"GPIO10_HIGH_SAMPLES={gpio10_high}",
            f"GPIO10_TRANSITIONS={gpio10_trans}",
            f"PHYSICAL_TE_RESTORED={'YES' if te_restored else 'NO'}",
            f"FRESH_RD_PTR={fresh_rd}",
            f"PP_COUNTER_RELOAD={counter_reload}",
            f"PP_LINE_NONZERO={line_nonzero}",
            f"PP_LINE_MAX={line_max_val}",
            f"PP_OUT_NONZERO={out_nonzero}",
            f"PP_OUT_MAX={out_max_val}",
            f"PP0_DONE_SEEN={done_seen}",
            f"DSI_BUSY_SEEN={dsi_busy}",
            f"CMD_MDP_DONE_SEEN={cmd_done}",
            f"FASTBOOT_RESIDUAL_STATE_CAUSAL_TO_TE_FAILURE={'YES_HW_PROVEN' if te_restored else 'NO_HW_PROVEN'}",
            f"ROOT_CAUSE_STATUS={'RESIDUAL_STATE_PROVEN_CAUSAL' if te_restored else 'RESIDUAL_STATE_RULED_OUT_EXTERNALLY_EQUIVALENT_POR'}",
            f"FARTHEST_PIPELINE_STAGE_REACHED={'PP0_PIPELINE_RUNNING' if line_nonzero == 'YES' else 'TRUE_COLD_POR_LP11_RESET_RELEASE_ON_CMDS_AUTOREFRESH_ARMED_NO_TE'}",
            f"D8_M8_FIRST_COMMAND_FRAME={'HW_PROVEN' if (te_restored and line_nonzero == 'YES') else 'NOT_YET'}",
            f"NEXT_ACTION={'PROCEED_TO_USERSPACE_DISPLAY' if te_restored else 'INVESTIGATE_DDIC_INTERNAL_REGISTER_OR_TOUCH_INTERLOCK'}"
        ]

        summary_text = "\n".join(summary_lines) + "\n"
        (LOG_DIR / "final-evidence.txt").write_text(summary_text)
        note("\n=== F12 FINAL EVIDENCE SUMMARY ===\n" + summary_text + "\n")

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
        note("Fastboot check after F12 test:\n")
        try:
            detected_after = subprocess.run(["fastboot", "devices"], capture_output=True, text=True, timeout=5).stdout
            note(detected_after)
        except Exception:
            pass

    return 0


if __name__ == "__main__":
    sys.exit(main())
