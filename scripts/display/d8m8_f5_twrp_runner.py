#!/usr/bin/env python3
"""D8-M8 F5 Task: TWRP Golden Same-State Periodic TE & Kickoff Dependency Audit.

Boots TWRP (RAM-only, NO FLASH), reproduces the exact logical state of XNU F4:
- Panel active, external TE configured.
- Reads MDP_INTR_STATUS.
- Explicitly clears PP0_DONE (bit 8), RD_PTR (bit 12), WR_PTR (bit 16).
- Observes for 150 ms while IDLE (no frame requested).
- Determines whether TWRP generates periodic TE / RD_PTR while idle (Branch E1 vs E2).
- Induces a controlled redraw and records post-kickoff TE / PP_LINE appearance.
- Reboots target to fastboot and saves artifacts to artifacts/hw/d8m8/f5-twrp-te-lifecycle/.
"""

import os
import subprocess
import sys
import time
from pathlib import Path


SERIAL = "BH905SX976"
ROOT = Path(__file__).resolve().parents[2]
TWRP_IMG = ROOT / "artifacts/builds/twrp-kagura.img"
OUT_DIR = ROOT / "artifacts/hw/d8m8/f5-twrp-te-lifecycle"


def run_cmd(cmd_list, timeout=60):
    res = subprocess.run(cmd_list, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=timeout)
    return res.stdout.strip(), res.stderr.strip(), res.returncode


def adb(cmd_str, timeout=15):
    full = ["adb", "-s", SERIAL, "shell", cmd_str]
    stdout, stderr, code = run_cmd(full, timeout=timeout)
    return stdout


def read_mdp_regs(off_hex, count_hex):
    adb(f"echo '{off_hex} {count_hex}' > /sys/kernel/debug/mdp/off")
    raw = adb("cat /sys/kernel/debug/mdp/reg")
    regs = {}
    for line in raw.splitlines():
        if ":" in line:
            parts = line.split(":")
            base_addr = int(parts[0], 16)
            words = parts[1].strip().split()
            for idx, w in enumerate(words):
                rel_addr = f"0x{base_addr + idx * 4:08x}"
                regs[rel_addr] = f"0x{w}"
                if base_addr < 0x00900000:
                    abs_addr = f"0x{0x00900000 + base_addr + idx * 4:08x}"
                    regs[abs_addr] = f"0x{w}"
    return regs, raw


def write_mdp_reg(off_hex, val_hex):
    return adb(f"echo '{off_hex} {val_hex}' > /sys/kernel/debug/mdp/reg")


