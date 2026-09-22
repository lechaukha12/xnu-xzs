# D7-T2 deferred

Status: **D7-T2 FULL = INCOMPLETE / UNSEALED / DEFERRED**

Date of this record: 2026-09-22

Device: Sony Xperia XZs G8231, Tone/Keyaki, MSM8996, ARM64 Kryo.

This file is the resume point. Do not restart from fork, `svc #0x80`, syscall dispatch, or `execve` entry unless new hardware evidence contradicts the proofs below.

## Freeze identity

| Field | Value |
|---|---|
| BRANCH | `xzs-d7t2-shell` |
| Investigation HEAD before this document | `88449c3b8ff9fdb30d0e079830cfde5ab6f2105d` |
| Last image that was hardware-booted | `895db10dcaaaf20d99cb24812eb25eabbf71f35f` |
| Last booted KERNEL_SHA256 | `bb4873e8619a62015eb36cacd46a93b0453f08e26fd1f9f8c9e9fbb7d6247ee2` |
| Last booted BOOT_SHA256 | `a9f9c38098092ebe8a2a3d816af076a487cae65950324a089ad1af9c738cd4c2` |
| Untested workaround commit | `88449c3b8ff9fdb30d0e079830cfde5ab6f2105d` |
| Untested workaround KERNEL_SHA256 | `14f6c49eeee1a8493fb0e922ca4a9729a0800ec7e99751cd16e20bbe232f074a` |
| Untested workaround BOOT_SHA256 | `758d0b2452a9e836ef470612057f4aa7171b50fb16cc0a491fe66a749f058597` |

`88449c3` manually creates one page at `0x100000000` and copies `/bin/hello` into it. That commit was built and not booted. It is forensic history only. It is not an exec implementation.

Tag `xzs-d7t2-deferred` points at the commit that adds this document, which is a descendant of `88449c3`.

Historical tags that must not move:

| Tag | Commit |
|---|---|
| `xzs-d7t1-complete` | `70983854455c39dc0ef4a9a615ea35aa7d69b7c2` |

## What is already hardware-proven

Builtins on the live USB shell, including `764daf1` run 5 and repeated later boots: `help`, `echo` (one copy of the typed line), `pwd`, `cd`, `ls`, `cat /etc/issue` → `XNU-XZS`.

USB EP0 reliability on `764daf1decc905c5e8405f8206859ed0a2beed49`: 5/5 `SET_CONFIGURATION` RC=0, claim, bulk OUT/IN, loopback. Kernel `8d77a7ae60ea891abbd92df73ad1c70bdc43b727478600037bc42d0caf68a915`. Boot `e5416b33cfe87269543450dffddcb0e378e007debcadc273a844bfc169f2ae69`. Do not treat later D7-T2 USB sessions as a reason to reopen sealed D7-T1.

External-exec frontier, proven on device and not to be re-litigated:

| Step | Result |
|---|---|
| fork child creation | PASS |
| child `x0=0`, `x1=1` | PASS (`1aba997` and later) |
| child EL0 resume at shell PC `0x10000153c` | PASS |
| `svc #0x80`, syscall 59 | PASS |
| `sysent[59]` is `execve` | PASS |
| `execve` / `__mac_execve` entered | PASS |
| Mach-O load reports success (`E06 macho loaded`) | PASS |
| `vm_map_exec` / `activate_exec_state` | PASS |
| argv/string copyout | PASS on boots that reached `E10 strings ok` |
| hello entry in the Mach-O | `0x1000002f0` |
| CPU fetched at that PC | PASS (`E03d far/pc 0x1000002f0`) |
| `vm_fault` there | `KERN_INVALID_ADDRESS` (1) |

## Root cause to resume from

`load_machfile()` returns success, then the vm map installed as the task map is empty.

Hardware log from `895db10` (`artifacts/hw/d7t2-895db10/host.txt`):

```text
[XZS-PROC] E10 task min  0000000100000000
[XZS-PROC] E10 task max  00007ffffe000000
[XZS-PROC] E10 task nent 0000000000000000
[XZS-PROC] E10 task no entry
[XZS-PROC] E13 execed
[XZS-PROC] E03d far    00000001000002f0
[XZS-PROC] E03d result 0000000000000001
```

`/bin/hello` `__TEXT` is one 16 KiB segment at `0x100000000`, entry `0x1000002f0`, preceded by `__PAGEZERO` of size `0x100000000`. The final task map's minimum is exactly that page-zero end, and it contains zero entries. `vm_fault(0x1000002f0)` therefore returns `KERN_INVALID_ADDRESS`.

The loss is between Mach-O segment insertion and the map that becomes `task->map`. Do not resume at AF, UXN, leaf PTEs, or TLB until an entry for `__TEXT` survives that handoff.

## Intermediate findings (superseded as the primary bug)

These were real on the boots that produced them. They are not the bug to start from now.

| Finding | What the boot showed |
|---|---|
| Child `x0` was the pid | `764daf1` hello returned to the prompt with no output. Fixed by storing `x0=0`, `x1=1` in `1aba997`. |
| Child text PTE missing after fork | `165276a`: promote reported the shell page missing; abort at `0x10000153c`. Later boots faulted that page in (`E03 text fault 0`, `E03 shell text exec ok`). |
| `vm_fault` while `exec` holds the vnode | `3f1a87b` stopped at `E09 fault enter`. `0145588` stopped inside `exec_prefault_data`. |
| `thread_stop` on the fork child | `c0f9623` stopped after `E01a clone`. `264f938` copies registers without `thread_stop`. That workaround is not a mainline fix. |
| UXN promote of hello returned 5 | `c0223b1`: `E09 kr 5` (`KERN_FAILURE`), no leaf. Consistent with the later empty-map result. |
| Thread map vs task map | `254c6bf`: `E03d same map` and result 1. They are the same map, and that map has no entry. `b9fa86d` assigned the task map before `eret` and the phone reset to fastboot. Do not repeat that assignment. |

## Where to resume

Answer this first: where are the Mach-O segment entries created, and which assignment drops them before the map is the final task map?

Log these pointers and entry counts on one boot:

```text
NEW_MAP=  NEW_MAP_NENT=
LOAD_MACHFILE_MAP=  LOAD_MACHFILE_NENT=
SEGMENT_MAP=  SEGMENT_MAP_NENT=
TASK_MAP_BEFORE_SWAP=  TASK_MAP_AFTER_SWAP=
THREAD_MAP=  FAULT_MAP=
```

Read `vm_map_enter` results in `map_segment`, `load_segment` for `__PAGEZERO` (`vm_map_raise_min_offset` to `0x100000000`), `load_machfile`'s new map, and `swap_task_map`.

`__PAGEZERO` is `vmaddr 0`, `vmsize 0x100000000`. `__TEXT` starts at that same address. A raise of `min_offset` to `0x100000000` followed by a failed or dropped `vm_map_enter` of `__TEXT` matches `nentries == 0`.

## Host rules that stay in force

No flash. One `fastboot boot` per image. Do not run `xzs# reboot` (it halts the shell thread; CPU0 still pets the MSM8996 watchdog). On macOS do not call `is_kernel_driver_active` or `detach_kernel_driver`. Call `set_configuration(1)` only when the active configuration is not already 1. After the tty bridge is up, a reconnect must not repeat the Z handshake. Trust pstore only when the buffer contains `[XZS`. Path is `/sys/fs/pstore/console-ramoops`.

## D7-T2-LITE

Full exec is deferred. The builtin shell and a kernel that stays up without USB are the next acceptance gate, on `main`, without this branch's hello-page reconstruction.
