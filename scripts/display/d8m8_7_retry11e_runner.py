#!/usr/bin/env python3
"""R11E one-boot passive MDP clock / VBIF / RGB0 observation.

One CTL_START. Reads only. No second boot and no corrective MMIO.
"""

import importlib.util
import re
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "artifacts/hw/d8m8/m8-7-retry11e"
SERIAL = "BH905SX976"
BOOT = ROOT / "artifacts/builds/xzs-xnu-boot.img"

# ftbl_mdp_clk_src in mmcc-msm8996.c. pre_div is (2 * divider - 1).
# cfg is the parent_map value, not the P_* enum.
MDP_TABLE = (
    (5, 13, 85714286, "GPLL0/7"),
    (5, 11, 100000000, "GPLL0/6"),
    (5, 7, 150000000, "GPLL0/4"),
    (5, 6, 171428571, "GPLL0/3.5"),
    (5, 5, 200000000, "GPLL0/3"),
    (2, 5, 275000000, "MMPLL5/3"),
    (5, 3, 300000000, "GPLL0/2"),
    (2, 4, 330000000, "MMPLL5/2.5"),
    (2, 3, 412500000, "MMPLL5/2"),
)
PARENT = {
    0: "BI_TCXO",
    1: "MMPLL0",
    2: "MMPLL5",
    5: "GPLL0",
    6: "GPLL0_DIV",
}
PHASE = {
    "0x00001100": "E1100",
    "0x00001110": "E1110",
    "0x00001120": "E1120",
    "0x00001130": "E1130",
    "0x00001140": "E1140",
    "0x00001150": "E1150",
    "0x00001160": "E1160",
    "0x00001170": "E1170",
    "0x00001180": "E1180",
    "0x00001190": "E1190",
    "0x000011a0": "E11A0",
    "0x000011b0": "E11B0",
    "0x000011c0": "E11C0",
    "0x000011d0": "E11D0",
}

