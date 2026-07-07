# ArtfulType Floppy Writer

A tiny classic-Mac app that turns a **blank 800K floppy** into a bootable
**ArtfulType (System 6.0.8)** disk — on real hardware, with nothing else
installed.

## Why it exists

Retro68's `LaunchAPPL` can send an *application* to a Mac over the network,
but it cannot send a data file or a disk image. So to make a physical bootable
floppy on, say, a Mac SE that has only a System and the launcher, this app
**embeds the whole 800K disk image inside itself** (as an `'ATdi'` resource)
and writes it to an inserted floppy sector-for-sector.

The embedded image is `dist/ArtfulType-800K.dsk` — the same known-bootable
System 6.0.8 volume the release ships, with ArtfulType already on it.

## How it works

1. Waits for a disk-inserted event.
2. `DIFormat`s the inserted disk to its native 800K GCR layout and `DIVerify`s it.
3. Raw-writes every logical block of the embedded image straight to the `.Sony`
   floppy driver with `PBWrite` — bypassing the File Manager, because it is
   *cloning a volume*, not mounting one. The result is byte-identical to the
   source `.dsk`, so the boot blocks and blessed System Folder come across intact.
4. Ejects. Re-inserting the disk mounts a fresh, bootable ArtfulType volume.

It logs each step and every Toolbox error code to its window, so it can be
driven blind over the network without a screen-share.

## Using it (Mac SE / System 6)

1. Build it (below) and send it to the Mac with LaunchAPPL:
   ```
   LaunchAPPL -e tcp --tcp-address <mac-ip> ArtfulTypeFloppyWriter.bin
   ```
2. When its window appears, insert a **blank 800K DD floppy** (an 800K drive
   like the SE's cannot use 1.44 MB HD media).
3. Confirm the **Erase** prompt. Watch the progress bar; it ejects when done.
4. Re-insert the finished disk to boot from it, or set it as the startup disk.

## Building

Needs a Retro68 **m68k** toolchain and the 800K image to embed:

```sh
# from this directory, after ./build-boot-images.sh has produced dist/
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=<retro68>/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
make
```

The image defaults to `../../dist/ArtfulType-800K.dsk`; override with
`-DAT_DISK_IMAGE=/path/to/ArtfulType-800K.dsk`. The build embeds the image via
Rez's `$$read`, so **rebuild after regenerating `dist/`** to pick up a new app
version.

## Scope

System 6 / 800K only, by design — it targets the compact-Mac use case
(a Mac SE with an 800K drive). A System 7.x / 1.44 MB variant is possible but
out of scope here; see the repo's task list.

## Safety

It **erases** the inserted disk. It only ever touches the disk you insert after
launching it (via the disk-inserted event), never a mounted hard drive.
