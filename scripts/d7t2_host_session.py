#!/usr/bin/env python3
"""Scripted D7-T2 shell session over the existing bulk console.

One process, one boot. Z1-Z4 run first so the tty bridge turns on, then
every shell command is sent in the same session. The last command is
reboot, which asks the target to warm-reset for pstore collection.
"""

import importlib.util
import sys
import time
from pathlib import Path

_TOOL = Path(__file__).resolve().parents[1] / "tools" / "xzs-console" / "xzs-console.py"
_spec = importlib.util.spec_from_file_location("xzs_console", _TOOL)
xzs_console = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(xzs_console)


def collect(dev, seconds):
    end = time.time() + seconds
    buf = b""
    quiet = 0
    while time.time() < end:
        chunk = xzs_console.test_bulk_in(dev, timeout_ms=200)
        if chunk:
            buf += chunk
            quiet = 0
        else:
            quiet += 1
            if buf and quiet >= 5:
                break
            time.sleep(0.05)
    return buf


def main() -> int:
    deadline = time.time() + 180
    dev = None
    while time.time() < deadline and dev is None:
        dev = xzs_console.find_xzs_device()
        if dev is None:
            time.sleep(0.2)
    if dev is None:
        print("USB_DEVICE_FOUND=no")
        print("VID_PID=")
        return 1
    print("USB_DEVICE_FOUND=yes")
    print("VID_PID=1209:000A")
    try:
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
    except Exception:
        pass
    try:
        dev.set_configuration()
        xzs_console.usb.util.claim_interface(dev, 0)
    except Exception:
        pass

    out_ok = False
    for _ in range(40):
        out_ok = xzs_console.test_bulk_out(dev, timeout_ms=1000)
        if out_ok:
            break
        time.sleep(0.3)
    in_data = None
    for _ in range(20):
        in_data = xzs_console.test_bulk_in(dev, timeout_ms=1000)
        if in_data is not None:
            break
        time.sleep(0.2)
    loop_ok = False
    for _ in range(12):
        loop_ok = xzs_console.test_loopback(dev, timeout_ms=2000)
        if loop_ok:
            break
        time.sleep(0.2)
    z_ok = out_ok and in_data == b"XZS-BULK-IN-TEST\n" and loop_ok
    print("Z_PREP=" + ("yes" if z_ok else "no"))
    print("Z2_BULK_OUT=" + ("yes" if out_ok else "no"))
    print("Z3_BULK_IN=" + ("yes" if in_data == b"XZS-BULK-IN-TEST\n" else "no"))
    print("Z4_LOOPBACK=" + ("yes" if loop_ok else "no"))

    # Give the bridge a moment after Z4 before the first shell line.
    transcript = collect(dev, 3)
    commands = [
        (b"\n", 2),
        (b"help\n", 3),
        (b"echo hello xnu\n", 3),
        (b"pwd\n", 3),
        (b"cd /bin\n", 3),
        (b"pwd\n", 3),
        (b"ls /\n", 4),
        (b"ls /bin\n", 4),
        (b"cat /etc/issue\n", 3),
        (b"/bin/hello\n", 8),
        (b"/bin/args one two three\n", 8),
        (b"nosuchcommand\n", 6),
        (b"cat /does-not-exist\n", 4),
        (b"cd /does-not-exist\n", 4),
        (b"reboot\n", 2),
    ]
    for cmd, wait_s in commands:
        xzs_console.test_bulk_out(dev, payload=cmd, timeout_ms=2000)
        transcript += collect(dev, wait_s)
    text = transcript.decode("latin1", errors="replace")
    print("HOST_TRANSCRIPT_BEGIN")
    print(text)
    print("HOST_TRANSCRIPT_END")
    print("HELP_SEEN=" + ("yes" if "help echo pwd cd ls cat exit" in text else "no"))
    print("ECHO_SEEN=" + ("yes" if "hello xnu" in text else "no"))
    print("PWD_ROOT_SEEN=" + ("yes" if "\n/\n" in text or "\n/\r" in text else "no"))
    print("PWD_BIN_SEEN=" + ("yes" if "\n/bin\n" in text or "\n/bin\r" in text else "no"))
    print("LS_ROOT_SEEN=" + ("yes" if "sbin" in text and "etc" in text else "no"))
    print("LS_BIN_SEEN=" + ("yes" if "hello" in text and "args" in text else "no"))
    print("CAT_SEEN=" + ("yes" if "XNU-XZS" in text else "no"))
    print("HELLO_SEEN=" + ("yes" if "hello from XNU-XZS userland" in text else "no"))
    print("ARGS_SEEN=" + ("yes" if "argc=4" in text and "argv[1]=one" in text and "argv[3]=three" in text else "no"))
    print("UNKNOWN_SEEN=" + ("yes" if "command not found" in text else "no"))
    print("ENOENT_SEEN=" + ("yes" if "open failed" in text else "no"))
    print("BAD_CD_SEEN=" + ("yes" if "cd: failed" in text else "no"))
    print("REBOOT_SENT=yes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
