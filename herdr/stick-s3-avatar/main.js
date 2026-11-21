import { Background, PathBuilder, View, div } from "gpui-kit";
import { v_flex } from "gpui-base";
import { agents, appHome, exec, layout, readTextFile, run, theme, window as herdrWindow } from "herdr";
import { unlink, writeFile } from "fs/promises";
import { summarize } from "./status.js";

const MOD = "stick-s3-avatar";
const MAX_WIDTH = 120;
const SLOT_HEIGHT = 76;
/** @typedef {{id?: string, size?: {px?: number}, children?: LayoutNode[]}} LayoutNode */
/** @param {LayoutNode | undefined} node @returns {LayoutNode | undefined} */
function avatarBlock(node) {
  if (node?.id === MOD) return node;
  for (const child of node?.children ?? []) {
    const found = avatarBlock(child);
    if (found) return found;
  }
  return undefined;
}

/** @typedef {[`#${string}`, [number, number, number, number][]]} Ink */

export default class StickAvatar extends View {
  width = 44;
  input = "";
  dir = "";
  lastWrite = 0;
  lastPoll = 0;
  lastInput = "";
  lastOutput = "";
  lastEnvelope = "";
  lastFrame = 0;
  retryAt = 0;
  pending = false;
  closed = false;
  active = false;
  /** @type {boolean | null} */
  connected = null;
  unavailable = false;
  sampleWidth = MAX_WIDTH;
  /** @type {`#${string}`[]} */
  backdrop = [];
  /** @type {Ink[]} */
  ink = [];

  /** @param {unknown} _props @param {import('gpui-kit').Context} cx */
  init(_props, cx) {
    cx.timer.every(67, (cx) => this.refresh(cx));
  }

  /** @param {import('gpui-kit').Context} cx */
  refresh(cx) {
    const now = Date.now();
    const changed = this.lastInput !== this.input;
    if (!this.input || this.pending || this.closed || now < this.retryAt) return;
    if ((!this.active || this.connected) && !changed && now - this.lastPoll < 400) return;
    this.lastPoll = now;
    this.pending = true;
    cx.spawn(async (cx) => {
      try {
        if (!this.dir) {
          const home = appHome();
          const mod = `${home}/mods/${MOD}`;
          const root = `${home}/data/${MOD}`;
          const config = await readTextFile(`${mod}/runtime.json`);
          const { node, usbSerial } = JSON.parse(config ?? "{}");
          if (typeof node !== "string") throw new Error("Node path not installed");
          const serial = typeof usbSerial === "string" ? usbSerial : "";
          const result = await exec(node, [`${mod}/renderer.mjs`, "--launch", root, this.input, serial]);
          const name = result.stdout.trim();
          if (result.code !== 0 || !/^view-[a-zA-Z0-9]+$/.test(name)) throw new Error("Renderer did not launch");
          this.dir = `${root}/${name}`;
          this.lastFrame = now;
          if (this.closed) {
            await this.release();
            return;
          }
        }
        if (changed || now - this.lastWrite >= 900) {
          const status = this.input;
          await writeFile(`${this.dir}/input.json`, status);
          this.lastInput = status;
          this.lastWrite = Date.now();
        }
        const envelope = await readTextFile(`${this.dir}/frame.json`);
        if (envelope && envelope !== this.lastEnvelope) {
          const output = envelope.slice(envelope.indexOf("\n") + 1);
          const frame = JSON.parse(output);
          let redraw = this.updatePresence(frame === null);
          if (frame) {
            const [width, background, sample, ink, backdrop = []] = frame;
            const current = JSON.parse(this.input);
            if (width === current.width && background === current.background && output !== this.lastOutput) {
              this.ink = ink;
              this.backdrop = backdrop;
              this.sampleWidth = sample;
              this.lastOutput = output;
              this.unavailable = false;
              redraw = true;
            }
          }
          if (redraw) cx.notify();
          this.lastEnvelope = envelope;
          this.lastFrame = now;
        } else if (now - this.lastFrame > 2500) throw new Error("Renderer stopped publishing");
      } catch (error) {
        console.warn(`Stick avatar: ${error instanceof Error ? error.message : "renderer error"}`);
        await this.release();
        this.lastOutput = "";
        this.lastEnvelope = "";
        this.ink = [];
        this.unavailable = true;
        this.retryAt = Date.now() + 3000;
        if (!this.closed) {
          this.updatePresence(false);
          cx.notify();
        }
      } finally {
        this.pending = false;
      }
    });
  }

