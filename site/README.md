# Product site

A static, playful product page with an interactive 3D StickS3. The screen is
not a recording: `avatar.wasm` runs the Stick's own frame code
(`firmware/avatar_frame.h`, through the desktop avatar in
`experiments/avatar-widget/main.cpp`), compiled to WebAssembly by
`scripts/build-site-wasm.sh`. Wrapper: `site/wasm/avatar_wasm.cpp`.

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
- `social-card.png`: the 1200 × 630 link preview (`og:image`) shown when the
  URL is pasted into Teams, Slack, X, etc. It is a committed screenshot of
  `scripts/social-card.html`, whose screen is a real `avatar.wasm` frame;
  re-render with `./scripts/render-social-card.sh` (any Chromium-family
  browser) after changing the face or the headline.

Deployment: `.github/workflows/pages.yml` rebuilds `avatar.wasm` and publishes
`site/` to GitHub Pages (Settings → Pages → Source: GitHub Actions).
Three.js and fonts load from jsDelivr / Google Fonts.
