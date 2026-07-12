# 9front

This repo contains a copy of the 9front sources (release 11879). It boots
diskless over PXE in qemu, with this checkout itself served into the VM
as the root file system via u9fs. That means you can edit the tree from
your host and the changes show up in the VM right away, and anything the
VM writes back shows up on the host, without needing to install or run a
separate 9front disk image.

To boot this under qemu, install qemu so `qemu-system-x86_64` is in your path, then:

	./boot/qemu # amd64
  ./boot/qemu -i386 # 386

> Pass `-i386` to boot the 32-bit kernel instead.

The qemu script builds u9fs in `sys/src/cmd/unix/u9fs` and then runs qemu
with the right options to boot diskless, using this checkout as the root
file system over 9P. Because the VM shares the files with the host, you
can edit files locally and see the changes immediately in the VM.

At boot time the kernel PXE-loads a plan9.ini and kernel over TFTP
(provided by qemu). Which plan9.ini gets loaded depends on the VM's MAC
address:

- amd64: [cfg/pxe/525400123456](cfg/pxe/525400123456)
- 386: [cfg/pxe/525400123457](cfg/pxe/525400123457)

Changes to either take effect on the next boot.

u9fs itself, the Unix port of the 9P server, is checked into
`sys/src/cmd/unix/u9fs` so it can be built and modified alongside the rest
of the tree. See [its README](sys/src/cmd/unix/u9fs/README.md) for build
instructions and inetd/launchd recipes.
