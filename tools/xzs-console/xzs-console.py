#!/usr/bin/env python3
"""
XZS USB-C Console Host Tool (Phase D7-T1 / T1-Z)
Bidirectional live console and automated test transport over USB-C Bulk endpoints.
Target VID: 0x1209, PID: 0x000A
Bulk OUT: EP 0x01 (Host -> Xperia)
Bulk IN:  EP 0x81 (Xperia -> Host)
"""

import sys
import os
import logging
import platform
import time
import argparse
import select
import termios
import tty
import threading
import signal
from ctypes import c_int, c_uint16, c_char_p, Structure, byref, POINTER

try:
    import usb.core
    import usb.util
    HAVE_PYUSB = True
except ImportError:
    HAVE_PYUSB = False

TARGET_VID = 0x1209
TARGET_PID = 0x000A
EP_BULK_OUT = 0x01
EP_BULK_IN  = 0x81
DEFAULT_BULK_OUT_PAYLOAD = b"XZS-BULK-OUT-TEST\n"
DEFAULT_LOOPBACK_PAYLOAD = bytes.fromhex("585a532d5553422d4c4f4f500a")  # "XZS-USB-LOOP\n"


def find_xzs_device(vid=TARGET_VID, pid=TARGET_PID):
    if not HAVE_PYUSB:
        print("[XZS-CONSOLE] ERROR: pyusb library not found. Install via 'pip3 install pyusb'.", file=sys.stderr)
        return None
    return usb.core.find(idVendor=vid, idProduct=pid)


def test_enumeration(timeout_sec=5, vid=TARGET_VID, pid=TARGET_PID):
    """
    Mode 4.1: Enumeration Test
    Discover VID:PID, open device, read descriptors, print active config and endpoint map.
    """
    start = time.time()
    dev = None
    while time.time() - start < timeout_sec:
        dev = find_xzs_device(vid, pid)
        if dev is not None:
            break
        time.sleep(0.2)

    if dev is None:
        print("USB_DEVICE_FOUND=no")
        print(f"VID=0x{vid:04x}")
        print(f"PID=0x{pid:04x}")
        print("BUS=")
        print("ADDRESS=")
        print("")
        print("CONFIGURATION=")
        print("INTERFACE_CLASS=")
        print("")
        print(f"BULK_OUT_EP=0x{EP_BULK_OUT:02x}")
        print(f"BULK_IN_EP=0x{EP_BULK_IN:02x}")
        print("")
        print("ENUM_TEST=FAIL")
        return None

    bus = getattr(dev, "bus", "unknown")
    address = getattr(dev, "address", "unknown")

    # Read configuration & interface class
    cfg_val = "unknown"
    iface_class = "0xff"
    try:
        cfg = dev.get_active_configuration()
        cfg_val = str(cfg.bConfigurationValue)
        if cfg.bNumInterfaces > 0:
            iface = cfg[(0, 0)]
            iface_class = f"0x{iface.bInterfaceClass:02x}"
    except Exception:
        pass

    try:
        mfg = usb.util.get_string(dev, dev.iManufacturer) or "unknown"
        prod = usb.util.get_string(dev, dev.iProduct) or "unknown"
        serial = usb.util.get_string(dev, dev.iSerialNumber) or "unknown"
    except Exception:
        mfg = prod = serial = "<descriptor read failed>"

    print("USB_DEVICE_FOUND=yes")
    print(f"VID=0x{dev.idVendor:04x}")
    print(f"PID=0x{dev.idProduct:04x}")
    print(f"BUS={bus}")
    print(f"ADDRESS={address}")
    print("")
    print(f"CONFIGURATION={cfg_val}")
    print(f"INTERFACE_CLASS={iface_class}")
    print("")
    print(f"BULK_OUT_EP=0x{EP_BULK_OUT:02x}")
    print(f"BULK_IN_EP=0x{EP_BULK_IN:02x}")
    print("")
    print(f"DEVICE_MANUFACTURER={mfg}")
    print(f"DEVICE_PRODUCT={prod}")
    print(f"DEVICE_SERIAL={serial}")
    print("ENUM_TEST=PASS")
    return dev