READ_MAP = """\
symbol                          phys        offset  source                                      read semantics                                      side effect          safe
MDSS_GDSC                       0x008c2304  0x2304  xzs_diag.c XZS_MMCC_MDSS_GDSC; gdsc.c PWR_ON bit31  power-on status                            none                 YES
mdss_mdp_clk CBCR               0x008c231c  0x231c  mmcc-msm8996.c mdss_mdp_clk halt/enable     bit0 enable, bit31 BRANCH_CLK_OFF              none                 YES
mdp_clk_src CMD_RCGR            0x008c2040  0x2040  mmcc-msm8996.c cmd_rcgr 0x2040; clk-rcg2.c  bit0 UPDATE, bit1 ROOT_EN, bit31 ROOT_OFF       none                 YES
mdp_clk_src CFG_RCGR            0x008c2044  0x2044  clk-rcg2.c CFG at cmd+4                     src-sel [10:8], hid [4:0]                         none                 YES
mdss_ahb_clk CBCR               0x008c2308  0x2308  mmcc-msm8996.c mdss_ahb_clk                 bit0 enable, bit1 HWCG, bit31 BRANCH_CLK_OFF      none                 YES
mdss_axi_clk CBCR               0x008c2310  0x2310  mmcc-msm8996.c mdss_axi_clk                 bit0 enable, bit31 BRANCH_CLK_OFF                 none                 YES
mdss_vsync_clk CBCR             0x008c2328  0x2328  mmcc-msm8996.c mdss_vsync_clk               bit0 enable, bit31 BRANCH_CLK_OFF                 none                 YES
vsync_clk_src CMD_RCGR          0x008c2080  0x2080  mmcc-msm8996.c vsync cmd_rcgr 0x2080        same CMD bits as mdp_clk_src                      none                 YES
vsync_clk_src CFG_RCGR          0x008c2084  0x2084  mmcc-msm8996.c; only F(19200000, XO, 1)     src-sel [10:8], hid [4:0]                         none                 YES
MDSS_REG_HW_VERSION             0x00900000  0x0     mdss_mdp_hwio.h; mdss_hw_rev_init          MDP rev. 0x10070000 is HW_REV_107                 none                 YES
MDSS_MDP_REG_INTR_STATUS        0x00901014  0x14    mdss_mdp_hwio.h; clear is +0x18 W1C        status. bit26 INTF1 underrun, bit8 PP0_DONE       none (not the W1C)   YES
CTL_FLUSH                       0x00902018  0x18    existing XNU read                           shadow flush                                          none                 YES
RGB0 CURRENT_SRC0_ADDR          0x009150a4  0xa4    existing XNU read                           latched source pointer, not an AXI beat           none                 YES
PP_INT_COUNT_VAL                0x00971014  0x14    existing XNU read                           internal counter                                      none                 YES
PP_OUT_LINE_COUNT               0x00971028  0x28    existing XNU read                           no Sony reader found                              none                 YES
PP_LINE_COUNT                   0x0097102c  0x2c    existing XNU read                           first line out / transfer ongoing                 none                 YES
PP_AUTOREFRESH_CONFIG           0x00971030  0x30    existing XNU read                           bit31 enable                                          none                 YES
DSI_STATUS                      0x00994008  0x08    existing R11A read                          bit2 treated as MDP busy by prior XNU trace       none                 YES
DSI_INT_CTRL                    0x00994110  0x110   mdss_dsi.h DSI_INTR_CMD_MDP_DONE BIT(8)     raw done bit                                         none                 YES
VBIF_QOS_REMAP n=0..3           0x009b0020  0x20+4n mdss_mdp_hwio.h; pipe.c legacy RMW         2 bits per xin. XIN1 is bits [3:2]                none                 YES
VBIF_RD_LIM_CONF                0x009b00b0  0xb0    mdss_mdp.c set_ot_limit                     xin1 is bits [15:8]                                   none                 YES
VBIF_XIN_HALT_CTRL0             0x009b0200  0x200   mdss_mdp.c set_ot_limit read-modify        bit xin_id is the halt request. XIN1 is bit 1      none on read         YES
VBIF_XIN_HALT_CTRL1             0x009b0204  0x204   wait_for_xin_halt polls bit xin_id         bit1 halt ack; bit17 used as client-idle           none                 YES
VBIF_AXI_HALT_CTRL1             0x009b020c  0x20c   mdss_mdp_hwio.h; driver polls the ack      AXI halt acknowledge                                  none                 YES

Not instrumented, and why:
VBIF SRC_ERR 0x194 / ERR_INFO 0x1a0: defined, never read by the pinned driver.
VBIF QoS RP/LVL 0x550/0x570: only the MDSS_QOS_REMAPPER path, which rev 107 does not set.
Bus bandwidth vote: msm_bus / RPM software state, no silicon register identified.
RGB0 fetch beat: no source-backed readable transaction counter. CURRENT_SRC0_ADDR is not one.
"""

spec = importlib.util.spec_from_file_location(
    "r11a_helpers", ROOT / "scripts/display/d8m8_7_retry11a_runner.py"
)
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)


def rows_from(prefix, text):
    lines = [line.strip() for line in text.splitlines() if line.startswith(prefix)]
    parsed = []
    for line in lines:
        parsed.append(dict(re.findall(r"([A-Z0-9_]+)=([^ ]+)", line)))
    return lines, parsed


def word(row, key):
    raw = row.get(key)
    if raw is None:
        return None
    try:
        return int(raw, 16) if raw.lower().startswith("0x") else int(raw, 16)
    except ValueError:
        return None


def branch_on(value):
    if value is None:
        return "UNKNOWN"
    enabled = (value & 1) == 1 and ((value >> 31) & 1) == 0
    return "YES" if enabled else "NO"


def mdp_source(cmd, cfg):
    if cmd is None or cfg is None:
        return "UNKNOWN", "UNKNOWN", "UNKNOWN"
    src = (cfg >> 8) & 7
    hid = cfg & 0x1f
    root_off = (cmd >> 31) & 1
    parent = PARENT.get(src, "UNMAPPED")
    match = None
    for table_src, table_hid, rate, label in MDP_TABLE:
        if table_src == src and table_hid == hid:
            match = (rate, label)
            break
    if root_off:
        return parent, "ROOT_OFF", "UNKNOWN"
    if match:
        return parent, "TABLE_MATCH " + match[1], str(match[0])
    if src == 0:
        if hid == 0:
            rate = 19200000
        else:
            rate = (19200000 * 2) // (hid + 1)
        return parent, "XO_NOT_IN_MDP_TABLE", str(rate)
    return parent, "NOT_IN_MDP_TABLE", "UNKNOWN"


