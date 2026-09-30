# D8-M8 T1 — MDP core clock readback

One RAM boot. No `CTL_START`. No clock write. No second boot.

```text
BRANCH=xzs-d8-display-m8-resume
BASE_HEAD=b9a36bd407e332da786d2d3933d0a0c04c00913e
T1_COMMIT=98845843cdf70eb1d625fab490a075fe0fca710d
KERNEL_SHA256=7d1b31918f19bc0ba8d2ccd98e097cc8d72b74bdfaf22443217140ec7cb9b54f
BOOT_SHA256=29b7cbe5972fbfd9e7a735b0dc6ecd8303b2a6059323b6a940aae78e875bae09
PAC=0
SERIAL=BH905SX976
```

Host log: `artifacts/hw/d8m8/t1-mdp-clock-readback/host.txt`.

## Result

```text
T1_CLASS=T1-D NON_TABLE_CONFIGURATION
M8_1_PASS=YES

MDP_RCG_CMD=0x00000000
MDP_RCG_CFG=0x00000000
MDP_ROOT_OFF=NO
MDP_SOURCE=BI_TCXO
MDP_HID=0
MDP_TABLE_MATCH=NO
MDP_TABLE_RATE_HZ=UNKNOWN

MDP_CBCR=0x00006221
MDP_BRANCH=ACTIVE
AHB_CBCR=0x20008001
AHB_BRANCH=ACTIVE
AXI_CBCR=0x00006221
AXI_BRANCH=ACTIVE
VSYNC_CBCR=0x80000000
VSYNC_BRANCH=INACTIVE
```

`CMD` bit 31 is clear, so the RCG root is not off. `CFG` is zero: source selector 0, hid 0, mode 0. Source 0 is `BI_TCXO` on `mmss_xo_mmpll0_mmpll5_gpll0_gpll0_div_map`. That parent is legal for the mux. The pair `(src=0, hid=0)` is not one of the nine `ftbl_mdp_clk_src` rows. Those rows start at GPLL0 divided by 7 (85,714,286 Hz) and MMPLL5 divided by 2 (412,500,000 Hz). Class `T1-D`.

`clk_rcg2_calc_rate` leaves the parent rate unchanged when hid is 0. With BI_TCXO at 19,200,000 Hz, that arithmetic is 19,200,000 Hz. It is not a table rate and not a PLL measurement. `MDP_TABLE_RATE_HZ` stays `UNKNOWN`.

The MDP branch CBCR `0x00006221` has enable bit 0 set and `BRANCH_CLK_OFF` clear, so the branch is on while its parent RCG is the XO bypass above. AHB `0x20008001` and AXI `0x00006221` are the same shape: bit 0 set, bit 31 clear. VSYNC `0x80000000` is off. This run stopped before `display m8-stream-config`, which is where XNU enables the VSYNC RCG and CBCR. The inactive VSYNC bit is that skipped stage, not a failed VSYNC program.

`M8_1 = PASS` printed and the shell returned before the six reads. `CTL_START_COUNT=0`. Panel shutdown returned `[T1] SAFE_SHUTDOWN=PASS` and `[D8-M6-SHUTDOWN] SUCCESS`. `xzs#` came back. No panic, no SError, no data abort.

## What this does to U1

```text
MDP_CLOCK_MISMATCH=HW_READBACK_PROVEN
U1=not a valid table entry
```

The core clock is not removed from the cause list. Sony's table does not contain this XO bypass, and XNU never calls `mdss_mdp_set_clk_rate`. The read shows the silicon is still on that bypass after the branch enable. This boot does not write a new rate. A later task, if one is authorized, is a separate one-change rate program against one table row, with this readout as the before image.

No VBIF read, no QoS change, no bus vote, no TE change, no `CTL_START`.
