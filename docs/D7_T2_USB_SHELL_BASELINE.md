# D7-T2 USB interactive shell baseline

Not a completion seal. Tag `xzs-d7t1-complete` stays on
`70983854455c39dc0ef4a9a615ea35aa7d69b7c2`.

Hardware-proven on one boot of kernel
`e7c00e18b5626d64b5af58926bf6722602868867`
with host `05982046eff6a3ced1b50998cbba384ccabe9ba2`.

```text
USB=PASS
TTY=PASS
INTERACTIVE_PROMPT=PASS
HELP=PASS
ECHO=PASS
PWD=PASS
CD=PASS
CAT=PASS
```

Bulk OUT, bulk IN, and loopback passed. `1209:000A` was claimed on
macOS without a kernel-driver detach. `cat /etc/issue` printed
`XNU-XZS`. `cd /bin` then `pwd` printed `/bin`.

Not yet proven on that boot: `ls` printed no names, and `/bin/hello`
produced no userland line before the phone returned to fastboot.
Later candidates must keep the commands above working.
