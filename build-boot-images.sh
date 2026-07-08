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
# Both the 800K floppy and the 20 MB volume are hand-built the same way
# (make_blessed_volume): a fresh HFS volume named "ArtfulType" is formatted,
# the System Folder + app + guide are installed, and the volume is blessed
# (boot blocks + the blessed-folder ID in the MDB) by tools/bless_hfs.py --
# the two things hformat does not do. The System, Finder and boot blocks all
# come verbatim from the proven-bootable base volume, so only the container is
# freshly made. See tools/bless_hfs.py and the CLAUDE.md "Building bootable
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
VOL_NAME="ArtfulType"          # HFS volume name for every image
ROOT="$(pwd)"                  # repo root (for absolute paths when we cd elsewhere)

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

# The app ships an ICN#/FREF/BNDL, but the Finder only installs those icons
# into a volume's Desktop file when the app advertises a bundle (the hasBundle
# Finder flag) and has not yet been "inited". Retro68's build leaves the flags
# at 0, so a freshly-copied ArtfulType shows the generic application icon. Set
# hasBundle (0x2000) and clear hasBeenInited (0x0100) in the MacBinary header
# before importing, and fix the MacBinary CRC so the header stays valid. hcopy
# then carries these onto the volume, and the Finder paints the real icon on
# first mount.
APPBIN="$WORK/ArtfulType.bin"
python3 - "$BIN" "$APPBIN" <<'PY'
import sys
src = bytearray(open(sys.argv[1], "rb").read())
src[73] |= 0x20          # hasBundle  (flags bits 15-8)
src[73] &= ~0x01         # clear hasBeenInited (bit 8)
def crc16(data):         # MacBinary II CRC-16-CCITT over bytes 0..123
    crc = 0
    for ch in data:
        crc ^= ch << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc
c = crc16(bytes(src[0:124]))
src[124] = (c >> 8) & 0xFF
src[125] = c & 0xFF
open(sys.argv[2], "wb").write(src)
PY

# Build a fresh, blessed, bootable HFS volume with ArtfulType on it.
#   $1 = output .dsk   $2 = size in KiB   $3 = volume name
# On a freshly formatted HFS volume the next catalog node ID is 16, so the
# first object created gets ID 16 deterministically. We create the System
# Folder first and bless that ID; the invariant is asserted, not trusted.
make_blessed_volume() {
    local out="$1" size_kb="$2" name="$3" cnid report

    dd if=/dev/zero of="$out" bs=1024 count="$size_kb" status=none
    hformat -l "$name" "$out" >/dev/null

    cnid="$(python3 -c "import struct,sys;print(struct.unpack('>I',open(sys.argv[1],'rb').read()[1054:1058])[0])" "$out")"
    [ "$cnid" = "16" ] || { echo "error: unexpected drNxtCNID $cnid on fresh $out" >&2; exit 1; }

    hmount "$out" >/dev/null
    hmkdir "System Folder"                 # <- becomes dir ID 16
    hcopy -m "$APPBIN" :ArtfulType         # root-level files while cwd is root
    hcopy -t "$GUIDE" :START_HERE.md       #   (hcd : does NOT return to root)
    hattrib -t TEXT -c ArtT :START_HERE.md
    hcd "System Folder"
    hcopy -m "$WORK/System.bin" :System
    hcopy -m "$WORK/Finder.bin" :Finder
    humount >/dev/null

    python3 tools/bless_hfs.py "$out" "$BASE" 16 >/dev/null
    # Capture djjr's output first: a bare `djjr … | grep -q` under `pipefail`
    # can SIGPIPE djjr (grep closes the pipe on match) and report a false failure.
    report="$(djjr analyze "$out")"
    # Match djjr's literal "(bootable)" token -- a plain `bootable` substring
    # would also match "not bootable".
    grep -qF '(bootable)' <<<"$report" \
        || { echo "error: $out not bootable after blessing" >&2; exit 1; }
}

# --- 800K bootable floppy -----------------------------------------------
echo "==> Building 800K bootable floppy"
make_blessed_volume "$WORK/floppy.dsk" 800 "$VOL_NAME"
cp "$WORK/floppy.dsk" "$DIST/ArtfulType-800K.dsk"
python3 make_diskcopy_image.py "$WORK/floppy.dsk" "$DIST/ArtfulType-800K.image" "$VOL_NAME"

