#!/usr/bin/env bash
set -Eeuo pipefail
# The documented analysis stack is reproducible; never mix client/server versions.
VERSION=17.26.3
TOOLS_VERSION=14.13.1
URL="https://github.com/Jordan231111/ae-patch-verifier/releases/download/build-tools-frida-$VERSION/undetected-frida-server-$VERSION-android-arm64.xz"
DIGEST=ac0b20ebd20b4b15e87e6bcdc1e29f8c697f2d44d7262db80de99ed9a61f9302
curl --fail --location --silent --show-error --retry 2 "$URL" -o "$RUNNER_TEMP/uf-server.xz"
printf '%s  %s\n' "$DIGEST" "$RUNNER_TEMP/uf-server.xz" | sha256sum --check
xz -dc "$RUNNER_TEMP/uf-server.xz" > "$RUNNER_TEMP/uf-server"
python -m pip install --disable-pip-version-check --quiet "frida==$VERSION" "frida-tools==$TOOLS_VERSION" pyelftools==0.33
adb -s 127.0.0.1:5555 push "$RUNNER_TEMP/uf-server" /data/local/tmp/uf-server
adb -s 127.0.0.1:5555 shell chmod 700 /data/local/tmp/uf-server
[ "$(adb -s 127.0.0.1:5555 shell /data/local/tmp/uf-server --version | tr -d '\r')" = "$VERSION" ]
adb -s 127.0.0.1:5555 shell '/data/local/tmp/uf-server -D </dev/null >/dev/null 2>&1'
sha256sum "$RUNNER_TEMP/uf-server"
