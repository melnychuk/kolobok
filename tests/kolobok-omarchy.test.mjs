// The Omarchy bar icon's JavaScript port of Kolobok (omarchy/kolobok) against the
// menu bar app's own pixels: the site's frames (site/media/kolobok), which the
// Swift app renders (menu-bar's SiteSprites test), and the behaviour they
// don't show. Run with `node --test tests/`.
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { promisify } from "node:util";
import { inflateSync } from "node:zlib";
import * as Kolobok from "../omarchy/kolobok/kolobok-avatar.mjs";
import CELEBRATE from "../site/media/kolobok/celebrate.js";

const run = promisify(execFile);
const S = Kolobok.sizes;

/** A minimal PNG reader: 8-bit RGBA or RGB, not interlaced. */
async function readPng(name) {
  const file = await readFile(new URL(`../site/media/kolobok/${name}.png`, import.meta.url));
  let width = 0, height = 0, type = 0;
  const chunks = [];
  for (let at = 8; at < file.length;) {
    const length = file.readUInt32BE(at), kind = file.toString("ascii", at + 4, at + 8);
    const data = file.subarray(at + 8, at + 8 + length);
    if (kind === "IHDR") {
      width = data.readUInt32BE(0); height = data.readUInt32BE(4);
      assert.equal(data[8], 8, `${name}: bit depth`);
      type = data[9];
      assert.equal(data[12], 0, `${name}: interlaced`);
    } else if (kind === "IDAT") chunks.push(data);
    at += 12 + length;
  }
  const channels = { 2: 3, 6: 4 }[type];
  assert.ok(channels, `${name}: color type ${type}`);
  const raw = inflateSync(Buffer.concat(chunks)), stride = width * channels;
  const out = new Uint8Array(width * height * 4), line = new Uint8Array(stride), prior = new Uint8Array(stride);
  for (let row = 0; row < height; row++) {
    const filter = raw[row * (stride + 1)];
    for (let i = 0; i < stride; i++) {
      const x = raw[row * (stride + 1) + 1 + i], a = i >= channels ? line[i - channels] : 0, b = prior[i];
      const c = i >= channels ? prior[i - channels] : 0;
      const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
      const predict = [0, a, b, (a + b) >> 1, pa <= pb && pa <= pc ? a : pb <= pc ? b : c][filter];
      line[i] = (x + predict) & 255;
    }
    for (let column = 0; column < width; column++) {
      for (let k = 0; k < 4; k++) {
        out[(row * width + column) * 4 + k] = k < channels ? line[column * channels + k] : 255;
      }
    }
    prior.set(line);
  }
  return { width, height, data: out };
}

// The site's frames have SF's digits; the icon draws its label in the bar's font.
const measure = (text) => (text === "!" ? 3 : 8);
const snapshot = (active, blocked, done, healthy = true) => ({ active, blocked, done, healthy });

/** The icon's pixels as the site draws them: white face, star over the base. */
function pixels(view) {
  const px = Kolobok.render(view, 2, [1, 1, 1]);
  const out = new Uint8Array(px.base.length);
  for (let i = 0; i < out.length; i += 4) {
    const base = [...px.base.subarray(i, i + 4)].map((v) => v / 255);
    const star = px.star ? [...px.star.subarray(i, i + 4)].map((v) => v / 255) : [0, 0, 0, 0];
    const alpha = star[3] + base[3] * (1 - star[3]);
    for (let k = 0; k < 3; k++) {
      // Premultiplied, to compare with the PNG's colors under their alpha.
      out[i + k] = Math.round((star[k] * star[3] + base[k] * base[3] * (1 - star[3])) * 255);
    }
    out[i + 3] = Math.round(alpha * 255);
  }
  return { width: px.width, height: px.height, data: out, label: view.left ? view.layout.left : 0 };
}

async function assertMatches(name, view) {
  const ours = pixels(view), theirs = await readPng(name);
  assert.equal(ours.width, theirs.width, `${name}: width`);
  assert.equal(ours.height, theirs.height, `${name}: height`);
  let worst = 0;
  for (let row = 0; row < ours.height; row++) {
    // Not the side's label, which comes from a different font.
    for (let column = ours.label ? (ours.label - S.moat) * 2 : 0; column < ours.width; column++) {
      const i = (row * ours.width + column) * 4, alpha = theirs.data[i + 3] / 255;
      for (let k = 0; k < 4; k++) {
        const value = k < 3 ? theirs.data[i + k] * alpha : theirs.data[i + k];
        worst = Math.max(worst, Math.abs(ours.data[i + k] - value));
      }
    }
  }
  assert.ok(worst <= 2, `${name}: differs by ${worst}`);
}

