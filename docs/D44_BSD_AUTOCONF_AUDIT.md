# D44 / BSD Autoconf Source Audit & Telemetry Specification

## 1. Scope & Objective

This document audits the exact execution sequence between:
- `[XZS-BOOT] [D44] BSD AUTOCONF ENTER` (`src/xnu/bsd/kern/bsd_init.c:898`)
and
- `[XZS-BOOT] [D45] BSD AUTOCONF COMPLETE` (`src/xnu/bsd/kern/bsd_init.c:902`)

The goal is to provide dense, non-invasive telemetry across every initialization step to isolate the exact boundary where execution stalls nondeterministically during early boot.

---

## 2. Audited Execution Path & Call Graph

In `src/xnu/bsd/kern/bsd_init.c`:
```text
bsd_init()
  └─ [D44] BSD AUTOCONF ENTER
  └─ bsd_autoconf()
       ├─ kminit()                                [src/xnu/bsd/dev/arm/km.c:52]
       ├─ pseudo_inits loop                      [14 initializers from ioconf.c]
       │    ├─ pty_init(128)
       │    ├─ ptmx_init(1)
       │    ├─ mdevinit(1)
       │    ├─ bpf_init(4)
       │    ├─ fsevents_init(1)
       │    ├─ random_init(1)
       │    ├─ dtrace_init(1)
       │    ├─ helper_init(1)
       │    ├─ lockstat_init(1)
       │    ├─ lockprof_init(1)
       │    ├─ sdt_init(1)
       │    ├─ systrace_init(1)
       │    ├─ fbt_init(1)
       │    └─ profile_init(1)
       └─ IOKitBSDInit()                         [src/xnu/iokit/bsddev/IOKitBSDInit.cpp:119]
            └─ IOService::publishResource("IOBSD") [src/xnu/iokit/Kernel/IOService.cpp:5149]
                 ├─ gIOResources->setProperty(key, value)
                 └─ gIOResources->registerService() [src/xnu/iokit/Kernel/IOService.cpp:1052]
                      └─ startMatching(options)      [src/xnu/iokit/Kernel/IOService.cpp:1111]
                           └─ _IOServiceJob::startJob() [src/xnu/iokit/Kernel/IOService.cpp:1318]
                                └─ pingConfig(job)       [src/xnu/iokit/Kernel/IOService.cpp:6158]
                                     ├─ _IOConfigThread::configThread() [src/xnu/iokit/Kernel/IOService.cpp:5304]
                                     │    └─ kernel_thread_start()      [src/xnu/osfmk/kern/thread.c:1983]
                                     │         ├─ kernel_thread_create()
                                     │         └─ thread_start()
                                     └─ semaphore_signal(gJobsSemaphore)

Asynchronous Worker Thread:
_IOConfigThread::main()                          [src/xnu/iokit/Kernel/IOService.cpp:6031]
  └─ semaphore_wait(gJobsSemaphore)
  └─ job->nub->doServiceMatch(job->options)
```

---

## 3. Source-Audited Thread & Scheduler Properties

| Property | Value | Classification | Source Evidence |
| :--- | :--- | :--- | :--- |
| `CONFIG_THREAD_CREATE_API` | `kernel_thread_start` | **SOURCE-AUDITED FACT** | `src/xnu/iokit/Kernel/IOService.cpp:5316` calls `kernel_thread_start(&_IOConfigThread::main, inst, &thread)` |
| `CONFIG_THREAD_CPU_AFFINITY` | `unbound` | **SOURCE-AUDITED FACT** | `_IOConfigThread::configThread` does not invoke `thread_bind` or `thread_affinity_set`; assigned to default `processor_set` |
| `CONFIG_THREAD_SCHEDULER_DEPENDENCY` | `yes` | **SOURCE-AUDITED FACT** | `thread_start` enqueues thread into runqueue; requires Mach scheduler (`thread_select` / `thread_invoke`) to run |
| `CONFIG_THREAD_INTERRUPT_DEPENDENCY` | Cross-core dispatch: SGI IPI; Local core: preemption / yield | **INFERENCE/HYPOTHESIS** | If assigned to secondary core, requires GICv3 SGI reschedule IPI; if on primary core, requires yield / `thread_block` |
| `CONFIG_THREAD_WAIT_PRIMITIVE` | `semaphore_wait(gJobsSemaphore)` | **SOURCE-AUDITED FACT** | `src/xnu/iokit/Kernel/IOService.cpp:6053` |
| `CONFIG_THREAD_WAKE_PRIMITIVE` | `semaphore_signal(gJobsSemaphore)` | **SOURCE-AUDITED FACT** | `src/xnu/iokit/Kernel/IOService.cpp:6205` |

