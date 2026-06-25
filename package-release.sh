#!/bin/bash
# Builds everything fresh and stages release-ready disk images in
# Disks/ with clean names. Disks/ is covered by the existing *.dsk
# and *.hda .gitignore patterns -- this is local staging, not
# something committed to git or published anywhere by itself.
set -e

cd "$(dirname "$0")"

DISKS_DIR="Disks"

echo "=== Building and deploying to test images ==="
./deploy.sh

echo
echo "=== Converting to a BlueSCSI device image ==="
./build-bluescsi-image.sh

echo
echo "=== Staging release artifacts in $DISKS_DIR/ ==="
mkdir -p "$DISKS_DIR"
cp "vmac/The Artful Type.hda" "$DISKS_DIR/ArtfulType.hda"
cp "vmac/ArtfulType_20M.dsk" "$DISKS_DIR/ArtfulType-20MB.dsk"
cp "vmac/Mac-800K.dsk" "$DISKS_DIR/ArtfulType-800K.dsk"

echo
echo "Done. Contents of $DISKS_DIR/:"
ls -la "$DISKS_DIR"
