/// <reference lib="dom" />
// Local renderer for the Herdr Desktop mod. No daemon registration, agent
// actions, audio, or device access, and no network beyond one loopback
// connection to the mod. A worker lives only while the mod keeps it connected.
import { spawn } from "node:child_process";
import { randomBytes, timingSafeEqual } from "node:crypto";
import { realpathSync } from "node:fs";
import { readFile } from "node:fs/promises";
import { createServer } from "node:net";
import process from "node:process";
import { setTimeout as delay } from "node:timers/promises";
import { fileURLToPath, pathToFileURL } from "node:url";
import { stickPresent } from "./presence.mjs";

const SOURCE_W = 240;
const SOURCE_H = 135;
const MAX_WIDTH = SOURCE_W / 2;
const FRAME_MS = 33; // Same simulation step as the site.
const LEASE_MS = 5000;
/** One pixel-mode frame message: length, type, background and RGB. */
const FRAME_MESSAGE = 8 + SOURCE_W * SOURCE_H * 3;

/** @typedef {{active: number, blocked: number, done: number, focused: boolean, background: number, light: boolean, width: number, pixels: boolean}} Input */
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
    stick_frame_ptr: () => number, stick_backdrop_ptr?: () => number,
    stick_set_display?: (dither: number, alphaLevels: number) => void,
    stick_set_theme?: (light: number) => void}} */ (/** @type {unknown} */ (wasm.instance.exports));
  memory = e.memory;
  e._initialize?.();
  e.stick_init();
  let clock = 0;
  let light = false;
  const frame = (/** @type {Input} */ input, steps = 1) => {
    // Light mode follows the host theme; an older avatar.wasm stays dark.
    if (input.light !== light) {
      light = input.light === true;
      e.stick_set_theme?.(light ? 1 : 0);
    }
    for (let i = 0; i < steps; i++) {
      clock += FRAME_MS;
      if (input.focused) e.stick_keep_awake();
      e.stick_frame(clock, input.active, input.blocked, input.done, 0, 1, input.background);
    }
    return new Uint8Array(e.memory.buffer, e.stick_frame_ptr(), SOURCE_W * SOURCE_H * 3);
  };
  return Object.assign(frame, {
    /** False when this avatar.wasm predates the widget display options. */
    canSetDisplay: typeof e.stick_set_display === "function",
    /** False when this avatar.wasm predates light mode. */
    canSetTheme: typeof e.stick_set_theme === "function",
    /** Widget display options for following frames; see avatar_wasm.cpp. */
    setDisplay(dither = true, alphaLevels = 0) {
      e.stick_set_display?.(dither ? 1 : 0, alphaLevels);
    },
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
    light: input.light === true,
    width: Number.isInteger(input.width) ? Math.min(MAX_WIDTH, Math.max(2, input.width ?? MAX_WIDTH)) : MAX_WIDTH,
    // The mod can show the frame itself (Herdr's setImage): send pixels, not rectangles.
    pixels: input.pixels === true,
  };
}

// Pixel mode's binary messages; herdr/stick-s3-avatar/frames.js reads them.
// A 4-byte little-endian payload length, then a type byte: "F" + background
// (3 bytes) + RGB frame, "N" (Stick plugged in), or "H" (heartbeat).
/** @param {"F" | "N" | "H"} type @param {number} [background] @param {Uint8Array} [rgb] */
export function encodeMessage(type, background = 0, rgb = undefined) {
  const frame = type === "F" || type === "R";
  const size = frame ? 4 + SOURCE_W * SOURCE_H * 3 : 1;
  const out = Buffer.alloc(4 + size);
  out.writeUInt32LE(size, 0);
  out[4] = type.charCodeAt(0);
  if (frame) {
    if (!rgb || rgb.length !== SOURCE_W * SOURCE_H * 3) throw new Error("A frame needs the whole RGB framebuffer");
    out[5] = (background >> 16) & 255;
    out[6] = (background >> 8) & 255;
    out[7] = background & 255;
    out.set(rgb, 8);
  }
  return out;
}