def test_bulk_out(dev, payload=None, timeout_ms=2000):
    """
    Mode 5: Bulk OUT Test
    Open device, claim interface, send one bounded transfer, report byte count.
    """
    if payload is None:
        data = DEFAULT_BULK_OUT_PAYLOAD
    elif isinstance(payload, str):
        # Allow hex string if valid, otherwise ascii encode
        try:
            data = bytes.fromhex(payload.replace(" ", ""))
        except ValueError:
            data = payload.encode("utf-8")
    else:
        data = bytes(payload)

    print(f"BULK_OUT_BYTES_REQUESTED={len(data)}")

    if dev is None:
        print("BULK_OUT_BYTES_WRITTEN=0")
        print("BULK_OUT_STATUS=FAIL")
        return False

    try:
        written = dev.write(EP_BULK_OUT, data, timeout=timeout_ms)
        print(f"BULK_OUT_BYTES_WRITTEN={written}")
        if written == len(data):
            print("BULK_OUT_STATUS=PASS")
            return True
        else:
            print("BULK_OUT_STATUS=SHORT_WRITE")
            return False
    except usb.core.USBTimeoutError:
        print("BULK_OUT_BYTES_WRITTEN=0")
        print("BULK_OUT_STATUS=TIMEOUT")
        return False
    except usb.core.USBError as e:
        print("BULK_OUT_BYTES_WRITTEN=0")
        print(f"BULK_OUT_STATUS=FAIL ({e})")
        return False


def test_bulk_in(dev, timeout_ms=3000):
    """
    Mode 6: Bulk IN Test
    Bounded read with timeout.
    """
    if dev is None:
        print("BULK_IN_BYTES_RECEIVED=0")
        print("BULK_IN_DATA_HEX=")
        print("BULK_IN_DATA_ASCII=")
        print("BULK_IN_STATUS=FAIL")
        return None

    try:
        data = dev.read(EP_BULK_IN, 512, timeout=timeout_ms)
        raw_bytes = bytes(data)
        hex_str = raw_bytes.hex(" ")
        ascii_str = "".join(chr(b) if 32 <= b <= 126 or b in (10, 13) else f"\\x{b:02x}" for b in raw_bytes)
        print(f"BULK_IN_BYTES_RECEIVED={len(raw_bytes)}")
        print(f"BULK_IN_DATA_HEX={hex_str}")
        print(f"BULK_IN_DATA_ASCII={ascii_str.strip()}")
        print("BULK_IN_STATUS=PASS")
        return raw_bytes
    except usb.core.USBTimeoutError:
        print("BULK_IN_BYTES_RECEIVED=0")
        print("BULK_IN_DATA_HEX=")
        print("BULK_IN_DATA_ASCII=")
        print("BULK_IN_STATUS=TIMEOUT")
        return None
    except usb.core.USBError as e:
        print("BULK_IN_BYTES_RECEIVED=0")
        print("BULK_IN_DATA_HEX=")
        print("BULK_IN_DATA_ASCII=")
        print(f"BULK_IN_STATUS=FAIL ({e})")
        return None


def test_loopback(dev, timeout_ms=3000):
    """
    Mode 7: Loopback Mode
    Mac Bulk OUT -> XNU -> loopback -> Bulk IN -> Mac
    """
    payload = DEFAULT_LOOPBACK_PAYLOAD
    print(f"[XZS-CONSOLE] Starting Bulk Loopback Test (payload: {payload.hex(' ')})")

    if dev is None:
        print("BULK_LOOPBACK_STATUS=DEVICE_NOT_FOUND")
        print("BULK_LOOPBACK_HARDWARE=NOT_TESTED")
        return False

    out_ok = test_bulk_out(dev, payload=payload, timeout_ms=timeout_ms)
    if not out_ok:
        print("BULK_LOOPBACK_STATUS=OUT_FAILED")
        print("BULK_LOOPBACK_HARDWARE=NOT_TESTED")
        return False

    in_data = test_bulk_in(dev, timeout_ms=timeout_ms)
    if in_data is None:
        print("BULK_LOOPBACK_STATUS=IN_TIMEOUT")
        print("BULK_LOOPBACK_HARDWARE=NOT_TESTED")
        return False

    if in_data == payload:
        print("BULK_LOOPBACK_MATCH=yes")
        print("BULK_LOOPBACK_STATUS=PASS")
        print("BULK_LOOPBACK_HARDWARE=PASS")
        return True
    else:
        print(f"BULK_LOOPBACK_MATCH=no (rx={in_data.hex(' ')})")
        print("BULK_LOOPBACK_STATUS=MISMATCH")
        print("BULK_LOOPBACK_HARDWARE=NOT_TESTED")
        return False


