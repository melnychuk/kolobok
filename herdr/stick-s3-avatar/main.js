import { Background, PathBuilder, View, div, image } from "gpui-kit";
import { v_flex } from "gpui-base";
import { agents, appHome, exec, layout, readTextFile, run, theme, window as herdrWindow } from "herdr";
import * as herdrApi from "herdr";
import { Buffer } from "buffer";
import { connect } from "net";
import { FRAME_H, FRAME_W, MessageReader } from "./frames.js";
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

/** Relative luminance (WCAG) of a 0xRRGGBB color, 0 black .. 1 white. @param {number} rgb */
function luminance(rgb) {
  const linear = (/** @type {number} */ c) => (c /= 255) <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
  return 0.2126 * linear((rgb >> 16) & 255) + 0.7152 * linear((rgb >> 8) & 255) + 0.0722 * linear(rgb & 255);
}

/**
 * Whether Herdr's window is shown. Herdr releases before `window().visible`
 * don't report it; treat them as always shown.
 * @returns {boolean}
 */
function windowVisible() {
  const info = /** @type {{visible?: boolean} | null | undefined} */ (herdrWindow());
  return info?.visible !== false;
}

/** @typedef {[`#${string}`, [number, number, number, number][]]} Ink */
/** @typedef {[number, number, number, Ink[], `#${string}`[]?]} Frame */
/** @typedef {Awaited<ReturnType<typeof connect>>} Socket */
/**
 * Herdr's setImage, in releases that have it (herdrdev/herdr-app#692).
 * @typedef {(path: string, bytes: Uint8Array | null, options?: {width: number, height: number, format: "rgb" | "rgba"}) => Promise<null>} SetImage
 */

export default class StickAvatar extends View {
  width = 44;
  input = "";
  /** @type {Socket | null} */
  socket = null;
  /** @type {import('gpui-kit').Task | null} */
  timer = null;
  /** Herdr's window isn't shown: no timer, no renderer, until it draws again. */
  hidden = false;
  lastWrite = 0;
  lastInput = "";
  lastOutput = "";
  lastFrame = 0;
  retryAt = 0;
  pending = false;
  writing = false;
  closed = false;
  /** @type {boolean | null} */
  connected = null;
  unavailable = false;
  sampleWidth = MAX_WIDTH;
  /** Width and background of the latest input, which frames must match. */
  shape = [0, 0];
  /** @type {`#${string}`[]} */
  backdrop = [];
  /** @type {Ink[]} */
  ink = [];
  /** Herdr shows the frame's pixels itself (setImage); else rectangles. @type {SetImage | null} */
  setImage = null;
  /** Where this copy's frames show (`image(path)`); no file is written there. */
  path = "";
  /** The newest frame not yet handed to Herdr, and whether one is being handed. */
  /** @type {Uint8Array | null} */
  nextPixels = null;
  showing = false;
  /** Background of the frame on screen, or -1 for none yet. */
  pixelBackground = -1;

  /** @param {unknown} _props @param {import('gpui-kit').Context} cx */
  init(_props, cx) {
    const setImage = /** @type {{setImage?: unknown}} */ (herdrApi).setImage;
    if (typeof setImage === "function") {
      this.setImage = /** @type {SetImage} */ (setImage);
      // One path per mod: a reload replaces its frame instead of leaving one behind.
      this.path = `mods/${MOD}/frame`;
    }
    this.start(cx);
  }

  /** @param {import('gpui-kit').Context} cx */
  start(cx) {
    this.hidden = false;
    this.timer = cx.timer.every(67, (cx) => this.refresh(cx));
  }

  // A hidden window draws nothing and runs no render, so the timer notices
  // and stops everything; render starts it again once the window shows.
  // Herdr releases without `window().visible` never pause.
  pause() {
    this.hidden = true;
    this.timer?.cancel();
    this.timer = null;
    // Closing the connection stops the worker and its USB probe.
    this.release();
  }

