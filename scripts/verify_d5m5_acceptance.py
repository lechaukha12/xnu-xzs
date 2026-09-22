#!/usr/bin/env python3
"""Verify the D5-M5 namespace + devfs/console hardware acceptance log."""

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
    (0x90, "acceptance telemetry emitted"),
    (0x91, "D5-M5 complete"),
    (0x01, "diagnostic terminal before PID 1"),
]

REQUIRED_TELEMETRY = [
    ("D5-M1_COMPLETE", "yes"),
    ("D5-M2_COMPLETE", "yes"),
    ("D5-M3_COMPLETE", "yes"),
    ("D5-M4_COMPLETE", "yes"),
    ("D5-M5_COMPLETE", "yes"),
    ("ROOT_NAMEI_PASS", "yes"),
    ("ROOT_NAMEI_RETURNS_GLOBAL_ROOTVNODE", "yes"),
    ("LAUNCHD_NAMEI_PASS", "yes"),
    ("LAUNCHD_VNODE_TYPE", "VREG"),
    ("LAUNCHD_OBJECT_ID", "10"),
    ("LAUNCHD_MODE", "0755"),
    ("LAUNCHD_SIZE", "16472"),
    ("DEVFS_KERNEL_MOUNT_PASS", "yes"),
    ("DEVFS_MOUNTPOINT", "/dev"),
    ("DEVFS_ROOT_NAMEI_PASS", "yes"),
    ("DEVFS_FS_TYPE", "devfs"),
    ("DEVFS_ROOT_VNODE_TYPE", "VDIR"),
    ("DEV_CONSOLE_NAMEI_PASS", "yes"),
    ("DEV_CONSOLE_VNODE_TYPE", "VCHR"),
    ("DEV_CONSOLE_MAJOR", "0"),
    ("DEV_CONSOLE_MINOR", "0"),
    ("CONSOLE_OPEN_ATTEMPTED", "no"),
    ("PID1_STARTED", "no"),
    ("EXECVE_ATTEMPTED", "no"),
    ("EL0_ENTRY_ATTEMPTED", "no"),
    ("CMD24_COUNT", "0"),
    ("CMD25_COUNT", "0"),
    ("ZERO_STORAGE_WRITES", "yes"),
    ("D5_COMPLETE", "no"),
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
    expected_m5 = [error for error, _ in REQUIRED_D5M5_SEQUENCE]
    failures = []

    print("=== D5-M5 CHECKPOINT AUDIT ===")
    if is_ordered_subsequence(REQUIRED_D5M4_PREFIX, d5m4):
        print("[PASS] D5-M4 root-mount regression prefix reached D530/91")
    else:
        print("[FAIL] incomplete or out-of-order D5-M4 regression prefix")
        failures.append("D5-M4 regression prefix")

    if 0x01 in d5m4:
        print("[FAIL] legacy D530/01 terminal fired before D5-M5")
        failures.append("premature D5-M4 terminal")
    else:
        print("[PASS] D5-M4 returned control to D5-M5")

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

    fatal_crumbs = [error for error in d5m5 if 0xE0 <= error <= 0xFF]
    if fatal_crumbs:
        values = ", ".join(f"D540/{error:02X}" for error in fatal_crumbs)
        print(f"[FAIL] fatal checkpoint(s) present: {values}")
        failures.append("fatal checkpoint")
    else:
        print("[PASS] no D5-M5 fatal checkpoint")

    print("\n=== D5-M5 TELEMETRY AUDIT ===")
    telemetry = parse_telemetry(text)
    for key, expected in REQUIRED_TELEMETRY:
        actual = telemetry.get(key)
        if actual == expected:
            print(f"[PASS] {key}={actual}")
        else:
            print(f"[FAIL] {key}={actual!r}; expected {expected!r}")
            failures.append(key)

    if failures:
        print("\nD5-M5 ACCEPTANCE VERIFICATION: FAIL")
        print("FAILED_INVARIANTS=" + ",".join(failures))
        return 1

    print("\nD5-M5 ACCEPTANCE VERIFICATION: 100% PASS")
    print("D5-M5_COMPLETE=yes")
    print("NAMESPACE_DEVFS_CONSOLE_VERIFIED=yes")
    print("HARD_STOP_BEFORE_PID1=yes")
    print("ZERO_STORAGE_WRITES=yes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
