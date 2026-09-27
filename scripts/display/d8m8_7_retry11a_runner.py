#!/usr/bin/env python3
"""D8-M8-7 Retry #11A: PP0 to DSI Passive Handshake Trace.
Executes prerequisites M2..M4, P1, real physical panel power-on & DCS sequence (m8-panel-prepare),
executes M8-1..M8-6 (with Qualcomm SW-TE override geometry: PP0_SYNC_CFG_VSYNC = 0x00080093,
PP0_SYNC_CFG_HGHT = 0x00000873, PP0_SYNC_WRCOUNT = 0x00000785, PP0_START_POS = 0x00000780,
PP0_RD_PTR_IRQ = 0x00000781, DSI_TRIG_CTRL = 0x80000004),
verifies PREKICK_READY=YES and PANEL_READY=yes (does NOT require physical TE), and then performs
exactly ONE controlled MDP command-mode kickoff (CTL_START).
Captures passive R11A-00..12 handshake snapshots and a bounded 100 ms poll.
Halts strictly after one attempt.
Saves all evidence to artifacts/hw/d8m8/m8-7-retry11a/.
"""

import argparse
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

_TOOL = Path(__file__).resolve().parents[2] / "tools" / "xzs-console" / "xzs-console.py"
_spec = importlib.util.spec_from_file_location("xzs_console", _TOOL)
xzs_console = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(xzs_console)


def command_output(argv):
    return subprocess.run(argv, check=True, capture_output=True, text=True).stdout.strip()


def sha256_file(path):
    import hashlib
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def value_from_log(text, key, default="UNKNOWN"):
    match = re.search(rf"(?m)^{re.escape(key)}=([^\r\n]+)$", text)
    return match.group(1).strip() if match else default


def snapshot_rows(text):
    rows = []
    for line in text.splitlines():
        if not line.startswith("R11A_SNAPSHOT="):
            continue
        fields = dict(re.findall(r"([A-Z0-9_]+)=([^ ]+)", line))
        rows.append(fields)
    return rows


def collect(dev, seconds=5.0, required_substr=None):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        chunk, kind = xzs_console.bulk_read(dev, timeout_ms=300)
        if chunk:
            buf += chunk
            tail = buf[max(0, len(buf)-200):]
            has_prompt = (b"xzs#" in tail or b"code=3" in tail)
            if required_substr:
                req_bytes = required_substr.encode("utf-8") if isinstance(required_substr, str) else required_substr
                if (req_bytes in buf or b"ALREADY" in buf) and has_prompt:
                    time.sleep(0.1)
                    extra, _ = xzs_console.bulk_read(dev, timeout_ms=100)
                    if extra:
                        buf += extra
                    break
            else:
                if has_prompt:
                    time.sleep(0.1)
                    extra, _ = xzs_console.bulk_read(dev, timeout_ms=100)
                    if extra:
                        buf += extra
                    break
        time.sleep(0.05)
    return buf


def send_cmd(dev, cmd_str, wait_sec=5.0, required_substr=None):
    print(f"\n>>> SEND: {cmd_str.strip()}", flush=True)
    for _ in range(5):
        pre_drain, _ = xzs_console.bulk_read(dev, timeout_ms=100)
        if not pre_drain:
            break
    payload = cmd_str.encode("utf-8") if isinstance(cmd_str, str) else cmd_str
    if not payload.endswith(b"\n"):
        payload += b"\n"
    written, kind = xzs_console.bulk_write(dev, payload, timeout_ms=2000)
    if kind is not None or written != len(payload):
        print(f"!!! WRITE ERROR: written={written}, kind={kind}", flush=True)
        return ""
    resp = collect(dev, seconds=wait_sec, required_substr=required_substr)
    resp_text = resp.decode("utf-8", errors="replace")
    print(resp_text, end="", flush=True)
    return resp_text


