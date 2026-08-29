#!/usr/bin/env bash
# Builds dist/SynthCard.bin (app image -> M5Launcher OTA) and
# dist/SynthCard-merged.bin (M5Burner / esptool full-flash image).
# Usage: tools/package.sh [output-dir]
set -euo pipefail

SKETCH="SynthCard"
FQBN="m5stack:esp32:m5stack_cardputer:PSRAM=disabled"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/dist}"
BUILD="$ROOT/build"
APP_NAME="SynthCard"
FLASH_SIZE="8MB"; FLASH_MODE="dio"; FLASH_FREQ="80m"
# The board's default partition scheme gives each OTA slot 1.25 MB.
MAX_APP_BYTES=$((1310720))

cd "$ROOT"
rm -rf "$BUILD"
arduino-cli compile --fqbn "$FQBN" --output-dir "$BUILD" "$SKETCH"

APP="$BUILD/$SKETCH.ino.bin"
BOOTLOADER="$BUILD/$SKETCH.ino.bootloader.bin"
PARTITIONS="$BUILD/$SKETCH.ino.partitions.bin"

app_size=$(stat -c%s "$APP")
echo "app image: $app_size bytes (limit $MAX_APP_BYTES)"
if [ "$app_size" -gt "$MAX_APP_BYTES" ]; then
    echo "ERROR: app image will not fit an OTA partition" >&2
    exit 1
fi

mkdir -p "$OUT"
cp "$APP" "$OUT/$APP_NAME.bin"

BOOT_APP0="$(find "$HOME/.arduino15/packages/m5stack" -name boot_app0.bin 2>/dev/null | head -1)"
ESPTOOL="$(find "$HOME/.arduino15/packages" -type f -name esptool -perm -u+x 2>/dev/null | head -1)"
[ -z "$ESPTOOL" ] && command -v esptool.py >/dev/null 2>&1 && ESPTOOL="esptool.py"
[ -z "$ESPTOOL" ] && command -v esptool >/dev/null 2>&1 && ESPTOOL="esptool"

if [ -n "$BOOT_APP0" ] && [ -n "$ESPTOOL" ]; then
    "$ESPTOOL" --chip esp32s3 merge_bin -o "$OUT/$APP_NAME-merged.bin" \
        --flash_mode "$FLASH_MODE" --flash_freq "$FLASH_FREQ" --flash_size "$FLASH_SIZE" \
        0x0 "$BOOTLOADER" 0x8000 "$PARTITIONS" 0xe000 "$BOOT_APP0" 0x10000 "$APP"
elif [ -f "$BUILD/$SKETCH.ino.merged.bin" ]; then
    cp "$BUILD/$SKETCH.ino.merged.bin" "$OUT/$APP_NAME-merged.bin"
else
    echo "note: no merged image (esptool not found)" >&2
fi

echo; echo "built:"; ls -l "$OUT"