HARD_BACKEND = {-1, -4, -9, -99}
MAX_CONSECUTIVE_HARD_USB_ERRORS = 3
usb_hard_errors = 0
usb_timeouts = 0


def is_hard_usb(exc):
    if isinstance(exc, usb.core.USBTimeoutError):
        return False
    if not isinstance(exc, usb.core.USBError):
        return False
    code = getattr(exc, "backend_error_code", None)
    if code in HARD_BACKEND:
        return True
    text = str(exc).lower()
    return ("other error" in text or "no device" in text or "pipe" in text)


def note_timeout():
    global usb_timeouts
    usb_timeouts += 1


def note_hard():
    global usb_hard_errors
    usb_hard_errors += 1


def release_device(dev):
    if dev is None:
        return
    try:
        usb.util.dispose_resources(dev)
    except Exception:
        pass


EXPECTED_CONFIGURATION = 1
EXPECTED_INTERFACE = 0
LIBUSB_ERROR_NAMES = {
    0: "LIBUSB_SUCCESS",
    -1: "LIBUSB_ERROR_IO",
    -2: "LIBUSB_ERROR_INVALID_PARAM",
    -3: "LIBUSB_ERROR_ACCESS",
    -4: "LIBUSB_ERROR_NO_DEVICE",
    -5: "LIBUSB_ERROR_NOT_FOUND",
    -6: "LIBUSB_ERROR_BUSY",
    -7: "LIBUSB_ERROR_TIMEOUT",
    -8: "LIBUSB_ERROR_OVERFLOW",
    -9: "LIBUSB_ERROR_PIPE",
    -10: "LIBUSB_ERROR_INTERRUPTED",
    -11: "LIBUSB_ERROR_NO_MEM",
    -12: "LIBUSB_ERROR_NOT_SUPPORTED",
    -99: "LIBUSB_ERROR_OTHER",
}


class _LibusbVersion(Structure):
    _fields_ = [
        ("major", c_uint16),
        ("minor", c_uint16),
        ("micro", c_uint16),
        ("nano", c_uint16),
        ("rc", c_char_p),
        ("describe", c_char_p),
    ]


def libusb_error_name(rc):
    return LIBUSB_ERROR_NAMES.get(int(rc), "LIBUSB_ERROR_" + str(int(rc)))


def host_versions():
    pyusb_version = getattr(usb, "__version__", "unknown") if HAVE_PYUSB else "missing"
    libusb_version = "unknown"
    if HAVE_PYUSB:
        try:
            backend = usb.backend.libusb1.get_backend()
            fn = backend.lib.libusb_get_version
            fn.restype = POINTER(_LibusbVersion)
            ver = fn().contents
            libusb_version = "%d.%d.%d" % (ver.major, ver.minor, ver.micro)
        except Exception as exc:
            libusb_version = "unavailable:" + type(exc).__name__
    return platform.platform(), libusb_version, pyusb_version


def raw_get_configuration(dev):
    """Return (libusb_rc, configuration_or_None). Does not set a configuration."""
    dev._ctx.managed_open()
    handle = dev._ctx.handle.handle
    cfg = c_int(-1)
    rc = int(dev._ctx.backend.lib.libusb_get_configuration(handle, byref(cfg)))
    if rc != 0:
        return rc, None
    return 0, int(cfg.value)


def raw_set_configuration(dev, value):
    """One libusb_set_configuration call. Returns the raw status code."""
    dev._ctx.managed_open()
    handle = dev._ctx.handle.handle
    return int(dev._ctx.backend.lib.libusb_set_configuration(handle, int(value)))


