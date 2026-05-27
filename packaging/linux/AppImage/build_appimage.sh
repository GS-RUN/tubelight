#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
#
# Build a Tubelight AppImage for Linux distribution.
#
# Prerequisites:
#   - linuxdeploy from https://github.com/linuxdeploy/linuxdeploy
#   - tubelight built in <repo>/build/linux-ninja/
#
# Output: tubelight-<version>-x86_64.AppImage in the current directory.

set -euo pipefail

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    echo "Usage: $0 [build-dir]"
    echo "  Default build-dir: <repo>/build/linux-ninja"
    exit 0
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." &>/dev/null && pwd)"
BUILD_DIR="${1:-$REPO_ROOT/build/linux-ninja}"

if [ ! -f "$BUILD_DIR/tubelight" ]; then
    echo "error: tubelight binary not found at $BUILD_DIR/tubelight" >&2
    echo "Build first with: cmake --preset linux-ninja && cmake --build $BUILD_DIR" >&2
    exit 2
fi

if ! command -v linuxdeploy &>/dev/null; then
    echo "error: linuxdeploy not in PATH. Get it from:" >&2
    echo "  https://github.com/linuxdeploy/linuxdeploy/releases" >&2
    exit 3
fi

WORK_DIR="$(mktemp -d)"
APPDIR="$WORK_DIR/AppDir"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/lib/tubelight"
mkdir -p "$APPDIR/usr/share/tubelight/shaders"
mkdir -p "$APPDIR/usr/share/tubelight/profiles"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$BUILD_DIR/tubelight"                       "$APPDIR/usr/bin/"
cp "$BUILD_DIR/libtubelight_preload.so"         "$APPDIR/usr/lib/tubelight/" || true
cp "$BUILD_DIR/libVkLayer_tubelight_overlay.so" "$APPDIR/usr/lib/tubelight/" || true
cp "$BUILD_DIR/VkLayer_tubelight_overlay.json"  "$APPDIR/usr/lib/tubelight/" || true

cp -r "$REPO_ROOT/shaders/."  "$APPDIR/usr/share/tubelight/shaders/"
cp -r "$REPO_ROOT/profiles/." "$APPDIR/usr/share/tubelight/profiles/"

cat > "$APPDIR/usr/share/applications/tubelight.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Tubelight
Comment=High-fidelity CRT shader overlay
Exec=tubelight
Icon=tubelight
Categories=Graphics;Utility;
StartupNotify=true
DESKTOP

# Placeholder icon (replace before official release).
if [ -f "$REPO_ROOT/packaging/linux/icon.png" ]; then
    cp "$REPO_ROOT/packaging/linux/icon.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/tubelight.png"
fi

cd "$WORK_DIR"
linuxdeploy --appdir "$APPDIR" --output appimage

mv tubelight*.AppImage "$REPO_ROOT/" || mv Tubelight*.AppImage "$REPO_ROOT/"
echo "AppImage written to $REPO_ROOT/"

rm -rf "$WORK_DIR"
