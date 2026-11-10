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
issues that interface directly, on amd64 and arm64, and reads no header of a C
library. `src/sys.h` states FreeBSD 14's system-call numbers, flags and
structure layouts. They were read from the base system's headers by compiling
them for both architectures, where they are identical, and each structure's
size is asserted. The kernel reports a failure with the carry flag and the
error number in the first result register; `okf::call6` folds the two into one
value, the result or the error number negated, as openkal-linux's system calls
answer.

## Two arrangements

**Hosted (the default).** The program carries a runtime that has received
control: FreeBSD's own C library and `libthr`, as in a program built for
`x86_64-freebsd` or `aarch64-freebsd`. This implementation borrows what that
runtime already does, as openkal-linux does above a C library:

- the program's arguments and environment, from the `(argc, argv, envp)` that
  FreeBSD's run-time linker passes to every `.init_array` function;
- a context, which is the runtime's thread (`pthread_create`), and its stack,
  which the runtime states (`pthread_attr_get_np`).

`-lthr` is named only on those rows.

**Standalone (feature `standalone`).** Nothing in the program has received
control: either it supplies its own runtime, or it is built above a C library
written on openkal (openkal-musl, the rows `x86_64-freebsd-musl` and
`aarch64-freebsd-musl`). This implementation then:

- begins the program with `_start`, which takes the argument block from `%rdi`
  or `x0`, and the auxiliary vector after it;
- carries the `.note.tag` ABI brand by which the kernel recognises a FreeBSD
  image;
- lays out the image's `PT_TLS` block and sets the thread pointer
  (`sysarch(AMD64_SET_FSBASE)` on amd64, `tpidr_el0` on arm64);
- hands over to a linked C library's `__libc_start_main`, or else runs the
  initialisers and calls `main`;
- creates contexts with `thr_new` on stacks it maps with a guard, and joins
  them on the word `thr_exit` sets;
- states the first stack from `AT_USRSTACKBASE` and `AT_USRSTACKLIM`.

In both arrangements everything else is a system call:

| interface | how |
|---|---|
| `openkal.abort` | `exit`; SIGABRT at its default disposition |
| `openkal.stream` | `read`, `write`; a terminal is what answers `TIOCGETA` |
| `openkal.memory` | size classes carved from `mmap`ed chunks; `hw.physmem` and the free and inactive page counts from `sysctl` |
| `openkal.env` | the recorded vectors; `kern.proc.pathname` for the image's name |
| `openkal.time` | `clock_gettime`, `clock_getres`, `nanosleep` |
| `openkal.random` | `getrandom` |
| `openkal.fs` | the `*at` calls relative to directory descriptors; `getdirentries`; `flock` for a lock on a whole file; two preopens, the working directory (`__getcwd`) and the root |
| `openkal.process` | `pdfork` (`PD_DAEMON`; the descriptor is held while the handle is) and `fexecve`; `wait6`; process groups as units; SIGINT, SIGTERM and SIGHUP taken over with `sigaction` |
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

## How it is examined

**Hosted.** openkal's conformance suite is built on Linux for `x86_64-freebsd`
and `aarch64-freebsd` above FreeBSD's C library, using the base-system sysroots
`xim:<arch>-freebsd-sysroot@14.5` named by `ci/freebsd-platform`. It is then run
in a FreeBSD virtual machine (vmactions/freebsd-vm; the arm64 machine is
emulated). On FreeBSD 14.5 amd64 it reports 334 observations held, none
failed, and 12 not observed (interfaces not provided, properties not claimed).
The suite's three-way check holds: the manifest's `provides-interfaces`,
`kal_interfaces()` and the exported names state the same fourteen interfaces.

**Standalone.** Every source compiles for both architectures from openkal's
headers alone, `-ffreestanding` and without a system header, and the objects
carry the brand (`tools/check-standalone.sh`). The suite in this arrangement
runs above openkal-musl on the `*-freebsd-musl` rows.

- `tools/defer-runner` puts the suite and the variables it is run with in a
  directory, where CI runs it inside a FreeBSD VM.
- `tools/vm-run.py` runs a program in a local FreeBSD VM image for development
  (`OKF_VM_IMAGE`, `tools/vm-runner`). It forwards `OPENKAL_*`, `KAL_*` and the
  mtest protocol's variables, so the suite can report through mtest once the
  runner's listing pass honours `--run-under` (mtest 0.5.3).

The targets need mcpp 2026.11.10.1 or later. Until that release, CI builds the
engine from the branch that carries it.

## License

Apache-2.0.
