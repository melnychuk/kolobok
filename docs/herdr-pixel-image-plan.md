# Plan: live pixel frames for the Stick widget in Herdr

Status: proposal. Nothing here is implemented yet. It needs a change to Herdr
Desktop, prototyped in our fork (`github.com/melnychuk/herdr-app`) and proposed
upstream through the existing request for the same capability,
[herdrdev/herdr-app#692](https://github.com/herdrdev/herdr-app/issues/692).
Until upstream ships it, only a locally built Herdr can use it.

The first step, stopping all widget work while the window is hidden, is
separate and already proposed: `window().visible`
([herdrdev/herdr-app#1320](https://github.com/herdrdev/herdr-app/pull/1320)).

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
| script time per render | 10.1 ms | 5.4 ms | expected under 1 ms (unmeasured) |

An image would show exactly what the Stick shows (the same bytes the site
draws), and the script's per-frame cost would no longer depend on what is on
screen. It doesn't remove the rest of a frame: each `cx.notify()` still redraws
Herdr's window, so the benchmark has to report that too (see Testing).

## What Herdr already has

- GPUI draws in-memory images through `ImageSource::Render(Arc<RenderImage>)`
  (`vendor/gpui-pre/src/elements/img.rs`). It is used in the same frame: no
  asynchronous load, so no blank frame between two images.
- Since 0.1.15 (`c3fe88f`, "release image frames when they leave the screen"),
  a window retires an image's atlas tile at the end of a frame once neither the
  frame just drawn nor the one before it uses it. Replacing the image a path
  shows is enough to free the old one; no explicit `drop_image` is needed.
- Image elements take the usual style, including rounded corners
  (`paint_image` gets the element's corner radii), so the widget's corner matte
  becomes unnecessary.
- The script runtime already passes `Uint8Array`s both ways (`net`, `fs`,
  `fetch`, `WebSocket`), and `socket.read()` returns one.
- Image bounds are snapped to device pixels before sampling
  (`Window::paint_image`), so a 240 × 135 image in a 120 × 67.5-point box on a
  2× display maps one image pixel to one screen pixel.

## Upstream

Herdr keeps its shared mod API small: a new API needs the maintainer's go
(`AGENTS.md`, "Before you grow the app's core"). #692 already asks for this
capability for a simulator mod (`setImage(path, bytes)` with PNG or JPEG
frames), and related issues cover the same file-reload limits (#521, #498, #22;
fixes in review: #615, #25). A second, larger API for the same need would
compete with it, so the plan is to extend that request with a raw-pixel form
rather than add a new component:

```js
import { image } from "gpui-kit";
import { setImage } from "herdr";

// Whenever a frame arrives (RGB or RGBA, row-major, width × height pixels):
await setImage("stick-s3/frame", bytes, { width: 240, height: 135, format: "rgb" });
cx.notify();

// In render(): the ordinary image element, sized and styled as usual.
image("stick-s3/frame").w(120).h(67.5).rounded(5);
```

- Without options, `bytes` is an encoded image (PNG or JPEG), decoded off the
  main thread, as #692 asks.
- With `{ width, height, format }`, `bytes` is raw pixels: at most
  4096 × 4096, the length exactly `width × height × 3` (or `× 4`). It is
  converted to GPUI's BGRA without decoding.
- The path shows the newest image from the next frame on; the previous image
  keeps showing until then. `setImage(path, null)` forgets it, and a mod's
  images are forgotten when it unloads.

## Implementation steps (herdr-app fork)

1. **Store.** A per-app map from the path `image()` resolves to the newest
   `Arc<RenderImage>`, owned by the mod that set it and cleared on unload.
2. **Binding** (`src/mods.rs`, the `herdr` module): `setImage(path, bytes,
   options?)`, validating the path, size, length and format before converting.
   Raw frames take one host call (about 97 KB per RGB frame, roughly 1.5 MB/s
   at 15 fps).
3. **Materialize** (`vendor/gpui-kit/.../materialize.rs`, `Component::Image`):
   when the map has the path, draw `gpui::img(ImageSource::Render(image))`
   with the element's style instead of loading the file. Rely on the frame-end
   tile retirement to free replaced images.
4. **Typings and docs:** `defaults/herdr.d.ts` and the mod guide in
   `defaults/AGENTS.md`, with the example above.

## Widget changes (this repo), once Herdr has it

- Feature-detect `setImage` in the mod; keep the rectangle renderer as the
  fallback for released Herdr builds.
- When it's available, the worker leaves `stick_set_display` at its defaults
  (dithered field, full anti-aliasing) and streams raw frames instead of
  packed rectangles. `packFrame`, the edge-level policy and the native gradient
  aren't needed.
- Framing: a frame arrives over several socket reads, so each one gets a short
  header (length, width, height, background) and the mod reassembles it. As
  today, only the newest complete frame in a read is shown; older ones are
  dropped, never queued.
- The mod passes the bytes to `setImage` and draws `image(...)` inside the
  existing rounded bezel, without the corner matte.

## Testing

- Herdr: a render test that sets an image twice and checks the first one's
  atlas tile is gone after the next frames, and that unloading the mod forgets
  the second; validation tests for size limits, length mismatch and unknown
  formats; a path that was never set loads its file as before.
- This repo: a worker test that the binary frame stream matches `avatar.wasm`
  byte for byte, and `scripts/bench-herdr-avatar.py` against the numbers above.
  Compare script time per render (`script_ms`) and also native drawing
  (`materialize_ms`) and render-to-paint time (`frame_ms`), which an image
  doesn't remove.
- Visual check with `herdr-app shot`: the widget pixels should equal the site's
  frame exactly at 2× scale.

## Open questions

- **Scaling filter.** GPUI samples images linearly. At exactly 2× that is
  pixel-exact, but other Herdr zoom levels or a narrow sidebar will blur
  slightly. A nearest-neighbor option would need a Metal/wgpu sampler change and
  is left for later.
- **API shape.** The maintainer may prefer a different shape for #692 (for
  example a separate function for raw pixels); the widget side only needs some
  way to hand Herdr one frame of pixels per call.
