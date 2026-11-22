// Waking up: shared by the loading placeholder, the camera and main.js. No
// dependencies, so it runs (and sizes the placeholder) while three.js and
// avatar.wasm are still downloading.
//
// While loading, the page shows the firmware's sleeping eyes as CSS slits.
// Once the renderer has dozed off (fast-forwarded) and drawn its first frame,
// the 3D screen fades in at exactly the same size and place, the camera
// slowly dollies in from WAKE_FROM × the intro distance, and the face wakes
// up near the end of the zoom.

export const SCREEN = { x: 6.6, w: 24.9, h: 14.0 }; // ST7789P3 active area (mm)
const FACE_Z = 7.55; // the pixel plane sits FRONT + 0.05 mm in front of the target
export const WAKE_FROM = 2.2;
export const WAKE_MS = 2600; // dolly duration
export const WAKE_AT = 0.62; // fraction of the dolly when the eyes open

export const narrow = () => matchMedia("(max-width: 860px)").matches;
// Far enough that the 240×135 screen is magnified only ~1.25×: closer, its
// pixels show.
export const introDist = () => (narrow() ? 120 : 150);
// The intro face sits above the middle (a screen-space view offset, as a
// fraction of the stage height) so the hero heading fits centred below it.
export const introLift = () => (narrow() ? 0.16 : 0.15);
// Vertical field of view: keeps the whole stick in frame on narrow screens.
export const fovFor = (w, h) => (w / h < 1.1 ? Math.min(52, 26 * (1.25 / Math.max(0.5, w / h))) : 26);

// Sleeping slits measured from the firmware frame (240×135), the fallback
// until the renderer's first frame: 33×3 px each, 12 px apart, centred 8 px
// below the middle of the screen. The dozing face breathes, so main.js refits
// them from that frame (measureSlits) right before the handoff.
const PX = SCREEN.w / 240;
const SLITS = { w: 33, h: 3, gap: 12, x: 0, y: 8 };
export function fitSleepyEyes(stage = document.getElementById("stage"), slits = SLITS) {
  if (!stage) return;
  const { clientWidth: w, clientHeight: h } = stage;
  if (!w || !h) return;
  const distance = introDist() * WAKE_FROM - FACE_Z;
  const scale = h / (2 * distance * Math.tan(((fovFor(w, h) / 2) * Math.PI) / 180)); // px per mm
  const set = (name, mm, min = 0) => stage.style.setProperty(name, `${Math.max(min, mm * scale).toFixed(2)}px`);
  set("--slit-w", slits.w * PX);
  set("--slit-h", slits.h * PX, 1.5);
  set("--slit-gap", slits.gap * PX);
  stage.style.setProperty("--slit-x", `${(slits.x * PX * scale).toFixed(2)}px`);
  stage.style.setProperty("--slit-y", `${(slits.y * PX * scale - introLift() * h).toFixed(2)}px`);
  stage.classList.add("slits"); // placed: show them (styles.css)
}

// The two sleeping slits in an RGBA frame (240×135, light eyes on a dark
// face), as fitSleepyEyes geometry in screen pixels: width, thickness and gap
// from each eye's coverage, offsets of the pair's centre from the screen's.
// Null when the frame doesn't show two slits.
export function measureSlits(rgba) {
  const eyes = [0, 1].map((side) => {
    let mass = 0, sumX = 0, sumY = 0, left = 240, right = -1;
    for (let y = 0; y < 135; y++) {
      for (let x = side * 120; x < side * 120 + 120; x++) {
        const i = (y * 240 + x) * 4;
        const a = Math.min(1, (rgba[i] + rgba[i + 1] + rgba[i + 2]) / 735);
        if (a < 0.05) continue;
        mass += a; sumX += a * (x + 0.5); sumY += a * (y + 0.5);
        if (a > 0.5) { left = Math.min(left, x); right = Math.max(right, x + 1); }
      }
    }
    return right > left ? { mass, cx: sumX / mass, cy: sumY / mass, left, right } : null;
  });
  if (!eyes[0] || !eyes[1]) return null;
  const w = (eyes[0].right - eyes[0].left + eyes[1].right - eyes[1].left) / 2;
  const h = (eyes[0].mass + eyes[1].mass) / (2 * w);
  if (h > 8) return null; // open eyes, not slits
  return {
    w, h, gap: eyes[1].left - eyes[0].right,
    x: (eyes[0].left + eyes[1].right) / 2 - 120,
    y: (eyes[0].cy + eyes[1].cy) / 2 - 67.5,
  };
}

// Where the woken intro screen ends, in px from the top of the stage.
export function introScreenBottom(stage = document.getElementById("stage")) {
  const { clientWidth: w, clientHeight: h } = stage;
  const scale = h / (2 * (introDist() - FACE_Z) * Math.tan(((fovFor(w, h) / 2) * Math.PI) / 180));
  return h * (0.5 - introLift()) + (SCREEN.h / 2) * scale;
}

// The opening title waits for the face: hidden while the sleeping eyes show,
// it fades in as the camera dollies in and the eyes open (main.js wakes it).
// A safety timer shows it anyway if the renderer never gets that far.
export function showTitle() {
  document.documentElement.classList.remove("sleepy");
}

if (typeof document !== "undefined") {
  // index.html adds .sleepy before the first paint; never leave it stuck.
  setTimeout(showTitle, 15000);
  fitSleepyEyes();
  const refit = () => (document.getElementById("stage")?.classList.contains("ready") ? null : fitSleepyEyes());
  addEventListener("resize", refit);
}
