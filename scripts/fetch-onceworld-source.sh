#!/usr/bin/env bash

# This script is sourced by the OnceWorld workflow. It prepares the same
# xapk/manifest.json contract regardless of source so the established
# LSPatch, signing, alignment, and packaging pipeline stays source-agnostic.

onceworld_source_error() {
  echo "[source] $*" >&2
}

onceworld_resolve_apkpure_latest() {
  node - "$PACKAGE_NAME" <<'NODE'
const { resolveLatestXapk } = require("./api/_shared/apkpure.js");
resolveLatestXapk(process.argv[2])
  .then(value => process.stdout.write(`${value.versionCode}\t${value.versionName || "unknown"}\n`))
  .catch(error => { console.error(error.message); process.exitCode = 1; });
NODE
}

onceworld_prepare_google_play() (
  set -Eeuo pipefail

  if [ -z "${GPLAYDL_API_KEY:-}" ]; then
    onceworld_source_error "Google Play credential is not configured"
    return 1
  fi

  local ready_dir="$WORK/google-play-ready"
  mkdir -p "$ready_dir"

  echo "[source] trying Google Play first with parallel compressed CDN transfers"
  python3 -m pip install --disable-pip-version-check --no-compile --quiet 'gplaydl==4.2.1'
  python3 scripts/download-onceworld-play.py \
    --package "$PACKAGE_NAME" \
    --architecture "$ARCHITECTURE" \
    --expected-version "$EXPECTED_VERSION_NAME" \
    --metadata-source "$METADATA_SOURCE" \
    --output "$ready_dir"
)

onceworld_prepare_apkpure() (
  set -Eeuo pipefail

  local fallback_code=${FALLBACK_VERSION_CODE:-0}
  local fallback_name=${FALLBACK_VERSION_NAME:-unknown}
  if ! [[ "$fallback_code" =~ ^[1-9][0-9]*$ ]]; then
    echo "[source] resolving APKPure only after the Google Play attempt failed"
    IFS=$'\t' read -r fallback_code fallback_name < <(onceworld_resolve_apkpure_latest)
  fi
  [[ "$fallback_code" =~ ^[1-9][0-9]*$ ]] || {
    onceworld_source_error "APKPure fallback did not resolve a valid version code"
    return 1
  }

  if [ "$METADATA_SOURCE" = "google-play" ] \
    && [ "$EXPECTED_VERSION_NAME" != "unknown" ] \
    && [ "$fallback_name" != "$EXPECTED_VERSION_NAME" ]; then
    onceworld_source_error "APKPure is still on $fallback_name while Google Play advertises $EXPECTED_VERSION_NAME"
    return 1
  fi

  local stage_dir="$WORK/apkpure-ready"
  local xapk_file="$WORK/apkpure-source.xapk"
  local xapk_partial="$xapk_file.aria2"
  local xapk_url="https://d.apkpure.net/b/XAPK/${PACKAGE_NAME}?versionCode=${fallback_code}&nc=${ARCHITECTURE}"
  mkdir -p "$stage_dir"
  rm -f "$xapk_file" "$xapk_partial"

  local attempt=0
  until [ "$attempt" -ge 5 ]; do
    attempt=$((attempt + 1))
    echo "[apkpure] attempt $attempt: version=$fallback_name code=$fallback_code abi=$ARCHITECTURE"
    if command -v aria2c >/dev/null 2>&1; then
      aria2c \
        --console-log-level=warn \
        --summary-interval=0 \
        --max-connection-per-server=16 \
        --split=16 \
        --min-split-size=2M \
        --file-allocation=none \
        --max-tries=3 \
        --retry-wait=2 \
        --connect-timeout=20 \
        --timeout=120 \
        --allow-overwrite=true \
        --auto-file-renaming=false \
        --user-agent='Mozilla/5.0 LSPatch Workshop' \
        --dir="$WORK" \
        --out="$(basename "$xapk_file")" \
        "$xapk_url" && break
    else
      curl -L --fail --silent --show-error \
        --retry 3 --retry-all-errors --retry-delay 2 \
        --connect-timeout 20 --max-time 600 \
        -A 'Mozilla/5.0 LSPatch Workshop' \
        -o "$xapk_file" "$xapk_url" && break
    fi
    rm -f "$xapk_file" "$xapk_partial"
  done

  test -s "$xapk_file"
  [ "$(head -c 4 "$xapk_file" | od -An -tx1 | tr -d ' \n')" = '504b0304' ]
  unzip -tq "$xapk_file" >/dev/null
  unzip -q -o "$xapk_file" -d "$stage_dir"

  local manifest="$stage_dir/manifest.json"
  test -s "$manifest"
  jq -e --arg package "$PACKAGE_NAME" --arg code "$fallback_code" \
    '.package_name == $package and (.version_code | tostring) == $code' \
    "$manifest" >/dev/null
  local expected_split="config.${ARCHITECTURE//-/_}"
  jq -e --arg split "$expected_split" \
    'any(.split_apks[]; .id == $split)' "$manifest" >/dev/null

  local manifest_name
  manifest_name=$(jq -r '.version_name // empty' "$manifest")
  if [ -n "$manifest_name" ]; then
    fallback_name=$manifest_name
  fi
  if [ "$METADATA_SOURCE" = "google-play" ] \
    && [ "$EXPECTED_VERSION_NAME" != "unknown" ] \
    && [ "$fallback_name" != "$EXPECTED_VERSION_NAME" ]; then
    onceworld_source_error "APKPure archive is $fallback_name while Google Play advertises $EXPECTED_VERSION_NAME"
    return 1
  fi

  printf 'VERSION_CODE=%q\nVERSION_NAME=%q\nSOURCE_KIND=%q\n' \
    "$fallback_code" "$fallback_name" "apkpure" \
    > "$stage_dir/source.env"
)

onceworld_commit_source() {
  local ready_dir=$1
  mkdir -p "$WORK/xapk"
  cp -R "$ready_dir/." "$WORK/xapk/"
  # shellcheck disable=SC1090
  source "$ready_dir/source.env"
  export VERSION_CODE VERSION_NAME SOURCE_KIND
  echo "[source] selected $SOURCE_KIND version=$VERSION_NAME code=$VERSION_CODE"
}

onceworld_fetch_source_main() {
  if onceworld_prepare_google_play; then
    onceworld_commit_source "$WORK/google-play-ready"
  else
    local play_status=$?
    echo "[source] Google Play primary failed (exit $play_status); trying the APKPure fallback" >&2
    if onceworld_prepare_apkpure; then
      onceworld_commit_source "$WORK/apkpure-ready"
    else
      local fallback_status=$?
      onceworld_source_error "APKPure fallback failed (exit $fallback_status)"
      return "$fallback_status"
    fi
  fi

  {
    echo "VERSION_CODE=$VERSION_CODE"
    echo "VERSION_NAME=$VERSION_NAME"
    echo "SOURCE_KIND=$SOURCE_KIND"
  } >> "$GITHUB_ENV"
}

onceworld_fetch_source_main
