// BAA's menu bar avatar for Omarchy's bar: a JavaScript port of
// menu-bar/Sources/BAA (Avatar.swift, Celebration.swift, and the frame
// choice in MenuBar.swift's StatusIcon), drawing the same pixels from the
// same art (baa-art.mjs, exported from the Swift by scripts/export-baa-art.py).
// It has no dependencies, so BaaIcon.qml and Node's tests both import it.
//
// Rendering returns straight-alpha RGBA bytes, ready for a Canvas's
// putImageData: `base` (the sides with the face over them, in the bar's
// foreground color where macOS would tint a template image), a separate
// `star` layer for a completion's burst, and where to draw the side's label
// with the bar's own font, between the two.
import ART from "./baa-art.mjs";

const C = ART.celebration;
export const colors = ART.colors;

/** The menu bar's sizes, in points, with its label's font size. */
export const sizes = Object.freeze(Object.assign({ labelSize: 11, compact: false }, ART.sizes));
/**
 * A compact avatar, two points shorter, for bars where the menu bar's would
 * look large among other icons: the face two points shorter (its eyes a
 * point higher in it, as when it squashes), the sides too, and smaller digits.
 */
export const compactSizes = Object.freeze(Object.assign({}, sizes, {
  compact: true, faceHeight: sizes.faceHeight - 2, sideHeight: sizes.sideHeight - 2,
  itemHeight: sizes.itemHeight - 2, labelSize: 9,
}));
const S = sizes;

// ---------------------------------------------------------------- art

function level(character) {
  if (character === ".") return 0;
  if (character === "#") return 1;
  return Number(character) / 10;
}

const levels = (rows) => rows.map((row) => Array.from(row, level));
const artRows = (name, scale) => ART.art[name][scale === 1 ? 0 : 1];

