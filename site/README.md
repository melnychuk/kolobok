# Product site

A static, playful product page with an interactive 3D StickS3. The screen is
not a recording: `avatar.wasm` is the header-widget compositor
(`experiments/avatar-widget/main.cpp`) and every shared `firmware/*.h` kernel,
compiled to WebAssembly by `scripts/build-site-wasm.sh`. Wrapper:
`site/wasm/avatar_wasm.cpp` (no firmware source changes).

```sh
# Rebuild the renderer after changing firmware headers (needs wasi-sdk 25)
WASI_SDK_PATH=/path/to/wasi-sdk ./scripts/build-site-wasm.sh

# Preview locally (module scripts and wasm need http://)
python3 -m http.server -d site 8000
```

- `js/avatar.js`: wasm loader, 33ms fixed-step firmware clock, frame copy.
- `js/device.js`: Three.js model (48 × 24 × 15 mm, stylized), live screen
  texture, pressable A button, IMU flip, Blueprint outline view with dimensions.
- `js/main.js`: status controls, scenarios, performance library, chime.

Deployment: `.github/workflows/pages.yml` rebuilds `avatar.wasm` and publishes
`site/` to GitHub Pages (Settings → Pages → Source: GitHub Actions).
Three.js and fonts load from jsDelivr / Google Fonts.