def raw_claim_interface(dev, interface):
    """Claim without set_configuration. Records the raw libusb return code."""
    dev._ctx.managed_open()
    handle = dev._ctx.handle.handle
    rc = int(dev._ctx.backend.lib.libusb_claim_interface(handle, int(interface)))
    if rc == 0:
        dev._ctx._claimed_intf.add(int(interface))
        for cfg in dev:
            if cfg.bConfigurationValue == EXPECTED_CONFIGURATION:
                dev._ctx._active_cfg_index = cfg.index
                break
    return rc


def print_not_attempted(reason):
    print("BULK_OUT=NOT_ATTEMPTED")
    print("BULK_IN=NOT_ATTEMPTED")
    print("LOOPBACK=NOT_ATTEMPTED")
    print("BULK_NOT_ATTEMPTED_REASON=" + reason)


def open_stable_device(timeout_sec=120, vid=TARGET_VID, pid=TARGET_PID):
    """
    Darwin claim path for the vendor-specific bulk interface.

    Never calls is_kernel_driver_active or detach_kernel_driver.
    Never calls set_configuration when configuration 1 is already active.
    No bulk transfer is issued unless claim_interface returns 0.
    """
    if os.environ.get("LIBUSB_DEBUG"):
        logging.basicConfig(level=logging.DEBUG, stream=sys.stderr)
        logging.getLogger("usb").setLevel(logging.DEBUG)
        logging.getLogger("usb.backend.libusb1").setLevel(logging.DEBUG)
    host_os, libusb_version, pyusb_version = host_versions()
    print("HOST_OS=" + host_os)
    print("LIBUSB_VERSION=" + libusb_version)
    print("PYUSB_VERSION=" + pyusb_version)
    print("MACOS_KERNEL_DRIVER_QUERY_REQUIRED=no")
    print("KERNEL_DRIVER_QUERY_RESULT=not_called")
    print("KERNEL_DRIVER_QUERY_IGNORED_ON_DARWIN=yes")
    print("EXPECTED_CONFIGURATION=%d" % EXPECTED_CONFIGURATION)
    print("EP_OUT=0x01")
    print("EP_OUT_TYPE=bulk")
    print("EP_IN=0x81")
    print("EP_IN_TYPE=bulk")

    deadline = time.time() + timeout_sec
    dev = None
    last_seen = None
    hard_streak = 0
    while time.time() < deadline and hard_streak < MAX_CONSECUTIVE_HARD_USB_ERRORS:
        dev = find_xzs_device(vid, pid)
        if dev is None:
            if last_seen is not None:
                print("DEVICE_DISAPPEARED_AT=%.3f" % time.time())
                print("DEVICE_DISAPPEARED_AFTER_ADDRESS=%s" % last_seen)
                print_not_attempted("device_disappeared_before_claim")
                return None
            time.sleep(0.25)
            continue
        last_seen = "%s:%s" % (getattr(dev, "bus", "?"), getattr(dev, "address", "?"))
        time.sleep(1.0)
        dev = find_xzs_device(vid, pid)
        if dev is None:
            print("DEVICE_DISAPPEARED_AT=%.3f" % time.time())
            print("DEVICE_DISAPPEARED_AFTER_ADDRESS=%s" % last_seen)
            print_not_attempted("device_disappeared_before_claim")
            return None
        try:
            rc, active = raw_get_configuration(dev)
        except usb.core.USBError as exc:
            rc = getattr(exc, "backend_error_code", None)
            active = None
            print("GET_CONFIGURATION_EXCEPTION=%s" % exc)
        print("GET_CONFIGURATION_RC=%s" % rc)
        print("GET_CONFIGURATION_ERROR_NAME=%s" % (libusb_error_name(rc) if rc is not None else "none"))
        print("ACTIVE_CONFIGURATION=%s" % ("unset" if active is None else active))
        if rc != 0:
            hard_streak += 1
            print("USB_HARD_STREAK=%d" % hard_streak)
            release_device(dev)
            if hard_streak >= MAX_CONSECUTIVE_HARD_USB_ERRORS:
                break
            time.sleep(0.5)
            continue
        hard_streak = 0
        if active == EXPECTED_CONFIGURATION:
            print("SET_CONFIGURATION_CALLED=no")
            print("SET_CONFIGURATION_RC=not_called")
            print("SET_CONFIGURATION_REASON=already_active")
        else:
            set_rc = raw_set_configuration(dev, EXPECTED_CONFIGURATION)
            print("SET_CONFIGURATION_CALLED=yes")
            print("SET_CONFIGURATION_RC=%d" % set_rc)
            print("SET_CONFIGURATION_ERROR_NAME=%s" % libusb_error_name(set_rc))
            print("SET_CONFIGURATION_REASON=active_was_%s" % active)
            if set_rc != 0:
                hard_streak += 1
            rc2, active2 = raw_get_configuration(dev)
            print("ACTIVE_CONFIGURATION_AFTER_SET=%s" % ("unset" if active2 is None else active2))
            print("GET_CONFIGURATION_AFTER_SET_RC=%s" % rc2)
            if rc2 == 0:
                active = active2

        print("USB_DEVICE_FOUND=yes")
        print("VID_PID=%04x:%04x" % (dev.idVendor, dev.idProduct))
        print("BUS=%s" % getattr(dev, "bus", ""))
        print("ADDRESS=%s" % getattr(dev, "address", ""))
        print("DEVICE_CLASS=0x%02x" % dev.bDeviceClass)
        try:
            intf = dev[0][(0, 0)]
            print("INTERFACE_NUMBER=%d" % intf.bInterfaceNumber)
            print("INTERFACE_CLASS=0x%02x" % intf.bInterfaceClass)
            print("INTERFACE_SUBCLASS=0x%02x" % intf.bInterfaceSubClass)
            print("INTERFACE_PROTOCOL=0x%02x" % intf.bInterfaceProtocol)
        except Exception as exc:
            print("INTERFACE_DESCRIPTOR_READ=%s" % exc)

        print("CLAIM_INTERFACE_ATTEMPTED=yes")
        claim_rc = raw_claim_interface(dev, EXPECTED_INTERFACE)
        print("CLAIM_INTERFACE_RC=%d" % claim_rc)
        print("CLAIM_INTERFACE_ERROR_NAME=%s" % libusb_error_name(claim_rc))
        print("CLAIM_INTERFACE_ERROR_TEXT=%s" % (
            "success" if claim_rc == 0 else libusb_error_name(claim_rc)))
        print("CLAIM_INTERFACE=%s" % ("PASS" if claim_rc == 0 else "FAIL"))
        if claim_rc != 0:
            print_not_attempted("claim_interface_failed")
            release_device(dev)
            return None
        print(f"USB_HARD_ERROR_COUNT={usb_hard_errors}")
        print(f"USB_TIMEOUT_COUNT={usb_timeouts}")
        return dev

    print("USB_DEVICE_FOUND=%s" % ("yes" if last_seen else "no"))
    print("CLAIM_INTERFACE_ATTEMPTED=no")
    print("CLAIM_INTERFACE=NOT_ATTEMPTED")
    print("USB_HARD_STREAK=%d" % hard_streak)
    print_not_attempted("stopped_before_claim")
    return None


