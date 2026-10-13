#!/bin/bash
# Explicit, administrator-only deployment. Never invoked by the build or tests.
set -euo pipefail
target=/Library/Audio/Plug-Ins/HAL/RaneSixtyFourDuplex.driver
case "${1:-}" in
    --install|--rollback) ;;
    *) echo "Usage: sudo bash $0 --install /absolute/path/RaneSixtyFourDuplex.driver | --rollback"; exit 2 ;;
esac
if [ "$EUID" -ne 0 ]; then echo 'Administrator authorization is required.' >&2; exit 1; fi
if [ "$1" = --install ]; then
    source_bundle="${2:?Pass the built, signed bundle path}"
    [ -d "$source_bundle" ] && [ ! -L "$source_bundle" ]
    [ ! -e "$target" ] && [ ! -L "$target" ] || { echo 'Existing target: refusing to overwrite.' >&2; exit 1; }
    identifier=$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$source_bundle/Contents/Info.plist")
    [ "$identifier" = org.openrane.sixtyfour.duplex.hal ]
    /usr/bin/codesign --verify --strict "$source_bundle"
    /usr/bin/lipo "$source_bundle/Contents/MacOS/RaneSixtyFourDuplex" -verify_arch arm64
    /bin/mkdir -p /Library/Audio/Plug-Ins/HAL
    staging=$(/usr/bin/mktemp -d /Library/Audio/Plug-Ins/HAL/.sixty-four-duplex-stage.XXXXXX)
    /usr/bin/ditto "$source_bundle" "$staging/RaneSixtyFourDuplex.driver"
    /usr/sbin/chown -R root:wheel "$staging/RaneSixtyFourDuplex.driver"
    /bin/chmod -R go-w "$staging/RaneSixtyFourDuplex.driver"
    /usr/bin/codesign --verify --strict "$staging/RaneSixtyFourDuplex.driver"
    /bin/mv "$staging/RaneSixtyFourDuplex.driver" "$target"
    /bin/rmdir "$staging"
    echo "Installed $target"
else
    [ -d "$target" ] && [ ! -L "$target" ] || { echo 'Driver is not installed.' >&2; exit 1; }
    identifier=$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$target/Contents/Info.plist")
    [ "$identifier" = org.openrane.sixtyfour.duplex.hal ]
    /bin/mkdir -p /Library/Audio/Plug-Ins/HAL-disabled
    archive=$(/usr/bin/mktemp -d /Library/Audio/Plug-Ins/HAL-disabled/sixty-four-duplex.XXXXXX)
    /bin/mv "$target" "$archive/RaneSixtyFourDuplex.driver"
    echo "Disabled driver retained at $archive/RaneSixtyFourDuplex.driver"
fi
echo 'Restarting macOS audio service; current audio streams will be interrupted.'
/usr/bin/killall coreaudiod