  /** @param {boolean} connected */
  updatePresence(connected) {
    if (this.closed || connected === this.connected) return false;
    this.connected = connected;
    this.lastOutput = "";
    this.ink = [];
    const px = connected ? 0 : SLOT_HEIGHT;
    const block = avatarBlock(/** @type {LayoutNode | undefined} */ (layout()));
    // Resize instead of hiding the node: even at 0px, Herdr can load this
    // watcher again after a restart and restore it on unplug.
    if (block && block.size?.px !== px) run("layout.resize", { id: MOD, px });
    return true;
  }

  async release() {
    const dir = this.dir;
    this.dir = "";
    if (dir) {
      try { await unlink(`${dir}/input.json`); } catch { /* The lease also expires on its own. */ }
    }
  }

  async close() {
    this.closed = true;
    await this.release();
  }

  render() {
    const { working, blocked, done } = summarize(agents());
    const t = theme();
    const color = t.chrome ?? "#000000";
    const outline = /** @type {`#${string}`} */ (t.border ?? "#383c40");
    const background = /^#[0-9a-f]{6}$/i.test(color) ? Number.parseInt(color.slice(1), 16) : 0;
    const width = Math.max(2, Math.min(MAX_WIDTH, Math.floor(this.width) - 8));
    const height = width * 9 / 16;
    const radius = Math.min(5, height / 2);
    this.active = herdrWindow()?.active ?? false;
    const input = JSON.stringify({ active: working + blocked, blocked, done, focused: this.active, background, width });
    this.input = input;
    if (this.connected !== false) return div().size_full();
    const label = this.unavailable ? "Stick renderer unavailable"
      : `Stick avatar — Working: ${working}, Needs you: ${blocked}, Done: ${done}`;
    const root = v_flex().id("stick-avatar").size_full().overflow_hidden().items_center().justify_center()
      .role("group").accessibility_label(label).tooltip(label)
      .on_size_change(({ width }, cx) => {
        if (width === this.width) return;
        this.width = width;
        cx.notify();
      });
    if (this.unavailable) return root.child(div().text_size(10).child("Stick unavailable"));
    const frame = div().relative().w(width).h(height).flex_none().rounded(radius).overflow_hidden();
    if (this.lastOutput.startsWith(`[${width},${background},`)) {
      // Draw the LCD backdrop in native gradients, then cover it with the
      // frame's actual face and numeral pixels. A half-size frame maps 1:1
      // onto the original 240×135 pixels on a 2× Retina display.
      const segments = this.backdrop.length - 1;
      for (let i = 0; i < segments; i++) {
        const x0 = i * width / segments, x1 = (i + 1) * width / segments;
        const from = this.backdrop[i], to = this.backdrop[i + 1];
        const geometry = PathBuilder.fill().move_to(x0, 0).line_to(x1, 0)
          .line_to(x1, height).line_to(x0, height).close().build();
        const paint = from === to ? from : Background.linear_gradient(90, from, to);
        frame.child(window.paint_path(geometry, paint).absolute().top(0).left(0).size_full());
      }
      const sx = width / this.sampleWidth;
      const sy = height / Math.round(this.sampleWidth * 9 / 16);
      for (const [color, rects] of this.ink) {
        const geometry = PathBuilder.fill();
        for (const [x0, y0, x1, y1] of rects) {
          geometry.move_to(x0 * sx, y0 * sy).line_to(x1 * sx, y0 * sy)
            .line_to(x1 * sx, y1 * sy).line_to(x0 * sx, y1 * sy).close();
        }
        frame.child(window.paint_path(geometry.build(), color).absolute().top(0).left(0).size_full());
      }
      // GPUI's overflow clip is rectangular, so fill outside the rounded bezel
      // with the sidebar color wherever the live status field reaches a corner.
      const corners = PathBuilder.fill();
      const tangent = radius * (1 - 0.55228475);
      for (const [x, dx] of [[0, 1], [width, -1]]) {
        for (const [y, dy] of [[0, 1], [height, -1]]) {
          corners.move_to(x, y).line_to(x + dx * radius, y)
            .cubic_bezier_to(x, y + dy * radius, x + dx * tangent, y, x, y + dy * tangent).close();
        }
      }
      const matte = /** @type {`#${string}`} */ (`#${background.toString(16).padStart(6, "0")}`);
      frame.child(window.paint_path(corners.build(), matte).absolute().top(0).left(0).size_full());
    }
    // Draw the bezel last so it stays visible where the status field meets an edge.
    return root.child(frame.child(div().absolute().top(0).left(0).size_full()
      .rounded(radius).border(1).border_color(outline)));
  }
}
