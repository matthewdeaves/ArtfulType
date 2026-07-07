# Base System volume

`system6.dsk` is a minimal, already-bootable **System 6.0.8** HFS floppy
volume (800 KB): a System Folder containing just `System` and `Finder`, with
~78 KB free. `build-boot-images.sh` uses it two ways:

- as the base for the bootable 800K app floppy (it is already blessed, so the
  build only adds `ArtfulType` and the guide);
- as the source of the `System` + `Finder` files, and of the boot blocks, that
  `tools/bless_hfs.py` installs into the freshly-formatted 20 MB volume.

## Provenance

This volume is the test asset shipped with Retro68
(`.github/test-assets/system6.dsk`), the cross-compiler ArtfulType is built
with. It is committed here so the disk-image build is hermetic and runs
unattended in CI — no Mac and no externally-fetched system software required.

## Licensing

The volume contains Apple system software (System 6.0.8). Apple has never
issued a formal redistribution licence for System 6/7, but the classic-Mac
community — and the upstream project this repo is forked from
([ActionRetro/ArtfulType](https://github.com/ActionRetro/ArtfulType)), which
ships the same System 6.0.8 on its own release disk images — treats these
decades-old, unsupported releases as freely shared for hobby and preservation
use. This fork follows that same established practice. The software remains
Apple's; it is included here solely to produce a ready-to-run disk for real
classic hardware.