def bulk_write(dev, payload, timeout_ms=2000):
    try:
        written = dev.write(EP_BULK_OUT, payload, timeout=timeout_ms)
        return int(written), None
    except usb.core.USBTimeoutError:
        note_timeout()
        return 0, "timeout"
    except usb.core.USBError as exc:
        if is_hard_usb(exc):
            note_hard()
            return 0, "hard"
        note_timeout()
        return 0, "timeout"


def bulk_read(dev, timeout_ms=2000):
    try:
        data = dev.read(EP_BULK_IN, 512, timeout=timeout_ms)
        return bytes(data), None
    except usb.core.USBTimeoutError:
        note_timeout()
        return b"", "timeout"
    except usb.core.USBError as exc:
        if is_hard_usb(exc):
            note_hard()
            return b"", "hard"
        note_timeout()
        return b"", "timeout"


def transport_handshake(dev):
    """Sealed Z2/Z3/Z4 bring-up. Stops after three consecutive hard errors."""
    streak = 0

    def broken(kind):
        nonlocal streak
        if kind == "hard":
            streak += 1
        else:
            streak = 0
        return streak >= MAX_CONSECUTIVE_HARD_USB_ERRORS

    written, kind = bulk_write(dev, DEFAULT_BULK_OUT_PAYLOAD, timeout_ms=2000)
    print(f"BULK_OUT_BYTES_WRITTEN={written}")
    if kind is not None or written != len(DEFAULT_BULK_OUT_PAYLOAD):
        print("BULK_OUT=FAIL")
        print(f"USB_HARD_ERROR_COUNT={usb_hard_errors}")
        print(f"USB_TIMEOUT_COUNT={usb_timeouts}")
        return False
    print("BULK_OUT=PASS")
    streak = 0

    data = b""
    for _ in range(4):
        chunk, kind = bulk_read(dev, timeout_ms=1000)
        if kind == "hard" and broken(kind):
            print("BULK_IN=FAIL")
            return False
        if chunk:
            data += chunk
            if data == b"XZS-BULK-IN-TEST\n":
                break
        if kind == "timeout":
            continue
    if data != b"XZS-BULK-IN-TEST\n":
        print("BULK_IN=FAIL")
        print(f"USB_HARD_ERROR_COUNT={usb_hard_errors}")
        print(f"USB_TIMEOUT_COUNT={usb_timeouts}")
        return False
    print("BULK_IN=PASS")

    written, kind = bulk_write(dev, DEFAULT_LOOPBACK_PAYLOAD, timeout_ms=2000)
    if kind is not None or written != len(DEFAULT_LOOPBACK_PAYLOAD):
        print("LOOPBACK=FAIL")
        return False
    echo = b""
    for _ in range(4):
        chunk, kind = bulk_read(dev, timeout_ms=1000)
        if kind == "hard" and broken(kind):
            print("LOOPBACK=FAIL")
            return False
        if chunk:
            echo += chunk
            if echo == DEFAULT_LOOPBACK_PAYLOAD:
                break
    if echo != DEFAULT_LOOPBACK_PAYLOAD:
        print("LOOPBACK=FAIL")
        print(f"USB_HARD_ERROR_COUNT={usb_hard_errors}")
        print(f"USB_TIMEOUT_COUNT={usb_timeouts}")
        return False
    print("LOOPBACK=PASS")
    print(f"USB_HARD_ERROR_COUNT={usb_hard_errors}")
    print(f"USB_TIMEOUT_COUNT={usb_timeouts}")
    return True