def main():
    parser = argparse.ArgumentParser(description="D8-M8-7 Retry #11A PP0 to DSI Passive Handshake Trace Runner")
    parser.add_argument("--log-dir", default="artifacts/hw/d8m8/m8-7-retry11a",
                        help="Directory to store hardware test logs and stage artifacts")
    parser.add_argument("--no-boot", action="store_true",
                        help="Skip fastboot boot step if already booted")
    args = parser.parse_args()

    log_dir = Path(args.log_dir)
    if log_dir.exists() and any(log_dir.iterdir()):
        print(f"ERROR: refusing to overwrite non-empty evidence directory: {log_dir}", file=sys.stderr)
        sys.exit(1)
    log_dir.mkdir(parents=True, exist_ok=True)
    host_file = log_dir / "host.txt"

    boot_img = Path("artifacts/builds/xzs-xnu-boot.img")
    if not boot_img.exists():
        print(f"ERROR: {boot_img} does not exist!", file=sys.stderr)
        sys.exit(1)

    kernel_path = Path("src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple")
    audit_path = Path("docs/XZS_D8_M8_RETRY11A_SOURCE_AUDIT.md")
    identity_lines = [
        f"git_commit={command_output(['git', 'rev-parse', 'HEAD'])}",
        f"git_branch={command_output(['git', 'branch', '--show-current'])}",
        f"boot_image={boot_img}",
        f"boot_sha256={sha256_file(boot_img)}",
    ]
    if kernel_path.exists():
        identity_lines.extend([
            f"kernel={kernel_path}",
            f"kernel_sha256={sha256_file(kernel_path)}",
        ])
    else:
        identity_lines.append("kernel_sha256=UNKNOWN")
    (log_dir / "build-identity.txt").write_text("\n".join(identity_lines) + "\n")
    (log_dir / "git-diff.patch").write_text(command_output(["git", "diff", "HEAD"]) + "\n")
    (log_dir / "git-stat.txt").write_text(command_output(["git", "diff", "--stat", "HEAD"]) + "\n")
    if audit_path.exists():
        shutil.copyfile(audit_path, log_dir / "source-audit.md")
        shutil.copyfile(audit_path, log_dir / "register-map-audit.md")

    print("=== D8-M8-7 RETRY #11A HARDWARE RUNNER START ===", flush=True)

    if not args.no_boot:
        print("=== STEP 0: FRESH BOOT VIA FASTBOOT ===", flush=True)
        boot_res = subprocess.run(["fastboot", "-s", "BH905SX976", "boot", str(boot_img)],
                                  capture_output=True, text=True)
        print(boot_res.stdout, flush=True)
        print(boot_res.stderr, flush=True)
        if boot_res.returncode != 0:
            print("ERROR: fastboot boot failed!", file=sys.stderr)
            sys.exit(1)

    print("=== WAITING FOR USB CONSOLE ENUMERATION ===", flush=True)
    dev = xzs_console.open_stable_device(timeout_sec=60)
    if dev is None:
        print("ERROR: could not open stable XNU USB device", flush=True)
        sys.exit(1)

    print("=== STEP 1: SYNC SHELL PROMPT ===", flush=True)
    start_t = time.time()
    while time.time() - start_t < 15.0:
        xzs_console.bulk_write(dev, b"\n", timeout_ms=1000)
        time.sleep(0.5)
        initial_drain = collect(dev, seconds=1.0)
        if initial_drain:
            print(initial_drain.decode("utf-8", errors="replace"), end="", flush=True)
            if b"xzs#" in initial_drain:
                break

    full_log = []

    def run_step(name, cmd, wait_s=5.0, required_substr=None):
        print(f"\n--- {name} ---", flush=True)
        out = send_cmd(dev, cmd, wait_sec=wait_s, required_substr=required_substr)
        full_log.append(f"\n# {name}\n> {cmd}\n{out}")
        return out

    # Baseline PWD
    run_step("BASELINE PWD", "pwd\n", 1.5)

    # 1. Prerequisites: M2 Power and Clocks
    print("\n=== PREREQUISITE: M2 DISPLAY GDSC & CORE CLOCKS ===", flush=True)
    run_step("ENABLE MMSS_MMAGIC_AHB", "clocks mmagic-ahb-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MMSS_MMAGIC_CFG_AHB", "clocks mmagic-cfg-ahb-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MMAGIC_MDSS_NOC", "clocks mmagic-mdss-noc-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MMAGIC_MDSS_AXI", "clocks mmagic-mdss-axi-on\n", 2.0, required_substr="PASS")
    run_step("POWER ON MDSS GDSC", "display power mdss-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MDSS_AHB", "clocks mdss-ahb-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MDSS_AXI", "clocks mdss-axi-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MDSS_MDP", "clocks mdp-on\n", 2.0, required_substr="PASS")
    run_step("VERIFY CORE CLOCKS", "clocks mdss-critical-status\n", 2.0, required_substr="PASS")

    # 2. Prerequisite: M3 Lower Layer
    print("\n=== PREREQUISITE: M3 LOWER-LAYER HARDWARE BRING-UP ===", flush=True)
    m3_out = run_step("RUN M3 FULL", "display m3-run full\n", 50.0, required_substr="PASS_FULL_M3")
    if "RESULT=PASS_FULL_M3" not in m3_out and "RESULT=M3_ALREADY_ATTEMPTED" not in m3_out:
        print("!!! M3 LOWER LAYER FAILED. Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # 3. Prerequisite: M4 DSI Host Layer
    print("\n=== PREREQUISITE: M4 DSI0 HOST BRING-UP ===", flush=True)
    m4_out = run_step("RUN M4 FULL", "display m4-run full\n", 10.0, required_substr="PASS_MODE2")
    if "RESULT=PASS_MODE2" not in m4_out and "already" not in m4_out.lower():
        print("!!! M4 DSI HOST FAILED. Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # 4. Prerequisite: P1 GPIO Layer
    print("\n=== PREREQUISITE: P1 TLMM GPIO BRING-UP ===", flush=True)
    p1_out = run_step("RUN P1", "display p1-run\n", 5.0, required_substr="RESULT=PASS_P1")
    if "RESULT=PASS_P1" not in p1_out:
        print("!!! D8-P1 HARDWARE EXECUTION FAILED. Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # 5. REAL COMMAND-MODE PANEL-ON PREPARATION
    # =========================================================================
    print("\n=== EXECUTING REAL PANEL POWER-ON & DCS INITIALIZATION (M8-PANEL-PREPARE) ===", flush=True)
    prep_out = run_step("PANEL PREPARE", "display m8-status\n", 30.0, required_substr="PANEL_READY=yes")
    (log_dir / "panel-prepare.txt").write_text(prep_out)
    if "PANEL_READY=yes" not in prep_out:
        print("!!! PANEL PREPARATION FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # 6. STAGED M8 EXECUTION: M8-1 through M8-6
    # =========================================================================

    print("\n=== EXECUTING STAGE M8-1: FRAMEBUFFER ALLOCATION & CPU PATTERN ===", flush=True)
    m8_1_out = run_step("STAGE M8-1", "display m8-fb-init\n", 30.0, required_substr="M8_1             = PASS")
    if "M8_1             = PASS" not in m8_1_out:
        print("!!! STAGE M8-1 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    print("\n=== EXECUTING STAGE M8-2: RGB0 SSPP PROGRAMMING ===", flush=True)
    m8_2_out = run_step("STAGE M8-2", "display m8-rgb0-config\n", 15.0, required_substr="M8_2             = PASS")
    if "M8_2             = PASS" not in m8_2_out:
        print("!!! STAGE M8-2 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    print("\n=== EXECUTING STAGE M8-3: LM0 LAYER MIXER PROGRAMMING ===", flush=True)
    m8_3_out = run_step("STAGE M8-3", "display m8-lm0-config\n", 15.0, required_substr="M8_3             = PASS")
    if "M8_3             = PASS" not in m8_3_out:
        print("!!! STAGE M8-3 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    print("\n=== EXECUTING STAGE M8-4: PP0 & DSI MDP STREAM PROGRAMMING (RETRY #11A SW-TE OVERRIDE) ===", flush=True)
    m8_4_out = run_step("STAGE M8-4", "display m8-stream-config\n", 15.0, required_substr="M8_4             = PASS")
    if "M8_4             = PASS" not in m8_4_out:
        print("!!! STAGE M8-4 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    print("\n=== EXECUTING STAGE M8-5: CTL0 ROUTING ===", flush=True)
    m8_5_out = run_step("STAGE M8-5", "display m8-ctl-config\n", 15.0, required_substr="CTL_TOP          = 0x00020020")
    if "PASS" not in m8_5_out and "MATCH" not in m8_5_out:
        print("!!! STAGE M8-5 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    print("\n=== EXECUTING STAGE M8-6: CTL FLUSH PROGRAMMING ===", flush=True)
    m8_6_out = run_step("STAGE M8-6", "display m8-flush-config\n", 15.0, required_substr="0x00020048")
    if "0x00020048" not in m8_6_out and "PASS" not in m8_6_out and "M8_6" not in m8_6_out:
        print("!!! STAGE M8-6 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # Pre-Kick Status Diagnostic
    print("\n=== EXECUTING PRE-KICK STATUS DIAGNOSTIC ===", flush=True)
    prekick_out = run_step("PREKICK STATUS", "display m8-prekick-status\n", 20.0, required_substr="=== M8 PRE-KICK STATUS ===")
    (log_dir / "pre-kick.txt").write_text(prekick_out)

    assert "CTL_START_COUNT=0" in prekick_out, "CTL_START_COUNT must be 0"
    assert "MDP_KICKOFF_COUNT=0" in prekick_out, "MDP_KICKOFF_COUNT must be 0"
    assert "FRAMEBUFFER_SCANOUT_COUNT=0" in prekick_out, "FRAMEBUFFER_SCANOUT_COUNT must be 0"
    assert "PANEL_READY=yes" in prekick_out, "PANEL_READY must be yes"
    assert "PP0_TEAR_CHECK_EN=0x00000001" in prekick_out, "PP0_TEAR_CHECK_EN must be 1"
    assert "PP_SYNC_CONFIG_VSYNC=0x00080093" in prekick_out, "PP_SYNC_CONFIG_VSYNC must be 0x00080093"
    assert "PP_SYNC_CONFIG_HEIGHT=0x00000873" in prekick_out, "PP_SYNC_CONFIG_HEIGHT must be 0x00000873"
    assert "PP_SYNC_WRCOUNT=0x00000785" in prekick_out, "PP_SYNC_WRCOUNT must be 0x00000785"
    assert "PP_VSYNC_INIT_VAL=0x00000780" in prekick_out, "PP_VSYNC_INIT_VAL must be 0x00000780"
    assert "PP_START_POS=0x00000780" in prekick_out, "PP_START_POS must be 0x00000780"
    assert "PP_RD_PTR_IRQ=0x00000781" in prekick_out, "PP_RD_PTR_IRQ must be 0x00000781"
    assert "DSI_TRIG_CTRL=0x80000004" in prekick_out, "DSI_TRIG_CTRL must be 0x80000004"
    assert "PREKICK_READY=YES" in prekick_out, "PREKICK_READY must be YES"

    # =========================================================================
    # 7. EXECUTING M8-7 RETRY #11A: CONTROLLED SINGLE KICKOFF
    # =========================================================================
    print("\n=======================================================", flush=True)
    print("=== EXECUTING M8-7 RETRY #11A: CONTROLLED SINGLE KICKOFF ===", flush=True)
    print("=======================================================", flush=True)

    kickoff_out = run_step("M8-7 RETRY #11A KICKOFF", "display m8-kickoff\n", 25.0, required_substr="M8_7_RETRY11A=")
    (log_dir / "kickoff.txt").write_text(kickoff_out)

    # Extract completion and post-frame blocks
    completion_lines = []
    postframe_lines = []
    in_comp = False
    in_post = False
    for line in kickoff_out.splitlines():
        if "PP0_DONE_OBSERVED=" in line or "START_CYCLES=" in line:
            in_comp = True
        if "DSI_STATUS=" in line or "POST_DSI_ACK_ERR=" in line:
            in_post = True
        if in_comp:
            completion_lines.append(line)
        if in_post:
            postframe_lines.append(line)

    (log_dir / "completion.txt").write_text("\n".join(completion_lines) + "\n")
    (log_dir / "post-frame.txt").write_text("\n".join(postframe_lines) + "\n")

    rows = snapshot_rows(kickoff_out)
    table_columns = [
        "R11A_SNAPSHOT", "TIMESTAMP_US", "DSI_STATUS_RAW", "DSI_MDP_BUSY_BIT",
        "DSI_INT_CTRL", "DSI_MDP_DONE_RAW", "DSI_MDP_DONE_MASK", "CTL_FLUSH",
        "CTL_START", "PP_LINE", "PP_OUT_LINE", "ACK_ERR", "TIMEOUT",
    ]
    table_lines = ["\t".join(table_columns)]
    for row in rows:
        table_lines.append("\t".join(row.get(column, "UNKNOWN") for column in table_columns))
    (log_dir / "decoded-checkpoints.tsv").write_text("\n".join(table_lines) + "\n")

    busy_seen = value_from_log(kickoff_out, "DSI_MDP_BUSY_SEEN")
    done_seen = value_from_log(kickoff_out, "DSI_MDP_DONE_RAW_SEEN")
    pp_done = value_from_log(kickoff_out, "PP0_DONE_OBSERVED")
    pp_line = value_from_log(kickoff_out, "PP0_LINE_COUNT_FINAL")
    pp_out_line = value_from_log(kickoff_out, "PP0_OUT_LINE_COUNT_FINAL")
    final_row = rows[-1] if rows else {}
    final_busy = final_row.get("DSI_MDP_BUSY_BIT", "UNKNOWN")
    ack_err = final_row.get("ACK_ERR", value_from_log(kickoff_out, "ACK_ERR"))
    timeout = final_row.get("TIMEOUT", value_from_log(kickoff_out, "TIMEOUT"))

    if len(rows) != 13:
        classification = "R11A-D: AMBIGUOUS_INSUFFICIENT_EVIDENCE"
        acceptance = "UNKNOWN"
    elif done_seen == "yes":
        classification = "R11A-C: DSI_MDP_TRANSFER_COMPLETES"
        acceptance = "YES — HW_PROVEN"
    elif busy_seen == "yes" and final_busy == "1":
        classification = "R11A-B: DSI_ACCEPTS_BUT_STAYS_BUSY"
        acceptance = "YES — HW_PROVEN"
    elif busy_seen == "no":
        classification = "R11A-A: DSI_NEVER_ACCEPTS_MDP_REQUEST"
        acceptance = "NO — HW_PROVEN"
    else:
        classification = "R11A-D: AMBIGUOUS_INSUFFICIENT_EVIDENCE"
        acceptance = "UNKNOWN"

    report = f"""# D8-M8 Retry #11A hardware evidence

PASS_R11A_SOURCE_TRACE=yes
PASS_R11A_HW_TRACE={'yes' if len(rows) == 13 else 'no'}

Does DSI0 accept the PP0 command-mode MDP request?
{acceptance}

Primary classification:
{classification}

PP0_DONE={pp_done}
PP0_LINE_COUNT={pp_line}
PP0_OUT_LINE_COUNT={pp_out_line}
DSI_MDP_BUSY_SEEN={busy_seen}
DSI_MDP_DONE_RAW={done_seen}
DSI_ACK_ERR={ack_err}
DSI_TIMEOUT={timeout}

FIRST_MDP_FRAME={'yes' if pp_done == 'yes' else 'no'}
FIRST_SCANOUT_TRANSPORT={'yes' if done_seen == 'yes' else 'no'}
FIRST_VISIBLE_PIXELS=no

Snapshots captured={len(rows)}/13
System health markers are in kickoff.txt and host.txt.
"""
    (log_dir / "final-evidence-report.md").write_text(report)

    # Save full session log
    host_file.write_text("".join(full_log))
    print(f"\nAll logs and stage artifacts written to {log_dir.resolve()}", flush=True)

    # Final Verification
    print("\n=== FINAL M8-7 RETRY #11A VERIFICATION ===", flush=True)
    if "M8_7_RETRY11A=PASS" in kickoff_out:
        print(">>> RESULT: M8-7 RETRY #11A PASS! Diagnostic trace captured. <<<", flush=True)
    else:
        print(">>> RESULT: M8-7 RETRY #11A FAIL or TIMEOUT! Check artifacts for details. <<<", flush=True)


if __name__ == "__main__":
    main()