  // Frames and input travel over one loopback connection to the local
  // renderer, never through files.
  /** @param {import('gpui-kit').Context} cx */
  refresh(cx) {
    if (!windowVisible()) {
      this.pause();
      return;
    }
    const now = Date.now();
    if (!this.input || this.pending || this.closed || now < this.retryAt) return;
    if (this.socket) {
      if (now - this.lastFrame > 2500) this.fail(cx, "Renderer stopped publishing");
      else if (this.input !== this.lastInput || now - this.lastWrite >= 900) this.send(cx, this.socket);
      return;
    }
    this.pending = true;
    cx.spawn(async (cx) => {
      try {
        const mod = `${appHome()}/mods/${MOD}`;
        const config = await readTextFile(`${mod}/runtime.json`);
        const { node, usbSerial } = JSON.parse(config ?? "{}");
        if (typeof node !== "string") throw new Error("Node path not installed");
        const serial = typeof usbSerial === "string" ? usbSerial : "";
        const result = await exec(node, [`${mod}/renderer.mjs`, "--launch", serial]);
        const [port, token] = result.stdout.trim().split(" ");
        if (result.code !== 0 || !/^\d+$/.test(port) || !/^[0-9a-f]{48}$/.test(token ?? "")) {
          throw new Error("Renderer did not launch");
        }
        const socket = await connect("127.0.0.1", Number(port));
        if (this.closed || this.hidden) {
          socket.close();
          return;
        }
        await socket.write(`${token}\n${this.input}\n`);
        this.lastInput = this.input;
        this.lastWrite = this.lastFrame = Date.now();
        this.socket = socket;
        this.listen(cx, socket);
      } catch (error) {
        this.fail(cx, error);
      } finally {
        this.pending = false;
      }
    });
  }

  /** @param {import('gpui-kit').Context} cx @param {Socket} socket */
  send(cx, socket) {
    if (this.writing) return;
    const status = this.input;
    this.writing = true;
    cx.spawn(async (cx) => {
      try {
        await socket.write(`${status}\n`);
        this.lastInput = status;
        this.lastWrite = Date.now();
      } catch (error) {
        if (socket === this.socket) this.fail(cx, error);
      } finally {
        this.writing = false;
      }
    });
  }

  // One frame per line; only the newest complete line in a read is drawn.
  /** @param {import('gpui-kit').Context} cx @param {Socket} socket */
  listen(cx, socket) {
    if (this.setImage) {
      this.listenPixels(cx, socket);
      return;
    }
    cx.spawn(async (cx) => {
      let text = "";
      try {
        for (;;) {
          const chunk = await socket.read();
          if (socket !== this.socket) return;
          if (!chunk) throw new Error("Renderer closed");
          text += Buffer.from(chunk).toString("utf8");
          const end = text.lastIndexOf("\n");
          if (end < 0) {
            if (text.length > 1 << 20) throw new Error("Renderer frame too large");
            continue;
          }
          const line = text.slice(text.lastIndexOf("\n", end - 1) + 1, end);
          text = text.slice(end + 1);
          this.lastFrame = Date.now();
          // A malformed line throws here and restarts the renderer.
          if (this.show(line, JSON.parse(line))) cx.notify();
        }
      } catch (error) {
        if (socket === this.socket) this.fail(cx, error);
      }
    });
  }

  // Pixel mode: binary messages (frames.js). Only the newest frame in a read
  // goes on; one being handed to Herdr is replaced, never queued.
  /** @param {import('gpui-kit').Context} cx @param {Socket} socket */
  listenPixels(cx, socket) {
    cx.spawn(async (cx) => {
      const reader = new MessageReader();
      try {
        for (;;) {
          const chunk = await socket.read();
          if (socket !== this.socket) return;
          if (!chunk) throw new Error("Renderer closed");
          const messages = reader.push(chunk);
          if (!messages.length) continue;
          this.lastFrame = Date.now();
          let redraw = false;
          for (const message of messages) {
            if (message.type === "H") continue;
            redraw = this.updatePresence(message.type === "N") || redraw;
            if (message.type === "F" && message.background === this.shape[1]) {
              this.nextPixels = message.rgb;
              this.unavailable = false;
            }
          }
          if (redraw) cx.notify();
          if (this.nextPixels && !this.showing) this.showPixels(cx);
        }
      } catch (error) {
        if (socket === this.socket) this.fail(cx, error);
      }
    });
  }

