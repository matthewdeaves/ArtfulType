#!/bin/bash
# Builds every bootable ArtfulType disk image from scratch on Linux -- no
# Mac required. Everything here runs with just hfsutils, djjr, and python3,
# so the whole set is reproducible in GitHub Actions (see release.yml).
#
# Produced in dist/:
#   ArtfulType-800K.dsk    bootable 800K floppy, raw HFS (System 6.0.8 + app
#                          + guide) -- for Mini vMac or a flux floppy writer.
#   ArtfulType-800K.image  the same floppy wrapped in a DiskCopy 4.2 header
#                          -- for Disk Copy 4.2 on a Mac, or Mini vMac.
#   ArtfulType-20MB.dsk    bootable 20 MB HFS volume -- for Mini vMac.
#   HD1_ArtfulType.hda     bootable 20 MB BlueSCSI/PiSCSI device image (Apple
#                          partition map + HD SC 7.3.5 driver). The HD1_
#                          prefix is BlueSCSI's SCSI-ID-1 convention: drop it
#                          on the SD card as-is, no renaming.
#   START_HERE.md          the getting-started guide, shipped alongside.
#
# The 800K floppy simply adds the app to the already-blessed base volume.
# The 20 MB images are hand-built: a fresh HFS volume is formatted, the
# System Folder is installed, and the volume is blessed (boot blocks + the
# blessed-folder ID in the MDB) by tools/bless_hfs.py -- the two things
# hformat does not do. See that script and the CLAUDE.md "Building bootable
# disk images on Linux" note.
#
# NOTE: the images embed Apple system software (System 6.0.8, from the base
# volume). See disk-base/README.md for provenance.
set -euo pipefail

cd "$(dirname "$0")"

BASE="${AT_SYSTEM_BASE:-disk-base/system6.dsk}"
BIN="app/build/ArtfulType.bin"
GUIDE="doc/START_HERE.md"
DIST="dist"
FLOPPY_NAME="The Artful Type"
VOL_NAME="The Artful Type"

for tool in hformat hmount hmkdir hcd hcopy hattrib humount djjr python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: $tool not found on PATH" >&2; exit 1; }
done
[ -f "$BASE" ]  || { echo "error: base System volume not found: $BASE" >&2; exit 1; }
[ -f "$BIN" ]   || { echo "error: build the app first ($BIN missing)" >&2; exit 1; }
[ -f "$GUIDE" ] || { echo "error: guide not found: $GUIDE" >&2; exit 1; }

rm -rf "$DIST"
mkdir -p "$DIST"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "==> Extracting System + Finder from the base volume"
hmount "$BASE" >/dev/null
hcd "System Folder"
hcopy -m :System "$WORK/System.bin"
hcopy -m :Finder "$WORK/Finder.bin"
humount >/dev/null

# --- 800K bootable floppy -----------------------------------------------
# The base is already a blessed, bootable System 6 volume; just add the app
# and guide and wrap it in a DiskCopy 4.2 header.
echo "==> Building 800K bootable floppy"
cp "$BASE" "$WORK/floppy.dsk"
chmod +w "$WORK/floppy.dsk"
hmount "$WORK/floppy.dsk" >/dev/null
hcopy -m "$BIN" :ArtfulType
hcopy -t "$GUIDE" :START_HERE.md
hattrib -t TEXT -c ArtT :START_HERE.md
humount >/dev/null
# Capture djjr's output first: a bare `djjr … | grep -q` under `pipefail`
# can SIGPIPE djjr (grep closes the pipe on match) and report a false failure.
floppy_report="$(djjr analyze "$WORK/floppy.dsk")"
grep -q bootable <<<"$floppy_report" \
    || { echo "error: 800K floppy not bootable" >&2; exit 1; }
cp "$WORK/floppy.dsk" "$DIST/ArtfulType-800K.dsk"
python3 make_diskcopy_image.py "$WORK/floppy.dsk" "$DIST/ArtfulType-800K.image" "$FLOPPY_NAME"

# --- 20 MB bootable volume ----------------------------------------------
echo "==> Building 20 MB bootable volume"
VOL="$WORK/vol20.dsk"
dd if=/dev/zero of="$VOL" bs=1024 count=20480 status=none
hformat -l "$VOL_NAME" "$VOL" >/dev/null

