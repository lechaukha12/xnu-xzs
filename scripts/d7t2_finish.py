#!/usr/bin/env python3
"""Same-boot D7-T2 finish run.

Confirms the proven builtins, then ls, then /bin/hello alone.
/bin/args is sent only after hello prints its line and the prompt returns.
Stops on three consecutive hard USB errors. Does not reboot the target.
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
    hard = 0
    while time.time() < end:
        chunk, kind = xzs.bulk_read(dev, timeout_ms=200)
        if kind == "hard":
            hard += 1
            if hard >= 3:
                print("USB_DATA_PLANE=stopped")
                print(f"USB_HARD_ERROR_COUNT={xzs.usb_hard_errors}")
                print(f"USB_TIMEOUT_COUNT={xzs.usb_timeouts}")
                return buf, False
            time.sleep(0.05)
            continue
        hard = 0
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
    print(f"COMMAND_ATTEMPTED={text}")
    print(f"COMMAND_BYTES_WRITTEN={written}")
    print("COMMAND_DELIVERED=" + ("yes" if delivered else "no"))
    if not delivered:
        print("USB_DATA_PLANE=stopped")
        return b"", False
    data, ok = collect(dev, wait_s)
    print("COMMAND_RESPONSE_OBSERVED=" + ("yes" if data else "no"))
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()
    print()
    return data, ok


def main() -> int:
    dev = xzs.open_stable_device(timeout_sec=120)
    if dev is None:
        return 1
    if not xzs.transport_handshake(dev):
        print("D7_T1_DATA_PLANE=FAIL")
        return 1
    print("D7_T1_DATA_PLANE=PASS")
    banner, ok = collect(dev, 2)
    if not ok:
        return 1
    sys.stdout.buffer.write(banner)
    transcript = banner

    commands = [
        ("help", 2),
        ("echo hello", 2),
        ("pwd", 2),
        ("ls /", 3),
        ("cd /bin", 2),
        ("pwd", 2),
        ("ls", 3),
        ("cd /", 2),
        ("cat /etc/issue", 2),
    ]
    for text, wait_s in commands:
        data, ok = send_line(dev, text, wait_s)
        transcript += data
        if not ok:
            print("SESSION_ABORTED=builtins")
            return 1

    data, ok = send_line(dev, "/bin/hello", 8)
    transcript += data
    hello_ok = ok and b"hello from XNU-XZS userland" in data and b"xzs#" in data
    print("HELLO=" + ("PASS" if hello_ok else "FAIL"))
    if not hello_ok:
        print("ARGS=NOT_ATTEMPTED")
        print("STOP_AFTER_HELLO=yes")
        return 2

    data, ok = send_line(dev, "/bin/args one two three", 8)
    transcript += data
    args_ok = ok and b"argc=4" in data and b"argv[3]=three" in data and b"xzs#" in data
    print("ARGS=" + ("PASS" if args_ok else "FAIL"))
    if not args_ok:
        return 3

    data, ok = send_line(dev, "nosuchcommand", 4)
    transcript += data
    if not ok:
        return 1

    t0 = time.time()
    for mark, text in ((60, "pwd"), (120, "echo alive"), (300, "ls /"), (480, "pwd"), (600, "echo still-alive")):
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
    text = transcript.decode("latin1", errors="replace")
    print("LS_ROOT_SEEN=" + ("yes" if "bin" in text and "etc" in text else "no"))
    print("LS_BIN_SEEN=" + ("yes" if "hello" in text and "args" in text else "no"))
    print("STILL_ALIVE_SEEN=" + ("yes" if "still-alive" in text else "no"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