def interactive_console(dev):
    """
    Mode 8: Interactive Console Mode
    Transparent bidirectional byte stream between stdin/stdout and USB Bulk endpoints.
    """
    print("[XZS-CONSOLE] Entering live interactive console (Press Ctrl-] or Ctrl-C to exit)...")
    stop_event = threading.Event()
    hard_streak = {"n": 0}

    def reader_thread():
        while not stop_event.is_set():
            try:
                data = dev.read(EP_BULK_IN, 512, timeout=100)
                hard_streak["n"] = 0
                if data:
                    sys.stdout.buffer.write(bytes(data))
                    sys.stdout.buffer.flush()
            except usb.core.USBTimeoutError:
                note_timeout()
                continue
            except usb.core.USBError as exc:
                if is_hard_usb(exc):
                    note_hard()
                    hard_streak["n"] += 1
                    if hard_streak["n"] >= MAX_CONSECUTIVE_HARD_USB_ERRORS:
                        print("\n[XZS-CONSOLE] stopping after 3 hard USB errors", file=sys.stderr)
                        stop_event.set()
                        break
                else:
                    note_timeout()
                if not stop_event.is_set():
                    time.sleep(0.05)
            except Exception:
                break

    t = threading.Thread(target=reader_thread, daemon=True)
    t.start()

    old_settings = None
    if sys.stdin.isatty():
        try:
            old_settings = termios.tcgetattr(sys.stdin)
            tty.setraw(sys.stdin.fileno())
        except Exception:
            old_settings = None

    def cleanup(*args):
        stop_event.set()
        if old_settings is not None:
            try:
                termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)
            except Exception:
                pass

    signal.signal(signal.SIGINT, cleanup)
    signal.signal(signal.SIGTERM, cleanup)

    try:
        while not stop_event.is_set():
            r, _, _ = select.select([sys.stdin], [], [], 0.05)
            if r:
                ch = sys.stdin.buffer.read(1)
                if not ch:
                    break
                if ch == b'\x1d':  # Ctrl-]
                    break
                written, kind = bulk_write(dev, ch, timeout_ms=1000)
                if kind == "hard" or written != len(ch):
                    hard_streak["n"] += 1
                    if hard_streak["n"] >= MAX_CONSECUTIVE_HARD_USB_ERRORS:
                        print("\n[XZS-CONSOLE] stopping after 3 hard USB errors", file=sys.stderr)
                        break
                else:
                    hard_streak["n"] = 0
    except Exception:
        pass
    finally:
        cleanup()
        t.join(timeout=0.5)
        print("\n[XZS-CONSOLE] Live session terminated cleanly.")


