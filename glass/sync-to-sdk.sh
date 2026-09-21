#!/bin/sh
# Copy the plugin into GlassSDK/examples (Studio only discovers real
# directories there, not symlinks) and build it in place.
set -e
SDK=${GLASS_SDK:-$HOME/Projects/plugin-open-platform/GlassSDK}
SRC="$(cd "$(dirname "$0")/x3timer" && pwd)"
mkdir -p "$SDK/examples/x3timer"
cp "$SRC/manifest.json" "$SRC/x3timer.c" "$SRC/README.md" "$SDK/examples/x3timer/"
cd "$SDK" && "$SDK/../.venv/bin/python" build.py build --example x3timer
