// M5Stack StickS3 (K150, 48 × 24 × 15 mm) modelled from the product photos:
// charcoal two-piece shell with crisp edges and a seam, flush 1.14" display,
// thin blue KEY1 pill, KEY2 / power side keys, "STICK S3" side label, HAT2 2×8
// header + speaker + IR on one end, USB-C + Grove on the other, pinout sticker.
// The device lies on its side (KEY2 edge down) with the live WebAssembly face.
import * as THREE from "three";
import { RoundedBoxGeometry } from "three/addons/geometries/RoundedBoxGeometry.js";
import { RoomEnvironment } from "three/addons/environments/RoomEnvironment.js";
import { CSS2DRenderer, CSS2DObject } from "three/addons/renderers/CSS2DRenderer.js";
import { W, H } from "./avatar.js";
import { SCREEN, fovFor } from "./intro.js";

export { SCREEN }; // ST7789P3 active area

// Landscape: x = length (USB-C end at −x, HAT end at +x), y = width, z = depth.
const L = 48, Wd = 24, D = 15, R = 3; // corner radius seen from the front
const FRONT = D / 2, BACK = -D / 2, SEAM = -2.6;
const KEY1 = { x: -11, len: 10.6, wid: 2.5 };
export const FLOOR = -Wd / 2 - 3.2; // the desk under the (hovering) stick

export const SHELLS = {
  stock: { body: 0x35373c, key: 0x1d8fea, label: "StickS3 charcoal" },
  snow: { body: 0xdfe2e8, key: 0x1d8fea, label: "Snow" },
  tangerine: { body: 0xf26b38, key: 0x16181d, label: "Tangerine" },
  mint: { body: 0x8fdcbc, key: 0xf7768e, label: "Mint" },
};

// ---------------------------------------------------------------- helpers
function roundedRect(w, h, r) {
  const s = new THREE.Shape();
  const x = -w / 2, y = -h / 2;
  s.moveTo(x + r, y);
  s.lineTo(x + w - r, y);
  s.absarc(x + w - r, y + r, r, -Math.PI / 2, 0);
  s.lineTo(x + w, y + h - r);
  s.absarc(x + w - r, y + h - r, r, 0, Math.PI / 2);
  s.lineTo(x + r, y + h);
  s.absarc(x + r, y + h - r, r, Math.PI / 2, Math.PI);
  s.lineTo(x, y + r);
  s.absarc(x + r, y + r, r, Math.PI, 1.5 * Math.PI);
  return s;
}
// Extruded slab with a small edge radius: crisp silhouette, soft highlight.
function slab(w, h, r, z0, z1, bevel) {
  const depth = z1 - z0 - 2 * bevel;
  const geo = new THREE.ExtrudeGeometry(roundedRect(w - 2 * bevel, h - 2 * bevel, Math.max(0.05, r - bevel)), {
    depth, bevelEnabled: true, bevelThickness: bevel, bevelSize: bevel, bevelSegments: 4, curveSegments: 16,
  });
  geo.translate(0, 0, z0 + bevel);
  return geo;
}
function outlinePoints(w, h, r, z = 0) {
  return roundedRect(w, h, r).getPoints(12).map((p) => new THREE.Vector3(p.x, p.y, z));
}
function canvasTexture(width, height, draw) {
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  draw(canvas.getContext("2d"), width, height);
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.anisotropy = 8;
  return texture;
}
function pill(g, x, y, w, h) {
  g.beginPath();
  g.roundRect(x, y, w, h, Math.min(w, h) / 2);
  g.fill();
}
function makeTag(text, cls = "") {
  const el = document.createElement("div");
  el.className = `bp-tag ${cls}`;
  el.textContent = text;
  return new CSS2DObject(el);
}
// Exponential easing expressed per 60 fps frame, scaled by the real frame time
// so transitions take the same wall-clock time on slow and fast devices.
let frameScale = 1;
const damp = (a, b, k) => a + (b - a) * (1 - (1 - k) ** frameScale);

