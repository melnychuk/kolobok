/// <reference lib="dom" />
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { once } from "node:events";
import { copyFile, mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { connect } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import process from "node:process";
import { test } from "node:test";
import { setTimeout as delay } from "node:timers/promises";
import { promisify } from "node:util";
import { control, displayFrame, frameCost, makeRenderer, nextLevels, packFrame, SOFT_BUDGET, smoothStatusBackdrop } from "../herdr/runtime/renderer.mjs";
import { linuxPresence, macPresence } from "../herdr/runtime/presence.mjs";
import { summarize } from "../herdr/stick-s3-avatar/status.js";
import { loadAvatar } from "../site/js/avatar.js";

const wasm = new URL("../site/avatar.wasm", import.meta.url);
const bytes = Uint8Array.from(await readFile(wasm));
const run = promisify(execFile);

test("Herdr statuses are modeled as firmware work (including blocked) and unread done", () => {
  const counts = summarize(["blocked", "working", "working", "done", null, "idle"].map((status) => ({ status })));
  assert.deepEqual(counts, { working: 2, blocked: 1, done: 1 });
  assert.deepEqual(control({ active: counts.working + counts.blocked, blocked: counts.blocked, done: counts.done }),
    { active: 3, blocked: 1, done: 1, focused: false, background: 0, width: 120 });
  assert.equal(control({ width: 200 }).width, 120);
});

test("live sidebar renderer matches the site framebuffer through count and attention changes", async () => {
  const render = await makeRenderer(bytes);
  const site = await loadAvatar(`data:application/wasm;base64,${Buffer.from(bytes).toString("base64")}`);
  const trace = [
    [0, 0, 0, 100], [3, 0, 0, 200], [2, 0, 1, 200],
    [12, 0, 44, 100], [12, 1, 44, 120], [0, 0, 0, 180],
  ];
  for (const [active, blocked, done, frames] of trace) {
    site.set({ active, blocked, done });
    site.advance(frames * 33);
    const expected = Uint8Array.from(site.rgba.filter((_, i) => i % 4 !== 3));
    assert.deepEqual(render(control({ active, blocked, done }), frames), expected);
  }
});

test("idle loses focus, dozes, and wakes; the dark matte is exact", async () => {
  const render = await makeRenderer(bytes);
  const background = 0x0e1311;
  const focused = control({ focused: true, background });
  const idle = control({ background });
  const lit = (/** @type {Uint8Array} */ frame) => frame.filter((value) => value > 200).length;
  const awake = lit(render(focused, 200));
  const asleep = lit(render(idle, 250));
  const wake = lit(render(focused, 60));
  assert.ok(awake > asleep * 2 && wake > asleep * 2);
  assert.deepEqual([...render(focused).slice(0, 3)], [14, 19, 17]);
});

test("packed geometry reconstructs every sampled pixel exactly", async () => {
  const render = await makeRenderer(bytes);
  const width = 192, height = 108, background = 0x0e1311;
  const rgb = render(control({ active: 3, done: 12, background }), 43);
  const pixel = new Uint32Array(width * height).fill(background);
  for (const [color, rects] of packFrame(rgb, width, background)) {
    for (const [x0, y0, x1, y1] of rects) {
      for (let y = y0; y < y1; y++) pixel.fill(Number.parseInt(color.slice(1), 16), y * width + x0, y * width + x1);
    }
  }
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const p = 3 * (Math.floor((y + 0.5) * 135 / height) * 240 + Math.floor((x + 0.5) * 240 / width));
      assert.equal(pixel[y * width + x], (rgb[p] << 16) | (rgb[p + 1] << 8) | rgb[p + 2]);
    }
  }
});