# On a freshly formatted HFS volume the next catalog node ID is 16, so the
# first object created gets ID 16 deterministically. We create the System
# Folder first and bless that ID. Assert the invariant rather than trust it.
next_cnid="$(python3 -c "import struct,sys;print(struct.unpack('>I',open(sys.argv[1],'rb').read()[1054:1058])[0])" "$VOL")"
[ "$next_cnid" = "16" ] || { echo "error: unexpected drNxtCNID $next_cnid on fresh volume" >&2; exit 1; }

hmount "$VOL" >/dev/null
hmkdir "System Folder"                 # <- becomes dir ID 16
hcopy -m "$BIN" :ArtfulType            # root-level files while cwd is root
hcopy -t "$GUIDE" :START_HERE.md       #   (hcd : does NOT return to root)
hattrib -t TEXT -c ArtT :START_HERE.md
hcd "System Folder"
hcopy -m "$WORK/System.bin" :System
hcopy -m "$WORK/Finder.bin" :Finder
humount >/dev/null

python3 tools/bless_hfs.py "$VOL" "$BASE" 16 >/dev/null
vol_report="$(djjr analyze "$VOL")"
grep -q bootable <<<"$vol_report" \
    || { echo "error: 20 MB volume not bootable after blessing" >&2; exit 1; }
cp "$VOL" "$DIST/ArtfulType-20MB.dsk"

# --- 20 MB BlueSCSI device image ----------------------------------------
echo "==> Wrapping into a BlueSCSI device image"
djjr convert to-device "$VOL" "$DIST/HD1_ArtfulType.hda" >/dev/null

# --- guide, shipped alongside like upstream -----------------------------
cp "$GUIDE" "$DIST/START_HERE.md"

echo
echo "==> Verifying outputs"
# Assert the actual on-disk boot structures, not just djjr's flag (which is
# derived from the boot signature and so is near-tautological after blessing):
# boot signature 'LK', MDB signature 'BD', blessed folder ID == 16, and the
# boot blocks byte-identical to the known-bootable base. This catches a
# mis-write or an off-by-one in the offsets; it does NOT replace a real boot
# test on hardware (see the note below).
python3 - "$DIST/ArtfulType-20MB.dsk" "$BASE" <<'PY'
import struct, sys
vol = open(sys.argv[1], 'rb').read()
base = open(sys.argv[2], 'rb').read()
def need(cond, msg):
    if not cond:
        sys.exit("error: 20 MB volume failed structural check: " + msg)
need(struct.unpack('>H', vol[0:2])[0] == 0x4C4B, "no 'LK' boot signature")
need(vol[0:1024] == base[0:1024], "boot blocks differ from base")
need(struct.unpack('>H', vol[1024:1026])[0] == 0x4244, "no 'BD' MDB signature")
need(struct.unpack('>I', vol[1116:1120])[0] == 16, "blessed folder ID != 16")
print("  structure OK: boot blocks + blessed folder ID == 16")
PY
raw_report="$(djjr analyze "$DIST/ArtfulType-20MB.dsk")"
grep -q bootable <<<"$raw_report" \
    || { echo "error: ArtfulType-20MB.dsk not bootable" >&2; exit 1; }
echo "  bootable OK: $DIST/ArtfulType-20MB.dsk"
# A device image (.hda) prints a partition map, not a "(bootable)" flag, so
# it's checked for the wrapped HFS partition.
hda_report="$(djjr analyze "$DIST/HD1_ArtfulType.hda")"
grep -q 'HFS Volume' <<<"$hda_report" \
    || { echo "error: HD1_ArtfulType.hda has no HFS partition" >&2; exit 1; }
echo "  HFS partition OK: $DIST/HD1_ArtfulType.hda"
echo
echo "Done. dist/ contents:"
ls -l "$DIST"
echo
echo "NOTE: the 20 MB images are blessed on Linux and structurally verified,"
echo "but not boot-tested here. The 800K floppy derives from a known-bootable"
echo "System 6 base. Boot-test on real hardware or an emulator before relying"
echo "on the .hda in production."
