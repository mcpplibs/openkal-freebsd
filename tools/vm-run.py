#!/usr/bin/env python3
"""Runs a program in a FreeBSD virtual machine and answers as the program did.

    vm-run.py --image <disk.qcow2> [--file <path>]... -- <program> [arguments...]

The machine is booted under qemu/KVM from <disk> (an overlay of FreeBSD's own
VM image; nothing is written to its backing file), the files are offered to it
on a read-only FAT drive and copied to /tmp/run, and the program is run there as
root on the serial console. What it writes is written here, and its status is
this program's status. Every argument is passed through single quotes, so the
command line is the one given.

THE PROGRAM'S ENVIRONMENT. Variables named OPENKAL_* and KAL_* are passed to
the program. Where a test runner speaks the mtest protocol to it (MTEST_PROTOCOL
and MTEST_REPORT set), the program writes its events into a file of the machine,
which is appended to MTEST_REPORT here when it ends; MTEST_MODE and MTEST_SELECT
(a file, offered to the machine) are passed as well.

It is how this repository's conformance suite is run on FreeBSD from a Linux
host: the suite's runner (OPENKAL_CONFORMANCE_RUNNER). The
image's kernel console is moved to the serial line at the loader prompt, as the
official image boots to the video console.
"""
import argparse, os, pty, re, select, shutil, subprocess, sys, tempfile, time

ap = argparse.ArgumentParser()
ap.add_argument("--image", required=True)
ap.add_argument("--file", action="append", default=[])
ap.add_argument("--memory", default="2048")
ap.add_argument("--timeout", type=int, default=1800)
ap.add_argument("command", nargs=argparse.REMAINDER)
args = ap.parse_args()
command = args.command[1:] if args.command[:1] == ["--"] else args.command
if not command:
    ap.error("no program to run")

share = tempfile.mkdtemp(prefix="vm-run-")
forward = {k: v for k, v in os.environ.items() if k.startswith("OPENKAL_") or k.startswith("KAL_")}
report = os.environ.get("MTEST_REPORT") if os.environ.get("MTEST_PROTOCOL") else None
if report:
    forward["MTEST_PROTOCOL"] = os.environ["MTEST_PROTOCOL"]
    forward["MTEST_REPORT"] = "/tmp/run/vm-run-report.ndjson"
    if os.environ.get("MTEST_MODE"):
        forward["MTEST_MODE"] = os.environ["MTEST_MODE"]
    if os.environ.get("MTEST_SELECT") and os.path.isfile(os.environ["MTEST_SELECT"]):
        shutil.copy2(os.environ["MTEST_SELECT"], os.path.join(share, "vm-run-select.txt"))
        forward["MTEST_SELECT"] = "/tmp/run/vm-run-select.txt"
for f in args.file + [command[0]]:
    if os.path.isfile(f):
        shutil.copy2(f, os.path.join(share, os.path.basename(f)))
# A program offered from here is run from the copy; one named otherwise is the
# machine's own (`/usr/bin/lldb').
program = "./" + os.path.basename(command[0]) if os.path.isfile(command[0]) else command[0]

def quote(a):
    return "'" + a.replace("'", "'\\''") + "'"

line = " ".join(["env"] + [quote(f"{k}={v}") for k, v in sorted(forward.items())]
               + [quote(program)] + [quote(a) for a in command[1:]])
argv = ["qemu-system-x86_64", "-enable-kvm", "-m", args.memory, "-smp", "2", "-nographic",
        "-drive", f"file={args.image},if=virtio,format=qcow2",
        "-drive", f"file=fat:{share},if=virtio,format=raw,readonly=on",
        "-nic", "none"]
pid, fd = pty.fork()
if pid == 0:
    os.execvp(argv[0], argv)

buf = b""
def read_until(pattern, timeout):
    global buf
    end = time.time() + timeout
    rx = re.compile(pattern)
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                return None
            buf += chunk
            m = rx.search(buf)
            if m:
                return m
    return None

def send(text):
    for ch in text:
        os.write(fd, ch.encode())
        time.sleep(0.02)

def stop(status):
    try:
        os.kill(pid, 9)
    except OSError:
        pass
    shutil.rmtree(share, ignore_errors=True)
    sys.exit(status)

if read_until(rb"Autoboot in", 180):
    send(" "); time.sleep(0.5); send("3")
    read_until(rb"OK ", 30); time.sleep(1); buf = b""
    send("set console=comconsole\r"); read_until(rb"OK ", 10); time.sleep(1); buf = b""
    send("boot\r")
if not read_until(rb"login: ", 300):
    send("\r")
    if not read_until(rb"login: ", 60):
        sys.stderr.write("vm-run: the machine gave no login prompt\n")
        stop(125)
buf = b""
send("root\r")
read_until(rb"(# |Password:)", 30)
if b"Password:" in buf:
    send("\r"); read_until(rb"# ", 30)

# The program's output between two marks, and its status after the second.
buf = b""
send("sh\r"); read_until(rb"\$ |# ", 10)
buf = b""
send("mkdir -p /mnt/s /tmp/run && mount -t msdosfs -o ro /dev/vtbd1s1 /mnt/s && cp /mnt/s/* /tmp/run/ "
     "&& chmod +x /tmp/run/* && cd /tmp/run && stty -echo; echo @@VM-RUN-BEGIN@@; "
     + line + "; rc=$?; echo; echo @@VM-RUN-STATUS=$rc@@\r")
m = read_until(rb"@@VM-RUN-STATUS=(\d+)@@", args.timeout)
if not m:
    sys.stderr.write("vm-run: the program did not finish within the time allowed\n")
    stop(124)
text = buf.decode(errors="replace").replace("\r", "")
begin = text.rfind("@@VM-RUN-BEGIN@@\n")
body = text[begin + len("@@VM-RUN-BEGIN@@\n"):text.rfind("\n@@VM-RUN-STATUS=")] if begin >= 0 else text
# `echo' after the program added one line feed, so that the mark begins a line.
if body.endswith("\n"):
    body = body[:-1]
sys.stdout.write(body + ("\n" if body and not body.endswith("\n") else ""))
sys.stdout.flush()
status = int(m.group(1))
if report:
    buf = b""
    send("echo @@VM-RUN-REPORT@@; cat /tmp/run/vm-run-report.ndjson 2>/dev/null; echo; echo @@VM-RUN-REPORT-END@@\r")
    if read_until(rb"@@VM-RUN-REPORT-END@@\r?\n", 120):
        text = buf.decode(errors="replace").replace("\r", "")
        a = text.find("@@VM-RUN-REPORT@@\n")
        b = text.rfind("@@VM-RUN-REPORT-END@@")
        if a >= 0 and b > a:
            events = [l for l in text[a + len("@@VM-RUN-REPORT@@\n"):b].split("\n") if l.startswith("{")]
            with open(report, "a") as out:
                for l in events:
                    out.write(l + "\n")
send("shutdown -p now\r")
read_until(rb"Uptime:", 60)
stop(status)