// Nearest-neighbor sampling of the real framebuffer, packed into rectangles in
// painter's order: colors with the most pixels are painted first, and a color's
// rectangles may run under pixels of any color painted after it. A field split
// by the face then costs one rectangle per row, not one per piece. Equal spans
// on adjacent rows merge. Pixels shown by the native backdrop (-1) are holes
// that no rectangle covers. The mod must paint colors in the returned order.
/** @param {Uint8Array} rgb @param {number} width @param {number} background @param {Uint32Array | null} rows */
export function packFrame(rgb, width, background, quantize = 1, rows = null) {
  const height = Math.round(width * SOURCE_H / SOURCE_W);
  const snap = (/** @type {number} */ channel) => Math.min(255, Math.round(channel / quantize) * quantize);
  const grid = new Int32Array(width * height);
  /** @type {Map<number, {area: number, x0: number, y0: number, x1: number, y1: number}>} */
  const found = new Map();
  for (let y = 0; y < height; y++) {
    const sourceY = Math.floor((y + 0.5) * SOURCE_H / height);
    for (let x = 0; x < width; x++) {
      const sourceX = Math.floor((x + 0.5) * SOURCE_W / width);
      const i = (sourceY * SOURCE_W + sourceX) * 3;
      const r = rgb[i], g = rgb[i + 1], b = rgb[i + 2];
      let color = (r << 16) | (g << 8) | b;
      if (color === (rows ? rows[(sourceY & 3) * SOURCE_W + sourceX] : background)) color = -1;
      else if (quantize !== 1 && color !== background && !EXACT_COLORS.has(color)) {
        color = (snap(r) << 16) | (snap(g) << 8) | snap(b);
      }
      grid[y * width + x] = color;
      if (color < 0) continue;
      const box = found.get(color);
      if (!box) found.set(color, { area: 1, x0: x, y0: y, x1: x + 1, y1: y + 1 });
      else {
        box.area++;
        if (x < box.x0) box.x0 = x;
        if (x >= box.x1) box.x1 = x + 1;
        box.y1 = y + 1;
      }
    }
  }
  const order = [...found].sort((a, b) => b[1].area - a[1].area);
  /** @type {Map<number, number>} */
  const rankOf = new Map(order.map(([color], rank) => [color, rank]));
  const rank = Int32Array.from(grid, (color) => color < 0 ? -1 : /** @type {number} */ (rankOf.get(color)));
  return order.map(([color, box], level) => {
    /** @type {[number, number, number, number][]} */
    const rects = [];
    let above = new Map();
    for (let y = box.y0; y < box.y1; y++) {
      const row = new Map();
      const base = y * width;
      // A span runs over this and later layers; only this color's pixels anchor it.
      for (let x = box.x0; x < box.x1;) {
        if (rank[base + x] < level) {
          x++;
          continue;
        }
        let end = x, first = -1, last = -1;
        while (end < width && rank[base + end] >= level) {
          if (grid[base + end] === color) {
            if (first < 0) first = end;
            last = end;
          }
          end++;
        }
        if (first >= 0) {
          const key = first * 65536 + last;
          let rect = above.get(key);
          if (rect) rect[3] = y + 1;
          else rects.push(rect = [first, y, last + 1, y + 1]);
          row.set(key, rect);
        }
        x = end;
      }
      above = row;
    }
    return /** @type {[`#${string}`, [number, number, number, number][]]} */ ([css(color), rects]);
  });
}

// Keep the solid status fills, eyes and numeral ink. In busy shots the LCD's
// dither can make thousands of tiny shapes. Stay within Herdr's per-render JS
// budget with a smaller sampling grid and a reduced edge/dither palette.
const EXACT_COLORS = new Set([0, 0x3981f6, 0xeef6ff, 0x20c65a, 0xf6758b, 0xffcb00,
  // Light mode: eyes and ink, working, done, needs-you, request.
  0x39404a, 0x94c6ff, 0x83eeac, 0xffa5ac, 0xfff28b]);

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

/** Rectangles plus a per-color overhead: roughly what one frame costs the mod. */
export const frameCost = (/** @type {[string, unknown[]][]} */ ink) =>
  ink.reduce((n, [, rects]) => n + rects.length + 2, 0);

// The widget asks the renderer for an undithered field and few edge colors.
// Two coverage levels keep a one-step soft edge; busier frames use hard edges
// so the mod's main-thread work stays well under the old budget.
export const SOFT_BUDGET = 250;
/** @param {number} levels @param {number} cost of the last frame at those levels */
export function nextLevels(levels, cost) {
  if (levels === 2 && cost > SOFT_BUDGET) return 1;
  if (levels === 1 && cost < SOFT_BUDGET * 0.4) return 2; // Hard edges cost about half.
  return levels;
}

