#!/usr/bin/env bash
set -Eeuo pipefail
# The server and host client are resolved together; no game or Frida version whitelist.
curl --fail --silent --show-error --retry 2 \
  https://api.github.com/repos/zer0def/undetected-frida/releases/latest > "$RUNNER_TEMP/frida-release.json"
VERSION=$(jq -er '.tag_name | select(test("^[0-9]+\\.[0-9]+\\.[0-9]+$"))' "$RUNNER_TEMP/frida-release.json")
ASSET=$(jq -cer '[.assets[] | select(.name | test("server-.*-android-arm64.xz$"))] | select(length == 1) | .[0]' "$RUNNER_TEMP/frida-release.json")
URL=$(jq -er .browser_download_url <<< "$ASSET")
[[ "$URL" == https://github.com/zer0def/undetected-frida/releases/download/* ]]
curl --fail --location --silent --show-error --retry 2 "$URL" -o "$RUNNER_TEMP/uf-server.xz"
DIGEST=$(jq -r '.digest // empty' <<< "$ASSET")
if [ -n "$DIGEST" ]; then
  [[ "$DIGEST" =~ ^sha256:[a-f0-9]{64}$ ]]
  printf '%s  %s\n' "${DIGEST#sha256:}" "$RUNNER_TEMP/uf-server.xz" | sha256sum --check
fi
xz -dc "$RUNNER_TEMP/uf-server.xz" > "$RUNNER_TEMP/uf-server"
python -m pip install --disable-pip-version-check --quiet "frida==$VERSION" frida-tools pyelftools==0.33
adb -s 127.0.0.1:5555 push "$RUNNER_TEMP/uf-server" /data/local/tmp/uf-server
adb -s 127.0.0.1:5555 shell chmod 700 /data/local/tmp/uf-server
adb -s 127.0.0.1:5555 shell '/data/local/tmp/uf-server -D </dev/null >/dev/null 2>&1'
sha256sum "$RUNNER_TEMP/uf-server"
