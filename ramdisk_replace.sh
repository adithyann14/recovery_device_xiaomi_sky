#!/bin/bash
# FOX_LOCAL_CALLBACK_SCRIPT
# Source: device/xiaomi/sky/etc/  (flat, just filenames)
# Target: searches entire ramdisk for matching filename and replaces

RAMDISK_PATH="$1"
CALL_TYPE="$2"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEVICE_ETC="$SCRIPT_DIR/etc"

log() { echo "[fox_callback] $*"; }

if [ "$CALL_TYPE" = "--first-call" ]; then
    log "=== Ramdisk root : $RAMDISK_PATH"
    log "=== Source etc   : $DEVICE_ETC"

    if [ ! -d "$DEVICE_ETC" ]; then
        log "ERROR: $DEVICE_ETC not found"
        exit 1
    fi

    if [ ! -d "$RAMDISK_PATH" ]; then
        log "ERROR: ramdisk path $RAMDISK_PATH not found"
        exit 1
    fi

    found_any=0

    # Loop every file in device etc/ (top level only — flat folder)
    for src_file in "$DEVICE_ETC"/*; do
        [ -f "$src_file" ] || continue

        filename="$(basename "$src_file")"

        # Find ALL matches of this filename anywhere in the ramdisk
        matches=()
        while IFS= read -r -d '' match; do
            matches+=("$match")
        done < <(find "$RAMDISK_PATH" -type f -name "$filename" -print0)

        if [ "${#matches[@]}" -eq 0 ]; then
            log "SKIP (not found in ramdisk): $filename"
            continue
        fi

        for dst in "${matches[@]}"; do
            rel="${dst#"$RAMDISK_PATH"/}"
            log "Replacing: $rel  <--  etc/$filename"
            cp -f "$src_file" "$dst"
            found_any=1
        done
    done

    [ "$found_any" -eq 0 ] && log "WARNING: no files were replaced"
    log "=== Done ==="

elif [ "$CALL_TYPE" = "--last-call" ]; then
    log "Last call — skipping"
fi
