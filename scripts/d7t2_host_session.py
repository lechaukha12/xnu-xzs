#!/usr/bin/env python3
"""Scripted D7-T2 shell session over the existing bulk console."""

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
    while time.time() < end:
        chunk = xzs_console.test_bulk_in(dev, timeout_ms=400)
        if chunk:
            buf += chunk
        elif buf:
            break
        else:
            time.sleep(0.05)
    return buf


def main() -> int:
    deadline = time.time() + 70
    dev = None
    while time.time() < deadline and dev is None:
        dev = xzs_console.find_xzs_device()
        if dev is None:
            time.sleep(0.2)
    if dev is None:
        print("USB_DEVICE_FOUND=no")
        return 1
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
    for _ in range(20):
        out_ok = xzs_console.test_bulk_out(dev, timeout_ms=1000)
        if out_ok:
            break
        time.sleep(0.3)
    in_data = None
    for _ in range(10):
        in_data = xzs_console.test_bulk_in(dev, timeout_ms=1000)
        if in_data is not None:
            break
        time.sleep(0.2)
    loop_ok = False
    for _ in range(8):
        loop_ok = xzs_console.test_loopback(dev, timeout_ms=2000)
        if loop_ok:
            break
        time.sleep(0.2)
    print("Z_PREP=" + ("yes" if out_ok and in_data == b"XZS-BULK-IN-TEST\n" and loop_ok else "no"))

    commands = [
        b"\n",
        b"help\n",
        b"echo hello xnu\n",
        b"pwd\n",
        b"ls /\n",
        b"ls /bin\n",
        b"cat /etc/issue\n",
        b"/bin/hello\n",
        b"/bin/args one two three\n",
        b"cd /bin\n",
        b"pwd\n",
        b"nosuchcommand\n",
    ]
    transcript = collect(dev, 2)
    for cmd in commands:
        xzs_console.test_bulk_out(dev, payload=cmd, timeout_ms=2000)
        transcript += collect(dev, 3)
    text = transcript.decode("latin1", errors="replace")
    print("HOST_TRANSCRIPT_BEGIN")
    print(text)
    print("HOST_TRANSCRIPT_END")
    print("HELP_SEEN=" + ("yes" if "help echo pwd cd ls cat exit" in text else "no"))
    print("ECHO_SEEN=" + ("yes" if "hello xnu" in text else "no"))
    print("PWD_ROOT_SEEN=" + ("yes" if "\n/\n" in text or text.startswith("/") else "no"))
    print("LS_ROOT_SEEN=" + ("yes" if "bin" in text and "etc" in text else "no"))
    print("CAT_SEEN=" + ("yes" if "XNU-XZS" in text else "no"))
    print("HELLO_SEEN=" + ("yes" if "hello from XNU-XZS userland" in text else "no"))
    print("ARGS_SEEN=" + ("yes" if "argc=4" in text and "argv[1]=one" in text else "no"))
    print("CD_SEEN=" + ("yes" if "/bin" in text else "no"))
    print("UNKNOWN_SEEN=" + ("yes" if "command not found" in text else "no"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