/** The body `height` points tall: its drawn corners with solid rows between. */
export function bodyRows(height, scale) {
  const rows = artRows("body", scale);
  let corner = 0;
  while (corner < rows.length && /[^#]/.test(rows[corner])) corner++;
  const solid = "#".repeat(rows[0].length);
  const middle = Math.max(0, height * scale - 2 * corner);
  return [...rows.slice(0, corner), ...Array(middle).fill(solid), ...rows.slice(rows.length - corner)];
}

/** Eye placements for a face, with lids met when it blinks. */
export function eyes(face, blinking = false) {
  const entry = ART.eyes[face];
  if (!entry) throw new Error(`No face ${face}`);
  if (!(blinking && entry.blinks)) return entry.eyes;
  return entry.eyes.map(([, left]) => ["closed", left, entry.lidsMeet]);
}

/** The face's alpha grid at one scale, `height` points tall; its eyes sit about the menu bar face's middle. */
export function faceAlpha(face, blinking = false, height = S.faceHeight, scale = 1) {
  const alpha = levels(bodyRows(height, scale));
  const shift = Math.trunc((height - S.faceHeight) / 2) * scale; // Swift's integer division
  for (const [art, left, top] of eyes(face, blinking)) {
    levels(artRows(art, scale)).forEach((line, row) => {
      const faceRow = top * scale + shift + row;
      if (faceRow < 0 || faceRow >= alpha.length) return;
      line.forEach((cut, column) => { alpha[faceRow][left * scale + column] *= 1 - cut; });
    });
  }
  const strength = face === "asleep" ? 0.5 : 1;
  return alpha.map((row) => row.map((value) => value * strength));
}

// ------------------------------------------------------------- layout

/**
 * `left` is the active side's width in points (0 for none), measured from its
 * label. `G` is the geometry, `sizes` or `compactSizes`; the layout carries it.
 */
export function layout(left = 0, done = false, G = S) {
  return { left, done, faceX: left, width: left + G.faceWidth + (done ? G.doneWidth : 0), G };
}

/** The active side's width for a label `textWidth` points wide (Avatar.swift's layout). */
export function leftWidth(textWidth) {
  return Math.max(12, Math.ceil(textWidth) + 6);
}

export const home = (G = S) => ({ dx: 0, lift: 0, height: G.faceHeight });

export function poseTop(pose, G = S) {
  return Math.trunc((G.itemHeight + G.faceHeight) / 2) - pose.lift - pose.height;
}

const grid = (width, height, value = 0) => Array.from({ length: height }, () => new Array(width).fill(value));

function placed(source, L, pose, scale) {
  const left = (L.faceX + pose.dx) * scale, top = poseTop(pose, L.G) * scale;
  const out = grid(L.width * scale, L.G.itemHeight * scale);
  source.forEach((line, row) => {
    const r = top + row;
    if (r < 0 || r >= out.length) return;
    line.forEach((value, column) => {
      const c = left + column;
      if (c >= 0 && c < out[r].length) out[r][c] = value;
    });
  });
  return out;
}

const bodyAlpha = (L, pose, scale) => placed(levels(bodyRows(pose.height, scale)), L, pose, scale);

export function sideCoverage(L, scale) {
  const G = L.G;
  const band = levels(bodyRows(G.sideHeight, scale));
  const top = Math.trunc((G.itemHeight - G.sideHeight) / 2) * scale;
  const corner = G.tuck * scale, width = L.width * scale;
  const left = grid(width, G.itemHeight * scale), done = grid(width, G.itemHeight * scale);
  band.forEach((line, bandRow) => {
    for (let column = 0; column < width; column++) {
      if (L.left > 0 && column < (L.left + G.tuck) * scale) {
        left[top + bandRow][column] = column < corner ? line[column] : 1;
      } else if (L.done && column >= (L.faceX + G.faceWidth - G.tuck) * scale) {
        const fromEdge = width - 1 - column;
        done[top + bandRow][column] = fromEdge < corner ? line[line.length - 1 - fromEdge] : 1;
      }
    }
  });
  return { left, done };
}

/** The sides' band right across the item. */
export function bar(L, scale) {
  const G = L.G;
  const band = levels(bodyRows(G.sideHeight, scale));
  const top = Math.trunc((G.itemHeight - G.sideHeight) / 2) * scale;
  const corner = G.tuck * scale, width = L.width * scale;
  const out = grid(width, G.itemHeight * scale);
  band.forEach((line, bandRow) => {
    for (let column = 0; column < width; column++) {
      const fromEdge = width - 1 - column;
      out[top + bandRow][column] = column < corner ? line[column]
        : fromEdge < corner ? line[line.length - 1 - fromEdge] : 1;
    }
  });
  return out;
}

export function grown(alpha, radius) {
  const offsets = [];
  for (let r = -radius; r <= radius; r++) {
    for (let c = -radius; c <= radius; c++) if (r * r + c * c <= radius * radius) offsets.push([r, c]);
  }
  return alpha.map((line, row) => line.map((_, column) => {
    let strongest = 0;
    for (const [dr, dc] of offsets) {
      const near = alpha[row + dr];
      if (near && column + dc >= 0 && column + dc < near.length) strongest = Math.max(strongest, near[column + dc]);
    }
    return strongest;
  }));
}

// ------------------------------------------------------------- colors

export function rgb(hex) {
  const n = parseInt(hex.slice(1), 16);
  return [(n >> 16) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255];
}

const WORKING = rgb(colors.working), DONE = rgb(colors.done);
const REQUEST = rgb(colors.request), REQUEST_PEAK = rgb(colors.requestPeak);
const CHARGE = DONE;

/** WCAG relative luminance of a straight sRGB color, components 0–1. */
function luminance(color) {
  const linear = color.map((c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)));
  return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2];
}

/** WCAG contrast ratio between two colors, 1–21. */
export function contrast(a, b) {
  const [light, dark] = [luminance(a), luminance(b)].sort((x, y) => y - x);
  return (light + 0.05) / (dark + 0.05);
}

/**
 * The ink for a label on a side: `preferred` (a theme's color) if it reads
 * clearly there (4.5:1), otherwise black or white, whichever contrasts more.
 */
export function labelInk(side, preferred) {
  if (preferred && contrast(preferred, side) >= 4.5) return preferred;
  return contrast([0, 0, 0], side) >= contrast([1, 1, 1], side) ? [0, 0, 0] : [1, 1, 1];
}

const css = (color) => `rgb(${color.map((c) => Math.round(Math.min(Math.max(c, 0), 1) * 255)).join(", ")})`;

function blend(from, to, start, end, column, scale) {
  const x = (column + 0.5) / scale;
  const t = Math.min(Math.max((x - start) / (end - start), 0), 1);
  const amount = t * t * (3 - 2 * t);
  return from.map((value, i) => value + (to[i] - value) * amount);
}

// ------------------------------------------------------------- burst

export function burstOrigin(L) {
  const x = L.left > 0 ? L.left - L.G.moat : L.faceX + Math.trunc(L.G.faceWidth / 2);
  return [x, L.G.itemHeight / 2];
}

export function burstStep(progress) {
  return Math.min(S.burstSteps, Math.max(1, Math.trunc(progress * S.burstSteps) + 1));
}