test("paired status sends an unquantized gradient and retains the themed foreground", async () => {
  const render = await makeRenderer(bytes);
  const input = control({ active: 3, done: 2, background: 0x121619 });
  const rgb = render(input, 180);
  const raw = render.backdrop();
  assert.ok(raw);
  const { rows, stops } = smoothStatusBackdrop(raw, input.background);
  const [width, , , ink, paint] = displayFrame(rgb, input, raw);
  assert.equal(width, 120);
  assert.equal(stops.length, 13);
  assert.ok(new Set(stops).size > 8);
  assert.deepEqual(paint, stops);
  assert.ok(Array.isArray(ink) && ink.some(([color]) => color === "#121619"));
  const [,,,, single] = displayFrame(rgb, { ...input, done: 0 }, raw);
  assert.deepEqual(single, []);
  // An untouched backdrop needs no foreground commands at all.
  const backdrop = new Uint8Array(240 * 135 * 3);
  for (let i = 0; i < backdrop.length; i += 3) {
    const c = rows[(i / 3) % rows.length];
    backdrop.set([c >> 16, (c >> 8) & 255, c & 255], i);
  }
  assert.deepEqual(packFrame(backdrop, 240, input.background, 1, rows), []);
});

test("widget display options flatten the field and cut edge colors at full resolution", async () => {
  const colors = (/** @type {Uint8Array} */ rgb) => {
    const seen = new Set();
    for (let i = 0; i < rgb.length; i += 3) seen.add((rgb[i] << 16) | (rgb[i + 1] << 8) | rgb[i + 2]);
    return seen.size;
  };
  const input = control({ active: 2, blocked: 1, background: 0x101114, focused: true });
  const site = await makeRenderer(bytes);
  const widget = await makeRenderer(bytes);
  assert.equal(widget.canSetDisplay, true);
  widget.setDisplay(false, 1);
  const soft = Uint8Array.from(site(input, 150));
  const hard = widget(input, 150);
  assert.ok(colors(hard) * 4 < colors(soft), `${colors(hard)} vs ${colors(soft)} colors`);
  const ink = packFrame(hard, 240, input.background);
  assert.ok(frameCost(ink) < 385);
  // Painted in order, later colors over earlier ones, the rectangles are exact.
  const painted = new Int32Array(240 * 135).fill(-1);
  for (const [color, rects] of ink) {
    for (const [x0, y0, x1, y1] of rects) {
      for (let y = y0; y < y1; y++) painted.fill(Number.parseInt(color.slice(1), 16), y * 240 + x0, y * 240 + x1);
    }
  }
  for (let i = 0; i < painted.length; i++) {
    const color = (hard[i * 3] << 16) | (hard[i * 3 + 1] << 8) | hard[i * 3 + 2];
    assert.equal(painted[i], color === input.background ? -1 : color);
  }
  // Undithered: all four backdrop rows are the same row.
  const rows = /** @type {Uint16Array} */ (widget.backdrop());
  for (let y = 1; y < 4; y++) assert.deepEqual(rows.subarray(y * 240, y * 240 + 240), rows.subarray(0, 240));
  // Switching back restores the site's exact frames from then on.
  widget.setDisplay(true, 0);
  site(input, 1);
  widget(input, 1);
  assert.deepEqual(widget(input, 30), site(input, 30));
});

test("edge levels drop to hard edges over budget and come back with headroom", () => {
  assert.equal(nextLevels(2, SOFT_BUDGET), 2);
  assert.equal(nextLevels(2, SOFT_BUDGET + 1), 1);
  assert.equal(nextLevels(1, SOFT_BUDGET * 0.5), 1);
  assert.equal(nextLevels(1, SOFT_BUDGET * 0.3), 2);
});

