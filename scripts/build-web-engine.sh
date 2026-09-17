#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ -z "${AE_NATIVE_DIR:-}" ]]; then
  exec python3 "$repo_dir/scripts/native_staging.py"
fi
native_dir=$AE_NATIVE_DIR
# Pin both the SDK checkout and the compiler. Build outputs are deployment assets,
# regenerated from the reviewed imported C++ source; no game libraries are shipped.
if ! command -v em++ >/dev/null 2>&1; then
  sdk_dir=$(mktemp -d)
  trap 'rm -rf -- "$sdk_dir"' EXIT
  curl --fail --location --retry 3 --silent --show-error \
    https://github.com/emscripten-core/emsdk/archive/5eb0bde7585670252e8ba05e9d361627bffd08b5.tar.gz \
    | tar -xz -C "$sdk_dir" --strip-components=1
  "$sdk_dir/emsdk" install 6.0.9
  "$sdk_dir/emsdk" activate 6.0.9
  source "$sdk_dir/emsdk_env.sh"
fi
compiler_version=$(em++ --version | head -n 1)
if [[ ! "$compiler_version" =~ [[:space:]]6\.0\.9(-git)?([[:space:]]|$) ]]; then
  echo "Emscripten 6.0.9 is required; found: $compiler_version" >&2
  exit 2
fi
em++ -std=c++20 -O3 -msimd128 -fno-exceptions -fno-rtti \
  "$native_dir/engine.cpp" \
  -sMEMORY64=2 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=1073741824 \
  -sMODULARIZE=1 -sEXPORT_NAME=createAENative -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 \
  '-sEXPORTED_RUNTIME_METHODS=["FS","callMain"]' \
  -o "$native_dir/engine.js"
node "$repo_dir/scripts/write-build-info.js"