test("the art export is up to date with the Swift", async () => {
  await run("python3", [new URL("../scripts/export-kolobok-art.py", import.meta.url).pathname, "--check"]);
});

test("states draw the menu bar app's pixels", async () => {
  const states = {
    "idle": [snapshot(0, 0, 0), {}], "idle-blink": [snapshot(0, 0, 0), { blinking: true }],
    "working-1": [snapshot(1, 0, 0), {}], "working-1-blink": [snapshot(1, 0, 0), { blinking: true }],
    "working-2": [snapshot(2, 0, 0), {}], "attention": [snapshot(1, 1, 0), {}],
    "attention-peak": [snapshot(1, 1, 0), { peak: true }], "done": [snapshot(0, 0, 1), {}],
  };
  for (const [name, [s, options]] of Object.entries(states)) {
    await assertMatches(name, Kolobok.describe(s, null, 0, { ...options, measure }));
  }
});

test("a completion plays the menu bar app's celebration, frame for frame", async () => {
  const celebration = { started: 0, before: snapshot(1, 0, 0) };
  assert.ok(Math.abs(Kolobok.celebrationLength(celebration) - CELEBRATE.at(-1).ms / 1000) < 1 / 60);
  for (const { frame, ms } of CELEBRATE) {
    await assertMatches(frame, Kolobok.describe(snapshot(0, 0, 1), celebration, ms / 1000, { measure }));
  }
});

test("only a rise between two healthy snapshots celebrates", () => {
  assert.ok(Kolobok.celebrates(snapshot(1, 0, 0), snapshot(0, 0, 1)));
  assert.ok(!Kolobok.celebrates(snapshot(0, 0, 1), snapshot(1, 0, 1)));
  assert.ok(!Kolobok.celebrates(snapshot(0, 0, 0, false), snapshot(0, 0, 1)));
  assert.ok(!Kolobok.celebrates(null, snapshot(0, 0, 1)));
});

test("the green stays in the sides' band while the burst blasts the face away", () => {
  const celebration = { started: 0, before: snapshot(2, 0, 0) };
  const top = (S.itemHeight - S.sideHeight) / 2 * 2, bottom = top + S.sideHeight * 2;
  let sawFace = false;
  for (let ms = 0; ms < Kolobok.celebrationLength(celebration) * 1000; ms += 1000 / 60) {
    const view = Kolobok.describe(snapshot(1, 0, 1), celebration, ms / 1000, { measure });
    if (!view.star && !view.flash) continue;
    const px = Kolobok.render(view, 2, [1, 1, 1]);
    for (let row = 0; row < px.height; row++) {
      if (row >= top && row < bottom) continue;
      for (let column = 0; column < px.width; column++) {
        const i = (row * px.width + column) * 4;
        // Above and below the band: only the face, never green.
        if (px.star) assert.equal(px.star[i + 3], 0);
        if (px.base[i + 3] > 0) {
          assert.deepEqual([...px.base.subarray(i, i + 3)], [255, 255, 255], `${view.flash ? "flash" : "burst"} ${row}`);
          sawFace = true;
        }
      }
    }
  }
  assert.ok(sawFace);
});

test("with work still active, the face drops in from above onto a blue-to-green bar", () => {
  const L = Kolobok.layout(14, true);
  const back = Kolobok.beats.flatMap((beat) => [0, 0.5, 0.95].map((p) => Kolobok.act(beat, p, L)));
  assert.ok(back.every((a) => a.pose.dx === 0));
  assert.equal(Kolobok.act("peek", 0.2, L).face, "lookingDown");
  const view = Kolobok.describe(snapshot(1, 0, 1), { started: 0, before: snapshot(2, 0, 0) }, 2.0, { measure });
  assert.ok(view.sides.wash);
  const px = Kolobok.render(view, 1, [1, 1, 1]);
  const at = (column) => [...px.base.subarray((9 * px.width + column) * 4, (9 * px.width + column) * 4 + 4)];
  const blue = at(L.faceX - 2), green = at(L.width - 2);
  assert.ok(blue[2] > blue[1] + 50 && green[1] > green[2] + 50, `${blue} ${green}`);
});

test("without headroom the icon keeps to the face's 16 points", () => {
  const celebration = { started: 0, before: snapshot(1, 0, 0) };
  for (let ms = 0; ms < Kolobok.celebrationLength(celebration) * 1000; ms += 1000 / 60) {
    const view = Kolobok.describe(snapshot(0, 0, 1), celebration, ms / 1000, { measure, headroom: 0 });
    const px = Kolobok.render(view, 1, [1, 1, 1]);
    assert.equal(px.height, S.faceHeight);
    if (view.pose) {
      const top = Kolobok.poseTop(view.pose);
      if (view.pose.dx !== 0 || view.pose.lift <= 1) {
        assert.ok(top >= 1 && top + view.pose.height <= S.itemHeight - 1, `${ms}ms: ${JSON.stringify(view.pose)}`);
      }
    }
  }
});

