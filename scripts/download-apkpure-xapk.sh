#!/usr/bin/env bash
set -Eeuo pipefail

apkpure_error() {
  echo "[apkpure] error: $*" >&2
}

if [ "$#" -ne 5 ]; then
  apkpure_error "usage: $0 PACKAGE ARCHITECTURE VERSION_CODE VERSION_NAME OUTPUT_DIR"
  exit 2
fi

PACKAGE_NAME=$1
ARCHITECTURE=$2
VERSION_CODE=$3
VERSION_NAME=$4
OUTPUT_DIR=$5

[[ "$PACKAGE_NAME" =~ ^[A-Za-z0-9._-]+$ ]] || {
  apkpure_error "invalid package name: $PACKAGE_NAME"
  exit 2
}
[[ "$ARCHITECTURE" == 'arm64-v8a' || "$ARCHITECTURE" == 'x86_64' ]] || {
  apkpure_error "unsupported architecture: $ARCHITECTURE"
  exit 2
}
[[ "$VERSION_CODE" =~ ^[1-9][0-9]*$ ]] || {
  apkpure_error "invalid version code: $VERSION_CODE"
  exit 2
}
[ -n "$VERSION_NAME" ] || {
  apkpure_error "version name is required"
  exit 2
}

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)
XAPK_FILE="$(dirname "$OUTPUT_DIR")/source.xapk"
rm -f "$XAPK_FILE" "$XAPK_FILE.aria2"
mkdir -p "$OUTPUT_DIR"
if find "$OUTPUT_DIR" -mindepth 1 -print -quit | grep -q .; then
  apkpure_error "output directory must be empty: $OUTPUT_DIR"
  exit 2
fi

resolve_variant() {
  node - "$REPO_ROOT/api/_shared/apkpure.js" \
    "$PACKAGE_NAME" "$VERSION_CODE" "$ARCHITECTURE" "$VERSION_NAME" <<'NODE'
const resolverPath = process.argv[2];
const packageName = process.argv[3];
const versionCode = process.argv[4];
const architecture = process.argv[5];
const versionName = process.argv[6];
const { resolveXapkVariant } = require(resolverPath);

resolveXapkVariant(packageName, versionCode, architecture, versionName)
  .then(result => {
    if (/\s/.test(result.downloadUrl)) {
      throw new Error("APKPure returned an unsafe download URL");
    }
    process.stdout.write(`${result.downloadUrl}\t${result.fullSize}\n`);
  })
  .catch(error => {
    console.error(error.message);
    process.exitCode = 1;
  });
NODE
}

