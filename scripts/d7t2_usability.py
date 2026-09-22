#!/usr/bin/env python3
"""One-boot D7-T2 usability session.

Uses the same open, handshake, and bulk helpers as ./xzs-console.
Stops sending after three consecutive hard USB errors.
Does not report a command or reboot as delivered unless the bytes were written.
"""

import importlib.util
import sys
import time
from pathlib import Path

_TOOL = Path(__file__).resolve().parents[1] / "tools" / "xzs-console" / "xzs-console.py"
_spec = importlib.util.spec_from_file_location("xzs_console", _TOOL)
xzs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(xzs)


def collect(dev, seconds):
    end = time.time() + seconds
    buf = b""
    quiet = 0
    while time.time() < end:
        chunk, kind = xzs.bulk_read(dev, timeout_ms=200)
        if kind == "hard":
            print("USB_DATA_PLANE=stopped")
            print(f"USB_HARD_ERROR_COUNT={xzs.usb_hard_errors}")
            print(f"USB_TIMEOUT_COUNT={xzs.usb_timeouts}")
            return buf, False
        if chunk:
            buf += chunk
            quiet = 0
        else:
            quiet += 1
            if buf and quiet >= 5:
                break
    return buf, True


def send_line(dev, text, wait_s):
    payload = (text + "\n").encode()
    written, kind = xzs.bulk_write(dev, payload, timeout_ms=2000)
    delivered = kind is None and written == len(payload)
    print(f"COMMAND_ATTEMPTED={text if text else '<empty>'}")
    print(f"COMMAND_BYTES_WRITTEN={written}")
    print("COMMAND_DELIVERED=" + ("yes" if delivered else "no"))
    if kind == "hard" or xzs.usb_hard_errors >= 3 and not delivered:
        print("USB_DATA_PLANE=stopped")
        return b"", False
    data, ok = collect(dev, wait_s)
    print("COMMAND_RESPONSE_OBSERVED=" + ("yes" if data else "no"))
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()
    print()
    return data, ok and delivered


def main() -> int:
    dev = xzs.open_stable_device(timeout_sec=120)
    if dev is None:
        return 1
    if not xzs.transport_handshake(dev):
        print("D7_T1_DATA_PLANE=FAIL")
        print(f"USB_HARD_ERROR_COUNT={xzs.usb_hard_errors}")
        print(f"USB_TIMEOUT_COUNT={xzs.usb_timeouts}")
        return 1
    print("D7_T1_DATA_PLANE=PASS")

    transcript = b""
    # Drain whatever the shell already printed.
    banner, ok = collect(dev, 2)
    transcript += banner
    if not ok:
        return 1

    t0 = time.time()
    early = [
        ("help", 2),
        ("echo one", 2),
        ("echo two", 2),
        ("pwd", 2),
        ("cd /bin", 2),
        ("pwd", 2),
        ("cd /", 2),
        ("ls /", 3),
        ("ls /bin", 3),
        ("cat /etc/issue", 2),
        ("/bin/hello", 8),
        ("/bin/args one two three", 8),
        ("nosuchcommand", 4),
        ("cat /does-not-exist", 3),
        ("cd /does-not-exist", 3),
        ("", 2),
        ("x" * 180, 3),
    ]
    for text, wait_s in early:
        data, ok = send_line(dev, text, wait_s)
        transcript += data
        if not ok:
            print("SESSION_ABORTED=early")
            return 1

    marks = [
        (60, "pwd"),
        (120, "echo alive"),
        (300, "ls /"),
        (480, "pwd"),
        (600, "echo still-alive"),
    ]
    for mark, text in marks:
        delay = t0 + mark - time.time()
        if delay > 0:
            time.sleep(delay)
        print(f"LONGEVITY_MARK={mark}")
        data, ok = send_line(dev, text, 3)
        transcript += data
        if not ok:
            print("SESSION_ABORTED=longevity")
            return 1
    print("SHELL_10_MINUTE_HELD=yes")

    xzs.release_device(dev)
    print("HOST_DISCONNECTED=yes")
    time.sleep(5)
    dev = xzs.open_stable_device(timeout_sec=30)
    if dev is None:
        print("HOST_RECONNECT_WORKS=no")
        return 1
    data, ok = send_line(dev, "pwd", 3)
    transcript += data
    print("HOST_RECONNECT_WORKS=" + ("yes" if ok and (b"/" in data or b"xzs#" in data) else "no"))

    payload = b"reboot\n"
    written, kind = xzs.bulk_write(dev, payload, timeout_ms=2000)
    print("REBOOT_ATTEMPTED=yes")
    print(f"REBOOT_BYTES_WRITTEN={written}")
    print("REBOOT_DELIVERED=" + ("yes" if kind is None and written == len(payload) else "no"))
    print(f"USB_HARD_ERROR_COUNT={xzs.usb_hard_errors}")
    print(f"USB_TIMEOUT_COUNT={xzs.usb_timeouts}")
    text = transcript.decode("latin1", errors="replace")
    print("HOST_TRANSCRIPT_BEGIN")
    print(text)
    print("HOST_TRANSCRIPT_END")
    print("HELP_SEEN=" + ("yes" if "help echo pwd cd ls cat exit" in text else "no"))
    print("ECHO_ONE_SEEN=" + ("yes" if "\none\n" in text or text.endswith("one\n") else "no"))
    print("ECHO_TWO_SEEN=" + ("yes" if "two" in text else "no"))
    print("PWD_ROOT_SEEN=" + ("yes" if "\n/\n" in text else "no"))
    print("PWD_BIN_SEEN=" + ("yes" if "\n/bin\n" in text else "no"))
    print("LS_ROOT_SEEN=" + ("yes" if "sbin" in text and "etc" in text else "no"))
    print("CAT_SEEN=" + ("yes" if "XNU-XZS" in text else "no"))
    print("HELLO_SEEN=" + ("yes" if "hello from XNU-XZS userland" in text else "no"))
    print("ARGS_SEEN=" + ("yes" if "argc=4" in text and "argv[3]=three" in text else "no"))
    print("UNKNOWN_SEEN=" + ("yes" if "command not found" in text else "no"))
    print("ENOENT_SEEN=" + ("yes" if "open failed" in text else "no"))
    print("BAD_CD_SEEN=" + ("yes" if "cd: failed" in text else "no"))
    print("EMPTY_SURVIVED=" + ("yes" if text.count("xzs#") >= 2 else "no"))
    print("OVERLONG_SEEN=" + ("yes" if "line too long" in text else "no"))
    print("ALIVE_SEEN=" + ("yes" if "still-alive" in text else "no"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