export function starCoverage(L, step, scale) {
  const [originX, originY] = burstOrigin(L);
  let reach = 0;
  for (const x of [0, L.width]) for (const y of [0, L.G.itemHeight]) reach = Math.max(reach, Math.hypot(x - originX, y - originY));
  const progress = step / S.burstSteps;
  const full = (reach + 1) / S.starInner;
  const outer = 4 + (full - 4) * Math.pow(progress, 1.6);
  const inner = S.starInner * outer;
  const spin = step * 0.35;
  return Array.from({ length: L.G.itemHeight * scale }, (_, row) =>
    Array.from({ length: L.width * scale }, (_, column) => {
      const dx = (column + 0.5) / scale - originX, dy = (row + 0.5) / scale - originY;
      const ray = Math.abs(Math.cos(5 * (Math.atan2(dy, dx) + spin)));
      const radius = inner + (outer - inner) * ray * ray * ray;
      return Math.min(Math.max((radius - Math.hypot(dx, dy)) * scale + 0.5, 0), 1);
    }));
}

// --------------------------------------------------------- celebration

export const beats = C.beats.map(([name]) => name);

/** Only active work charges up, as on the Stick. */
function charges(celebration) {
  const hint = leftHint(celebration.before);
  return !!hint && hint.kind === "working";
}

/** The whole performance, in seconds. */
export function celebrationLength(celebration) {
  return (charges(celebration) ? C.chargeLength : 0) + C.frightLength + C.burstLength + C.goneLength
    + C.beats.reduce((total, [, length]) => total + length, 0);
}

/** A new completion: the done count rose between two healthy snapshots. */
export function celebrates(old, next) {
  return !!old && !!next && old.healthy && next.healthy && next.done > old.done;
}


/** Celebration.frame(at:): {kind, ...} at `now` seconds, same clock as `started`. */
export function celebrationFrame(celebration, now) {
  const elapsed = now - celebration.started;
  const frightAt = charges(celebration) ? C.chargeLength : 0;
  const burstAt = frightAt + C.frightLength;
  const goneAt = burstAt + C.burstLength;
  const backAt = goneAt + C.goneLength;
  if (elapsed < 0) return { kind: "finished" };
  if (elapsed >= backAt) {
    let start = backAt;
    for (const [beat, length] of C.beats) {
      if (elapsed < start + length) return { kind: "back", beat, progress: (elapsed - start) / length };
      start += length;
    }
    return { kind: "finished" };
  }
  if (elapsed >= goneAt) return { kind: "gone" };
  if (elapsed >= burstAt) return { kind: "burst", progress: (elapsed - burstAt) / C.burstLength };
  if (elapsed >= frightAt) return { kind: "fright", jumped: elapsed < frightAt + C.jumpLength };
  let flicker = 0;
  C.flickers.forEach((time, i) => { if (time <= elapsed) flicker = i; });
  return { kind: "charge", lit: flicker % 2 === 0 };
}

export function burstEyes(progress) {
  return progress < 0.4 ? ["wincing", false] : ["attention", true];
}

const mix = (from, to, amount) => {
  const delta = (to - from) * amount; // Swift's .rounded(): halves away from zero
  return from + Math.sign(delta) * Math.floor(Math.abs(delta) + 0.5);
};
const pose = (dx, lift, height) => ({ dx, lift, height });

// Squashed on landing two points shorter than at rest; stretched a point taller.
function landing(beat, G) {
  return { face: "done", blinking: false, pose: pose(0, 0, G.faceHeight + (beat === "land" ? -2 : 1)), wash: false };
}

/** Celebration.act: the face on its way home in the new icon. */
export function act(beat, progress, L) {
  const G = L.G, rest = G.faceHeight;
  if (beat === "land" || beat === "rebound") return landing(beat, G);
  if (L.left > 0) {
    // Dropping in from above, so it doesn't cross the active side.
    const start = rest - 2;
    const looking = (lift, blinking = false) => ({ face: "lookingDown", blinking, pose: pose(0, lift, rest), wash: true });
    if (beat === "creep") return looking(mix(start, C.dropPeek, 1 - (1 - progress) * (1 - progress)));
    if (beat === "peek") return looking(C.dropPeek, progress >= 0.45 && progress < 0.75);
    if (beat === "crouch") return looking(C.dropPeek + 1);
    const lift = mix(C.dropPeek + 1, 0, progress * progress);
    return { face: "attention", blinking: false, pose: pose(0, lift, progress < 0.85 ? rest + 1 : rest), wash: true };
  }
  const start = -(L.faceX + G.faceWidth - 3), peek = -(L.faceX + C.peekHidden);
  const peeking = (p, blinking = false) => ({ face: "peeking", blinking, pose: p, wash: true });
  if (beat === "creep") return peeking(pose(mix(start, peek, 1 - (1 - progress) * (1 - progress)), 0, rest));
  if (beat === "peek") return peeking(pose(peek, 0, rest), progress >= 0.45 && progress < 0.75);
  if (beat === "crouch") return peeking(pose(peek, 0, rest - 2));
  const dx = mix(peek, 0, progress * progress * (3 - 2 * progress));
  const p = progress < 0.25 ? pose(dx, 0, rest + 1) : progress < 0.85 ? pose(dx, 1, rest) : pose(dx, 0, rest);
  return { face: "attention", blinking: false, pose: p, wash: true };
}

