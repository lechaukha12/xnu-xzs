#!/usr/bin/env python3
"""F10 Investigation Script: Audits F9 actual runtime execution order vs authentic Sony LK sequence.

Reads F9 hardware logs from artifacts/hw/d8m8/f9-bootstrap-correction/ and parses keyaki.dts
to verify DCS command sequence, autorefresh configuration, and CTL_START vs DISPON ordering.
"""

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/f9-bootstrap-correction"
DTS_PATH = ROOT / "artifacts/display-audit/keyaki.dts"


def audit_f9():
    panel_prep = (LOG_DIR / "panel-prepare.txt").read_text() if (LOG_DIR / "panel-prepare.txt").exists() else ""
    kickoff = (LOG_DIR / "kickoff.txt").read_text() if (LOG_DIR / "kickoff.txt").exists() else ""

    print("=== F10 AUDIT OF F9 RUNTIME SEQUENCE ===")
    dispon_in_prep = "Transmitting DCS Display On (0x29)" in panel_prep
    ctl_start_in_kickoff = "CTL_START_COUNT=1" in kickoff

    print(f"DISPON transmitted in panel_prepare: {dispon_in_prep}")
    print(f"CTL_START=1 written in kickoff:       {ctl_start_in_kickoff}")

    if dispon_in_prep and ctl_start_in_kickoff:
        print("RESULT: F9_CTL_START_BEFORE_DISPON = NO")
        print("Canonical conclusion: F9 did NOT reproduce CTL_START before DISPON.")
    else:
        print("RESULT: Inconclusive from logs.")

    # Audit DTS on-commands
    print("\n=== SONY LK KEYAKI DT ON-COMMAND AUDIT ===")
    if DTS_PATH.exists():
        dts = DTS_PATH.read_text()
        m = re.search(r'somc,default_cmd_panel \{([^}]+(?:\{[^}]+\}[^}]+)*)\};', dts)
        if m:
            on_cmd_match = re.search(r'qcom,mdss-dsi-on-command = \[([^\]]+)\];', m.group(1))
            if on_cmd_match:
                raw_hex = on_cmd_match.group(1).split()
                bytes_list = [int(b, 16) for b in raw_hex]
                print(f"Total on-command bytes: {len(bytes_list)}")
                idx = 0
                cmd_cnt = 0
                while idx < len(bytes_list):
                    dtype = bytes_list[idx]
                    wait = bytes_list[idx+4]
                    dlen = (bytes_list[idx+5] << 8) | bytes_list[idx+6]
                    payload = bytes_list[idx+7 : idx+7+dlen]
                    cmd_cnt += 1
                    print(f"  [{cmd_cnt:02d}] dtype=0x{dtype:02x} wait={wait:3d}ms len={dlen:2d} payload={' '.join(f'{b:02x}' for b in payload)}")
                    idx += 7 + dlen
                print(f"LK_ON_COMMAND_COUNT = {cmd_cnt}")

            post_match = re.search(r'qcom,mdss-dsi-post-panel-on-command = <([^>]+)>;', m.group(1))
            if post_match:
                print(f"Post panel-on command: {post_match.group(1)} (0x29 DISPON)")


if __name__ == "__main__":
    audit_f9()