def vsync_source(cmd, cfg):
    if cmd is None or cfg is None:
        return "UNKNOWN", "UNKNOWN"
    src = (cfg >> 8) & 7
    hid = cfg & 0x1f
    root_off = (cmd >> 31) & 1
    if root_off:
        return "ROOT_OFF", "UNKNOWN"
    if src == 0 and hid == 1:
        return "BI_TCXO/1 TABLE_MATCH", "19200000"
    return PARENT.get(src, "UNMAPPED") + " hid=%d" % hid, "UNKNOWN"


def xin1_qos(value):
    if value is None:
        return "UNKNOWN"
    return str((value >> 2) & 3)


def decode(kickoff):
    sample_lines, sample_rows = rows_from("R11E_SAMPLE ", kickoff)
    (LOG_DIR / "raw-r11e-samples.txt").write_text(
        "\n".join(sample_lines) + ("\n" if sample_lines else ""))

    clock_header = [
        "PHASE", "NAME", "MDP_BRANCH", "MDP_SRC", "MDP_CFG_CLASS",
        "MDP_TABLE_OR_XO_HZ", "AHB_BRANCH", "AXI_BRANCH", "VSYNC_BRANCH",
        "VSYNC_CFG", "VSYNC_HZ", "MDP_CBCR", "MDP_CMD", "MDP_CFG",
        "AHB_CBCR", "AXI_CBCR", "VSYNC_CBCR", "VSYNC_CMD", "VSYNC_CFG_RAW",
    ]
    clock_lines = ["\t".join(clock_header)]
    vbif_header = [
        "PHASE", "NAME", "HW_VERSION", "XIN1_HALT_REQ", "XIN1_HALT_ACK",
        "XIN1_IDLE_BIT17", "QOS0", "QOS1", "QOS2", "QOS3", "RD_LIM_XIN1",
        "XIN_HALT0", "XIN_HALT1", "AXI_HALT1", "RD_LIM",
    ]
    vbif_lines = ["\t".join(vbif_header)]
    fetch_header = [
        "PHASE", "NAME", "TIMESTAMP_US", "PP_COUNT", "PP_LINE", "PP_OUT",
        "AUTOREFRESH", "CTL_FLUSH", "RGB0_CUR", "MDP_INTR",
        "INTF1_UNDERRUN", "PP_DONE8", "DSI_BUSY", "CMD_MDP_DONE",
    ]
    fetch_lines = ["\t".join(fetch_header)]

    for row in sample_rows:
        phase = row.get("PHASE", "").lower()
        name = PHASE.get(phase, phase)
        cmd = word(row, "MDP_CMD")
        cfg = word(row, "MDP_CFG")
        parent, cfg_class, mdp_hz = mdp_source(cmd, cfg)
        vcfg, vhz = vsync_source(word(row, "VSYNC_CMD"), word(row, "VSYNC_CFG"))
        clock_lines.append("\t".join([
            phase, name, branch_on(word(row, "MDP_CBCR")), parent, cfg_class,
            mdp_hz, branch_on(word(row, "AHB_CBCR")),
            branch_on(word(row, "AXI_CBCR")),
            branch_on(word(row, "VSYNC_CBCR")), vcfg, vhz,
            row.get("MDP_CBCR", ""), row.get("MDP_CMD", ""),
            row.get("MDP_CFG", ""), row.get("AHB_CBCR", ""),
            row.get("AXI_CBCR", ""), row.get("VSYNC_CBCR", ""),
            row.get("VSYNC_CMD", ""), row.get("VSYNC_CFG", ""),
        ]))
        halt0 = word(row, "XIN_HALT0")
        halt1 = word(row, "XIN_HALT1")
        rd = word(row, "RD_LIM")
        vbif_lines.append("\t".join([
            phase, name, row.get("HW_VERSION", ""),
            "UNKNOWN" if halt0 is None else str((halt0 >> 1) & 1),
            "UNKNOWN" if halt1 is None else str((halt1 >> 1) & 1),
            "UNKNOWN" if halt1 is None else str((halt1 >> 17) & 1),
            xin1_qos(word(row, "QOS0")), xin1_qos(word(row, "QOS1")),
            xin1_qos(word(row, "QOS2")), xin1_qos(word(row, "QOS3")),
            "UNKNOWN" if rd is None else str((rd >> 8) & 0xff),
            row.get("XIN_HALT0", ""), row.get("XIN_HALT1", ""),
            row.get("AXI_HALT1", ""), row.get("RD_LIM", ""),
        ]))
        fetch_lines.append("\t".join([
            phase, name, row.get("TIMESTAMP_US", ""),
            row.get("PP_COUNT", ""), row.get("PP_LINE", ""),
            row.get("PP_OUT", ""), row.get("PP_AUTOREFRESH", ""),
            row.get("CTL_FLUSH", ""), row.get("RGB0_CUR", ""),
            row.get("MDP_INTR", ""), row.get("INTF1_UNDERRUN", ""),
            row.get("PP_DONE8", ""), row.get("DSI_BUSY", ""),
            row.get("CMD_MDP_DONE", ""),
        ]))

    (LOG_DIR / "decoded-clock.tsv").write_text("\n".join(clock_lines) + "\n")
    (LOG_DIR / "decoded-vbif.tsv").write_text("\n".join(vbif_lines) + "\n")
    (LOG_DIR / "decoded-fetch.tsv").write_text("\n".join(fetch_lines) + "\n")
    return sample_rows


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    (LOG_DIR / "source-read-map.txt").write_text(READ_MAP)
    host_path = LOG_DIR / "host.txt"
    if host_path.exists():
        raise RuntimeError("host.txt exists: refusing a second R11E XNU boot")
    if not BOOT.is_file():
        raise RuntimeError(f"boot image missing: {BOOT}")

    transcript = []
    boot_attempted = False

    def note(message):
        transcript.append(message)
        print(message, end="", flush=True)

    def step(dev, label, command, timeout, expected):
        note(f"\n--- {label} ---\n> {command}\n")
        output = helpers.send_cmd(dev, command + "\n", wait_sec=timeout,
                                  required_substr=expected)
        transcript.append(output)
        if expected not in output or "xzs#" not in output:
            raise RuntimeError(f"{label}: completion/prompt missing; stop active testing")
        return output

    try:
        detected = subprocess.run(["fastboot", "devices"], capture_output=True,
                                  text=True, check=True, timeout=10).stdout
        note("fastboot_devices_before=\n" + detected)
        if not any(line.split() == [SERIAL, "fastboot"] for line in detected.splitlines()):
            raise RuntimeError("target not in fastboot; no XNU boot attempted")

        note("=== R11E ONE XNU FASTBOOT BOOT ===\n")
        boot_attempted = True
        result = subprocess.run(["fastboot", "-s", SERIAL, "boot", str(BOOT)],
                                capture_output=True, text=True, timeout=60)
        note(result.stdout + result.stderr)
        note(f"fastboot_boot_exit={result.returncode}\n")
        if result.returncode != 0:
            raise RuntimeError("fastboot boot failed; no retry")

        dev = helpers.xzs_console.open_stable_device(timeout_sec=60)
        if dev is None:
            raise RuntimeError("XNU USB console did not enumerate")
        prompt = False
        for _ in range(8):
            helpers.xzs_console.bulk_write(dev, b"\n", timeout_ms=1000)
            time.sleep(0.4)
            data = helpers.collect(dev, seconds=1.5)
            decoded_boot = data.decode("utf-8", errors="replace")
            note(decoded_boot)
            if "xzs#" in decoded_boot:
                prompt = True
                break
        if not prompt:
            raise RuntimeError("shell prompt missing; no further commands")

        prerequisites = [
            ("MMAGIC_AHB", "clocks mmagic-ahb-on", 2, "PASS"),
            ("MMAGIC_CFG_AHB", "clocks mmagic-cfg-ahb-on", 2, "PASS"),
            ("MMAGIC_NOC", "clocks mmagic-mdss-noc-on", 2, "PASS"),
            ("MMAGIC_AXI", "clocks mmagic-mdss-axi-on", 2, "PASS"),
            ("MDSS_GDSC", "display power mdss-on", 2, "PASS"),
            ("MDSS_AHB", "clocks mdss-ahb-on", 2, "PASS"),
            ("MDSS_AXI", "clocks mdss-axi-on", 2, "PASS"),
            ("MDSS_MDP", "clocks mdp-on", 2, "PASS"),
            ("CORE_STATUS", "clocks mdss-critical-status", 2, "PASS"),
            ("M3", "display m3-run full", 50, "PASS_FULL_M3"),
            ("M4", "display m4-run full", 10, "PASS_MODE2"),
            ("P1", "display p1-run", 5, "RESULT=PASS_P1"),
            ("PANEL_PREPARE", "display m8-status", 30, "PANEL_READY=yes"),
            ("M8-1", "display m8-fb-init", 45, "M8_1             = PASS"),
            ("M8-2", "display m8-rgb0-config", 15, "M8_2             = PASS"),
            ("M8-3", "display m8-lm0-config", 15, "M8_3             = PASS"),
            ("M8-4", "display m8-stream-config", 15, "M8_4             = PASS"),
            ("M8-5", "display m8-ctl-config", 15, "M8_5             = PASS"),
            ("M8-6", "display m8-flush-config", 15, "M8_6              = PASS"),
        ]
        for label, command, timeout, expected in prerequisites:
            step(dev, label, command, timeout, expected)

        prekick = step(dev, "PREKICK", "display m8-prekick-status", 20,
                       "PREKICK_READY=YES")
        (LOG_DIR / "pre-kick.txt").write_text(prekick)
        for marker in ("PANEL_READY=yes", "CTL_START_COUNT=0", "MDP_KICKOFF_COUNT=0",
                       "DSI_TRIG_CTRL=0x80000004", "PREKICK_READY=YES"):
            if marker not in prekick:
                raise RuntimeError(f"pre-kick guard missing: {marker}")

        kickoff = step(dev, "ONE KICKOFF", "display m8-kickoff", 90,
                       "R11C_SAFE_SHUTDOWN=PASS")
        (LOG_DIR / "kickoff.txt").write_text(kickoff)
        sample_rows = decode(kickoff)
        phases = [row.get("PHASE", "").lower() for row in sample_rows]
        for required in ("0x00001140", "0x00001150"):
            if required not in phases:
                raise RuntimeError(f"missing R11E phase {required}")
        if kickoff.count("CTL_START_WRITE=0x00000001") != 1:
            raise RuntimeError("CTL_START write was not exactly once")
        for marker in ("CTL_START_COUNT=1", "MDP_KICKOFF_COUNT=1",
                       "BUS_BW_VOTE_HW_STATE=UNOBSERVABLE",
                       "RGB0_FETCH_DIRECT_SIGNAL=NONE_SOURCE_PROVEN",
                       "R11C_SAFE_SHUTDOWN=PASS"):
            if marker not in kickoff:
                raise RuntimeError(f"kickoff guard missing: {marker}")
        if "Data Abort" in kickoff or "Kernel panic" in kickoff:
            raise RuntimeError("dangerous fault marker in kickoff log")
        note("R11E_RUN_COMPLETE=yes\nNO_SECOND_KICKOFF=yes\n")
        return 0
    except Exception as exc:
        note(f"R11E_STOP_REASON={type(exc).__name__}: {exc}\n")
        note("NO_FURTHER_XNU_COMMANDS=yes\n")
        return 1
    finally:
        note(f"XNU_BOOT_ATTEMPTED={'yes' if boot_attempted else 'no'}\n")
        if boot_attempted:
            host_path.write_text("".join(transcript))
        else:
            (LOG_DIR / "not-booted.txt").write_text("".join(transcript))


if __name__ == "__main__":
    sys.exit(main())
