# Plan: a pixel image element in Herdr for the Stick widget

Status: proposal. Nothing here is implemented yet. It needs a change to Herdr
Desktop (our fork: `github.com/melnychuk/herdr-app`), so it ships only in a
locally built Herdr until it is accepted upstream.

## Why

The widget redraws each 240 × 135 frame as filled rectangles (`window.paint_path`)
because a Herdr mod has no way to show pixels from memory: `image(path)` only
loads files and caches them by path. Every rectangle costs four host calls on
Herdr's main thread, and every color is another element.

The widget display options (`stick_set_display`) cut that work enough to show
every frame at full resolution, but they do it by giving up quality. With them,
on an isolated Herdr (two working agents and one blocked, 15 frames a second):

| | rectangles (before) | rectangles (display options) | one image (this plan) |
| --- | --- | --- | --- |
| resolution | half in active states | full | full |
| edges | reduced palette | one soft step or hard | the Stick's own anti-aliasing |
| status field | gradient approximated natively | gradient approximated natively | the Stick's own dithered field |
| script time per render | 10.1 ms | 5.4 ms | expected well under 1 ms |

An image element would show exactly what the Stick shows (the same bytes the
site draws), and the per-frame cost would no longer depend on what is on screen.

## What Herdr already has

- GPUI draws in-memory images through `ImageSource::Render(Arc<RenderImage>)`
  (`vendor/gpui-pre/src/elements/img.rs`). Unlike a path or URL, it is used in
  the same frame: no asynchronous load, so no flicker between frames.
- `App::drop_image(image, window)` removes an image's texture from every
  window's sprite atlas (`vendor/gpui-pre/src/app.rs`). Herdr already uses it to
  evict changed files (`invalidate_image` in `src/window.rs`).
- Image bounds are snapped to device pixels before sampling
  (`Window::paint_image`), so a 240 × 135 image in a 120 × 67.5-point box on a
  2× display maps one image pixel to one screen pixel.
- gpui-kit keeps per-view state across renders in its component registry
  (`vendor/gpui-kit/crates/shell/src/component_registry.rs`), which is where a
  reusable image belongs.

## Proposed API (gpui-kit)

A small retained surface, so a mod replaces pixels rather than creating a new
image (and a new atlas texture) every frame:

```js
import { PixelImage, pixel_image } from "gpui-kit";

// In init(): one surface per view.
this.screen = PixelImage.new(240, 135);

// Whenever a frame arrives (RGB or RGBA, row-major, width × height pixels):
this.screen.write(bytes, "rgb");
cx.notify();

// In render(): an ordinary sized element.
pixel_image(this.screen).w(120).h(67.5);
```

- `PixelImage.new(width, height)`: at most 4096 × 4096. It belongs to the view
  that made it and is freed with it.
- `write(bytes, format)`: `format` is `"rgb"` or `"rgba"`; the length must be
  exactly `width × height × 3` (or `× 4`). It copies the bytes, converts them to
  GPUI's BGRA, builds a new `RenderImage`, and drops the previous one with
  `cx.drop_image` so its atlas texture is freed. Legal outside `render`
  (a task or timer), like other state updates.
- `pixel_image(surface)`: draws the surface's current image, or nothing before
  the first `write`. It takes the usual size and style methods.

## Implementation steps (herdr-app fork)

1. **Spec and registry.** Add a retained `PixelSurface { width, height, image:
   Option<Arc<RenderImage>> }` to the component registry, and a
   `Component::PixelImage(handle)` variant in `spec.rs` with its name in the
   component-name table.
2. **JavaScript binding** (`engine/quickjs/mod.rs`): export `PixelImage.new`,
   `write` and `pixel_image`. `write` takes the `Uint8Array` in one host call
   (about 97 KB per RGB frame, roughly 1.5 MB/s at 15 fps) and validates the
   length and format before converting.
3. **Release.** On `write`, swap in the new image and call `drop_image` on the
   old one. Do the same when the owning view is closed or the mod reloads, so a
   long-running widget never grows the sprite atlas.
4. **Materialize** (`materialize.rs`): render `Component::PixelImage` as
   `gpui::img(ImageSource::Render(image))` with the element's style, and an empty
   element before the first frame.
5. **Typings** (`typings.rs`, which generates `gpui-kit.d.ts`): declare the API
   above with its limits.
6. **Herdr docs** (`AGENTS.md` mod reference): one paragraph and the example.

## Widget changes (this repo), once Herdr has it

- Feature-detect `PixelImage` in the mod; keep the rectangle renderer as the
  fallback for released Herdr builds.
- When it's available, the worker leaves `stick_set_display` at its defaults
  (dithered field, full anti-aliasing) and streams the raw frame instead of
  packed rectangles: a short header line (width, height, background) followed by
  the 97,200 RGB bytes, one frame per message on the existing loopback socket.
  `packFrame`, the edge-level policy and the native gradient aren't needed.
- The mod reads the bytes (`socket.read` already returns a `Uint8Array`), calls
  `write`, and draws `pixel_image` inside the existing rounded bezel and corner
  matte.

## Testing

- Herdr unit test, modeled on the existing image-invalidation test in
  `src/window.rs`: write a surface twice and assert the first image's atlas
  entry is gone; close the view and assert the second one is gone.
- gpui-kit tests for argument validation (size limits, length mismatch, unknown
  format) and for drawing nothing before the first `write`.
- This repo: a worker test that the binary frame stream matches `avatar.wasm`
  byte for byte, and `scripts/bench-herdr-avatar.py`
  (script time per render, widget frames per second, with streaming terminals
  and CPU load), compared against the numbers above.
- Visual check with `herdr-app shot`: the widget pixels should equal the site's
  frame exactly at 2× scale.

## Open questions

- **Scaling filter.** GPUI samples images linearly. At exactly 2× that is
  pixel-exact, but other Herdr zoom levels or a narrow sidebar will blur
  slightly. A nearest-neighbor option would need a Metal/wgpu sampler change and
  is left for later.
- **Upstream.** The released Herdr.app in `/Applications` won't have this until
  upstream accepts it. Until then, using it means running a locally built Herdr.