---

## 4. Checkpoint Family 0xD440 Complete Allocation

Every checkpoint ID is unique. No IDs are shared between ENTER and RETURN.

```text
=== Family 0xD440 Checkpoint Mapping ===

-- bsd_autoconf Core --
0xD440/0x00: bsd_autoconf ENTER
0xD440/0x01: kminit ENTER
0xD440/0x02: kminit RETURN

-- pseudo_inits (14 initializers) --
0xD440/0x10: pseudo_inits loop ENTER
0xD440/0x11: pty_init ENTER
0xD440/0x12: pty_init RETURN
0xD440/0x13: ptmx_init ENTER
0xD440/0x14: ptmx_init RETURN
0xD440/0x15: mdevinit ENTER
0xD440/0x16: mdevinit RETURN
0xD440/0x17: bpf_init ENTER
0xD440/0x18: bpf_init RETURN
0xD440/0x19: fsevents_init ENTER
0xD440/0x1A: fsevents_init RETURN
0xD440/0x1B: random_init ENTER
0xD440/0x1C: random_init RETURN
0xD440/0x1D: dtrace_init ENTER
0xD440/0x1E: dtrace_init RETURN
0xD440/0x1F: helper_init ENTER
0xD440/0x20: helper_init RETURN
0xD440/0x21: lockstat_init ENTER
0xD440/0x22: lockstat_init RETURN
0xD440/0x23: lockprof_init ENTER
0xD440/0x24: lockprof_init RETURN
0xD440/0x25: sdt_init ENTER
0xD440/0x26: sdt_init RETURN
0xD440/0x27: systrace_init ENTER
0xD440/0x28: systrace_init RETURN
0xD440/0x29: fbt_init ENTER
0xD440/0x2A: fbt_init RETURN
0xD440/0x2B: profile_init ENTER
0xD440/0x2C: profile_init RETURN
0xD440/0x2F: pseudo_inits loop RETURN

-- IOKitBSDInit & publishResource --
0xD440/0x30: IOKitBSDInit ENTER
0xD440/0x31: publishResource("IOBSD") ENTER
0xD440/0x32: publishResource setProperty RETURN
0xD440/0x33: registerService ENTER
0xD440/0x34: startMatching ENTER
0xD440/0x35: _IOServiceJob::startJob ENTER
0xD440/0x36: pingConfig ENTER
0xD440/0x37: configThread CREATE REQUESTED
0xD440/0x38: configThread OBJECT RETURNED
0xD440/0x39: configThread WAKE SIGNAL SENT (semaphore_signal done)
0xD440/0x3A: _IOServiceJob::startJob / startMatching RETURN
0xD440/0x3B: publishResource("IOBSD") RETURN
0xD440/0x3C: IOKitBSDInit RETURN

-- _IOConfigThread Worker Lifecycle --
0xD440/0x40: configThread ENTRY REACHED (_IOConfigThread::main entered)
0xD440/0x41: configThread WORK STARTED (semaphore_wait returned)
0xD440/0x42: configThread SERVICE MATCH COMPLETED (doServiceMatch returned)
0xD440/0x43: configThread JOB FINISHED (job released)

-- Autoconf Exit --
0xD440/0x90: bsd_autoconf COMPLETE (prior to [D45])
```

---

## 5. Non-Invasive Breadcrumb Implementation Rule

- `D440_TELEMETRY_WATCHDOG_PET=no`: Under NO circumstances may a `0xD440` diagnostic checkpoint pet the APCS watchdog timer.
- Telemetry function `xzs_d440_crumb(step)`:
  - Updates non-volatile IMEM SRAM (`0x066bf660`): `CP=0xD440`, `step`.
  - Updates persistent DRAM (`0x80060020`): `CP=0xD440`, `step` (with cache flush `dc cvac`).
  - Emits minimal console marker: `[D440/xx] CPU=c TH=p INT=i PRE=l\n`.
  - **Does NOT invoke `xzs_watchdog_pet()`**.
  - Does NOT allocate heap memory.
  - Does NOT take blocking locks.
  - Does NOT sleep.