/** @param {Uint8Array} rgb @param {Input} input @param {Uint16Array | null} rawBackdrop */
export function displayFrame(rgb, input, rawBackdrop = null) {
  const { width, background } = input;
  const status = rawBackdrop && input.active > 0 && input.done > 0 && !input.blocked
    ? smoothStatusBackdrop(rawBackdrop, background) : null;
  for (const [sample, quantize] of [[width * 2, 1], [width, 1], [width, 16], [width, 32],
    [width, 48], [Math.ceil(width * 0.75), 48], [Math.ceil(width * 0.5), 64], [Math.ceil(width / 3), 64]]) {
    const ink = packFrame(rgb, sample, background, quantize, status?.rows ?? null);
    if (frameCost(ink) <= (status ? 345 : 385) || sample <= Math.ceil(width / 3)) {
      return [width, background, sample, ink, status?.stops ?? []];
    }
  }
  throw new Error("No display grid");
}

// The mod and its worker talk over one loopback TCP connection, so frames and
// input stay in memory. The launcher prints the worker's port and a one-time
// token; the first line on the connection must be that token.
/** @param {string} serial */
async function launch(serial) {
  const child = spawn(process.execPath, [fileURLToPath(import.meta.url), "--worker", serial], {
    detached: true, stdio: ["ignore", "pipe", "ignore"], windowsHide: true,
  });
  try {
    const stdout = /** @type {import("node:stream").Readable} */ (child.stdout);
    const line = await new Promise((resolve, reject) => {
      let text = "";
      const timer = setTimeout(() => reject(new Error("Renderer did not start")), 5000);
      stdout.setEncoding("utf8").on("data", (/** @type {string} */ chunk) => {
        text += chunk;
        const end = text.indexOf("\n");
        if (end >= 0) {
          clearTimeout(timer);
          resolve(text.slice(0, end));
        }
      });
      child.once("error", reject).once("exit", () => reject(new Error("Renderer exited")));
    });
    if (!/^\d+ [0-9a-f]{48}$/.test(line)) throw new Error("Renderer did not start");
    stdout.destroy();
    child.unref();
    process.stdout.write(line);
  } catch (error) {
    child.kill();
    throw error;
  }
}

/** @param {string} a @param {string} b */
const sameToken = (a, b) => a.length === b.length && timingSafeEqual(Buffer.from(a), Buffer.from(b));

