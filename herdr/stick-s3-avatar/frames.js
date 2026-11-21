// Binary messages from the renderer to the mod, used when Herdr can show
// pixels from memory (`setImage`). Each one is a 4-byte little-endian payload
// length, then the payload: a type byte and its data.
//   "F" + background (3 bytes, RGB) + FRAME_W × FRAME_H × 3 RGB bytes: a frame
//   "N": the Stick is plugged in, show nothing
//   "H": heartbeat, nothing changed
// The renderer's encodeMessage writes them; tests check the two agree.

export const FRAME_W = 240;
export const FRAME_H = 135;
export const FRAME_BYTES = FRAME_W * FRAME_H * 3;
const HEADER = 4;
const LIMIT = 1 << 20;

/**
 * @typedef {{type: "F", background: number, rgb: Uint8Array} | {type: "N" | "H"}} Message
 */

/** Reassembles messages from socket reads, which split and join them freely. */
export class MessageReader {
  constructor() {
    /** @type {Uint8Array[]} */
    this.chunks = [];
    this.length = 0;
  }

  /** @param {Uint8Array} chunk @returns {Message[]} the messages it completed */
  push(chunk) {
    this.chunks.push(chunk);
    this.length += chunk.length;
    /** @type {Message[]} */
    const out = [];
    for (;;) {
      if (this.length < HEADER) return out;
      const head = this.peek(HEADER);
      const size = new DataView(head.buffer, head.byteOffset, HEADER).getUint32(0, true);
      if (size < 1 || size > LIMIT) throw new Error(`Renderer message of ${size} bytes`);
      if (this.length < HEADER + size) return out;
      const body = this.take(HEADER + size).subarray(HEADER);
      const type = String.fromCharCode(body[0]);
      if (type === "F") {
        if (size !== 4 + FRAME_BYTES) throw new Error("Renderer frame of the wrong size");
        out.push({ type, background: (body[1] << 16) | (body[2] << 8) | body[3], rgb: body.subarray(4) });
      } else if (type === "N" || type === "H") {
        out.push({ type });
      } else {
        throw new Error(`Renderer message of unknown type ${body[0]}`);
      }
    }
  }

  /** The first `n` buffered bytes, without consuming them. @param {number} n */
  peek(n) {
    if (this.chunks[0].length >= n) return this.chunks[0].subarray(0, n);
    return this.join().subarray(0, n);
  }

  /** Consumes and returns the first `n` buffered bytes. @param {number} n */
  take(n) {
    const first = this.chunks[0].length >= n ? this.chunks[0] : this.join();
    const out = first.subarray(0, n);
    const rest = first.subarray(n);
    this.chunks = rest.length ? [rest, ...this.chunks.slice(1)] : this.chunks.slice(1);
    this.length -= n;
    return out;
  }

  join() {
    const all = new Uint8Array(this.length);
    let at = 0;
    for (const chunk of this.chunks) {
      all.set(chunk, at);
      at += chunk.length;
    }
    this.chunks = [all];
    return all;
  }
}