// ---------------------------------------------------------------- device
export class Device {
  constructor(container, avatar) {
    this.container = container;
    this.avatar = avatar;
    this.listeners = {};
    this.blueprint = 0;
    this.blueprintTarget = 0;
    this.flipped = false;
    this.flipAngle = 0;
    this.pressDepth = 0;
    this.pressed = false;
    this.shakeAt = -1; // seconds; a loud noise rattles the stick briefly
    // Subtle look-at-the-cursor tilt (mouse only): pointer in client px, and
    // the eased tilt in radians. Null pointer = level.
    this.cursor = null;
    this.tilt = { x: 0, y: 0 };
    this.glow = new THREE.Color(0x3b82f6);
    this.glowTarget = new THREE.Color(0x3b82f6);
    this.reduceMotion = matchMedia("(prefers-reduced-motion: reduce)").matches;
    // Camera choreography: a scroll-driven pose plus a springy drag offset.
    this.pose = { az: -18, el: 9, dist: 178, tx: 0, ty: 0, sx: 0.18, sy: 0, exposure: 1.05, reveal: 1 };
    this.view = { ...this.pose };
    this.drag = { az: 0, el: 0, enabled: false };
    this.snapNext = true;

    this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    this.renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.05;
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    container.appendChild(this.renderer.domElement);

    this.labels = new CSS2DRenderer();
    this.labels.domElement.className = "bp-layer";
    container.appendChild(this.labels.domElement);

    this.scene = new THREE.Scene();
    const pmrem = new THREE.PMREMGenerator(this.renderer);
    this.scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
    this.camera = new THREE.PerspectiveCamera(26, 1, 1, 1000);

    const key = new THREE.DirectionalLight(0xffffff, 1.5);
    key.position.set(-30, 70, 60);
    this.scene.add(key);
    const rim = new THREE.DirectionalLight(0xbfd4ff, 0.9);
    rim.position.set(40, 20, -60);
    this.scene.add(rim);
    this.statusLight = new THREE.PointLight(0x3b82f6, 700, 140, 2);
    this.statusLight.position.set(0, FLOOR + 2, 22);
    this.scene.add(this.statusLight);

    this.root = new THREE.Group(); // hover
    this.rig = new THREE.Group(); // flip (IMU)
    this.root.add(this.rig);
    this.scene.add(this.root);
    this.fadeable = [];
    this.#build();
    this.#depthPrepass();
    this.#floor();
    this.#blueprint();
    this.#pointer();
    this.setShell("stock");

    new ResizeObserver(() => this.resize()).observe(container);
    this.resize();
  }