// ------------------------------------------------------------ snapshot

/** A snapshot: {active, blocked, done, healthy}, as the Stick counts them. */
export function faceFor(snapshot) {
  if (!snapshot.healthy) return "puzzled";
  if (snapshot.blocked > 0) return "attention";
  if (snapshot.active > 0) return "working";
  if (snapshot.done > 0) return "done";
  return "idle";
}

/** The colored side left of the face: {kind: "working"|"attention", text}, or null. */
export function leftHint(snapshot) {
  if (!snapshot.healthy) return null;
  if (snapshot.blocked > 0) return { kind: "attention", text: "!" };
  if (snapshot.active > 0) return { kind: "working", text: snapshot.active > 9 ? "9+" : String(snapshot.active) };
  return null;
}

export const hasDone = (snapshot) => snapshot.healthy && snapshot.done > 0;

/**
 * What to draw at a moment: MenuBar.swift's StatusIcon.images. `measure`
 * gives a label's width in points. `headroom` 1 keeps a point above and
 * below the face for its jumps; 0 keeps everything in the face's own rows,
 * for a smaller bar: the jumps become flinches, nothing stretches. `compact`
 * draws `compactSizes`, two points shorter than the menu bar's avatar.
 */
export function describe(snapshot, celebration, now,
                         { blinking = false, peak = false, headroom = 1, compact = false, measure }) {
  const G = compact ? compactSizes : sizes;
  const rest = home(G);
  const layoutFor = (s) => {
    const hint = leftHint(s);
    return layout(hint ? leftWidth(measure(hint.text)) : 0, hasDone(s), G);
  };
  const before = celebration ? celebration.before : snapshot;
  const old = layoutFor(before), next = layoutFor(snapshot);
  const frame = celebration ? celebrationFrame(celebration, now) : { kind: "finished" };
  const fit = (p) => headroom >= 1 ? p
    : pose(p.dx, p.lift === 1 && p.height >= G.faceHeight ? 0 : p.lift, Math.min(p.height, G.faceHeight));
  const view = (L, hint, extra) => Object.assign({ layout: L, left: hint, headroom }, extra);
  switch (frame.kind) {
    case "charge":
      return view(old, leftHint(before), { face: faceFor(before), pose: rest, sides: { charged: frame.lit } });
    case "fright": {
      const p = !frame.jumped ? rest : headroom >= 1 ? pose(0, 1, G.faceHeight) : pose(0, 0, G.faceHeight - 1);
      return view(old, leftHint(before), { face: "attention", pose: p, sides: { charged: true } });
    }
    case "burst": {
      const [face, closed] = burstEyes(frame.progress);
      const step = burstStep(frame.progress);
      return view(old, leftHint(before), {
        face, blinking: closed, pose: rest, blast: step, sides: { charged: true }, star: step });
    }
    case "gone":
      return view(next, leftHint(snapshot), { face: null, flash: true });
    case "back": {
      const a = act(frame.beat, frame.progress, next);
      return view(next, leftHint(snapshot), {
        face: a.face, blinking: a.blinking, pose: fit(a.pose), sides: { peak, wash: a.wash } });
    }
    default:
      return view(next, leftHint(snapshot), { face: faceFor(snapshot), blinking, pose: rest, sides: { peak } });
  }
}

// ------------------------------------------------------------- render

/**
 * Pixels for a description at `scale` (1 or 2), the face in `foreground`
 * ([r, g, b], 0–1). Returns {width, height, base, star, label}: straight
 * RGBA bytes, item rows only (just the face's rows without headroom), and the
 * label to draw over `base` before `star`: {text, color, size, x, y}, its
 * font size in pixels and its centre in points: white digits on blue and a
 * black `!` on yellow, as the menu bar draws them, or with `ink` ([r, g, b],
 * say the bar's background) whichever of it, black or white reads clearly.
 */