def main():
    parser = argparse.ArgumentParser(description="XZS USB-C Console Host Tool (Phase D7-T1 / T1-Z)")
    parser.add_argument("--test-enum", action="store_true", help="Verify device enumeration and print descriptor map")
    parser.add_argument("--test-bulk-out", nargs="?", const="DEFAULT", metavar="DATA", help="Send payload to Bulk OUT (default: XZS-BULK-OUT-TEST\\n)")
    parser.add_argument("--test-bulk-in", action="store_true", help="Read payload from Bulk IN (bounded timeout)")
    parser.add_argument("--test-loopback", action="store_true", help="Perform Bulk OUT -> Bulk IN loopback test")
    parser.add_argument("--interactive", action="store_true", help="Launch live interactive console")
    parser.add_argument("--no-handshake", action="store_true", help="Skip the sealed bulk bring-up and talk to an already open shell")
    parser.add_argument("--timeout", type=int, default=120, help="Device search timeout in seconds (default: 120)")
    parser.add_argument("--vid", type=lambda x: int(x, 0), default=TARGET_VID, help="Target USB VID (default: 0x1209)")
    parser.add_argument("--pid", type=lambda x: int(x, 0), default=TARGET_PID, help="Target USB PID (default: 0x000A)")

    args = parser.parse_args()

    # If --test-enum requested, execute enumeration test directly
    if args.test_enum:
        dev = test_enumeration(timeout_sec=args.timeout, vid=args.vid, pid=args.pid)
        sys.exit(0 if dev is not None else 1)

    dev = open_stable_device(timeout_sec=args.timeout, vid=args.vid, pid=args.pid)

    if args.test_bulk_out is not None:
        payload = None if args.test_bulk_out == "DEFAULT" else args.test_bulk_out
        ok = test_bulk_out(dev, payload=payload)
        sys.exit(0 if ok else 1)

    if args.test_bulk_in:
        res = test_bulk_in(dev)
        sys.exit(0 if res is not None else 1)

    if args.test_loopback:
        ok = test_loopback(dev)
        sys.exit(0 if ok else 1)

    if args.interactive or (not args.test_enum and args.test_bulk_out is None and not args.test_bulk_in and not args.test_loopback):
        if dev is None:
            print(f"[XZS-CONSOLE] ERROR: USB device {args.vid:04x}:{args.pid:04x} not usable within {args.timeout}s.", file=sys.stderr)
            sys.exit(1)
        if not args.no_handshake:
            if not transport_handshake(dev):
                print("[XZS-CONSOLE] ERROR: bulk transport did not come up. Not sending shell traffic.", file=sys.stderr)
                print(f"USB_HARD_ERROR_COUNT={usb_hard_errors}")
                print(f"USB_TIMEOUT_COUNT={usb_timeouts}")
                sys.exit(1)
        interactive_console(dev)


if __name__ == "__main__":
    main()
