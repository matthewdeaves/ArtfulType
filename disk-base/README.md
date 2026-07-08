# Base System volume

`system6.dsk` is a minimal, already-bootable **System 6.0.8** HFS floppy
volume (800 KB): a System Folder containing just `System` and `Finder`, with
~78 KB free. [`build-boot-images.sh`](../build-boot-images.sh) uses it as the
source of the `System` and `Finder` files, and of the boot blocks, that
[`tools/bless_hfs.py`](../tools/bless_hfs.py) installs into each volume it builds. Both output images — the 800K floppy and the 20 MB
volume — are freshly `hformat`'d and then blessed this way; nothing is copied
from `system6.dsk` wholesale, only its System Folder contents and its
proven-bootable boot blocks.

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
