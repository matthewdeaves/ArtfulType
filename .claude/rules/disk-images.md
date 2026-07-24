---
paths:
  - "*.sh"
  - "*.py"
  - "tools/**"
  - "disk-base/**"
  - ".github/workflows/**"
---

# Building bootable disk images & deploy tooling

Loaded when working on the build/deploy scripts, `tools/`, `disk-base/`, or the
workflows. The images are formatted and *blessed* in software on Linux — no Mac.

- [`build-boot-images.sh`](../../build-boot-images.sh) — the primary, **hermetic** disk builder: produces every
  bootable image (800K floppy, 20 MB volume, `HD1_*.hda`) from scratch on Linux
  with no Mac. `release.yml` runs it, so `git tag vX.Y.Z && git push --tags`
  builds and publishes the whole release. Needs `hfsutils`, `djjr`, `python3`.
- [`deploy.sh`](../../deploy.sh) — older path: copies `ArtfulType.bin` + the guide onto *pre-built*
  20 MB and 800 K base images under `vmac/` (a fast Mini vMac test loop); wraps
  the floppy in a DiskCopy 4.2 header ([`make_diskcopy_image.py`](../../make_diskcopy_image.py)).
- [`build-bluescsi-image.sh`](../../build-bluescsi-image.sh) — converts a 20 MB `vmac/` volume to a BlueSCSI device
  image with `djjr`. [`package-release.sh`](../../package-release.sh) runs deploy + this, staging into `Disks/`.
- [`tools/floppy-writer/`](../../tools/floppy-writer/) — a throwaway System-6-safe 68k app that **embeds the
  800K image** (via Rez `$$read`) and writes it to an inserted floppy on real
  hardware: `DIFormat`/`DIVerify` then a raw `PBWrite` clone to the `.Sony`
  driver. It exists because `LaunchAPPL` can ship an *app* to a bare Mac but not
  a disk image, so this is how a Mac SE with only a System makes its own bootable
  ArtfulType floppy. Hardware-validated on a real Mac SE (deploy: `LaunchAPPL -e
  tcp --tcp-address <mac-ip> ArtfulTypeFloppyWriter.bin`, then insert a blank
  800K DD floppy — it writes and boots). **`release.yml` now builds and ships it**
  (the `build-floppy-writer` job runs in the Retro68 container after
  `build-disks` and embeds that release's freshly built `dist/ArtfulType-800K.dsk`,
  so the attached `ArtfulTypeFloppyWriter.bin` can never carry a stale image);
  building from source is only for embedding a different/in-dev image. See its README.

- **Blessing an HFS volume in software (no Mac).** `hformat` makes a mountable
  volume but *not a bootable one*: it writes neither the boot blocks nor the
  blessed-folder ID a ROM needs to find the System file. [`tools/bless_hfs.py`](../../tools/bless_hfs.py)
  supplies both — it copies the 1024-byte boot blocks (`'LK'`/0x4C4B) verbatim
  from a known-bootable System volume, and writes the System Folder's directory
  ID into the MDB's `drFndrInfo[0]` (MDB at logical block 2 = byte 1024; the
  field is at MDB offset 92, so byte 1116). The blessed dir ID is made
  deterministic by creating the System Folder *first* on a fresh volume, where
  the next catalog node ID is always 16 (asserted from `drNxtCNID`, MDB offset
  30). `hcd :` does **not** return to the volume root in hfsutils, so root-level
  files are copied while cwd is still root, before descending into the folder.
  The committed System 6.0.8 base lives in [`disk-base/`](../../disk-base/) (git-ignored `*.dsk`
  except that one file). djjr reports a raw volume as `(bootable)` off the boot
  signature; a *device* image (`.hda`) instead prints its partition map, so
  verify it via the wrapped `HFS Volume` line, not a `bootable` grep. The 800K
  floppy and the 20 MB volume are built the *same* way (`make_blessed_volume`),
  both named `ArtfulType`; only the container is fresh — System, Finder and the
  boot blocks come verbatim from the base. **Getting the app's icon to show is
  its own problem on a Mac-less build:** the normal route (the Finder installs
  the app's `BNDL` into the volume's Desktop database on first mount, given the
  `hasBundle` flag) needs a Desktop database the hermetic build never creates,
  and on real hardware it didn't fire. So the app also carries a copy of its
  icon at the custom-icon resource ID `-16455` (`app/main.r`), and the build
  sets **both** `hasCustomIcon` (0x0400) and `hasBundle` (0x2000) — clearing
  `hasBeenInited` and fixing the MacBinary CRC — in the app's Finder flags
  before `hcopy`. The Finder draws a `-16455` custom icon straight off the file,
  no Desktop database required. The 800K floppy is **boot-tested on a real Mac
  SE** (System 6.0.8, booting and launching ArtfulType, volume named
  `ArtfulType`); the 20 MB `.hda` is structurally verified and validated by
  mounting under an emulator, but not System-6 boot-tested there (a Quadra-class
  `qemu-system-m68k` can't run System 6). *(The custom-icon fix itself still
  needs a hardware boot test to confirm the icon appears; tracked in issue #8.)*
