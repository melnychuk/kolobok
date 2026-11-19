// Thin JS wrapper around avatar.wasm: the header-widget compositor plus every
// shared firmware kernel (acting, life layer, fluid rim, eye raster, numerals,
// task cues), compiled from this repo by scripts/build-site-wasm.sh.

export const W = 240;
export const H = 135;
export const STATES = ["thinking", "done", "attention", "listening"];
export const MODES = ["offline", "idle", "sleeping", "working", "waiting", "finished", "listening"];
const FRAME_MS = 33; // The preview/header cadence (~30fps).

export async function loadAvatar(url = "avatar.wasm") {
  let memory;
  // The renderer never performs I/O; satisfy the WASI imports with no-ops.
  const wasi = new Proxy(
    {
      random_get(ptr, len) {
        crypto.getRandomValues(new Uint8Array(memory.buffer, ptr, len));
        return 0;
      },
      fd_write(_fd, _iovs, _len, written) {
        new DataView(memory.buffer).setUint32(written, 0, true);
        return 0;
      },
      proc_exit() {},
    },
    { get: (target, name) => target[name] ?? (() => 0) },
  );
  const imports = { wasi_snapshot_preview1: wasi };
  let instance;
  try {
    ({ instance } = await WebAssembly.instantiateStreaming(fetch(url), imports));
  } catch {
    const bytes = await (await fetch(url)).arrayBuffer();
    ({ instance } = await WebAssembly.instantiate(bytes, imports));
  }
  const e = instance.exports;
  memory = e.memory;
  e._initialize?.();
  e.stick_init();
  return new Avatar(e);
}

class Avatar {
  constructor(exports) {
    this.e = exports;
    this.status = { active: 0, blocked: 0, done: 0, listening: false, healthy: true };
    this.rgba = new Uint8Array(W * H * 4);
    this.clock = 0;
    this.last = null;
    this.dirty = false;
    this.awake = false; // hold idle-awake (site intro) instead of dozing
    this.clips = STATES.map((_, s) =>
      Array.from({ length: exports.stick_clip_count(s) }, (_, i) => ({
        state: s,
        index: i,
        name: this.#string(exports.stick_clip_name(s, i)),
        duration: exports.stick_clip_duration(s, i),
      })),
    );
  }

  #string(ptr) {
    const bytes = new Uint8Array(this.e.memory.buffer, ptr);
    let end = 0;
    while (bytes[end]) end++;
    return new TextDecoder().decode(bytes.slice(0, end));
  }

  set(patch) {
    Object.assign(this.status, patch);
    const s = this.status;
    s.active = Math.max(0, Math.min(3999, s.active | 0));
    s.done = Math.max(0, Math.min(3999, s.done | 0));
    s.blocked = Math.max(0, Math.min(s.active, s.blocked | 0));
  }

  // Advance the firmware clock in fixed 33ms steps. A hidden tab never
  // fast-forwards more than a few frames (the renderer clamps dt to 100ms).
  tick(now) {
    if (this.last === null) this.last = now - FRAME_MS;
    let budget = Math.min(now - this.last, FRAME_MS * 4);
    if (budget < FRAME_MS) return false;
    while (budget >= FRAME_MS) {
      this.#step();
      budget -= FRAME_MS;
    }
    this.last = now - budget;
    this.#copy();
    return true;
  }

  // Run the firmware clock ahead without waiting for wall time, e.g. to let it
  // doze off before the first frame is shown. The next tick resumes in real time.
  advance(ms) {
    for (let t = 0; t < ms; t += FRAME_MS) this.#step();
    this.last = null;
    this.#copy();
  }

  #step() {
    const s = this.status;
    this.clock += FRAME_MS;
    if (this.awake) this.e.stick_keep_awake?.();
    this.e.stick_frame(this.clock, s.active, s.blocked, s.done, s.listening ? 1 : 0, s.healthy ? 1 : 0, 0);
  }

  #copy() {
    const src = new Uint8Array(this.e.memory.buffer, this.e.stick_frame_ptr(), W * H * 3);
    const dst = this.rgba;
    for (let i = 0, j = 0; i < src.length; i += 3, j += 4) {
      dst[j] = src[i];
      dst[j + 1] = src[i + 1];
      dst[j + 2] = src[i + 2];
      dst[j + 3] = 255;
    }
    this.dirty = true;
  }

  tap() { this.e.stick_button(0); }
  releaseVoice() { this.e.stick_button(1); }
  startle(side = Math.random() < 0.5 ? -1 : 1) { this.e.stick_startle(side); }
  play(state, index) { this.e.stick_play(state, index); }

  get mode() { return MODES[this.e.stick_mode()] ?? "offline"; }
  get clip() {
    const id = this.e.stick_current_clip();
    if (id < 0) return null;
    return this.clips[Math.floor(id / 100)]?.[id % 100] ?? null;
  }
}