test("USB detection requires both IDs on the same macOS device and matches a configured serial", () => {
  const device = (/** @type {number} */ vid, /** @type {number} */ pid) =>
    `  +-o USB Device <class IOUSBHostDevice>\n    "idVendor" = ${vid}\n    "idProduct" = ${pid}\n    "USB Serial Number" = "AA:11"\n`;
  assert.equal(macPresence(device(0x303a, 2) + device(2, 0x1001), ""), false);
  assert.equal(macPresence(device(0x303a, 0x1001), "aa:11"), true);
  assert.equal(macPresence(device(0x303a, 0x1001), "BB:22"), false);
  assert.equal(macPresence(device(0x303a, 0x1001), ""), true);
});

test("Linux USB detection checks sysfs and not the serial device", async (cx) => {
  const folder = await mkdtemp(join(tmpdir(), "stick-usb-test-"));
  cx.after(() => rm(folder, { recursive: true, force: true }));
  const device = join(folder, "1-1");
  await mkdir(device);
  await writeFile(join(device, "idVendor"), "303a\n");
  await writeFile(join(device, "idProduct"), "1001\n");
  await writeFile(join(device, "serial"), "AA:11\n");
  assert.equal(await linuxPresence(folder, "aa:11"), true);
  assert.equal(await linuxPresence(folder, "BB:22"), false);
  await rm(device, { recursive: true });
  assert.equal(await linuxPresence(folder, ""), false);
});

test("the socket worker pauses on USB attach, resumes on removal, and exits on close", async () => {
  const folder = await mkdtemp(join(tmpdir(), "stick-renderer-test-"));
  try {
    const script = join(folder, "renderer.mjs");
    await copyFile(new URL("../herdr/runtime/renderer.mjs", import.meta.url), script);
    const presence = join(folder, "presence.txt");
    await writeFile(presence, "0");
    await writeFile(join(folder, "presence.mjs"), `import { readFile } from "node:fs/promises";
      export const stickPresent = async () => (await readFile(new URL("presence.txt", import.meta.url), "utf8")) === "1";`);
    await copyFile(wasm, join(folder, "avatar.wasm"));
    const { stdout } = await run(process.execPath, [script, "--launch"], { timeout: 2500 });
    const [port, token] = stdout.split(" ");
    assert.match(stdout, /^\d+ [0-9a-f]{48}$/);

    // A wrong token is refused without taking the worker's only seat.
    const stranger = connect(Number(port), "127.0.0.1");
    stranger.on("error", () => {});
    stranger.write("0".repeat(48) + "\n");
    await once(stranger, "close");

    const socket = connect(Number(port), "127.0.0.1");
    socket.setEncoding("utf8");
    /** @type {string[]} */
    const lines = [];
    let text = "";
    socket.on("data", (/** @type {string} */ chunk) => {
      text += chunk;
      for (let end = text.indexOf("\n"); end >= 0; end = text.indexOf("\n")) {
        lines.push(text.slice(0, end));
        text = text.slice(end + 1);
      }
    });
    const initial = { active: 2, focused: true, width: 120 };
    socket.write(`${token}\n${JSON.stringify(initial)}\n`);
    const waitFor = async (/** @type {number | null} */ width) => {
      for (let i = 0; i < 80; i++) {
        const frame = lines.length ? JSON.parse(lines[lines.length - 1]) : undefined;
        if (frame !== undefined && (frame?.[0] ?? null) === width) return;
        await delay(30);
      }
      assert.fail(`No frame for width ${width}`);
    };
    await waitFor(120);
    await writeFile(presence, "1");
    await waitFor(null);
    socket.write(`${JSON.stringify({ ...initial, width: 50, done: 1 })}\n`);
    await writeFile(presence, "0");
    await waitFor(50);
    // Closing our end stops the worker, which then refuses new connections.
    socket.end();
    await once(socket, "close");
    for (let i = 0; i < 40; i++) {
      const probe = connect(Number(port), "127.0.0.1");
      const refused = await once(probe, "connect").then(() => false, () => true);
      probe.destroy();
      if (refused) return;
      await delay(30);
    }
    assert.fail("Worker kept listening after close");
  } finally {
    await rm(folder, { recursive: true, force: true });
  }
});
