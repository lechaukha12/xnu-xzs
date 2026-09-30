#!/usr/bin/env python3
"""D8-M8 F17 Golden TWRP DDIC State Readback.

Boots TWRP (RAM-only, NO FLASH) on BH905SX976:
1. Verifies fastboot connection.
2. RAM boots artifacts/builds/twrp-kagura.img.
3. Waits for ADB.
4. Confirms active display state (TWRP_GOLDEN_DISPLAY_ACTIVE=YES, PP line, GPIO10).
5. Reads DDIC registers via /sys/kernel/debug/mdp/panel_off and panel_reg:
   - 0x0A: Display Power Mode
   - 0x0B: Address Mode
   - 0x0C: Pixel Format
   - 0x0D: Display Mode
   - 0x0E: Signal Mode
   - 0x0F: Diagnostic Result
   - 0x04: Read DDB Start
   - 0xDA: DSI ID1 (raw_ud)
   - 0xDB: DSI ID2 (raw_vd)
   - 0xDC: DSI ID3
   - Vendor test: 0xB0, 0xD6, 0xC6
6. Saves raw telemetry to artifacts/hw/d8m8/f17-twrp-ddic/.
7. Safely reboots device back to fastboot.
"""

import os
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

    note("=== D8-M8 F17: GOLDEN TWRP DDIC STATE READBACK ===\n")

    # 1. Fastboot detection
    stdout, _, _ = run(["fastboot", "devices"])
    note(f"Fastboot devices before boot:\n{stdout}\n")
    if SERIAL not in stdout:
        adb_devs, _, _ = run(["adb", "devices"])
        if SERIAL not in adb_devs:
            note("ERROR: Device not found in fastboot or adb!\n")
            return 1
        note("Device already in ADB mode. Skipping boot...\n")
    else:
        note(f"Booting TWRP from {TWRP_IMG} (RAM-only, NO FLASH)...\n")
        out, err, rc = run(["fastboot", "-s", SERIAL, "boot", str(TWRP_IMG)], timeout=60)
        note(f"fastboot boot output:\n{out}\n{err}\n")
        if rc != 0:
            note("ERROR: fastboot boot failed!\n")
            return 1

    # 2. Wait for ADB
    note("Waiting for ADB connection (up to 45s)...")
    connected = False
    for _ in range(45):
        time.sleep(1)
        out, _, _ = run(["adb", "devices"])
        if SERIAL in out and "device" in out:
            connected = True
            break
        sys.stdout.write(".")
        sys.stdout.flush()
    note(f"\nADB connected: {connected}\n")
    if not connected:
        note("ERROR: Failed to connect to ADB!\n")
        return 1

    time.sleep(2)

    # 3. Check active display state
    note("--- 1. Display & Framebuffer Status ---")
    fb_modes = adb_shell("cat /sys/class/graphics/fb0/modes")
    note(f"fb0 modes: {fb_modes}")

    # Read MDP / PP0 registers to verify active display commit
    adb_shell("echo '0x71000 0x30' > /sys/kernel/debug/mdp/off")
    pp0_raw = adb_shell("cat /sys/kernel/debug/mdp/reg")
    (OUT_DIR / "twrp-pp0-raw.txt").write_text(pp0_raw + "\n")
    note(f"PP0 registers dump:\n{pp0_raw}\n")

    # Read interrupt status
    adb_shell("echo '0x1014 0x1' > /sys/kernel/debug/mdp/off")
    intr_raw = adb_shell("cat /sys/kernel/debug/mdp/reg")
    note(f"MDP_INTR_STATUS dump:\n{intr_raw}\n")

    # Check dmesg for panel driver messages
    dmesg_panel = adb_shell("dmesg | grep -iE 'panel|pcc|dsi'")
    (OUT_DIR / "twrp-dmesg-panel.txt").write_text(dmesg_panel + "\n")
    note(f"dmesg panel lines:\n{dmesg_panel}\n")

    # 4. Check available debugfs nodes
    note("--- 2. Checking Panel Debugfs Nodes ---")
    ls_debug_mdp = adb_shell("ls -la /sys/kernel/debug/mdp/")
    note(f"Contents of /sys/kernel/debug/mdp/:\n{ls_debug_mdp}\n")

    has_panel_debugfs = ("panel_off" in ls_debug_mdp and "panel_reg" in ls_debug_mdp)
    note(f"PANEL_DEBUGFS_EXISTS = {has_panel_debugfs}\n")

    regs_to_read = [
        (0x0A, 1, "Display Power Mode"),
        (0x0B, 1, "Address Mode"),
        (0x0C, 1, "Pixel Format"),
        (0x0D, 1, "Display Mode"),
        (0x0E, 1, "Signal Mode"),
        (0x0F, 1, "Diagnostic Result"),
        (0x04, 3, "Read DDB Start"),
        (0x45, 2, "Get Scanline"),
        (0x52, 2, "Read DDB Continue"),
        (0x54, 2, "Read First Checksum"),
        (0x56, 2, "Read Continue Checksum"),
        (0xDA, 1, "DSI ID1 (raw_ud)"),
        (0xDB, 1, "DSI ID2 (raw_vd)"),
        (0xDC, 1, "DSI ID3"),
        (0xB0, 2, "Vendor B0 (Protect)"),
        (0xD6, 2, "Vendor D6 (Output)"),
        (0xC6, 2, "Vendor C6 (Waveform/FPS)"),
    ]

    readings = []
    if has_panel_debugfs:
        note("--- 3. Reading DDIC Registers via panel_reg ---")
        for reg_addr, count, name in regs_to_read:
            # Set offset and count
            adb_shell(f"echo '0x{reg_addr:02x} {count}' > /sys/kernel/debug/mdp/panel_off")
            val = adb_shell("cat /sys/kernel/debug/mdp/panel_reg")
            note(f"Reg 0x{reg_addr:02X} ({name:25s}) [cnt={count}]: {val}")
            readings.append(f"0x{reg_addr:02X}: {val} # {name}")

        (OUT_DIR / "twrp-ddic-readings.txt").write_text("\n".join(readings) + "\n")
    else:
        note("WARNING: /sys/kernel/debug/mdp/panel_off not present; checking alternate nodes...")
        ls_debug_all = adb_shell("find /sys/kernel/debug -maxdepth 3 -name '*panel*'")
        note(f"Alternate panel nodes:\n{ls_debug_all}\n")

    # 5. Measure GPIO10 transitions in TWRP
    note("--- 4. Active TE & GPIO10 Validation in TWRP ---")
    # Read TLMM GPIO10 register: 0x0101a000
    # In TWRP, devmem can read physical memory if enabled
    devmem_test = adb_shell("which devmem || busybox which devmem")
    note(f"devmem binary: {devmem_test}")
    gpio10_readbacks = []
    for _ in range(10):
        v = adb_shell("busybox devmem 0x0101a004 32 || devmem 0x0101a004 32")
        gpio10_readbacks.append(v)
        time.sleep(0.005)
    note(f"GPIO10 IN_OUT readbacks: {gpio10_readbacks}")

    # 6. Reboot safely back to fastboot
    note("\nRebooting cleanly back to fastboot...")
    run(["adb", "-s", SERIAL, "reboot", "bootloader"])
    time.sleep(4)

    fb_after, _, _ = run(["fastboot", "devices"])
    note(f"Fastboot check after test:\n{fb_after}\n")

    (OUT_DIR / "twrp-readback-report.txt").write_text("\n".join(report) + "\n")
    note(f"Report saved to {OUT_DIR / 'twrp-readback-report.txt'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