def push_file(local_content, remote_path):
    tmp_local = Path("/tmp") / Path(remote_path).name
    tmp_local.write_text(local_content)
    subprocess.run(["adb", "-s", SERIAL, "push", str(tmp_local), remote_path], check=True)
    adb(f"chmod +x {remote_path}")
    tmp_local.unlink(missing_ok=True)


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    transcript = []

    def note(msg):
        sys.stdout.write(msg)
        sys.stdout.flush()
        transcript.append(msg)

    note("=== D8-M8 F5: TWRP SAME-STATE PERIODIC TE AUDIT ===\n")

    if not TWRP_IMG.exists():
        note(f"ERROR: {TWRP_IMG} does not exist!\n")
        return 1

    # 1. Fastboot detection & boot
    stdout, _, _ = run_cmd(["fastboot", "devices"])
    note(f"Fastboot devices:\n{stdout}\n")
    if SERIAL not in stdout:
        adb_devs, _, _ = run_cmd(["adb", "devices"])
        note(f"ADB devices:\n{adb_devs}\n")
        if SERIAL not in adb_devs:
            note("ERROR: Device not found in fastboot or adb!\n")
            return 1
        note("Device already in ADB mode. Continuing...\n")
    else:
        note(f"Booting TWRP from {TWRP_IMG} (RAM-only, NO FLASH)...\n")
        boot_out, boot_err, boot_rc = run_cmd(["fastboot", "-s", SERIAL, "boot", str(TWRP_IMG)], timeout=60)
        note(f"fastboot boot output:\n{boot_out}\n{boot_err}\n")
        if boot_rc != 0:
            note("ERROR: fastboot boot failed!\n")
            return 1

        note("Waiting for ADB recovery...\n")
        run_cmd(["adb", "-s", SERIAL, "wait-for-recovery"], timeout=60)

    # 2. Wait for TWRP GUI to settle into idle state
    note("Waiting 10 seconds for TWRP GUI to settle into idle state...\n")
    time.sleep(10)

    who = adb("id")
    kernel = adb("uname -a")
    note(f"ADB User: {who}\nKernel: {kernel}\n")
    (OUT_DIR / "device_info.txt").write_text(f"USER: {who}\nKERNEL: {kernel}\n")

    # 3. Read initial MDSS and PP0 registers
    note("\n--- 1. INITIAL MDSS & PP0 STATE ---\n")
    # Read MDP interrupt registers (0x1010: INTR_EN, INTR_STATUS, INTR_CLEAR)
    regs_intr, raw_intr = read_mdp_regs("1010", "10")
    note(f"MDP Interrupt Regs:\n{raw_intr}\n")

    # Read PP0 sync config & counters (0x71000: TEAR_CHECK_EN, SYNC_CFG_VSYNC, SYNC_CFG_HGHT, SYNC_WRCOUNT)
    regs_pp_cfg, raw_pp_cfg = read_mdp_regs("71000", "10")
    note(f"PP0 Cfg Regs (0x71000):\n{raw_pp_cfg}\n")

    # Read PP0 counters (0x71010: VSYNC_INIT, INT_COUNT_VAL, SYNC_THRESH, START_POS)
    regs_pp_cnt, raw_pp_cnt = read_mdp_regs("71010", "10")
    note(f"PP0 Cnt Regs (0x71010):\n{raw_pp_cnt}\n")

    # Read PP0 line count (0x71020: RD_PTR_IRQ, WR_PTR_IRQ, OUT_LINE_COUNT, LINE_COUNT)
    regs_pp_line, raw_pp_line = read_mdp_regs("71020", "10")
    note(f"PP0 Line Regs (0x71020):\n{raw_pp_line}\n")

    initial_intr_status = regs_intr.get("0x00901014", regs_intr.get("0x00001014", "UNKNOWN"))
    initial_pp_int_cnt = regs_pp_cnt.get("0x00971014", regs_pp_cnt.get("0x00071014", "UNKNOWN"))
    initial_pp_line = regs_pp_line.get("0x0097102c", regs_pp_line.get("0x0007102c", "UNKNOWN"))
    note(f"INITIAL_INTR_STATUS={initial_intr_status}\n")
    note(f"INITIAL_PP_INT_COUNT={initial_pp_int_cnt}\n")
    note(f"INITIAL_PP_LINE_COUNT={initial_pp_line}\n")

    (OUT_DIR / "initial_state.txt").write_text(
        f"INITIAL_INTR_STATUS={initial_intr_status}\n"
        f"INITIAL_PP_INT_COUNT={initial_pp_int_cnt}\n"
        f"INITIAL_PP_LINE_COUNT={initial_pp_line}\n\n"
        f"INTR:\n{raw_intr}\n"
        f"PP_CFG:\n{raw_pp_cfg}\n"
        f"PP_CNT:\n{raw_pp_cnt}\n"
        f"PP_LINE:\n{raw_pp_line}\n"
    )

    # 4. PHASE B SAME-STATE EXPERIMENT: Explicit Interrupt Clear & Idle Observation
    note("\n--- 2. PHASE B: EXPLICIT INTERRUPT CLEAR & IDLE OBSERVATION ---\n")
    # Clear PP0_DONE (bit 8), RD_PTR (bit 12), WR_PTR (bit 16) -> 0x00011100
    write_mdp_reg("1018", "00011100")
    time.sleep(0.005)  # 5 ms

    # Verify post-clear status
    regs_post_clear, raw_post_clear = read_mdp_regs("1010", "10")
    post_clear_intr_status = regs_post_clear.get("0x00901014", regs_post_clear.get("0x00001014", "UNKNOWN"))
    note(f"POST_CLEAR_INTR_STATUS={post_clear_intr_status}\n")
    intr_clear_pass = (post_clear_intr_status in ["0x00000000", "0x0"])
    note(f"TWRP_IDLE_INTR_CLEAR_PASS={'YES' if intr_clear_pass else 'NO'}\n")

    # Run on-target fast observation loop script for 150 ms without frame request
    idle_observe_script = """#!/bin/sh
OUT="/tmp/idle_obs.txt"
rm -f "$OUT"
for i in $(seq 1 40); do
    echo "1010 10" > /sys/kernel/debug/mdp/off
    INTR=$(cat /sys/kernel/debug/mdp/reg | grep "00001010:" | awk '{print $3}')
    echo "71010 10" > /sys/kernel/debug/mdp/off
    CNT=$(cat /sys/kernel/debug/mdp/reg | grep "00071010:" | awk '{print $3}')
    echo "$i $INTR $CNT" >> "$OUT"
    usleep 3000
done
"""
    push_file(idle_observe_script, "/tmp/run_idle_obs.sh")
    note("Running 150 ms on-target idle observation script...\n")
    adb("/tmp/run_idle_obs.sh")
    idle_obs_raw = adb("cat /tmp/idle_obs.txt")
    note(f"Idle Observation Telemetry:\n{idle_obs_raw}\n")
    (OUT_DIR / "idle_observation.txt").write_text(idle_obs_raw + "\n")

    # Analyze idle observation telemetry
    seen_fresh_rd_ptr_idle = False
    backward_jumps_idle = 0
    prev_cnt = None
    min_cnt = None
    max_cnt = None

    for line in idle_obs_raw.splitlines():
        parts = line.strip().split()
        if len(parts) >= 3:
            try:
                intr_val = int(parts[1], 16)
                cnt_val = int(parts[2], 16)

                if (intr_val & 0x00001000) != 0:
                    seen_fresh_rd_ptr_idle = True

                if min_cnt is None or cnt_val < min_cnt:
                    min_cnt = cnt_val
                if max_cnt is None or cnt_val > max_cnt:
                    max_cnt = cnt_val

                if prev_cnt is not None and cnt_val < prev_cnt and (prev_cnt - cnt_val) > 100:
                    backward_jumps_idle += 1
                prev_cnt = cnt_val
            except ValueError:
                continue

    counter_reload_seen_idle = (backward_jumps_idle > 0)
    note(f"TWRP_IDLE_FRESH_RD_PTR_AFTER_CLEAR={'YES' if seen_fresh_rd_ptr_idle else 'NO'}\n")
    note(f"TWRP_IDLE_COUNTER_RELOAD_SEEN={'YES' if counter_reload_seen_idle else 'NO'}\n")
    note(f"TWRP_IDLE_BACKWARD_JUMPS={backward_jumps_idle}\n")

    # 5. Controlled Redraw Experiment (Induce frame redraw)
    note("\n--- 3. CONTROLLED REDRAW EXPERIMENT ---\n")
    redraw_script = """#!/bin/sh
OUT="/tmp/redraw_obs.txt"
rm -f "$OUT"
echo "1018 00011100" > /sys/kernel/debug/mdp/reg
for i in $(seq 1 50); do
    echo "1010 10" > /sys/kernel/debug/mdp/off
    INTR=$(cat /sys/kernel/debug/mdp/reg | grep "00001010:" | awk '{print $3}')
    echo "71010 20" > /sys/kernel/debug/mdp/off
    REG_DUMP=$(cat /sys/kernel/debug/mdp/reg)
    CNT=$(echo "$REG_DUMP" | grep "00071010:" | awk '{print $3}')
    LINE=$(echo "$REG_DUMP" | grep "00071020:" | awk '{print $5}')
    echo "$i $INTR $CNT $LINE" >> "$OUT"
    usleep 2000
done
"""
    push_file(redraw_script, "/tmp/run_redraw_obs.sh")

    # Start background sampler
    proc = subprocess.Popen(["adb", "-s", SERIAL, "shell", "/tmp/run_redraw_obs.sh"])
    time.sleep(0.01)
    # Issue touch input swipe to force frame redraw
    note("Triggering UI redraw via input swipe...\n")
    adb("input swipe 300 1000 300 500 50")
    proc.wait(timeout=10)

    redraw_obs_raw = adb("cat /tmp/redraw_obs.txt")
    note(f"Redraw Observation Telemetry:\n{redraw_obs_raw}\n")
    (OUT_DIR / "redraw_observation.txt").write_text(redraw_obs_raw + "\n")

    seen_rd_ptr_redraw = False
    seen_pp_line_redraw = False
    max_line_redraw = 0

    for line in redraw_obs_raw.splitlines():
        parts = line.strip().split()
        if len(parts) >= 4:
            try:
                intr_val = int(parts[1], 16)
                line_val = int(parts[3], 16)
                if (intr_val & 0x00001000) != 0:
                    seen_rd_ptr_redraw = True
                if line_val > max_line_redraw:
                    max_line_redraw = line_val
                if line_val > 0:
                    seen_pp_line_redraw = True
            except ValueError:
                continue

    note(f"REDRAW_SEEN_RD_PTR={'YES' if seen_rd_ptr_redraw else 'NO'}\n")
    note(f"REDRAW_SEEN_PP_LINE={'YES' if seen_pp_line_redraw else 'NO'}\n")
    note(f"REDRAW_MAX_PP_LINE=0x{max_line_redraw:08x}\n")

    if seen_fresh_rd_ptr_idle:
        twrp_first_appears = "ALREADY_PERIODIC"
    elif seen_rd_ptr_redraw:
        twrp_first_appears = "POST_KICK"
    else:
        twrp_first_appears = "UNKNOWN"
    note(f"TWRP_TE_FIRST_APPEARS={twrp_first_appears}\n")

    summary = [
        f"TWRP_IDLE_INTR_CLEAR_PASS={'YES' if intr_clear_pass else 'NO'}",
        f"TWRP_IDLE_FRESH_RD_PTR_AFTER_CLEAR={'YES' if seen_fresh_rd_ptr_idle else 'NO'}",
        f"TWRP_IDLE_COUNTER_RELOAD_SEEN={'YES' if counter_reload_seen_idle else 'NO'}",
        f"TWRP_IDLE_BACKWARD_JUMPS={backward_jumps_idle}",
        f"TWRP_TE_FIRST_APPEARS={twrp_first_appears}",
        f"REDRAW_SEEN_RD_PTR={'YES' if seen_rd_ptr_redraw else 'NO'}",
        f"REDRAW_SEEN_PP_LINE={'YES' if seen_pp_line_redraw else 'NO'}",
        f"REDRAW_MAX_PP_LINE=0x{max_line_redraw:08x}",
    ]
    (OUT_DIR / "final_summary.txt").write_text("\n".join(summary) + "\n")
    (OUT_DIR / "host.txt").write_text("".join(transcript))

    # 6. Safely return device to fastboot
    note("\nReturning device cleanly to fastboot mode...\n")
    adb("reboot bootloader")
    time.sleep(3)
    fb_out, _, _ = run_cmd(["fastboot", "devices"])
    note(f"Fastboot check after test:\n{fb_out}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
