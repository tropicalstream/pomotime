#!/bin/sh
# Host-side engine test. The stock CommandLineTools linker chokes on the
# macOS 27 SDK, so pin an older sysroot that it understands.
set -e
cd "$(dirname "$0")"
SDK=${SDK:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk}
INC=${GLASS_SDK:-$HOME/Projects/plugin-open-platform/GlassSDK}/include
OUT=${TMPDIR:-/tmp}/x3timer_host
cc -isysroot "$SDK" -std=gnu99 -Wall -Wno-unused-function -I "$INC" -o "$OUT" host_test.c
"$OUT"