test("the compact avatar is two points shorter, its face filling a 16 px icon at rest", () => {
  const C = Kolobok.compactSizes;
  assert.deepEqual([C.faceHeight, C.sideHeight, C.itemHeight], [S.faceHeight - 2, S.sideHeight - 2, S.itemHeight - 2]);
  assert.ok(C.labelSize < S.labelSize);
  const view = Kolobok.describe(snapshot(2, 0, 1), null, 0, { measure, compact: true });
  const px = Kolobok.render(view, 1, [1, 1, 1]);
  assert.equal(px.height, 16);
  assert.equal(Kolobok.poseTop(view.pose, C), 1);
  // The face fills rows 1-14; the sides keep a point clear of it above and below.
  const alpha = (column, row) => px.base[(row * px.width + column) * 4 + 3];
  const face = view.layout.faceX + C.faceWidth / 2;
  assert.equal(alpha(face, 1), 255);
  assert.equal(alpha(face, 14), 255);
  assert.equal(alpha(face, 0), 0);
  assert.equal(alpha(face, 15), 0);
  for (const row of [0, 1, 14, 15]) assert.equal(alpha(1, row), 0, `side row ${row}`);
  assert.equal(alpha(1, 8), 255);
  assert.equal(px.label.size, C.labelSize);
});

test("the compact avatar's celebration stays in its item, eyes inside the face", () => {
  const C = Kolobok.compactSizes;
  for (const [before, after] of [[snapshot(1, 0, 0), snapshot(0, 0, 1)], [snapshot(2, 0, 0), snapshot(1, 0, 1)]]) {
    const celebration = { started: 0, before };
    for (let ms = 0; ms < Kolobok.celebrationLength(celebration) * 1000; ms += 1000 / 60) {
      const view = Kolobok.describe(after, celebration, ms / 1000, { measure, compact: true });
      const px = Kolobok.render(view, 2, [1, 1, 1]);
      assert.equal(px.height, C.itemHeight * 2);
      if (!view.face) continue;
      const { pose } = view;
      assert.ok(Kolobok.poseTop(pose, C) + pose.height <= C.itemHeight, `${ms}ms bottom`);
      if (pose.dx !== 0 || pose.lift <= 1) assert.ok(Kolobok.poseTop(pose, C) >= 0, `${ms}ms top`);
      const shift = Math.trunc((pose.height - S.faceHeight) / 2);
      for (const [art, , top] of Kolobok.eyes(view.face, view.blinking)) {
        const rows = { open: 7, resting: 5, focused: 5, happy: 4, wide: 8, closed: 1, squint: 4 }[art];
        assert.ok(top + shift >= 1 && top + shift + rows <= pose.height - 1, `${ms}ms ${view.face} eyes`);
      }
    }
  }
});

test("a theme's ink labels the side where it reads clearly, else black or white", () => {
  const blue = Kolobok.rgb(Kolobok.colors.working), yellow = Kolobok.rgb(Kolobok.colors.request);
  const dark = Kolobok.rgb("#030a03"), light = Kolobok.rgb("#f2f0e6"), mid = Kolobok.rgb("#3b4252");
  assert.deepEqual(Kolobok.labelInk(blue, dark), dark);
  assert.deepEqual(Kolobok.labelInk(yellow, dark), dark);
  // A light theme's background doesn't read on blue: the more contrasting of black and white does.
  assert.deepEqual(Kolobok.labelInk(blue, light), [0, 0, 0]);
  assert.deepEqual(Kolobok.labelInk(blue, mid), [0, 0, 0]);
  assert.ok(Kolobok.contrast([0, 0, 0], blue) > Kolobok.contrast([1, 1, 1], blue));
  // The menu bar's own colors without one; the theme's with one.
  const view = Kolobok.describe(snapshot(2, 0, 0), null, 0, { measure, compact: true });
  assert.equal(Kolobok.render(view, 1, [1, 1, 1]).label.color, "white");
  assert.equal(Kolobok.render(view, 1, [1, 1, 1], dark).label.color, "rgb(3, 10, 3)");
  const ask = Kolobok.describe(snapshot(1, 1, 0), null, 0, { measure, compact: true });
  assert.equal(Kolobok.render(ask, 1, [1, 1, 1]).label.color, "black");
  assert.equal(Kolobok.render(ask, 1, [1, 1, 1], light).label.color, "rgb(0, 0, 0)");
});
