#!/usr/bin/env python3
"""One-boot EP0 configuration check.

Opens 1209:000A, issues SET_CONFIGURATION(1) once when the active
configuration is not already 1, then claims interface 0. Bulk loopback
runs only after that claim succeeds. No shell commands.
"""

import importlib.util
import sys
from pathlib import Path

_TOOL = Path(__file__).resolve().parents[1] / "tools" / "xzs-console" / "xzs-console.py"
_spec = importlib.util.spec_from_file_location("xzs_console", _TOOL)
xzs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(xzs)


def main() -> int:
    dev = xzs.open_stable_device(timeout_sec=90)
    if dev is None:
        print("ENUMERATION=FAIL")
        print("CLAIM_INTERFACE=FAIL")
        print("BULK_OUT=NOT_ATTEMPTED")
        print("BULK_IN=NOT_ATTEMPTED")
        print("LOOPBACK=NOT_ATTEMPTED")
        return 1
    print("ENUMERATION=PASS")
    if not xzs.transport_handshake(dev):
        print("BULK_DATA_PLANE=FAIL")
        xzs.release_device(dev)
        return 2
    print("BULK_DATA_PLANE=PASS")
    xzs.release_device(dev)
    return 0


if __name__ == "__main__":
    sys.exit(main())
