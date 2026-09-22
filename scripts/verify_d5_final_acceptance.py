#!/usr/bin/env python3
"""Verify the complete Phase D5 final rootfs/filesystem hardware acceptance log."""

import os
import re
import sys


REQUIRED_D5M4_PREFIX = [
    0x00, 0x10, 0x20, 0x21, 0x22, 0x30, 0x31, 0x32, 0x40, 0x50,
    0x51, 0x60, 0x70, 0x71, 0x72, 0x80, 0x81, 0x90, 0x91,
]

REQUIRED_D5M5_SEQUENCE = [
    (0x00, "enter D5-M5 probe"),
    (0x10, "namei('/') returned global rootvnode"),
    (0x20, "namei('/sbin/launchd') returned VREG"),
    (0x21, "launchd identity/getattr verified"),
    (0x30, "devfs_kernel_mount('/dev') entered"),
    (0x31, "devfs_kernel_mount('/dev') succeeded"),
    (0x40, "namei('/dev') crossed into devfs"),
    (0x50, "namei('/dev/console') returned VCHR"),
    (0x51, "console device identity 0:0 verified"),
    (0x60, "namespace and devfs overlay complete"),
    (0x90, "D5-M5 acceptance telemetry emitted"),
    (0x91, "D5-M5 complete"),
    (0x01, "D5-M5 terminal handoff to D5-M6"),
]

REQUIRED_D5M6_SEQUENCE = [
    (0x00, "enter D5-M6 final seal probe"),
    (0x10, "D5-M1/M2 RAMDisk md0 rootdev regression verified"),
    (0x20, "D5-M3 XZSFS VFS driver regression verified"),
    (0x30, "D5-M4 real mounted rootvnode regression verified"),
    (0x40, "D5-M5 namespace and devfs overlay regression verified"),
    (0x50, "namei('/bin/sh') returned VREG"),
    (0x51, "/bin/sh identity & VNOP_GETATTR verified"),
    (0x60, "root filesystem read-only invariant verified"),
    (0x61, "zero storage write invariant verified"),
    (0x70, "D6 boundary closed"),
    (0x90, "final D5 acceptance telemetry emitted"),
    (0x91, "PHASE D5 COMPLETE & SEALED"),
    (0x01, "diagnostic terminal before D6 userspace bootstrap"),
]

REQUIRED_TELEMETRY = [
    ("D5-M1_COMPLETE", "yes"),
    ("D5-M2_COMPLETE", "yes"),
    ("D5-M3_COMPLETE", "yes"),
    ("D5-M4_COMPLETE", "yes"),
    ("D5-M5_COMPLETE", "yes"),
    ("D5-M6_COMPLETE", "yes"),
    ("D5_COMPLETE", "yes"),
    ("D5_SEALED", "yes"),
    ("ROOTDEV_IS_MD0", "yes"),
    ("ROOT_FS_TYPE", "xzsfs"),
    ("ROOT_FS_DEVICE", "md0"),
    ("GLOBAL_ROOTVNODE_INSTALLED", "yes"),
    ("NAMEI_ROOT_PASS", "yes"),
    ("NAMEI_SBIN_LAUNCHD_PASS", "yes"),
    ("NAMEI_BIN_SH_PASS", "yes"),
    ("BIN_SH_VNODE_TYPE", "VREG"),
    ("BIN_SH_OBJECT_ID", "5"),
    ("BIN_SH_MODE", "0755"),
    ("BIN_SH_SIZE", "16736"),
    ("DEVFS_MOUNTED", "yes"),
    ("NAMEI_DEV_PASS", "yes"),
    ("NAMEI_DEV_CONSOLE_PASS", "yes"),
    ("DEV_CONSOLE_VNODE_TYPE", "VCHR"),
    ("DEV_CONSOLE_MAJOR", "0"),
    ("DEV_CONSOLE_MINOR", "0"),
    ("NAMESPACE_DEVFS_OVERLAY_VERIFIED", "yes"),
    ("XZSFS_MOUNT_READ_ONLY", "yes"),
    ("ZERO_STORAGE_WRITES", "yes"),
    ("PID1_STARTED", "no"),
    ("EXECVE_ATTEMPTED", "no"),
    ("EL0_ENTRY_ATTEMPTED", "no"),
    ("CMD24_COUNT", "0"),
    ("CMD25_COUNT", "0"),
    ("ROADMAP_ADVANCED_TO", "D6"),
]


def parse_breadcrumbs(text):
    pattern = re.compile(
        r"CP[=:]\s*0x([0-9a-fA-F]+)[,\s]+ERR[=:]\s*0x([0-9a-fA-F]+)"
    )
    return [
        (int(match.group(1), 16), int(match.group(2), 16))
        for match in pattern.finditer(text)
    ]