/** @param {string} serial */
async function worker(serial) {
  const token = randomBytes(24).toString("hex");
  const started = performance.now();
  let input = control({});
  let lastInput = started;
  /** The authenticated connection, and the same once its first input arrived. */
  /** @type {import("node:net").Socket | null} */
  let viewer = null;
  /** @type {import("node:net").Socket | null} */
  let client = null;
  let closed = false;
  const server = createServer((socket) => {
    if (viewer) {
      socket.destroy();
      return;
    }
    let text = "";
    let trusted = false;
    const timer = setTimeout(() => socket.destroy(), 2000);
    socket.setNoDelay(true).setEncoding("utf8").on("error", () => {});
    socket.on("close", () => {
      clearTimeout(timer);
      if (socket === viewer) closed = true;
    });
    socket.on("data", (/** @type {string} */ chunk) => {
      text += chunk;
      for (let end = text.indexOf("\n"); end >= 0; end = text.indexOf("\n")) {
        const line = text.slice(0, end);
        text = text.slice(end + 1);
        if (!trusted) {
          if (viewer || !sameToken(line, token)) {
            socket.destroy();
            return;
          }
          trusted = true;
          viewer = socket;
          server.close(); // One viewer per worker; a new view launches its own.
        } else {
          // Keep the last good input if a line is ever malformed.
          let next;
          try { next = control(JSON.parse(line)); } catch { continue; }
          // The first input picks the framing for good; nothing is sent before it.
          if (client) next.pixels = input.pixels;
          input = next;
          if (!client) {
            clearTimeout(timer);
            client = socket;
          }
        }
        lastInput = performance.now();
      }
      if (text.length > 65536) socket.destroy();
    });
  });
  await new Promise((resolve, reject) => server.once("error", reject).listen(0, "127.0.0.1", () => resolve(null)));
  const address = /** @type {import("node:net").AddressInfo} */ (server.address());
  process.stdout.end(`${address.port} ${token}\n`);

  let last = performance.now() - FRAME_MS;
  let connected = false;
  let nextProbe = 0;
  /** @type {Promise<void> | null} */
  let probe = null;
  let levels = 2;
  let tick = performance.now();
  /** @type {Awaited<ReturnType<typeof makeRenderer>> | null} */
  let render = null;
  let lastSequence = 0;
  let lastVisual = "";
  /** @type {Buffer | null} */
  let lastRaw = null;
  let lastBackground = -1;
  let lastFull = 0;
  let blocked = false;
  try {
    for (;;) {
      // The mod sends its input at least every second; silence means it is gone.
      if (closed || performance.now() - lastInput > LEASE_MS) break;
      const socket = client;
      if (!socket) {
        await delay(FRAME_MS);
        continue;
      }
      // USB detection runs a process; never let it hold up a frame.
      if (!probe && performance.now() >= nextProbe) {
        probe = stickPresent(serial).then((present) => {
          connected = present ?? connected;
        }).finally(() => {
          probe = null;
          nextProbe = performance.now() + 1000;
        });
        // Only the first answer is awaited, so a plugged-in Stick never flashes open.
        if (!nextProbe) await probe;
      }
      const now = performance.now();
      const steps = Math.min(4, Math.floor((now - last) / FRAME_MS));
      let visual = "null";
      /** The frame in WASM memory, which the next frame reuses. @type {Uint8Array | null} */
      let raw = null;
      if (connected) {
        render = null;
        last = now - FRAME_MS;
      } else if (steps > 0) {
        if (!render) {
          render = await makeRenderer(await readFile(new URL("avatar.wasm", import.meta.url)));
          // Pixels show the Stick's own dithered field and anti-aliasing;
          // rectangles need the widget display options to stay cheap.
          if (input.pixels) render.setDisplay(true, 0);
          else render.setDisplay(false, levels);
        }
        last = now - ((now - last) % FRAME_MS);
        const rgb = render(input, steps);
        if (input.pixels) {
          raw = rgb;
          visual = "frame";
        } else {
          const shown = displayFrame(rgb, input, render.backdrop());
          const next = nextLevels(levels, frameCost(shown[3]));
          if (next !== levels) render.setDisplay(false, (levels = next));
          visual = JSON.stringify(shown);
        }
      } else {
        await delay(FRAME_MS);
        continue;
      }
      // Resend an unchanged frame twice a second as a heartbeat (pixels: a
      // short "H", and every two seconds the whole frame again as "R", so a
      // frame the mod had to drop doesn't stay missing). Skip frames rather
      // than queue them if the mod falls behind.
      if (input.pixels) {
        const same = raw
          ? lastRaw !== null && lastBackground === input.background && lastRaw.equals(raw)
          : lastVisual === "null";
        /** @type {Buffer | null} */
        let message = null;
        if (!same) message = raw ? encodeMessage("F", input.background, raw) : encodeMessage("N");
        else if (raw && now - lastFull > 2000) message = encodeMessage("R", input.background, raw);
        else if (now - lastSequence > 500) message = encodeMessage("H");
        if (message && !blocked && socket.writableLength < 2 * FRAME_MESSAGE) {
          if (!socket.write(message)) {
            blocked = true;
            socket.once("drain", () => { blocked = false; });
          }
          if (message.length > 5) {
            lastRaw = raw ? message.subarray(8) : null;
            lastFull = now;
          }
          lastVisual = visual;
          lastBackground = input.background;
          lastSequence = now;
        }
      } else if ((visual !== lastVisual || now - lastSequence > 500) && socket.writableLength < 65536) {
        socket.write(`${visual}\n`);
        lastVisual = visual;
        lastSequence = now;
      }
      // Hold a steady cadence: wait out the rest of the interval, not a whole
      // interval after the work, so a slow frame doesn't push every later one.
      let interval = 130;
      if (connected) interval = 250;
      else if (input.focused) interval = 65;
      tick = Math.max(tick + interval, performance.now());
      await delay(tick - performance.now());
    }
  } finally {
    server.close();
    client?.destroy();
  }
}

if (process.argv[1] && pathToFileURL(realpathSync(process.argv[1])).href === import.meta.url) {
  const [mode, serial = ""] = process.argv.slice(2);
  if (mode === "--launch") await launch(serial);
  else if (mode === "--worker") await worker(serial);
  else process.exitCode = 2;
}
