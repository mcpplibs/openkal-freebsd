# Changelog

## 0.1.1

- `tools/vm-run.py` boots each run from a temporary overlay of the image
  (qemu-img), so the image is never written to and two runs at once do not
  meet; the overlay is discarded when the machine is shut down. The image is
  `--image` or `OKF_VM_IMAGE`, and KVM is used where the host has it.
- `tools/vm-runner` is a stable entry a runner declaration can name:
  `[target.x86_64-freebsd] runner = [".../tools/vm-runner", "{}"]` (README,
  "The runner declaration").

## 0.1.0

- openkal for FreeBSD, on the kernel's system-call interface: fourteen
  interfaces of openkal 0.25, hosted and standalone.
