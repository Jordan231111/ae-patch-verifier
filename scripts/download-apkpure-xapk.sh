#!/usr/bin/env bash
set -Eeuo pipefail

PACKAGE_NAME=$1
ARCHITECTURE=$2
VERSION_CODE=$3
VERSION_NAME=$4
OUTPUT_DIR=$5

[[ "$PACKAGE_NAME" =~ ^[A-Za-z0-9._-]+$ ]]
[[ "$ARCHITECTURE" == 'arm64-v8a' || "$ARCHITECTURE" == 'x86_64' ]]
[[ "$VERSION_CODE" =~ ^[1-9][0-9]*$ ]]
mkdir -p "$OUTPUT_DIR"

XAPK_FILE="$(dirname "$OUTPUT_DIR")/source.xapk"
XAPK_URL="https://d.apkpure.net/b/XAPK/${PACKAGE_NAME}?versionCode=${VERSION_CODE}&nc=${ARCHITECTURE}"
rm -f "$XAPK_FILE" "$XAPK_FILE.aria2"

attempt=0
until [ "$attempt" -ge 5 ]; do
  attempt=$((attempt + 1))
  echo "[apkpure] attempt $attempt: version=$VERSION_NAME code=$VERSION_CODE abi=$ARCHITECTURE"
  if command -v aria2c >/dev/null 2>&1; then
    aria2c \
      --console-log-level=warn --summary-interval=0 \
      --max-connection-per-server=16 --split=16 --min-split-size=2M \
      --file-allocation=none --max-tries=3 --retry-wait=2 \
      --connect-timeout=20 --timeout=120 \
      --allow-overwrite=true --auto-file-renaming=false \
      --user-agent='Mozilla/5.0 AE Patch Builder' \
      --dir="$(dirname "$XAPK_FILE")" --out="$(basename "$XAPK_FILE")" \
      "$XAPK_URL" && break
  else
    curl -L --fail --silent --show-error \
      --retry 3 --retry-all-errors --retry-delay 2 \
      --connect-timeout 20 --max-time 600 \
      -A 'Mozilla/5.0 AE Patch Builder' \
      -o "$XAPK_FILE" "$XAPK_URL" && break
  fi
  rm -f "$XAPK_FILE" "$XAPK_FILE.aria2"
done

test -s "$XAPK_FILE"
[ "$(head -c 4 "$XAPK_FILE" | od -An -tx1 | tr -d ' \n')" = '504b0304' ]
unzip -tq "$XAPK_FILE" >/dev/null
unzip -q -o "$XAPK_FILE" -d "$OUTPUT_DIR"

MANIFEST="$OUTPUT_DIR/manifest.json"
test -s "$MANIFEST"
jq -e --arg package "$PACKAGE_NAME" --arg code "$VERSION_CODE" \
  '.package_name == $package and (.version_code | tostring) == $code' "$MANIFEST" >/dev/null
EXPECTED_SPLIT="config.${ARCHITECTURE//-/_}"
jq -e --arg split "$EXPECTED_SPLIT" 'any(.split_apks[]; .id == $split)' "$MANIFEST" >/dev/null
ACTUAL_NAME=$(jq -r '.version_name // empty' "$MANIFEST")
[ -z "$VERSION_NAME" ] || [ "$VERSION_NAME" = unknown ] || [ "$ACTUAL_NAME" = "$VERSION_NAME" ]

printf 'VERSION_CODE=%q\nVERSION_NAME=%q\nSOURCE_KIND=apkpure\n' \
  "$VERSION_CODE" "${ACTUAL_NAME:-$VERSION_NAME}" > "$OUTPUT_DIR/source.env"
echo "[source] APKPure delivered ${ACTUAL_NAME:-$VERSION_NAME} code=$VERSION_CODE"
