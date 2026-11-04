#!/system/bin/sh
# aera_logcopy.sh - keep copying AERA recovery logs to /sdcard until stopped or the device hangs.
#
# Run (adb shell or terminal in recovery):
#   sh /sdcard/aera_logcopy.sh &        (or: nohup sh /sdcard/aera_logcopy.sh >/dev/null 2>&1 &)
# Stop:
#   sh /sdcard/aera_logcopy.sh stop
#
# Output: <sdcard>/aera-logs-<date>/
#   logcat/logcat.txt     live logcat (all buffers)
#   logcat/kernel.log     live kernel log (dmesg)
#   tmp/                  copies of /tmp/aera* , /tmp/*.log , /tmp/aera-cellular/*
#   heartbeat.txt         last line = last time the loop was alive (shows WHEN it hung)
#   copy_errors.txt       files that timed out/failed to copy (shows WHAT hung)

INTERVAL=${INTERVAL:-2}          # seconds between copy rounds
MAXSIZE=${MAXSIZE:-52428800}     # skip files larger than 50 MB
PIDFILE=/tmp/aera_logcopy.pid

if [ "$1" = "stop" ]; then
  if [ -f "$PIDFILE" ]; then
    for p in $(cat "$PIDFILE"); do kill "$p" 2>/dev/null; done
    rm -f "$PIDFILE"
    echo "stopped"
  else
    echo "not running"
  fi
  exit 0
fi

# pick a writable destination
DEST=""
for d in ${DEST_OVERRIDE:-} /sdcard /data/media/0 /storage/emulated/0 /data/media; do
  [ -n "$d" ] && [ -d "$d" ] || continue
  if touch "$d/.aera_w" 2>/dev/null; then rm -f "$d/.aera_w"; DEST="$d"; break; fi
done
if [ -z "$DEST" ]; then
  echo "No writable /sdcard or /data/media - is data mounted/decrypted?" >&2
  exit 1
fi

BASE="$DEST/aera-logs-$(date +%Y%m%d_%H%M%S)"
mkdir -p "$BASE/logcat" "$BASE/tmp" || exit 1
HB="$BASE/heartbeat.txt"
ERR="$BASE/copy_errors.txt"
SRC=${SRC_TMP:-/tmp}
echo "writing to $BASE"

# ---- background streamers (own processes, so a stuck copy never blocks them) ----
logcat -b all -v threadtime >> "$BASE/logcat/logcat.txt" 2>&1 &
P1=$!
# dmesg -w may not exist everywhere; fall back to snapshotting in the loop
DMESG_W=0
( dmesg -w >> "$BASE/logcat/kernel.log" 2>/dev/null ) &
P2=$!
sleep 1
if ! kill -0 "$P2" 2>/dev/null; then DMESG_W=1; fi

echo "$$ $P1 $P2" > "$PIDFILE"

copy_one() {
  # $1 = source file. Regular files only (skips sockets/fifos/locks that block cp).
  f="$1"
  [ -f "$f" ] || return 0
  [ -p "$f" ] && return 0
  sz=$(wc -c < "$f" 2>/dev/null) || return 0
  [ -n "$sz" ] && [ "$sz" -gt "$MAXSIZE" ] && return 0
  rel=$(echo "$f" | sed "s|^$SRC/||; s|/|__|g")
  # timeout so one stuck file can't freeze the whole loop
  if ! timeout 3 cat "$f" > "$BASE/tmp/$rel.part" 2>/dev/null; then
    echo "$(date +%T) TIMEOUT/FAIL $f" >> "$ERR"
    rm -f "$BASE/tmp/$rel.part"
    return 0
  fi
  mv -f "$BASE/tmp/$rel.part" "$BASE/tmp/$rel"
}

n=0
while :; do
  n=$((n+1))
  echo "$n $(date +%T) alive" >> "$HB"

  for f in "$SRC"/aera* "$SRC"/*.log "$SRC"/aera-cellular/* "$SRC"/aera-cellular/*/* ; do
    copy_one "$f"
  done
  # recovery log + props
  copy_one /tmp/recovery.log
  copy_one /cache/recovery/last_log

  if [ "$DMESG_W" = 1 ]; then
    timeout 3 dmesg > "$BASE/logcat/kernel.log" 2>/dev/null
  fi

  # flush to disk every ~5 s, in the background with a timeout, so a hang
  # keeps as much as possible but sync itself can't block the loop
  if [ $((n % 3)) -eq 0 ]; then
    ( timeout 10 sync ) >/dev/null 2>&1 &
  fi
  sleep "$INTERVAL"
done
