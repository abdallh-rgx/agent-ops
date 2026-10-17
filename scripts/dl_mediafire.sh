#!/usr/bin/env bash
# Download src.zip from MediaFire and extract.
set -u
URL="$1"; OUT="${2:-$HOME/fortnite-src}"
mkdir -p "$OUT"
echo "[mf] $(date -u +%H:%M:%S) page: $URL"
DIRECT=""
for i in 1 2 3 4 5; do
  PAGE=$(curl -sL --connect-timeout 25 --max-time 90 "$URL" || true)
  DIRECT=$(printf '%s' "$PAGE" | grep -oE 'https://download[0-9]+\.mediafire\.com/[^"'"'"']+' | head -1)
  [ -n "$DIRECT" ] && break
  echo "[mf] retry $i"; sleep 8
done
[ -z "$DIRECT" ] && { echo "[mf] ERROR: no direct link"; exit 1; }
echo "[mf] direct: $DIRECT"
for a in 1 2 3; do
  curl -L --connect-timeout 25 --max-time 3600 --retry 2 -C - -o "$OUT/src.zip" "$DIRECT" && break
  sleep 5
done
[ -s "$OUT/src.zip" ] || { echo "[mf] ERROR: zip missing"; exit 1; }
echo "[mf] downloaded: $(du -h "$OUT/src.zip" | cut -f1)"
cd "$OUT" && unzip -o -q src.zip && echo "[mf] extracted"
ls -la "$OUT" | head -40
echo "[mf] ALL DONE"
