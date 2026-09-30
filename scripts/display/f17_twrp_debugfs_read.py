#!/usr/bin/env python3
"""D8-M8 F17 TWRP Golden DDIC Readback with Debugfs Mount.

1. Boots TWRP in RAM (NO FLASH).
2. Waits for recovery ADB.
3. Mounts debugfs: mount -t debugfs none /sys/kernel/debug
4. Checks /sys/kernel/debug/mdp/panel_off and panel_reg
5. Reads DDIC registers (0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xDA, 0xDB, 0xDC, 0x04)
6. Reads MDP PP0 registers & verifies active TE
7. Reboots cleanly back to fastboot.
"""

import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SERIAL = "BH905SX976"
TWRP_IMG = ROOT / "artifacts/builds/twrp-kagura.img"
OUT_DIR = ROOT / "artifacts/hw/d8m8/f17-twrp-ddic"


def run(cmd_list, timeout=60):
    res = subprocess.run(cmd_list, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=timeout)
    return res.stdout.strip(), res.stderr.strip(), res.returncode


def adb_shell(cmd_str, timeout=15):
    res = subprocess.run(["adb", "-s", SERIAL, "shell", cmd_str], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=timeout)
    return res.stdout.strip()


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    report = []

    def note(msg):
        print(msg)
        report.append(msg)

    note("=== D8-M8 F17: TWRP GOLDEN DDIC READBACK (DEBUGFS MOUNT) ===\n")

    # 1. Fastboot detection
    stdout, _, _ = run(["fastboot", "devices"])
    note(f"Fastboot devices:\n{stdout}\n")
    if SERIAL not in stdout:
        note("ERROR: Target not in fastboot mode!\n")
        return 1

    # 2. RAM Boot TWRP
    note(f"RAM-booting TWRP from {TWRP_IMG}...\n")
    out, err, rc = run(["fastboot", "-s", SERIAL, "boot", str(TWRP_IMG)], timeout=60)
    note(f"fastboot boot output:\n{out}\n{err}\n")
    if rc != 0:
        note("ERROR: fastboot boot failed!\n")
        return 1

    # 3. Wait for recovery
    note("Waiting for ADB recovery mode (up to 45s)...")
    connected = False
    for _ in range(45):
        time.sleep(1)
        out, _, _ = run(["adb", "-s", SERIAL, "get-state"])
        if "recovery" in out or "device" in out:
            connected = True
            break
        sys.stdout.write(".")
        sys.stdout.flush()
    note(f"\nADB status: {connected}\n")
    if not connected:
        note("ERROR: Failed to connect to recovery ADB!\n")
        return 1

    time.sleep(8)  # Let TWRP GUI settle

    # 4. Mount debugfs
    note("--- 1. Mounting debugfs ---")
    mnt_out = adb_shell("mount -t debugfs none /sys/kernel/debug 2>&1")
    note(f"mount debugfs: {mnt_out}")

    ls_mdp = adb_shell("ls -la /sys/kernel/debug/mdp/")
    note(f"Contents of /sys/kernel/debug/mdp/:\n{ls_mdp}\n")

    # 5. Read active PP0 registers to prove display is active
    note("--- 2. Active PP0 Registers ---")
    adb_shell("echo '71000 10' > /sys/kernel/debug/mdp/off")
    pp0_cfg = adb_shell("cat /sys/kernel/debug/mdp/reg")
    note(f"PP0 Cfg (0x71000):\n{pp0_cfg}")

    adb_shell("echo '71010 10' > /sys/kernel/debug/mdp/off")
    pp0_cnt = adb_shell("cat /sys/kernel/debug/mdp/reg")
    note(f"PP0 Cnt (0x71010):\n{pp0_cnt}")

    adb_shell("echo '71020 10' > /sys/kernel/debug/mdp/off")
    pp0_line = adb_shell("cat /sys/kernel/debug/mdp/reg")
    note(f"PP0 Line (0x71020):\n{pp0_line}")

    adb_shell("echo '1010 10' > /sys/kernel/debug/mdp/off")
    intr = adb_shell("cat /sys/kernel/debug/mdp/reg")
    note(f"MDP Intr (0x1010):\n{intr}")

    # 6. Read DDIC registers
    note("\n--- 3. Reading Golden DDIC Registers ---")
    regs = [
        (0x0A, 1, "Display Power Mode"),
        (0x0B, 1, "Address Mode"),
        (0x0C, 1, "Pixel Format"),
        (0x0D, 1, "Display Mode"),
        (0x0E, 1, "Signal Mode"),
        (0x0F, 1, "Diagnostic Result"),
        (0x04, 3, "Read DDB Start"),
        (0xDA, 1, "DSI ID1 (raw_ud)"),
        (0xDB, 1, "DSI ID2 (raw_vd)"),
        (0xDC, 1, "DSI ID3"),
        (0xB0, 2, "Vendor B0"),
        (0xD6, 2, "Vendor D6"),
        (0xC6, 2, "Vendor C6"),
    ]

    has_panel_reg = ("panel_reg" in ls_mdp)
    readings = []
    if has_panel_reg:
        for reg_addr, count, name in regs:
            adb_shell(f"echo '{reg_addr:x} {count}' > /sys/kernel/debug/mdp/panel_off")
            val = adb_shell("cat /sys/kernel/debug/mdp/panel_reg")
            note(f"Reg 0x{reg_addr:02X} ({name:25s}) [cnt={count}]: {val}")
            readings.append(f"0x{reg_addr:02X}: {val} # {name}")
    else:
        note("panel_reg node not found in /sys/kernel/debug/mdp/!")
        # Check all nodes in debugfs
        find_panel = adb_shell("find /sys/kernel/debug/ -name '*panel*' 2>/dev/null")
        note(f"Find panel in debugfs:\n{find_panel}")

    # 7. Sample GPIO10 via busybox devmem
    note("\n--- 4. Active TE Sampling on GPIO10 ---")
    gpio10_samples = []
    for _ in range(15):
        v = adb_shell("/sbin/busybox devmem 0x0101a004 32 2>/dev/null || devmem 0x0101a004 32 2>/dev/null")
        gpio10_samples.append(v)
        time.sleep(0.002)
    note(f"GPIO10 samples: {gpio10_samples}")

    # 8. Reboot safely back to fastboot
    note("\nRebooting cleanly back to fastboot...")
    run(["adb", "-s", SERIAL, "reboot", "bootloader"])
    time.sleep(4)

    fb_after, _, _ = run(["fastboot", "devices"])
    note(f"Fastboot check after test:\n{fb_after}\n")

    summary_path = OUT_DIR / "twrp-golden-readback.txt"
    summary_path.write_text("\n".join(report) + "\n")
    note(f"Saved golden readback to {summary_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
