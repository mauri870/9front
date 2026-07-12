# 9front

This repository contains the 9front source tree (release 11879) with a
diskless QEMU setup.

The VM boots over PXE and mounts this repository as its root file system
using u9fs over 9P. Since the VM is using your working tree directly,
changes made on the host are immediately visible inside the VM, and files
written from the VM are reflected back on the host.

## Booting

Install QEMU so `qemu-system-x86_64` is available in your `PATH`, then run:

```sh
./boot/qemu          # amd64
./boot/qemu -i386    # 386
```

The boot script builds `sys/src/cmd/unix/u9fs` if needed, starts a 9P
server exporting this repository, and launches QEMU configured for a
diskless PXE boot.

## PXE configuration

During boot, the VM downloads `plan9.ini` and the kernel over TFTP. The
configuration is selected based on the virtual NIC MAC address.

| Architecture | PXE configuration |
|--------------|-------------------|
| amd64 | `cfg/pxe/525400123456` |
| 386 | `cfg/pxe/525400123457` |

Changes take effect on the next boot.

## u9fs

The Unix port of u9fs is included in this repository under
`sys/src/cmd/unix/u9fs`, allowing it to be built and modified alongside
the rest of the source tree.

See [sys/src/cmd/unix/u9fs/README.md](sys/src/cmd/unix/u9fs/README.md) for
additional build instructions and inetd/launchd integration.
