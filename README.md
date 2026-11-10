# openkal-freebsd

An implementation of [openkal](https://github.com/mcpplibs/openkal) for FreeBSD,
written on the kernel's own system-call interface.

```toml
[target.'cfg(os = "freebsd")'.dependencies]
openkal-freebsd = "0.1.0"
```

## What it is

FreeBSD's kernel interface is stable: a program built for FreeBSD 14 runs on
every later 14.x and, through `COMPAT_FREEBSD14`, on 15. This implementation
issues that interface directly. It reads the kernel's numbers and structure
layouts from the system's headers and calls none of the C library's functions
(`src/sys.h`). On amd64 the kernel reports a failure with the carry flag and the
error number in `%rax`; `okf::call6` folds the two into one value, the result
or the error number negated, as openkal-linux's system calls answer.

## The arrangement of 0.1

Version 0.1 is built for one arrangement: the program carries a runtime that
has received control, FreeBSD's own C library and `libthr`, as a program
compiled for `x86_64-freebsd` does. In that arrangement it borrows what the
runtime already does, as openkal-linux does above a C library:

- the program's arguments and environment, from the `(argc, argv, envp)` that
  FreeBSD's run-time linker passes to every `.init_array` function;
- a context, which is the runtime's thread (`pthread_create`), and a context's
  stack, which the runtime states (`pthread_attr_get_np`).

Everything else is a system call:

| interface | how |
|---|---|
| `openkal.abort` | `exit`; SIGABRT at its default disposition |
| `openkal.stream` | `read`, `write`; a terminal is what answers `TIOCGETA` |
| `openkal.memory` | size classes carved from `mmap`ed chunks; `hw.physmem` and the free and inactive page counts from `sysctl` |
| `openkal.env` | the recorded vectors; `kern.proc.pathname` for the image's name |
| `openkal.time` | `clock_gettime`, `clock_getres`, `nanosleep` |
| `openkal.random` | `getrandom` |
| `openkal.fs` | the `*at` calls relative to directory descriptors; `getdirentries`; `flock` for a lock on a whole file; two preopens, the working directory (`__getcwd`) and the root |
| `openkal.process` | `fork` (the child is told by `%rdx`), `fexecve`, `wait4`; process groups as units; SIGINT, SIGTERM and SIGHUP taken over with `sigaction` |
| `openkal.task` | a word waited upon and woken with `_umtx_op` (`UMTX_OP_WAIT_UINT_PRIVATE`, `UMTX_OP_WAKE_PRIVATE`) |
| `openkal.exec` | `mmap`, `mprotect`, never writable and executable at once; a region at an address is `MAP_FIXED \| MAP_EXCL` |
| `openkal.terminal` | `TIOCGETA`, `TIOCSETAF`, `TIOCGWINSZ` |
| `openkal.timeout` | `ppoll`; a program awaited with a bound |
| `openkal.map` | `mmap` of a descriptor |
| `openkal.region` | reserved `PROT_NONE` mappings backed by `mprotect` |

Absent in 0.1, neither stated nor defined: `openkal.space`, `openkal.net`,
`openkal.datagram`, `openkal.prof` and `openkal.debug`. Within the stated
interfaces, the property words do not claim the following, and the operations
answer `kal_err_not_supported` as clause 6.2 requires:

- confinement, a bound lifetime and a one-string command line for a started
  program;
- the observable units of 0.24, and a unit's usage and limits;
- a rename that does not replace (FreeBSD has none);
- the timeout forms of `accept` and `recv_from`, whose resources belong to the
  absent network interfaces.

The stop word is counted and never zero again (openkal 0.25). The handler wakes
the word's waiters itself, because a wake is a system call, which a handler may
make.

**Not yet.** The standalone arrangement, in which this implementation is the
whole of the program's environment: its own `_start` and the `.note.tag` brand
the kernel recognises a FreeBSD image by, the thread pointer through `sysarch`,
and contexts created with `thr_new`. Also aarch64, and the interfaces above.

## How it is examined

openkal's conformance suite is built on Linux for `x86_64-freebsd` above
FreeBSD's C library, using the base-system sysroot `xim:x86_64-freebsd-sysroot`
named by `ci/freebsd-platform`. It is then run in a FreeBSD virtual machine. On
FreeBSD 14.5 it reports 334 observations held, none failed, and 12 not observed
(interfaces not provided, properties not claimed). The suite's three-way check
holds: the manifest's `provides-interfaces`, `kal_interfaces()` and the exported
names state the same fourteen interfaces.

- `tools/defer-runner` puts the suite and the variables it is run with in a
  directory, where CI runs it inside a FreeBSD VM.
- `tools/vm-run.py` runs a program in a local FreeBSD VM image for development
  (`OKF_VM_IMAGE`, `tools/vm-runner`).

The target needs mcpp 2026.11.10.1 or later. Until that release, CI builds the
engine from the branch that carries it.

## License

Apache-2.0.