# --- 20 MB bootable volume ----------------------------------------------
echo "==> Building 20 MB bootable volume"
make_blessed_volume "$WORK/vol20.dsk" 20480 "$VOL_NAME"
cp "$WORK/vol20.dsk" "$DIST/ArtfulType-20MB.dsk"

# --- 20 MB BlueSCSI device image ----------------------------------------
echo "==> Wrapping into a BlueSCSI device image"
# `djjr convert` drops a scratch UUID directory in its cwd; run it inside $WORK
# (cleaned by the trap) with absolute paths so nothing lands in the repo root.
( cd "$WORK" && djjr convert to-device "$WORK/vol20.dsk" "$ROOT/$DIST/HD1_ArtfulType.hda" >/dev/null )

# --- guide, shipped alongside like upstream -----------------------------
cp "$GUIDE" "$DIST/START_HERE.md"

echo
echo "==> Verifying outputs"
# Assert the actual on-disk structures for each blessed .dsk, not just djjr's
# flag (which is derived from the boot signature and so is near-tautological
# after blessing): boot signature 'LK', MDB signature 'BD', blessed folder ID
# == 16, boot blocks byte-identical to the known-bootable base, and the volume
# name. This catches a mis-write or an off-by-one in the offsets; it does NOT
# replace a real boot test on hardware (see the note below).
verify_volume() {
    python3 - "$1" "$BASE" "$VOL_NAME" <<'PY'
import struct, sys
vol  = open(sys.argv[1], 'rb').read()
base = open(sys.argv[2], 'rb').read()
want = sys.argv[3].encode()
def need(cond, msg):
    if not cond:
        sys.exit("error: %s failed structural check: %s" % (sys.argv[1], msg))
need(struct.unpack('>H', vol[0:2])[0] == 0x4C4B, "no 'LK' boot signature")
need(vol[0:1024] == base[0:1024], "boot blocks differ from base")
need(struct.unpack('>H', vol[1024:1026])[0] == 0x4244, "no 'BD' MDB signature")
need(struct.unpack('>I', vol[1116:1120])[0] == 16, "blessed folder ID != 16")
n = vol[1060]
need(vol[1061:1061 + n] == want, "volume name is %r, want %r" % (vol[1061:1061 + n], want))
print("  structure OK (%s): boot blocks + blessed ID 16 + name %r" % (sys.argv[1], want.decode()))
PY
}
verify_volume "$DIST/ArtfulType-800K.dsk"
verify_volume "$DIST/ArtfulType-20MB.dsk"

# A device image (.hda) prints a partition map, not a "(bootable)" flag, so
# it's checked for the wrapped HFS partition.
hda_report="$(djjr analyze "$DIST/HD1_ArtfulType.hda")"
grep -q 'HFS Volume' <<<"$hda_report" \
    || { echo "error: HD1_ArtfulType.hda has no HFS partition" >&2; exit 1; }
echo "  HFS partition OK: $DIST/HD1_ArtfulType.hda"

# The app must advertise its bundle (hasBundle set, hasBeenInited clear) or the
# Finder paints the generic application icon instead of ArtfulType's ICN#.
hmount "$DIST/ArtfulType-800K.dsk" >/dev/null
hcopy -m :ArtfulType "$WORK/icon_check.bin"
humount >/dev/null
python3 - "$WORK/icon_check.bin" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
flags = (d[73] << 8) | d[101]
if not (flags & 0x2000) or (flags & 0x0100):
    sys.exit("error: app icon will be generic -- hasBundle unset or inited set (flags=0x%04X)" % flags)
print("  icon OK: app advertises its bundle (flags=0x%04X)" % flags)
PY

echo
echo "Done. dist/ contents:"
ls -l "$DIST"
echo
echo "NOTE: these volumes are blessed on Linux and structurally verified, but"
echo "only the 800K floppy is boot-tested on real hardware (a Mac SE). The 20 MB"
echo ".hda is validated by mounting under an emulator; boot-test it on a real"
echo "compact Mac (BlueSCSI) before relying on it in production."
