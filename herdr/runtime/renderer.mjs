/// <reference lib="dom" />
// Local renderer for the Herdr Desktop mod. No daemon registration, network,
// agent actions, audio, or device access. A worker lives only while leased.
import { spawn } from "node:child_process";
import { realpathSync } from "node:fs";
import { readFile, mkdir, mkdtemp, rename, rm, rmdir, stat, writeFile } from "node:fs/promises";
import { basename, join } from "node:path";
import process from "node:process";
import { setTimeout as delay } from "node:timers/promises";
import { fileURLToPath, pathToFileURL } from "node:url";
import { stickPresent } from "./presence.mjs";

const SOURCE_W = 240;
const SOURCE_H = 135;
const MAX_WIDTH = SOURCE_W / 2;
const FRAME_MS = 33; // Same simulation step as the site.
const LEASE_MS = 5000;

/** @typedef {{active: number, blocked: number, done: number, focused: boolean, background: number, width: number}} Input */
/** @param {BufferSource} bytes */
export async function makeRenderer(bytes) {
  let memory = /** @type {WebAssembly.Memory | undefined} */ (undefined);
  /** @type {Record<string, (...args: number[]) => number>} */
  const wasi = {
    random_get(/** @type {number} */ ptr, /** @type {number} */ len) {
      if (!memory) return 1;
      globalThis.crypto.getRandomValues(new Uint8Array(memory.buffer, ptr, len));
      return 0;
    },
    fd_write(_fd = 0, _iovs = 0, _len = 0, written = 0) {
      if (!memory) return 1;
      new DataView(memory.buffer).setUint32(written, 0, true);
      return 0;
    },
  };
  const wasm = await WebAssembly.instantiate(bytes, {
    wasi_snapshot_preview1: new Proxy(wasi, { get: (target, key) => target[String(key)] ?? (() => 0) }),
  });
  const e = /** @type {{memory: WebAssembly.Memory, _initialize?: () => void,
    stick_init: () => void, stick_keep_awake: () => void, stick_frame: (...args: number[]) => void,
    stick_frame_ptr: () => number, stick_backdrop_ptr?: () => number}} */ (/** @type {unknown} */ (wasm.instance.exports));
  memory = e.memory;
  e._initialize?.();
  e.stick_init();
  let clock = 0;
  const frame = (/** @type {Input} */ input, steps = 1) => {
    for (let i = 0; i < steps; i++) {
      clock += FRAME_MS;
      if (input.focused) e.stick_keep_awake();
      e.stick_frame(clock, input.active, input.blocked, input.done, 0, 1, input.background);
    }
    return new Uint8Array(e.memory.buffer, e.stick_frame_ptr(), SOURCE_W * SOURCE_H * 3);
  };
  return Object.assign(frame, {
    backdrop() {
      const ptr = e.stick_backdrop_ptr?.();
      return ptr ? new Uint16Array(e.memory.buffer, ptr, SOURCE_W * 4) : null;
    },
  });
}

/** @param {Partial<Input>} input @returns {Input} */
export function control(input) {
  const count = (/** @type {number | undefined} */ n) => Number.isInteger(n) ? Math.min(3999, Math.max(0, n ?? 0)) : 0;
  const active = count(input.active);
  return {
    active, blocked: Math.min(active, count(input.blocked)), done: count(input.done),
    focused: input.focused === true,
    background: typeof input.background === "number" && Number.isInteger(input.background) &&
      input.background >= 0 && input.background <= 0xffffff ? input.background : 0,
    width: Number.isInteger(input.width) ? Math.min(MAX_WIDTH, Math.max(2, input.width ?? MAX_WIDTH)) : MAX_WIDTH,
  };
}