  on(name, fn) {
    this.listeners[name] ??= [];
    this.listeners[name].push(fn);
  }
  #emit(name, ...args) {
    for (const fn of this.listeners[name] ?? []) fn(...args);
  }

  // While the shell fades in, draw its depth first (opaque pass, no colour) so
  // only its front-most surfaces blend in, not an x-ray of screws and seams.
  #depthPrepass() {
    const fade = new Set(this.fadeable);
    const depthOnly = new THREE.MeshBasicMaterial({ colorWrite: false });
    const meshes = [];
    this.rig.traverse((o) => o.isMesh && fade.has(o.material) && !o.material.userData.decal && meshes.push(o));
    this.prepass = meshes.map((m) => {
      const d = new THREE.Mesh(m.geometry, depthOnly);
      m.add(d);
      return d;
    });
  }

  #mat(props, Kind = THREE.MeshPhysicalMaterial) {
    const m = new Kind({ transparent: true, ...props });
    this.fadeable.push(m);
    return m;
  }

  #build() {
    const add = (geo, mat, parent = this.rig) => {
      const mesh = new THREE.Mesh(geo, mat);
      parent.add(mesh);
      return mesh;
    };
    // Matte, slightly satin PC shell.
    this.shellMat = this.#mat({ color: 0x35373c, roughness: 0.6, clearcoat: 0.12, clearcoatRoughness: 0.6, envMapIntensity: 0.45 });
    this.keyMat = this.#mat({ color: 0x1d8fea, roughness: 0.32, clearcoat: 0.7, emissive: 0x0b3d70, emissiveIntensity: 0.12, envMapIntensity: 0.7 });
    const seamMat = this.#mat({ color: 0x141518, roughness: 0.8 });
    const ink = this.#mat({ color: 0x050507, roughness: 0.14, clearcoat: 1 });
    const metal = this.#mat({ color: 0xc7cbd3, metalness: 1, roughness: 0.28 }, THREE.MeshStandardMaterial);
    const brass = this.#mat({ color: 0xc9a227, metalness: 1, roughness: 0.32 }, THREE.MeshStandardMaterial);

    // Two-piece shell: thick front (display) half, thin back half, dark seam.
    add(slab(L, Wd, R, SEAM, FRONT, 0.85), this.shellMat);
    add(slab(L, Wd, R, BACK, SEAM - 0.25, 0.6), this.shellMat);
    add(slab(L - 0.9, Wd - 0.9, R - 0.4, SEAM - 0.6, SEAM + 0.3, 0.05), seamMat);

    // Flush glass window: thin black border around the live display.
    const glass = add(slab(SCREEN.w + 1.3, SCREEN.h + 1.3, 0.6, FRONT - 0.5, FRONT + 0.04, 0.08), ink);
    glass.position.x = SCREEN.x;
    this.pixels = new THREE.DataTexture(this.avatar.rgba, W, H, THREE.RGBAFormat);
    this.pixels.colorSpace = THREE.SRGBColorSpace;
    // Linear, not nearest: the firmware already antialiases, and up close
    // (the intro) nearest-neighbour turns its soft edges into stair steps.
    this.pixels.magFilter = THREE.LinearFilter;
    this.pixels.minFilter = THREE.LinearFilter;
    this.pixels.flipY = true;
    this.pixels.center.set(0.5, 0.5);
    this.pixels.needsUpdate = true;
    this.screenMat = new THREE.MeshBasicMaterial({ map: this.pixels, toneMapped: false, transparent: true });
    // Intro: key the screen against black so only the face floats on the page.
    // The firmware antialiases against black in sRGB, so un-mix there: alpha is
    // the brightest channel, colour is the pixel divided by it.
    this.screenKey = { value: 0 };
    this.screenMat.onBeforeCompile = (shader) => {
      shader.uniforms.uKey = this.screenKey;
      shader.fragmentShader = shader.fragmentShader
        .replace("void main() {", "uniform float uKey;\nvoid main() {")
        .replace(
          "#include <map_fragment>",
          `#include <map_fragment>
          vec3 srgb = pow(max(diffuseColor.rgb, 0.0), vec3(1.0 / 2.2));
          float keyAlpha = max(max(srgb.r, srgb.g), srgb.b);
          diffuseColor.rgb = mix(diffuseColor.rgb, pow(srgb / max(keyAlpha, 1e-3), vec3(2.2)), uKey);
          diffuseColor.a *= mix(1.0, keyAlpha, uKey);`,
        );
    };
    const screen = add(new THREE.PlaneGeometry(SCREEN.w, SCREEN.h), this.screenMat);
    screen.position.set(SCREEN.x, 0, FRONT + 0.05);
    this.glassMat = new THREE.MeshPhysicalMaterial({ color: 0x000000, roughness: 0.03, transparent: true, opacity: 0.06, envMapIntensity: 1.4 });
    const sheen = add(new THREE.PlaneGeometry(SCREEN.w + 1.3, SCREEN.h + 1.3), this.glassMat);
    sheen.position.set(SCREEN.x, 0, FRONT + 0.07);

    // KEY1 ("A"): a thin blue pill in a dark slot, beside the display.
    const slot = add(new THREE.ShapeGeometry(roundedRect(KEY1.wid + 0.9, KEY1.len + 0.9, (KEY1.wid + 0.9) / 2), 12), seamMat);
    slot.position.set(KEY1.x, 0, FRONT + 0.01);
    this.button = new THREE.Group();
    this.button.position.set(KEY1.x, 0, FRONT);
    this.rig.add(this.button);
    add(slab(KEY1.wid, KEY1.len, KEY1.wid / 2, -0.4, 0.75, 0.35), this.keyMat, this.button);
    // Generous invisible hit area so the tiny key is easy to press.
    const hit = add(new THREE.BoxGeometry(9, 18, 4), new THREE.MeshBasicMaterial({ visible: false }), this.button);
    this.hitTargets = [hit];

    // Top edge: "STICK │S3" label and the power key (hold: boot, double: off).
    const label = add(
      new THREE.PlaneGeometry(22, 4.2),
      this.#mat({
        roughness: 0.5,
        map: canvasTexture(1056, 202, (g, w, h) => {
          g.fillStyle = "#4a4c52";
          g.fillRect(0, 0, w, h);
          g.fillStyle = "#1d8fea";
          g.beginPath();
          g.moveTo(w * 0.7, h);
          g.lineTo(w * 0.73, 0);
          g.lineTo(w, 0);
          g.lineTo(w, h);
          g.fill();
          g.fillStyle = "#e8e9ec";
          g.font = `300 ${h * 0.78}px "Montserrat", "Bricolage Grotesque", system-ui, sans-serif`;
          g.textBaseline = "middle";
          g.fillText("STICK", w * 0.04, h * 0.54);
          g.fillStyle = "#fff";
          g.font = `800 ${h * 0.8}px "Bricolage Grotesque", system-ui, sans-serif`;
          g.fillText("S3", w * 0.79, h * 0.54);
        }),
      }),
    );
    label.rotation.x = -Math.PI / 2;
    label.position.set(1, Wd / 2 + 0.01, 1.6);
    const power = add(new RoundedBoxGeometry(4.2, 1.6, 5.2, 3, 0.6), this.shellMat);
    power.position.set(-18.2, Wd / 2 + 0.15, 1.4);
    const powerRing = add(new THREE.PlaneGeometry(5.2, 6.2), seamMat);
    powerRing.rotation.x = -Math.PI / 2;
    powerRing.position.set(-18.2, Wd / 2 + 0.005, 1.4);
    // Bottom edge: KEY2.
    const key2 = add(new RoundedBoxGeometry(4.6, 1.6, 5.6, 3, 0.6), this.shellMat);
    key2.position.set(15.5, -Wd / 2 - 0.15, 1.4);

    // End caps are decals: transparent except for the ports, kept inside the
    // flat part of each end wall (y ±9; front half z −1.75…6.65, back half
    // z −6.9…−3.45) so nothing overhangs the rounded corners or the seam.
    const decal = (u0, draw) => {
      const m = this.#mat({
        roughness: 0.55,
        alphaTest: 0.02,
        depthWrite: false,
        polygonOffset: true,
        polygonOffsetFactor: -2,
        polygonOffsetUnits: -2,
        map: canvasTexture(280, 440, (g, w) => {
          const mm = w / 14;
          draw(g, (z) => u0(z) * mm, (y) => (11 - y) * mm, mm);
        }),
      });
      m.userData.decal = true;
      const mesh = add(new THREE.PlaneGeometry(14, 22), m);
      mesh.renderOrder = 1; // after the shell, so it lands on top of it
      return mesh;
    };
    const rrect = (g, x, y, w, h, r) => {
      g.beginPath();
      g.roundRect(x, y, w, h, r);
      g.fill();
    };

    // HAT end (+x): HAT2 header (front half), IR TX/RX, speaker slots (back half).
    const hat = decal((z) => 7 - z, (g, u, v, mm) => {
      g.fillStyle = "#0b0b0d"; // header body, z −1.2…4.4
      rrect(g, u(4.4), v(8.5), 5.6 * mm, 17 * mm, 0.6 * mm);
      g.fillStyle = "#000";
      for (const z of [0.55, 2.65])
        for (let r = 0; r < 8; r++) g.fillRect(u(z) - 0.65 * mm, v(7.35 - r * 2.1) - 0.65 * mm, 1.3 * mm, 1.3 * mm);
      g.fillStyle = "#0d0d10"; // IR receiver window
      rrect(g, u(6.2), v(8.2), 1.4 * mm, 1.8 * mm, 0.3 * mm);
      g.fillStyle = "#cfd6e4"; // IR LED
      g.beginPath();
      g.arc(u(5.5), v(-7.4), 0.6 * mm, 0, Math.PI * 2);
      g.fill();
      g.fillStyle = "#15161a"; // speaker slots, z −6.3…−3.9
      for (const y of [-1.6, 0, 1.6]) pill(g, u(-3.9), v(y) - 0.45 * mm, 2.4 * mm, 0.9 * mm);
    });
    hat.rotation.y = Math.PI / 2;
    hat.position.set(L / 2 + 0.01, 0, 0);

    // USB end (−x): USB-C on the front half, the Grove (HY2.0-4P) opening on
    // the back half: flush dark sockets, not protruding parts.
    const usb = decal((z) => 7 + z, (g, u, v, mm) => {
      g.fillStyle = "#050506";
      pill(g, u(1.0), v(4.5), 3.3 * mm, 9 * mm);
      g.fillStyle = "#9aa0aa";
      g.fillRect(u(2.35), v(3.1), 0.6 * mm, 6.2 * mm);
      g.fillStyle = "#26282c"; // Grove: a shallow bezel and a dark mouth
      rrect(g, u(-6.55), v(4.4), 2.9 * mm, 8.8 * mm, 0.5 * mm);
      g.fillStyle = "#0a0a0c";
      rrect(g, u(-6.2), v(3.8), 2.2 * mm, 7.6 * mm, 0.35 * mm);
    });
    usb.rotation.y = -Math.PI / 2;
    usb.position.set(-L / 2 - 0.01, 0, 0);

    // Back: the colourful M5 pinout sticker, two screws and brass inserts.
    const sticker = add(
      new THREE.PlaneGeometry(40, 20),
      this.#mat({
        roughness: 0.45,
        map: canvasTexture(1200, 600, (g, w, h) => this.#drawSticker(g, w, h)),
      }),
    );
    sticker.rotation.y = Math.PI;
    sticker.position.set(2.6, 0, BACK - 0.01);
    for (const y of [-8.4, 8.4]) {
      const screw = add(new THREE.CylinderGeometry(1.05, 1.05, 0.4, 20), metal);
      screw.rotation.x = Math.PI / 2;
      screw.position.set(-20.4, y, BACK - 0.1);
      const insert = add(new THREE.CylinderGeometry(0.85, 0.85, 0.3, 20), brass);
      insert.rotation.x = Math.PI / 2;
      insert.position.set(-16, y * 1.1, BACK - 0.08);
    }
  }

  #drawSticker(g, w, h) {
    // Seen from the back: texture-left is the HAT (+x) end.
    g.fillStyle = "#fbfbf8";
    g.beginPath();
    g.roundRect(0, 0, w, h, 40);
    g.fill();
    const tag = (text, x, y, bg, fg = "#fff", size = 34, pad = 10) => {
      g.font = `800 ${size}px "JetBrains Mono", ui-monospace, monospace`;
      const tw = g.measureText(text).width + pad * 2;
      g.fillStyle = bg;
      g.beginPath();
      g.roundRect(x, y, tw, size * 1.3, 6);
      g.fill();
      g.fillStyle = fg;
      g.textBaseline = "middle";
      g.fillText(text, x + pad, y + size * 0.68);
      return tw;
    };
    const pins = [["G5", "#555"], ["G4", "#555"], ["G6", "#555"], ["G7", "#555"], ["G0", "#555"], ["G1", "#555"], ["G8", "#555"], ["G2", "#555"]];
    for (const [i, [p, c]] of pins.entries()) tag(p, 18, 26 + i * 70, c, "#fff", 30);
    const power = [["GND", "#111"], ["5V", "#e23d3d"], ["BAT", "#f2b705"], ["3V3", "#e23d3d"], ["G43", "#2b7de0"], ["G44", "#2b7de0"], ["G3", "#555"], ["G9", "#555"]];
    for (const [i, [p, c]] of power.entries()) tag(p, 112, 26 + i * 70, c, c === "#f2b705" ? "#111" : "#fff", 30);
    let x = 230;
    x += tag("M5STICK-S3", x, 30, "#2b7de0", "#fff", 46) + 16;
    tag("1.14\" IPS-LCD", x, 30, "#fff", "#111", 38);
    g.strokeStyle = "#111";
    g.lineWidth = 3;
    g.strokeRect(x, 30, g.measureText("1.14\" IPS-LCD").width + 20, 50);
    tag("ST7789P3", 230, 100, "#7c5ce0", "#fff", 36);
    tag("BMI270", 470, 100, "#11a37f", "#fff", 36);
    tag("ES8311", 230, 166, "#111", "#fff", 36);
    tag("AW8737", 420, 166, "#e0662b", "#fff", 36);
    tag("ESP32-S3-PICO", 230, 232, "#d6332f", "#fff", 36);
    tag("250mAh", 600, 232, "#f2b705", "#111", 36);
    // A little cameo of the avatar that lives on the other side.
    g.fillStyle = "#06070b";
    g.beginPath();
    g.roundRect(230, 320, 300, 170, 44);
    g.fill();
    g.fillStyle = "#ecf4ff";
    g.beginPath();
    g.roundRect(290, 360, 70, 82, 22);
    g.roundRect(400, 360, 70, 82, 22);
    g.fill();
    g.fillStyle = "#111";
    g.font = `800 40px "Bricolage Grotesque", system-ui, sans-serif`;
    g.fillText("Copilot Stick S3", 560, 360);
    g.fillStyle = "#666";
    g.font = `500 28px "JetBrains Mono", monospace`;
    g.fillText("a tiny face for your agents", 560, 410);
    // M5 STACK block + KEY1 note, near the USB end like the real label.
    g.fillStyle = "#2b7de0";
    g.font = `800 120px "Bricolage Grotesque", system-ui, sans-serif`;
    g.fillText("M5", 920, 130);
    g.font = `800 44px "Bricolage Grotesque", system-ui, sans-serif`;
    g.fillText("STACK", 925, 200);
    tag("KEY1:G11", 900, 250, "#2b7de0", "#fff", 36);
    tag("PORT.A", 930, 470, "#111", "#fff", 32);
    tag("G9", 1080, 470, "#f2b705", "#111", 32);
    tag("G10", 1080, 525, "#555", "#fff", 32);
  }

  #floor() {
    const tex = canvasTexture(256, 256, (g, w, h) => {
      const grad = g.createRadialGradient(w / 2, h / 2, 0, w / 2, h / 2, w / 2);
      grad.addColorStop(0, "rgba(0,0,0,.6)");
      grad.addColorStop(0.5, "rgba(0,0,0,.25)");
      grad.addColorStop(1, "rgba(0,0,0,0)");
      g.fillStyle = grad;
      g.fillRect(0, 0, w, h);
    });
    this.shadow = new THREE.Mesh(
      new THREE.PlaneGeometry(70, 26),
      new THREE.MeshBasicMaterial({ map: tex, transparent: true, depthWrite: false }),
    );
    this.shadow.rotation.x = -Math.PI / 2;
    this.shadow.position.y = FLOOR;
    this.scene.add(this.shadow);
    // Use normal alpha blending so the glow fades into the CSS background
    // behind the transparent renderer. Leave a fully clear rim on the mesh.
    this.poolMat = new THREE.MeshBasicMaterial({
      transparent: true, depthWrite: false, color: 0x3b82f6,
      map: canvasTexture(256, 256, (g, w, h) => {
        const grad = g.createRadialGradient(w / 2, h / 2, 0, w / 2, h / 2, w / 2);
        grad.addColorStop(0, "rgba(255,255,255,.28)");
        grad.addColorStop(0.28, "rgba(255,255,255,.21)");
        grad.addColorStop(0.50, "rgba(255,255,255,.10)");
        grad.addColorStop(0.72, "rgba(255,255,255,.025)");
        grad.addColorStop(0.92, "rgba(255,255,255,0)");
        g.fillStyle = grad;
        g.fillRect(0, 0, w, h);
      }),
    });
    const pool = new THREE.Mesh(new THREE.PlaneGeometry(110, 60), this.poolMat);
    pool.rotation.x = -Math.PI / 2;
    pool.position.set(0, FLOOR - 0.05, 12);
    this.scene.add(pool);
  }

  #blueprint() {
    this.lineMat = new THREE.LineBasicMaterial({ color: 0x9fd2ff, transparent: true, opacity: 0 });
    this.dimMat = new THREE.LineDashedMaterial({ color: 0xfacc15, transparent: true, opacity: 0, dashSize: 1.2, gapSize: 0.8 });
    this.outline = new THREE.Group();
    const outline = this.outline;
    const line = (points, loop = true, parent = outline) => {
      const geo = new THREE.BufferGeometry().setFromPoints(points);
      const obj = loop ? new THREE.LineLoop(geo, this.lineMat) : new THREE.Line(geo, this.lineMat);
      parent.add(obj);
      return obj;
    };
    const ring = (w, h, r, z, x = 0, y = 0, parent) => {
      const l = line(outlinePoints(w, h, r, z), true, parent);
      l.position.set(x, y, 0);
      return l;
    };
    // Shell: face perimeters, shoulders, seam, and the eight straight edges.
    ring(L - 1.7, Wd - 1.7, R - 0.85, FRONT);
    ring(L, Wd, R, FRONT - 0.85);
    ring(L, Wd, R, SEAM + 0.85);
    ring(L - 0.9, Wd - 0.9, R - 0.4, SEAM - 0.12);
    ring(L, Wd, R, SEAM - 0.85);
    ring(L, Wd, R, BACK + 0.6);
    ring(L - 1.2, Wd - 1.2, R - 0.6, BACK);
    const v = (x, y, z) => new THREE.Vector3(x, y, z);
    for (const sx of [-1, 1])
      for (const sy of [-1, 1]) {
        line([v(sx * (L / 2), sy * (Wd / 2 - R), FRONT - 0.85), v(sx * (L / 2), sy * (Wd / 2 - R), BACK + 0.6)], false);
        line([v(sx * (L / 2 - R), sy * (Wd / 2), FRONT - 0.85), v(sx * (L / 2 - R), sy * (Wd / 2), BACK + 0.6)], false);
      }
    // Display, KEY1, side keys, ports.
    ring(SCREEN.w + 1.3, SCREEN.h + 1.3, 0.6, FRONT + 0.06, SCREEN.x);
    ring(SCREEN.w, SCREEN.h, 0.05, FRONT + 0.08, SCREEN.x);
    this.keyOutline = ring(KEY1.wid, KEY1.len, KEY1.wid / 2, FRONT + 0.8, KEY1.x);
    const sideRect = (w, d, x, y, z) => {
      const l = ring(w, d, 0.6, 0, x, y);
      l.rotation.x = Math.PI / 2;
      l.position.z = z;
    };
    sideRect(4.2, 5.2, -18.2, Wd / 2 + 0.95, 1.4);
    sideRect(4.6, 5.6, 15.5, -Wd / 2 - 0.95, 1.4);
    sideRect(22, 4.2, 1, Wd / 2 + 0.02, 1.6);
    const endRect = (w, h, r, x, z) => {
      const l = ring(w, h, r, 0, 0, 0);
      l.rotation.y = Math.PI / 2;
      l.position.set(x, 0, z);
    };
    endRect(3.3, 9, 1.6, -L / 2 - 0.03, 2.65);
    endRect(2.9, 8.8, 0.5, -L / 2 - 0.03, -5.1);
    endRect(5.6, 17, 0.6, L / 2 + 0.03, 1.6);
    this.rig.add(outline);

    // Dimensions and callouts.
    this.dims = new THREE.Group();
    const dims = this.dims;
    const dim = (a, b, text, tick) => {
      const l = new THREE.Line(new THREE.BufferGeometry().setFromPoints([a, b]), this.dimMat);
      l.computeLineDistances();
      dims.add(l);
      for (const end of [a, b])
        dims.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints([end.clone().add(tick), end.clone().sub(tick)]), this.dimMat));
      const tag = makeTag(text, "bp-dim");
      tag.position.copy(a.clone().add(b).multiplyScalar(0.5));
      dims.add(tag);
    };
    dim(v(-L / 2, -Wd / 2 - 4.5, FRONT), v(L / 2, -Wd / 2 - 4.5, FRONT), "48 mm", v(0, 1.2, 0));
    dim(v(L / 2 + 4.5, -Wd / 2, FRONT), v(L / 2 + 4.5, Wd / 2, FRONT), "24 mm", v(1.2, 0, 0));
    dim(v(L / 2 + 4.5, Wd / 2 + 2, FRONT), v(L / 2 + 4.5, Wd / 2 + 2, BACK), "15 mm", v(0, 1.2, 0));
    const callout = (text, x, y, z) => {
      const tag = makeTag(text, "bp-callout");
      tag.position.set(x, y, z);
      dims.add(tag);
    };
    callout("1.14″ IPS · 240×135", SCREEN.x, SCREEN.h / 2 + 3, FRONT);
    callout("KEY1 · tap / hold", KEY1.x, KEY1.len / 2 + 3.4, FRONT + 1);
    callout("PWR", -18.2, Wd / 2 + 3.4, 2);
    callout("KEY2", 15.5, -Wd / 2 - 3.6, 2);
    callout("USB‑C · Grove", -L / 2 - 3, -6.5, 0);
    callout("HAT2 2×8 · IR · SPK", L / 2 + 2, 7.5, -2);
    this.rig.add(dims);
    this.#applyVisibility();
  }

  // One place for both fades: blueprint (x-ray) and reveal (intro: face only).
  #applyVisibility() {
    const t = this.blueprint;
    const r = this.view.reveal;
    this.lineMat.opacity = t;
    this.dimMat.opacity = t * 0.9;
    this.outline.visible = this.dims.visible = t > 0.01;
    for (const c of this.dims.children) {
      if (!c.isCSS2DObject) continue;
      c.visible = t > 0.01; // CSS2DRenderer ignores parent visibility
      c.element.style.opacity = t;
    }
    for (const m of this.fadeable) {
      m.opacity = (1 - 0.9 * t) * r;
      m.depthWrite = !m.userData.decal && t < 0.5 && r > 0.98;
    }
    this.glassMat.opacity = 0.06 * (1 - t) * r;
    for (const d of this.prepass) d.visible = t < 0.5; // blueprint wants the x-ray
    this.screenKey.value = 1 - Math.min(1, r * 1.6);
    this.poolMat.opacity = r;
    this.statusLight.intensity = 700 * r;
  }

  #pointer() {
    const el = this.renderer.domElement;
    const ray = new THREE.Raycaster();
    const ndc = new THREE.Vector2();
    const onKey = (event) => {
      const rect = el.getBoundingClientRect();
      ndc.set(((event.clientX - rect.left) / rect.width) * 2 - 1, -((event.clientY - rect.top) / rect.height) * 2 + 1);
      ray.setFromCamera(ndc, this.camera);
      return ray.intersectObjects(this.hitTargets, false).length > 0;
    };
    let last = null;
    el.addEventListener("pointermove", (event) => {
      if (this.drag.enabled && last && event.pointerId === this.dragId) {
        this.drag.az -= (event.clientX - last.x) * 0.35;
        this.drag.el = Math.max(-25, Math.min(40, this.drag.el + (event.clientY - last.y) * 0.25));
        last = { x: event.clientX, y: event.clientY };
        return;
      }
      if (!this.pressed && this.dragId === undefined) el.style.cursor = onKey(event) ? "pointer" : "";
    });
    el.addEventListener("pointerdown", (event) => {
      if (event.button !== 0 || this.pointerId !== undefined || this.dragId !== undefined) return;
      if (onKey(event)) {
        el.setPointerCapture(event.pointerId);
        event.preventDefault();
        this.pointerId = event.pointerId;
        this.press(true);
        return;
      }
      if (!this.drag.enabled) return;
      el.setPointerCapture(event.pointerId);
      this.dragId = event.pointerId;
      last = { x: event.clientX, y: event.clientY };
      el.style.cursor = "grabbing";
    });
    const up = (event) => {
      if (this.pointerId === event.pointerId) {
        this.pointerId = undefined;
        this.press(false);
      }
      if (this.dragId === event.pointerId) {
        this.dragId = undefined;
        last = null;
        el.style.cursor = "";
      }
    };
    el.addEventListener("pointerup", up);
    el.addEventListener("pointercancel", up);
    el.addEventListener("lostpointercapture", up);
    // The copy cards cover the canvas, so follow the mouse page-wide.
    addEventListener("pointermove", (event) => {
      this.cursor = event.pointerType === "mouse" ? { x: event.clientX, y: event.clientY } : null;
    }, { passive: true });
    document.documentElement.addEventListener("pointerleave", () => { this.cursor = null; });
    addEventListener("blur", () => { this.cursor = null; });
  }

  // Turn the stick a few degrees toward the cursor, measured from where the
  // stick actually is on screen (the camera shifts it beside the copy).
  #updateTilt() {
    let x = 0, y = 0;
    if (this.drag.enabled && this.cursor && !this.reduceMotion && this.dragId === undefined) {
      const rect = this.renderer.domElement.getBoundingClientRect();
      const at = this.root.getWorldPosition(new THREE.Vector3()).project(this.camera);
      const px = rect.left + (at.x + 1) / 2 * rect.width, py = rect.top + (1 - at.y) / 2 * rect.height;
      // Saturating: a cursor half the stage away already gets most of the turn.
      const reach = Math.max(240, Math.min(rect.width, rect.height) * 0.5);
      y = Math.tanh((this.cursor.x - px) / reach) * 0.16; // yaw toward it
      x = Math.tanh((this.cursor.y - py) / reach) * 0.1; // pitch toward it
    }
    this.tilt.x = damp(this.tilt.x, x, 0.05);
    this.tilt.y = damp(this.tilt.y, y, 0.05);
    this.root.rotation.x = this.tilt.x;
    this.root.rotation.y = this.tilt.y;
  }

  press(down) {
    if (down === this.pressed) return;
    this.pressed = down;
    this.#emit(down ? "buttondown" : "buttonup");
  }

  setShell(name) {
    const shell = SHELLS[name] ?? SHELLS.stock;
    this.shellMat.color.setHex(shell.body);
    this.keyMat.color.setHex(shell.key);
    this.keyMat.emissive.setHex(shell.key).multiplyScalar(0.35);
    this.shell = name;
  }
  setBlueprint(on) { this.blueprintTarget = on ? 1 : 0; }
  setGlow(hex) { this.glowTarget.set(hex); }
  flip() { this.flipped = !this.flipped; }
  // Scroll choreography target, independent of the playground rotation.
  // `follow` is the per-frame catch-up (default lazy 0.075); a scroll scene
  // whose progress is already eased passes a quick one to stay in step.
  setPose(pose, snap = false, follow = 0.075) {
    Object.assign(this.pose, pose);
    this.follow = follow;
    if (snap || this.reduceMotion) this.snapNext = true;
  }
  setRotationEnabled(on) {
    if (on === this.drag.enabled) return;
    this.drag.enabled = on;
    const el = this.renderer.domElement;
    el.classList.toggle("can-rotate", on);
    el.style.cursor = "";
    if (!on && this.dragId !== undefined) {
      const id = this.dragId;
      this.dragId = undefined;
      if (el.hasPointerCapture(id)) el.releasePointerCapture(id);
    }
  }
  // Waking up: start `from` × further away and slowly dolly in over `ms`.
  // Multiplies the camera distance only, so scrolling meanwhile still works.
  wakeZoom(from, ms) {
    if (this.reduceMotion) return;
    this.dolly = { from, ms, start: null };
  }
  #dollyFactor(now) {
    const d = this.dolly;
    if (!d) return 1;
    d.start ??= now;
    const u = Math.min(1, (now - d.start) / d.ms);
    if (u >= 1) this.dolly = null;
    const e = u < 0.5 ? 4 * u * u * u : 1 - (-2 * u + 2) ** 3 / 2; // ease in-out
    return d.from + (1 - d.from) * e;
  }

  // A short, decaying rattle that sells a loud clap. Skipped for reduced motion.
  shake() {
    if (!this.reduceMotion) this.shakeAt = performance.now() / 1000;
  }

  resize() {
    const { clientWidth: w, clientHeight: h } = this.container;
    if (!w || !h) return;
    this.renderer.setSize(w, h, false);
    this.labels.setSize(w, h);
    this.size = { w, h };
    this.camera.aspect = w / h;
    // Keep the whole stick in frame on narrow screens.
    this.camera.fov = fovFor(w, h);
    this.camera.updateProjectionMatrix();
  }

  #updateCamera(now) {
    const k = this.snapNext ? 1 : this.follow ?? 0.075;
    this.snapNext = false;
    for (const key of Object.keys(this.pose)) this.view[key] = damp(this.view[key], this.pose[key], k);
    if (!this.drag.enabled) {
      this.drag.az = damp(this.drag.az, 0, 0.04);
      this.drag.el = damp(this.drag.el, 0, 0.04);
    }
    const v = this.view;
    const az = THREE.MathUtils.degToRad(v.az + this.drag.az);
    const el = THREE.MathUtils.degToRad(Math.max(-20, Math.min(70, v.el + this.drag.el)));
    const target = new THREE.Vector3(v.tx, v.ty, 0);
    const dist = v.dist * this.#dollyFactor(now);
    this.camera.position.set(
      target.x + dist * Math.sin(az) * Math.cos(el),
      target.y + dist * Math.sin(el),
      target.z + dist * Math.cos(az) * Math.cos(el),
    );
    this.camera.lookAt(target);
    // Shift the subject beside the copy without perspective skew.
    const { w, h } = this.size ?? { w: 1, h: 1 };
    this.camera.setViewOffset(w, h, -v.sx * w, v.sy * h, w, h);
    this.renderer.toneMappingExposure = v.exposure;
  }

  render(now) {
    const t = now / 1000;
    frameScale = Math.min(6, Math.max(0.05, (now - (this.lastNow ?? now - 16.7)) / 16.7));
    this.lastNow = now;
    if (this.avatar.dirty) {
      this.pixels.needsUpdate = true;
      this.avatar.dirty = false;
    }
    const b = damp(this.blueprint, this.blueprintTarget, 0.1);
    const r = this.view.reveal;
    if (b !== this.blueprint || r !== this.shownReveal) {
      this.blueprint = Math.abs(b - this.blueprintTarget) < 1e-3 ? this.blueprintTarget : b;
      this.shownReveal = r;
      this.#applyVisibility();
    }
    this.pressDepth = damp(this.pressDepth, this.pressed ? -0.7 : 0, 0.45);
    this.button.position.z = FRONT + this.pressDepth;
    this.keyOutline.position.z = this.pressDepth;
    this.flipAngle = damp(this.flipAngle, this.flipped ? Math.PI : 0, 0.09);
    this.rig.rotation.z = this.flipAngle;
    this.pixels.rotation = this.flipAngle > Math.PI / 2 ? Math.PI : 0;
    // A gentle hover above the desk, starting level and easing in so the
    // first frame lines up with the CSS sleeping eyes it replaces.
    this.bornAt ??= t;
    const life = t - this.bornAt;
    const hover = this.reduceMotion ? 0 : Math.min(1, life / 2.5);
    const bob = Math.sin(life * 1.4) * 0.7 * hover;
    // Rattle: ~26Hz, decaying over ~0.45s; mm and radians.
    const since = this.shakeAt < 0 ? 1 : t - this.shakeAt;
    const rattle = since < 0.45 ? Math.exp(-since / 0.12) : 0;
    const phase = since * Math.PI * 2 * 26;
    this.root.position.x = Math.sin(phase) * 0.9 * rattle;
    this.root.position.y = bob + Math.sin(phase * 1.3 + 1) * 0.35 * rattle;
    this.root.rotation.z = Math.sin(life * 0.7) * 0.012 * hover + Math.sin(phase * 0.8 + 2) * 0.03 * rattle;
    this.shadow.scale.setScalar(1 - bob * 0.03);
    this.shadow.material.opacity = (1 - this.blueprint) * (0.85 - bob * 0.08) * this.view.reveal;
    this.glow.lerp(this.glowTarget, 0.06);
    this.statusLight.color.copy(this.glow);
    this.poolMat.color.copy(this.glow);
    this.#updateCamera(now);
    this.#updateTilt();
    this.renderer.render(this.scene, this.camera);
    this.labels.render(this.scene, this.camera);
  }
}
