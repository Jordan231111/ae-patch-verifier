#!/usr/bin/env bash
set -Eeuo pipefail
gh run download "$GITHUB_RUN_ID" --repo "$GITHUB_REPOSITORY" --name relevel-inputs --dir "$RUNNER_TEMP/inputs"
mkdir -p "$RUNNER_TEMP/tools" "$RUNNER_TEMP/candidate" "$RUNNER_TEMP/bin" "$RUNNER_TEMP/smali"
openssl enc -d -aes-256-cbc -pbkdf2 -iter 200000 -pass env:RELEVEL_CI_TRANSFER_KEY \
  -in "$RUNNER_TEMP/inputs/private.enc" | tar -xz -C "$RUNNER_TEMP/tools"
export RELEVEL_KEYSTORE="$RUNNER_TEMP/host.keystore"
trap 'rm -f "$RELEVEL_KEYSTORE"' EXIT
printf '%s' "$KEYSTORE_BASE64" | base64 --decode > "$RELEVEL_KEYSTORE"
chmod 600 "$RELEVEL_KEYSTORE"
unset KEYSTORE_BASE64 RELEVEL_CI_TRANSFER_KEY
export RELEVEL_MODULE_APK="$RUNNER_TEMP/tools/module/app-release.apk"
export RELEVEL_LSPATCH_JAR="$RUNNER_TEMP/lspatch.jar"
curl --fail --location --silent --show-error --retry 2 \
  https://github.com/JingMatrix/LSPatch/releases/download/v1.2/lspatch-v1.2-487-release.jar -o "$RELEVEL_LSPATCH_JAR"
sudo apt-get update -qq
sudo apt-get install -y -qq patchelf ripgrep maven
python -m pip install --disable-pip-version-check --quiet androguard==4.1.3 pyelftools==0.33
mvn -q -f scripts/relevel/smali-pom.xml dependency:copy-dependencies -DoutputDirectory="$RUNNER_TEMP/smali"
for TOOL in smali baksmali; do
  MAIN=$TOOL
  if [ "$TOOL" = baksmali ]; then MAIN=baksmali; fi
  printf '#!/usr/bin/env bash\nexec "%s/bin/java" -cp "%s/*" com.android.tools.smali.%s.Main "$@"\n' \
    "$JAVA_HOME" "$RUNNER_TEMP/smali" "$MAIN" > "$RUNNER_TEMP/bin/$TOOL"
  chmod 700 "$RUNNER_TEMP/bin/$TOOL"
done
export PATH="$RUNNER_TEMP/bin:$PATH"
ARGS=()
if [ -n "$RECOVERED_TAG" ]; then
  mkdir "$RUNNER_TEMP/recovered"
  tar -xf "$RUNNER_TEMP/inputs/recovered.tar" -C "$RUNNER_TEMP/recovered"
  ARGS+=(--recovered "$RUNNER_TEMP/recovered")
else
  gh run download "$GITHUB_RUN_ID" --repo "$GITHUB_REPOSITORY" --name relevel-capture --dir "$RUNNER_TEMP/capture"
  ARGS+=(--capture "$RUNNER_TEMP/capture" --cache-output "$RUNNER_TEMP/recovered")
fi
python "$RUNNER_TEMP/tools/tools/cloud/assemble.py" "${ARGS[@]}" \
  --output "$RUNNER_TEMP/candidate/ReLevel-${VERSION}-arm64-lspatched.apks" --metadata "$RUNNER_TEMP/candidate/bundle.json"
if [ -z "$RECOVERED_TAG" ]; then tar -cf "$RUNNER_TEMP/candidate/recovered.tar" -C "$RUNNER_TEMP/recovered" .; fi
