#!/usr/bin/env bash
# Builds the Haiku package (.hpkg) on Haiku. Run from the repository root.
set -euo pipefail
TA_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$TA_ROOT"
NAME=$(awk '$1 == "name" { print $2; exit }' resources/TasAmp.PackageInfo)
VERSION=$(awk '$1 == "version" { print $2; exit }' resources/TasAmp.PackageInfo)
ARCH=$(awk '$1 == "architecture" { print $2; exit }' resources/TasAmp.PackageInfo)
FILE="$TA_ROOT/artifacts/$NAME-$VERSION-$ARCH.hpkg"
make -j6
STAGE=$(mktemp -d /tmp/tasamp-package-XXXXXX)
trap 'rm -rf -- "$STAGE"' EXIT
mkdir -p "$STAGE/apps" "$STAGE/documentation/packages/tasamp/vendor/nlohmann" \
    "$STAGE/data/deskbar/menu/Applications" "$STAGE/boot/post-install" "$TA_ROOT/artifacts"
cp build-haiku/TasAmp "$STAGE/apps/TasAmp"
strip --strip-debug "$STAGE/apps/TasAmp"
# GNU strip removes the appended Haiku resources; restore them.
xres -o "$STAGE/apps/TasAmp" build-haiku/TasAmp.rsrc
cp resources/TasAmp.PackageInfo "$STAGE/.PackageInfo"
cp README.md LICENSE "$STAGE/documentation/packages/tasamp/"
cp -R docs "$STAGE/documentation/packages/tasamp/"
cp vendor/nlohmann/LICENSE.MIT vendor/nlohmann/UPSTREAM.md "$STAGE/documentation/packages/tasamp/vendor/nlohmann/"
cp resources/tasamp-post-install.sh "$STAGE/boot/post-install/tasamp.sh"
ln -s ../../../../apps/TasAmp "$STAGE/data/deskbar/menu/Applications/TasAmp"
( cd "$STAGE" && mimeset --all -f --mimedb data/mime_db --mimedb /boot/system/data/mime_db apps/TasAmp )
package create -C "$STAGE" "$FILE"
printf '%s\n' "$FILE"
