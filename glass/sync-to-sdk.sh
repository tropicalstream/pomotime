#!/bin/sh
# Copy the plugin into GlassSDK/examples (Studio only discovers real
# directories there, not symlinks) and build it in place.
set -e
SDK=${GLASS_SDK:-$HOME/Projects/plugin-open-platform/GlassSDK}
SRC="$(cd "$(dirname "$0")/pomotime" && pwd)"
mkdir -p "$SDK/examples/pomotime"
cp "$SRC/manifest.json" "$SRC/pomotime.c" "$SRC/README.md" "$SDK/examples/pomotime/"
cd "$SDK" && "$SDK/../.venv/bin/python" build.py build --example pomotime
