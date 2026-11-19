#!/usr/bin/env bash
# Build site/avatar.wasm from the shared firmware rendering headers.
# Requires wasi-sdk (https://github.com/WebAssembly/wasi-sdk). Set WASI_SDK_PATH
# or install it at /opt/wasi-sdk. The renderer keeps ~160KB of frame scratch on
# the stack (as on the native preview), so the default 64KB wasm stack is raised.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
sdk="${WASI_SDK_PATH:-/opt/wasi-sdk}"
"$sdk/bin/clang++" --target=wasm32-wasip1 --sysroot="$sdk/share/wasi-sysroot" \
  -std=c++17 -O2 -fno-exceptions -fno-rtti -flto \
  -mexec-model=reactor \
  -Wl,--strip-all -Wl,--gc-sections -Wl,-z,stack-size=1048576 \
  -o "$root/site/avatar.wasm" "$root/site/wasm/avatar_wasm.cpp"
echo "Built $root/site/avatar.wasm ($(wc -c <"$root/site/avatar.wasm") bytes)"