refresh_variant() {
  local resolved
  if ! resolved=$(resolve_variant); then
    return 1
  fi
  IFS=$'\t' read -r XAPK_URL EXPECTED_SIZE <<< "$resolved"
  [[ "$XAPK_URL" == https://* ]] || return 1
  [[ "$EXPECTED_SIZE" =~ ^[1-9][0-9]*$ ]] || return 1
}

download_archive() {
  if command -v aria2c >/dev/null 2>&1; then
    aria2c \
      --console-log-level=warn --summary-interval=0 \
      --max-connection-per-server=16 --split=16 --min-split-size=2M \
      --file-allocation=none --max-tries=3 --retry-wait=2 \
      --connect-timeout=20 --timeout=120 \
      --allow-overwrite=true --auto-file-renaming=false \
      --user-agent='Mozilla/5.0 AE Patch Builder' \
      --dir="$(dirname "$XAPK_FILE")" --out="$(basename "$XAPK_FILE")" \
      "$XAPK_URL"
  else
    curl --fail --silent --show-error \
      --retry 3 --retry-all-errors --retry-delay 2 \
      --connect-timeout 20 --max-time 600 \
      -A 'Mozilla/5.0 AE Patch Builder' \
      -o "$XAPK_FILE" "$XAPK_URL"
  fi
}

validate_archive() {
  local actual_size magic
  if [ ! -s "$XAPK_FILE" ]; then
    apkpure_error "download produced an empty file"
    return 1
  fi

  actual_size=$(wc -c < "$XAPK_FILE" | tr -d '[:space:]')
  if [ "$actual_size" != "$EXPECTED_SIZE" ]; then
    apkpure_error "incomplete download: expected $EXPECTED_SIZE bytes, got $actual_size"
    return 1
  fi

  magic=$(head -c 4 "$XAPK_FILE" | od -An -tx1 | tr -d ' \n')
  if [ "$magic" != '504b0304' ]; then
    apkpure_error "download is not a ZIP/XAPK archive (signature $magic)"
    return 1
  fi
}

if ! refresh_variant; then
  apkpure_error "could not resolve a validated $ARCHITECTURE archive; refusing a mismatched ABI"
  exit 1
fi

downloaded=false
for attempt in 1 2 3; do
  echo "[apkpure] attempt $attempt: version=$VERSION_NAME code=$VERSION_CODE abi=$ARCHITECTURE bytes=$EXPECTED_SIZE"
  if download_archive && validate_archive; then
    downloaded=true
    break
  fi

  rm -f "$XAPK_FILE" "$XAPK_FILE.aria2"
  if [ "$attempt" -lt 3 ]; then
    sleep $((attempt * 2))
    if ! refresh_variant; then
      apkpure_error "could not refresh the signed download URL"
      exit 1
    fi
  fi
done

if [ "$downloaded" != true ]; then
  apkpure_error "download failed validation after 3 attempts"
  exit 1
fi

if ! unzip -Z1 "$XAPK_FILE" | awk '
  /^\// || /(^|\/)\.\.(\/|$)/ { unsafe = 1 }
  END { exit unsafe }
'; then
  apkpure_error "XAPK contains an unsafe archive path"
  exit 1
fi
unzip -q -o "$XAPK_FILE" -d "$OUTPUT_DIR"

MANIFEST="$OUTPUT_DIR/manifest.json"
if [ ! -s "$MANIFEST" ]; then
  apkpure_error "XAPK does not contain manifest.json"
  exit 1
fi
if ! jq -e --arg package "$PACKAGE_NAME" --arg code "$VERSION_CODE" '
  .package_name == $package and
  (.version_code | tostring) == $code and
  (.split_apks | type) == "array" and
  (.split_apks | length) > 1 and
  any(.split_apks[]; .id == "base")
' "$MANIFEST" >/dev/null; then
  apkpure_error "XAPK manifest does not match $PACKAGE_NAME versionCode $VERSION_CODE"
  exit 1
fi

EXPECTED_SPLIT="config.${ARCHITECTURE//-/_}"
if ! jq -e --arg split "$EXPECTED_SPLIT" \
  'any(.split_apks[]; .id == $split)' "$MANIFEST" >/dev/null; then
  available=$(jq -r '[.split_apks[].id] | join(", ")' "$MANIFEST" 2>/dev/null || true)
  apkpure_error "XAPK is missing $EXPECTED_SPLIT; available splits: ${available:-<unreadable>}"
  exit 1
fi

while IFS= read -r split_file; do
  if ! [[ "$split_file" =~ ^[A-Za-z0-9._-]+\.apk$ ]]; then
    apkpure_error "unsafe split filename in manifest: $split_file"
    exit 1
  fi
  if [ ! -s "$OUTPUT_DIR/$split_file" ]; then
    apkpure_error "manifest references missing split: $split_file"
    exit 1
  fi
done < <(jq -r '.split_apks[].file' "$MANIFEST")

ACTUAL_NAME=$(jq -r '.version_name // empty' "$MANIFEST")
if [ "$VERSION_NAME" != unknown ] && [ "$ACTUAL_NAME" != "$VERSION_NAME" ]; then
  apkpure_error "XAPK version is ${ACTUAL_NAME:-<missing>}, expected $VERSION_NAME"
  exit 1
fi

printf 'VERSION_CODE=%q\nVERSION_NAME=%q\nSOURCE_KIND=apkpure\n' \
  "$VERSION_CODE" "${ACTUAL_NAME:-$VERSION_NAME}" > "$OUTPUT_DIR/source.env"
echo "[source] APKPure delivered ${ACTUAL_NAME:-$VERSION_NAME} code=$VERSION_CODE abi=$ARCHITECTURE"