// Nearest-neighbor sampling of the real framebuffer. Merge equal-color runs
// across adjacent rows, keeping the mod's number of path operations small.
/** @param {Uint8Array} rgb @param {number} width @param {number} background @param {Uint32Array | null} rows */
export function packFrame(rgb, width, background, quantize = 1, rows = null) {
  const height = Math.round(width * SOURCE_H / SOURCE_W);
  const colors = new Map();
  let above = new Map();
  const snap = (/** @type {number} */ channel) => Math.min(255, Math.round(channel / quantize) * quantize);
  for (let y = 0; y < height; y++) {
    const row = new Map();
    const sourceY = Math.floor((y + 0.5) * SOURCE_H / height);
    const pixel = (/** @type {number} */ x) => {
      const sourceX = Math.floor((x + 0.5) * SOURCE_W / width);
      const i = (sourceY * SOURCE_W + sourceX) * 3;
      const r = rgb[i], g = rgb[i + 1], b = rgb[i + 2];
      const color = (r << 16) | (g << 8) | b;
      if (color === (rows ? rows[(sourceY & 3) * SOURCE_W + sourceX] : background)) return -1;
      if (quantize === 1 || color === background || EXACT_COLORS.has(color)) return color;
      return (snap(r) << 16) | (snap(g) << 8) | snap(b);
    };
    for (let x = 0; x < width;) {
      const color = pixel(x);
      let end = x + 1;
      while (end < width && pixel(end) === color) end++;
      if (color >= 0) {
        const key = `${color}:${x}:${end}`;
        let rect = above.get(key);
        if (rect) rect[3] = y + 1;
        else {
          rect = [x, y, end, y + 1];
          if (!colors.has(color)) colors.set(color, []);
          colors.get(color).push(rect);
        }
        row.set(key, rect);
      }
      x = end;
    }
    above = row;
  }
  return [...colors].map(([color, rects]) => [`#${color.toString(16).padStart(6, "0")}`, rects]);
}

// Keep the solid status fills, eyes and numeral ink. In busy shots the LCD's
// dither can make thousands of tiny shapes. Stay within Herdr's per-render JS
// budget with a smaller sampling grid and a reduced edge/dither palette.
const EXACT_COLORS = new Set([0, 0x3981f6, 0xeef6ff, 0x20c65a, 0xf6758b, 0xffcb00]);

/** @param {number} rgb @returns {`#${string}`} */
const css = (rgb) => `#${rgb.toString(16).padStart(6, "0")}`;

// Read the Bayer rows the renderer actually used. Average one 4×4 tile per
// stop; Herdr interpolates between the stops in sRGB without a reduced palette.
/** @param {Uint16Array} raw @param {number} background */
export function smoothStatusBackdrop(raw, background) {
  const dark = (((background >> 19) & 31) << 11) | (((background >> 10) & 63) << 5) | ((background >> 3) & 31);
  const rows = Uint32Array.from(raw, (c) => {
    if (c === 0 || c === dark) return background;
    return (Math.floor((c >> 11) * 255 / 31) << 16) |
      (Math.floor(((c >> 5) & 63) * 255 / 63) << 8) | Math.floor((c & 31) * 255 / 31);
  });
  const stops = [];
  for (let x = 0; x <= SOURCE_W; x += 20) {
    let r = 0, g = 0, b = 0;
    const start = Math.max(0, Math.min(SOURCE_W - 4, x - 2));
    for (let y = 0; y < 4; y++) {
      for (let dx = 0; dx < 4; dx++) {
        const c = rows[y * SOURCE_W + start + dx];
        r += c >> 16;
        g += (c >> 8) & 255;
        b += c & 255;
      }
    }
    stops.push(css((Math.round(r / 16) << 16) | (Math.round(g / 16) << 8) | Math.round(b / 16)));
  }
  return { rows, stops };
}