export function render(view, scale, foreground, ink = null) {
  const L = view.layout, G = L.G;
  const width = L.width * scale, height = G.itemHeight * scale;
  const hint = view.left;
  const blinking = !!view.blinking && !!view.face && ART.eyes[view.face].blinks;
  // The colored layer: sides, wash or flash, clear of the face and its moat.
  let layers = [];
  if (view.flash) {
    layers = [[bar(L, scale), () => DONE]];
  } else if (view.sides) {
    const { charged = false, peak = false, wash = false } = view.sides;
    const cover = sideCoverage(L, scale);
    const leftShade = hint ? (hint.kind === "working" ? WORKING : peak ? REQUEST_PEAK : REQUEST) : null;
    if (wash) {
      const start = L.faceX - G.moat, end = L.faceX + G.faceWidth;
      layers.push([bar(L, scale), (c) => leftShade ? blend(leftShade, DONE, start, end, c, scale) : DONE]);
    } else {
      if (leftShade) {
        const lit = charged && hint.kind === "working";
        const end = L.left - G.moat;
        layers.push([cover.left, (c) => !lit ? leftShade : L.done ? blend(leftShade, CHARGE, 0, end, c, scale) : CHARGE]);
      }
      layers.push([cover.done, () => DONE]);
    }
  }
  const face = view.face ? placed(faceAlpha(view.face, blinking, view.pose.height, scale), L, view.pose, scale) : null;
  const body = view.face ? bodyAlpha(L, view.pose, scale) : null;
  const near = body ? grown(body, G.moat * scale) : null;
  const star = view.star ? starCoverage(L, view.star, scale) : null;
  const band = view.star ? bar(L, scale) : null;

  const base = new Uint8ClampedArray(width * height * 4);
  const starBytes = view.star ? new Uint8ClampedArray(width * height * 4) : null;
  const byte = (v) => Math.round(Math.min(Math.max(v, 0), 1) * 255);
  for (let row = 0; row < height; row++) {
    for (let column = 0; column < width; column++) {
      // Sides.
      let r = 0, g = 0, b = 0, a = 0;
      if (!body || body[row][column] === 0) {
        for (const [coverage, shade] of layers) {
          const alpha = coverage[row][column];
          if (alpha <= 0) continue;
          const [sr, sg, sb] = shade(column);
          const below = a * (1 - alpha), total = alpha + below;
          r = (sr * alpha + r * below) / total;
          g = (sg * alpha + g * below) / total;
          b = (sb * alpha + b * below) / total;
          a = total;
        }
        if (near) a *= 1 - near[row][column];
      }
      // The face under them (they never share a pixel), cut away by a burst.
      let fa = face ? face[row][column] : 0;
      if (star) fa *= 1 - star[row][column];
      const total = a + fa * (1 - a);
      const i = (row * width + column) * 4;
      if (total > 0) {
        base[i] = byte((r * a + foreground[0] * fa * (1 - a)) / total);
        base[i + 1] = byte((g * a + foreground[1] * fa * (1 - a)) / total);
        base[i + 2] = byte((b * a + foreground[2] * fa * (1 - a)) / total);
        base[i + 3] = byte(total);
      }
      if (starBytes) {
        const sa = star[row][column] * band[row][column];
        if (sa > 0) {
          starBytes[i] = byte(DONE[0]); starBytes[i + 1] = byte(DONE[1]); starBytes[i + 2] = byte(DONE[2]);
          starBytes[i + 3] = byte(sa);
        }
      }
    }
  }
  const label = hint && view.sides && !view.flash
    ? { text: hint.text, size: G.labelSize,
        // The menu bar's white digits and black `!`, unless a theme's ink is given.
        color: ink ? css(labelInk(hint.kind === "working" ? WORKING : view.sides.peak ? REQUEST_PEAK : REQUEST, ink))
          : hint.kind === "working" ? "white" : "black",
        x: (L.left - G.moat) / 2, y: G.itemHeight / 2 }
    : null;
  // Without headroom, only the face's own rows.
  const crop = view.headroom >= 1 ? 0 : Math.trunc((G.itemHeight - G.faceHeight) / 2);
  const keep = (bytes) => bytes && bytes.slice(crop * scale * width * 4, (G.itemHeight - crop) * scale * width * 4);
  return {
    width, height: height - 2 * crop * scale, scale,
    base: keep(base), star: keep(starBytes),
    label: label && Object.assign({}, label, { y: label.y - crop }),
  };
}