  /** @param {import('gpui-kit').Context} cx */
  showPixels(cx) {
    const setImage = /** @type {SetImage} */ (this.setImage);
    this.showing = true;
    cx.spawn(async (cx) => {
      try {
        while (this.nextPixels && !this.closed) {
          const rgb = this.nextPixels;
          this.nextPixels = null;
          await setImage(this.path, rgb, { width: FRAME_W, height: FRAME_H, format: "rgb" });
          this.pixelBackground = this.shape[1];
          cx.notify();
        }
      } catch (error) {
        this.fail(cx, error);
      } finally {
        this.showing = false;
      }
    });
  }

  /** @param {string} output @param {Frame | null} frame */
  show(output, frame) {
    let redraw = this.updatePresence(frame === null);
    if (frame) {
      const [width, background, sample, ink, backdrop = []] = frame;
      if (width === this.shape[0] && background === this.shape[1] && output !== this.lastOutput) {
        this.ink = ink;
        this.backdrop = backdrop;
        this.sampleWidth = sample;
        this.lastOutput = output;
        this.unavailable = false;
        redraw = true;
      }
    }
    return redraw;
  }

  /** @param {import('gpui-kit').Context} cx @param {unknown} error */
  fail(cx, error) {
    console.warn(`Stick avatar: ${error instanceof Error ? error.message : String(error)}`);
    this.release();
    this.lastOutput = "";
    this.ink = [];
    this.unavailable = true;
    this.retryAt = Date.now() + 3000;
    if (!this.closed) {
      this.updatePresence(false);
      cx.notify();
    }
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

  // Closing the connection stops the worker; if Herdr dies instead, the
  // worker exits once input stops arriving.
  release() {
    const socket = this.socket;
    this.socket = null;
    socket?.close();
  }

  close() {
    this.closed = true;
    this.timer?.cancel();
    this.timer = null;
    this.release();
    // Herdr forgets it with this copy anyway; don't keep the last frame alive.
    if (this.setImage) void this.setImage(this.path, null).catch(() => {});
  }

  /** @param {import('gpui-kit').Context} cx */
  render(cx) {
    const visible = windowVisible();
    // The window shows again (render runs only then): resume. A screenshot of
    // a hidden window renders too, and must not.
    if (this.hidden && visible && !this.closed) this.start(cx);
    const { working, blocked, done } = summarize(agents());
    const t = theme();
    const color = t.chrome ?? "#000000";
    const outline = /** @type {`#${string}`} */ (t.border ?? "#383c40");
    const background = /^#[0-9a-f]{6}$/i.test(color) ? Number.parseInt(color.slice(1), 16) : 0;
    // A light sidebar gets the Stick's light mode: dark gray eyes, paler statuses.
    const light = luminance(background) > 0.5;
    const width = Math.max(2, Math.min(MAX_WIDTH, Math.floor(this.width) - 8));
    const height = width * 9 / 16;
    const radius = Math.min(5, height / 2);
    const focused = herdrWindow()?.active ?? false;
    const input = JSON.stringify({ active: working + blocked, blocked, done, focused, background, light, width,
      pixels: this.setImage !== null });
    this.input = input;
    this.shape = [width, background];
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
    if (this.setImage) {
      // Herdr draws the Stick's own frame, scaled to fit and rounded natively.
      if (this.pixelBackground === background) {
        frame.child(image(this.path).absolute().top(0).left(0).size_full().rounded(radius));
      }
    } else if (this.lastOutput.startsWith(`[${width},${background},`)) {
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
          // Fills close each subpath themselves; an explicit close() would cost
          // a fifth host call per rectangle.
          geometry.move_to(x0 * sx, y0 * sy).line_to(x1 * sx, y0 * sy)
            .line_to(x1 * sx, y1 * sy).line_to(x0 * sx, y1 * sy);
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