/** @param {Uint8Array} rgb @param {Input} input @param {Uint16Array | null} rawBackdrop */
export function displayFrame(rgb, input, rawBackdrop = null) {
  const { width, background } = input;
  const status = rawBackdrop && input.active > 0 && input.done > 0 && !input.blocked
    ? smoothStatusBackdrop(rawBackdrop, background) : null;
  for (const [sample, quantize] of [[width * 2, 1], [width, 1], [width, 16], [width, 32],
    [width, 48], [Math.ceil(width * 0.75), 48], [Math.ceil(width * 0.5), 64], [Math.ceil(width / 3), 64]]) {
    const ink = packFrame(rgb, sample, background, quantize, status?.rows ?? null);
    const rectangles = ink.reduce((n, [, rects]) => n + rects.length, 0);
    if (rectangles + ink.length * 2 <= (status ? 345 : 385) || sample <= Math.ceil(width / 3)) {
      return [width, background, sample, ink, status?.stops ?? []];
    }
  }
  throw new Error("No display grid");
}

/** @param {string} root @param {string} initial @param {string} serial */
async function launch(root, initial, serial) {
  await mkdir(root, { recursive: true, mode: 0o700 });
  const dir = await mkdtemp(join(root, "view-"));
  try {
    await writeFile(join(dir, "input.json"), initial, { mode: 0o600 });
    const child = spawn(process.execPath, [fileURLToPath(import.meta.url), "--worker", dir, serial], {
      detached: true, stdio: "ignore", windowsHide: true,
    });
    await new Promise((resolve, reject) => child.once("spawn", resolve).once("error", reject));
    child.unref();
    process.stdout.write(basename(dir));
  } catch (error) {
    await rm(dir, { recursive: true, force: true });
    throw error;
  }
}

/** @param {string} dir @param {string} serial */
async function worker(dir, serial) {
  const lease = join(dir, "input.json");
  let last = performance.now() - FRAME_MS;
  let input = control({});
  let connected = false;
  let nextProbe = 0;
  /** @type {Awaited<ReturnType<typeof makeRenderer>> | null} */
  let render = null;
  try {
    let lastInput = "";
    let lastSequence = 0;
    let lastVisual = "";
    for (;;) {
      const leaseStat = await stat(lease);
      if (Date.now() - leaseStat.mtimeMs > LEASE_MS) break;
      const next = await readFile(lease, "utf8");
      if (next !== lastInput) {
        // A mod write can still be in progress. Keep the last complete input.
        try {
          input = control(JSON.parse(next));
          lastInput = next;
        } catch { /* Retry on the next tick. */ }
      }
      if (performance.now() >= nextProbe) {
        connected = (await stickPresent(serial)) ?? connected;
        nextProbe = performance.now() + 1000;
      }
      const now = performance.now();
      const steps = Math.min(4, Math.floor((now - last) / FRAME_MS));
      let visual = "null";
      if (connected) {
        render = null;
        last = now - FRAME_MS;
      } else if (steps > 0) {
        render ??= await makeRenderer(await readFile(new URL("avatar.wasm", import.meta.url)));
        last = now - ((now - last) % FRAME_MS);
        visual = JSON.stringify(displayFrame(render(input, steps), input, render.backdrop()));
      } else {
        await delay(FRAME_MS);
        continue;
      }
      if (visual !== lastVisual || now - lastSequence > 500) {
        await writeFile(join(dir, "frame.tmp"), `${now}\n${visual}`, { mode: 0o600 });
        await rename(join(dir, "frame.tmp"), join(dir, "frame.json"));
        lastVisual = visual;
        lastSequence = now;
      }
      await delay(connected ? 250 : input.focused ? 65 : 130);
    }
  } catch (error) {
    if (!(error instanceof Error && "code" in error && error.code === "ENOENT")) throw error;
  } finally {
    await Promise.all(["input.json", "frame.tmp", "frame.json"].map((name) => rm(join(dir, name), { force: true })));
    await rmdir(dir);
  }
}

if (process.argv[1] && pathToFileURL(realpathSync(process.argv[1])).href === import.meta.url) {
  const [mode, dir, initial = "", serial = ""] = process.argv.slice(2);
  if (mode === "--launch" && dir && initial) await launch(dir, initial, serial);
  else if (mode === "--worker" && dir && /^view-[a-zA-Z0-9]+$/.test(basename(dir))) await worker(dir, initial);
  else process.exitCode = 2;
}