def parse_telemetry(text):
    telemetry = {}
    pattern = re.compile(r"^([A-Za-z0-9_-]+)\s*[=:]\s*(.+?)\s*$")
    for line in text.splitlines():
        match = pattern.match(line.strip())
        if match:
            telemetry[match.group(1)] = match.group(2)
    return telemetry


def is_ordered_subsequence(expected, actual):
    position = 0
    for value in actual:
        if position < len(expected) and value == expected[position]:
            position += 1
    return position == len(expected)


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <console-log>")
        return 2

    log_path = sys.argv[1]
    if not os.path.isfile(log_path):
        print(f"ERROR: log file not found: {log_path}")
        return 2

    with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
        text = handle.read()

    crumbs = parse_breadcrumbs(text)
    d5m4 = [error for checkpoint, error in crumbs if checkpoint == 0xD530]
    d5m5 = [error for checkpoint, error in crumbs if checkpoint == 0xD540]
    d5m6 = [error for checkpoint, error in crumbs if checkpoint == 0xD550]
    expected_m5 = [error for error, _ in REQUIRED_D5M5_SEQUENCE]
    expected_m6 = [error for error, _ in REQUIRED_D5M6_SEQUENCE]
    failures = []

    print("=== D5 FINAL ACCEPTANCE CHECKPOINT AUDIT ===")
    if is_ordered_subsequence(REQUIRED_D5M4_PREFIX, d5m4):
        print("[PASS] D5-M4 root-mount regression prefix reached D530/91")
    else:
        print("[FAIL] incomplete or out-of-order D5-M4 regression prefix")
        failures.append("D5-M4 regression prefix")

    print("\n--- D5-M5 Namespace & DevFS Sequence ---")
    for error, description in REQUIRED_D5M5_SEQUENCE:
        status = "PASS" if error in d5m5 else "FAIL"
        print(f"[{status}] D540/{error:02X}: {description}")
        if status == "FAIL":
            failures.append(f"D540/{error:02X}")

    if not is_ordered_subsequence(expected_m5, d5m5):
        print("[FAIL] D540 checkpoints are not in canonical order")
        failures.append("D540 order")
    else:
        print("[PASS] D540 checkpoints are in canonical order")

    print("\n--- D5-M6 Final Seal Sequence ---")
    for error, description in REQUIRED_D5M6_SEQUENCE:
        status = "PASS" if error in d5m6 else "FAIL"
        print(f"[{status}] D550/{error:02X}: {description}")
        if status == "FAIL":
            failures.append(f"D550/{error:02X}")

    if not is_ordered_subsequence(expected_m6, d5m6):
        print("[FAIL] D550 checkpoints are not in canonical order")
        failures.append("D550 order")
    else:
        print("[PASS] D550 checkpoints are in canonical order")

    fatal_crumbs_m5 = [error for error in d5m5 if 0xE0 <= error <= 0xFF]
    if fatal_crumbs_m5:
        values = ", ".join(f"D540/{error:02X}" for error in fatal_crumbs_m5)
        print(f"[FAIL] D5-M5 fatal checkpoint(s) present: {values}")
        failures.append("D5-M5 fatal checkpoint")

    fatal_crumbs_m6 = [error for error in d5m6 if 0xE0 <= error <= 0xFF]
    if fatal_crumbs_m6:
        values = ", ".join(f"D550/{error:02X}" for error in fatal_crumbs_m6)
        print(f"[FAIL] D5-M6 fatal checkpoint(s) present: {values}")
        failures.append("D5-M6 fatal checkpoint")

    if not fatal_crumbs_m5 and not fatal_crumbs_m6:
        print("[PASS] no fatal checkpoints in D5 execution")

    print("\n=== D5 FINAL ACCEPTANCE TELEMETRY AUDIT ===")
    telemetry = parse_telemetry(text)
    for key, expected in REQUIRED_TELEMETRY:
        actual = telemetry.get(key)
        if actual == expected:
            print(f"[PASS] {key}={actual}")
        else:
            print(f"[FAIL] {key}={actual!r}; expected {expected!r}")
            failures.append(key)

    if failures:
        print("\nD5 FINAL ACCEPTANCE VERIFICATION: FAIL")
        print("FAILED_INVARIANTS=" + ",".join(failures))
        return 1

    print("\nD5 FINAL ACCEPTANCE VERIFICATION: 100% PASS")
    print("D5_FINAL_ACCEPTANCE_VERIFIER=PASS")
    print("D5_COMPLETE=yes")
    print("D5_SEALED=yes")
    print("ALL_D5_MILESTONES_VERIFIED=yes")
    print("HARD_STOP_BEFORE_PID1=yes")
    print("ZERO_STORAGE_WRITES=yes")
    print("ROADMAP_ADVANCED_TO=D6")
    return 0


if __name__ == "__main__":
    sys.exit(main())
