import { loadAvatar, STATES } from "./avatar.js";
import { SHELLS } from "./device.js";
import { SCREEN, WAKE_AT, WAKE_FROM, WAKE_MS, introDist, narrow } from "./intro.js";

const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];

const COLORS = {
  working: "#3b82f6", waiting: "#facc15", finished: "#22c55e", listening: "#c4b5fd",
  sleeping: "#64748b", idle: "#94a3b8", offline: "#475569",
};
const MODE_LABEL = {
  working: "Working", waiting: "Needs you", finished: "Finished", listening: "Listening",
  sleeping: "Sleeping", idle: "Idle", offline: "Bridge offline",
};
const STATE_LABEL = ["Thinking", "Completion", "Request", "Listening"];

function roman(n) {
  if (n <= 0) return "–";
  const table = [[1000, "M"], [900, "CM"], [500, "D"], [400, "CD"], [100, "C"], [90, "XC"], [50, "L"], [40, "XL"], [10, "X"], [9, "IX"], [5, "V"], [4, "IV"], [1, "I"]];
  let out = "";
  for (const [v, s] of table) while (n >= v) { out += s; n -= v; }
  return out;
}

// The firmware's attention chime: C6 for 70ms, then G6 for 110ms.
let audio = null;
let soundOn = true;
let userGesture = false;
addEventListener("pointerdown", () => (userGesture = true), { once: true });
addEventListener("keydown", () => (userGesture = true), { once: true });
function chime() {
  if (!soundOn || !userGesture) return;
  audio ||= new AudioContext();
  const t = audio.currentTime + 0.02;
  [[1047, 0, 0.07], [1568, 0.105, 0.11]].forEach(([f, at, len]) => {
    const osc = audio.createOscillator();
    const gain = audio.createGain();
    osc.type = "square";
    osc.frequency.value = f;
    gain.gain.setValueAtTime(0.0001, t + at);
    gain.gain.exponentialRampToValueAtTime(0.05, t + at + 0.005);
    gain.gain.exponentialRampToValueAtTime(0.0001, t + at + len);
    osc.connect(gain).connect(audio.destination);
    osc.start(t + at);
    osc.stop(t + at + len + 0.02);
  });
}

let toastTimer = 0;
function toast(text) {
  const el = $("#toast");
  el.textContent = text;
  el.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.remove("show"), 2400);
}

async function main() {
  const stage = $("#stage");
  let avatar;
  try {
    avatar = await loadAvatar(new URL("../avatar.wasm", import.meta.url));
  } catch (error) {
    stage.classList.add("failed");
    $("#stage-error").textContent = `Couldn't load the firmware renderer (${error.message}).`;
    throw error;
  }
  // Doze off before anything is shown, so the 3D screen takes over from the
  // CSS sleeping eyes seamlessly; it wakes up as the camera moves in.
  avatar.set({ active: 0, done: 0 });
  avatar.awake = false;
  avatar.advance(8000);
  let sleepy = true;

  // --- Device (3D, or a flat pixel-perfect fallback without WebGL) ---------
  let device;
  try {
    const { Device } = await import("./device.js");
    device = new Device($("#canvas-host"), avatar);
  } catch (error) {
    console.warn("3D unavailable, using flat preview", error);
    stage.classList.add("flat");
    const ctx = $("#flat-screen").getContext("2d");
    const image = ctx.createImageData(240, 135);
    const handlers = {};
    device = {
      pressed: false,
      on(name, fn) { handlers[name] = fn; },
      press(down) { this.pressed = down; handlers[down ? "buttondown" : "buttonup"]?.(); },
      setShell() {}, setBlueprint() {}, setGlow() {}, flip() {}, setPose() {}, setStickyDrag() {}, shake() {},
      render() {
        if (!avatar.dirty) return;
        image.data.set(avatar.rgba);
        ctx.putImageData(image, 0, 0);
        avatar.dirty = false;
      },
    };
  }

  // --- Status --------------------------------------------------------------
  let lastBlocked = 0;
  function set(patch) {
    avatar.set(patch);
    if (avatar.status.blocked > 0 && lastBlocked === 0) chime();
    lastBlocked = avatar.status.blocked;
    sync();
  }
  function sync() {
    const s = avatar.status;
    $("#done-value").textContent = roman(s.done);
    $("#active-value").textContent = roman(s.active);
    $("#toggle-blocked").setAttribute("aria-pressed", s.blocked > 0);
    $("#toggle-healthy").setAttribute("aria-pressed", s.healthy);
  }

  // --- KEY1: tap = wink (+ focus); hold ≥300ms = push-to-talk --------------
  let holdTimer = 0;
  let talking = false;
  const voiceBtn = $("#btn-hold");
  device.on("buttondown", () => {
    clearTimeout(holdTimer);
    holdTimer = setTimeout(() => {
      talking = true;
      set({ listening: true });
      stage.classList.add("talking");
      voiceBtn.classList.add("on");
    }, 300);
  });
  device.on("buttonup", () => {
    clearTimeout(holdTimer);
    if (talking) {
      talking = false;
      set({ listening: false });
      avatar.releaseVoice();
      stage.classList.remove("talking");
      voiceBtn.classList.remove("on");
      if (!scripted) toast("Released: a little nod");
    } else {
      avatar.tap();
      if (!scripted) toast("Tap: a wink, and the bridge focuses the waiting session");
    }
  });
  let scripted = false; // true while a chapter is pressing the key itself
  const press = (down) => device.press(down);

  const typing = (e) => e.target.closest("input,textarea,select,[contenteditable]");
  addEventListener("keydown", (e) => {
    if (e.code !== "Space" || typing(e)) return;
    e.preventDefault();
    if (!e.repeat) { cancelScriptedPress(); press(true); }
  });
  addEventListener("keyup", (e) => {
    if (e.code !== "Space" || typing(e)) return;
    e.preventDefault();
    press(false);
  });
  voiceBtn.addEventListener("pointerdown", (e) => { e.preventDefault(); cancelScriptedPress(); press(true); });
  voiceBtn.addEventListener("pointerup", () => press(false));
  voiceBtn.addEventListener("pointerleave", () => device.pressed && press(false));

  // --- Chapters ------------------------------------------------------------
  // Timers belong to the current chapter and are cleared when it changes.
  let timers = [];
  const later = (fn, ms) => timers.push(setTimeout(fn, ms));
  const every = (fn, ms) => timers.push(setInterval(fn, ms));
  function clearTimers() {
    timers.forEach((t) => { clearTimeout(t); clearInterval(t); });
    timers = [];
  }
  function cancelScriptedPress() {
    if (!scripted) return;
    clearTimers();
    scripted = false;
    if (device.pressed) press(false);
  }
  const calm = (patch) => {
    if (talking || device.pressed) { scripted = true; press(false); scripted = false; }
    set({ listening: false, healthy: true, blocked: 0, ...patch });
  };

  // Camera poses: azimuth/elevation (deg), distance (mm), look-at target,
  // and a screen-space shift (sx/sy) that moves the stick beside the copy.
  // `reveal` fades the shell in around the face (0 = only the eyes on the page).
  // `awake` holds idle-awake with no tasks; otherwise it dozes after ~4s.
  const CHAPTERS = {
    intro: {
      pose: { az: 0, el: 0, dist: introDist(), tx: SCREEN.x, ty: 0, exposure: 1.05, reveal: 0 },
      awake: true,
      enter() { calm({ active: 0, done: 0 }); },
    },
    hero: {
      pose: { az: -20, el: 9, dist: 178, tx: 0, ty: 0, exposure: 1.05, reveal: 1 },
      awake: true,
      enter() { calm({ active: 0, done: 0 }); },
    },
    arrive: {
      pose: { az: 22, el: 14, dist: 160, tx: 0, ty: 2, exposure: 1.05, reveal: 1 },
      awake: true,
      enter() {
        calm({ active: 0, done: 0 });
        // The first task drops in, then a couple more join it.
        const drop = () => avatar.status.active < 3 && set({ active: avatar.status.active + 1 });
        later(drop, 900);
        every(drop, 4800);
      },
    },
    think: {
      pose: { az: -16, el: 6, dist: 158, tx: 2, ty: 0, exposure: 1.05, reveal: 1 },
      enter() {
        calm({ active: 2, done: 0 });
        later(() => avatar.play(0, 4), 300); // "Aha insight", then free choice
      },
    },
    finish: {
      pose: { az: 24, el: 8, dist: 150, tx: 0, ty: 0, exposure: 1.08, reveal: 1 },
      enter() {
        // The transfer cue: one active task charges, flips onto the done
        // side, the face hops home. It plays when the first one completes.
        const cycle = () => {
          calm({ active: 2, done: 0 });
          later(() => set({ active: 1, done: 1 }), 1500);
        };
        cycle();
        every(cycle, 11000);
      },
    },
    sort: {
      pose: { az: -18, el: 10, dist: 150, tx: 2, ty: 1, exposure: 1.08, reveal: 1 },
      enter() {
        // Completion sorting: with both numerals on screen and settled, done
        // rising and active falling in one snapshot makes the body droop two
        // blobs, lift both numerals, swap them at the apex and set them down.
        // A pickup would cut a falling numeral short, so each drop lands first
        // (arrival 2.4s); the sort itself runs ~3.3s.
        const finish = () => set({ active: avatar.status.active - 1, done: avatar.status.done + 1 });
        const cycle = (first) => {
          if (first || avatar.status.done >= 5) calm({ active: 3, done: 1 });
          else set({ active: avatar.status.active + 1 }); // fresh work keeps both sides full
          later(finish, 2800);
        };
        cycle(true);
        every(() => cycle(false), 7000);
      },
    },
    needs: {
      pose: { az: 8, el: 12, dist: 142, tx: -3, ty: 1, exposure: 1.08, reveal: 1 },
      enter() {
        calm({ active: 1, done: 1 });
        later(() => set({ blocked: 1 }), 350);
      },
    },
    talk: {
      pose: { az: -30, el: 20, dist: 140, tx: 4, ty: 0, exposure: 1.05, reveal: 1 },
      enter() {
        calm({ active: 1, done: 1 });
        const say = () => {
          if (talking && !scripted) return; // the visitor is holding it
          scripted = true;
          press(true);
          later(() => { press(false); scripted = false; }, 3600);
        };
        later(say, 900);
        every(say, 6500);
      },
      leave() { cancelScriptedPress(); },
    },
    sleep: {
      pose: { az: 12, el: 3, dist: 176, tx: 0, ty: -1, exposure: 0.72, reveal: 1 },
      enter() {
        // Ease into the nap: the last unread completion is opened, so instead
        // of fading the green it steps over the numeral and settles into
        // idle; with nothing left to do it dozes off ~4s later.
        calm({ active: 0, done: 1 });
        later(() => set({ done: 0 }), 2400);
        // Once it is properly asleep, a loud clap jolts it awake, wide-eyed;
        // it looks around, then dozes off again and the clap comes back.
        const clap = () => { if (avatar.mode === "sleeping") { avatar.startle(); device.shake(); } };
        later(clap, 11500);
        every(clap, 12000);
      },
    },
    anatomy: {
      pose: { az: -38, el: 26, dist: 190, tx: -2, ty: 0, exposure: 1.1, reveal: 1 },
      enter() { calm({ active: 2, done: 1 }); device.setBlueprint(true); },
      leave() { if (!blueprintPinned) device.setBlueprint(false); },
    },
    play: {
      pose: { az: 16, el: 10, dist: 180, tx: 0, ty: 0, exposure: 1.05, reveal: 1 },
      enter() { calm({ active: 2, done: 1 }); device.setStickyDrag(true); },
      leave() { device.setStickyDrag(false); },
    },
  };

  const sections = $$("[data-chapter]");
  const names = sections.map((s) => s.dataset.chapter);
  const dots = $("#dots");
  sections.forEach((section, i) => {
    const li = document.createElement("li");
    const b = document.createElement("button");
    b.setAttribute("aria-label", section.querySelector("h1,h2")?.textContent ?? section.getAttribute("aria-label") ?? names[i]);
    b.title = b.getAttribute("aria-label");
    b.onclick = () => section.scrollIntoView({ behavior: "smooth", block: "center" });
    li.append(b);
    dots.append(li);
  });

  // Screen-space placement: beside the copy on wide screens, above it on phones.
  const shift = (section) => {
    if (section.dataset.chapter === "intro") return { sx: 0, sy: 0, dist: introDist() };
    if (narrow()) return { sx: 0, sy: section.dataset.chapter === "play" ? 0.3 : 0.2 };
    return { sx: section.classList.contains("left") ? 0.19 : -0.19, sy: 0 };
  };

  let active = -1;
  let pending = -1;
  let pendingTimer = 0;
  function activate(i) {
    if (i === active) return;
    const prev = CHAPTERS[names[active]];
    prev?.leave?.();
    clearTimers();
    scripted = false;
    active = i;
    sections.forEach((s, j) => s.classList.toggle("is-active", j === i));
    $$("button", dots).forEach((b, j) => b.toggleAttribute("aria-current", j === i));
    avatar.awake = !sleepy && !!CHAPTERS[names[i]]?.awake;
    CHAPTERS[names[i]]?.enter?.();
  }

  // Continuous chapter position from section centres; the camera blends
  // between neighbouring poses while the avatar switches at the midpoint.
  const ease = (t) => t * t * (3 - 2 * t);
  function onScroll() {
    const mid = scrollY + innerHeight / 2;
    const centres = sections.map((s) => s.offsetTop + s.offsetHeight / 2);
    let i = 0;
    while (i < centres.length - 1 && mid > centres[i + 1]) i++;
    const last = i === centres.length - 1;
    const t = last ? 0 : Math.max(0, Math.min(1, (mid - centres[i]) / Math.max(1, centres[i + 1] - centres[i])));
    const j = Math.min(i + 1, sections.length - 1);
    const a = { ...CHAPTERS[names[i]].pose, ...shift(sections[i]) };
    const b = { ...CHAPTERS[names[j]].pose, ...shift(sections[j]) };
    const k = ease(t);
    const pose = {};
    for (const key of Object.keys(a)) pose[key] = a[key] + (b[key] - a[key]) * k;
    device.setPose(pose);
    stage.classList.toggle("intro", pose.reveal < 0.5);
    // Fade copy by distance from its chapter.
    sections.forEach((s, n) => {
      const d = Math.abs(n - (i + t));
      s.style.setProperty("--reveal", Math.max(0, 1 - d * 1.5).toFixed(3));
    });
    // Debounce chapter switches so a fast fling doesn't churn the avatar.
    const next = t < 0.5 ? i : j;
    if (next !== pending) {
      pending = next;
      clearTimeout(pendingTimer);
      pendingTimer = setTimeout(() => activate(pending), active < 0 ? 0 : 160);
    }
  }
  addEventListener("scroll", onScroll, { passive: true });
  addEventListener("resize", onScroll);
  onScroll();
  device.setPose({}, true);

  // --- Playground controls -------------------------------------------------
  $("#done-minus").onclick = () => set({ done: avatar.status.done - 1 });
  $("#done-plus").onclick = () => set({ done: avatar.status.done + 1 });
  $("#active-minus").onclick = () => set({ active: avatar.status.active - 1 });
  $("#active-plus").onclick = () => set({ active: avatar.status.active + 1 });
  $("#toggle-blocked").onclick = () => {
    const s = avatar.status;
    set(s.blocked ? { blocked: 0 } : { blocked: 1, active: Math.max(1, s.active), healthy: true });
  };
  $("#toggle-healthy").onclick = () => set({ healthy: !avatar.status.healthy });
  const SCENES = {
    arrive: () => { calm({}); set({ active: avatar.status.active + 1 }); },
    finish: () => {
      calm({});
      if (avatar.status.active === 0) {
        set({ active: 1 });
        later(() => set({ active: 0, done: avatar.status.done + 1 }), 2600);
      } else set({ active: avatar.status.active - 1, done: avatar.status.done + 1 });
    },
    step: () => {
      calm({ active: 0, done: 1 });
      later(() => set({ done: 0 }), 2400);
      toast("One unread completion… now opened: it steps over the green");
    },
  };
  $$("[data-scene]").forEach((el) => (el.onclick = () => SCENES[el.dataset.scene]?.()));
  $("#btn-tap").onclick = () => avatar.tap();
  $("#btn-clap").onclick = () => { avatar.startle(); device.shake(); toast("Startled! (opt-in loud-noise detector)"); };
  $("#btn-flip").onclick = () => { device.flip(); toast("The IMU turns the face upright"); };
  let blueprintPinned = false;
  $("#btn-blueprint").onclick = (e) => {
    blueprintPinned = !blueprintPinned;
    e.currentTarget.setAttribute("aria-pressed", blueprintPinned);
    device.setBlueprint(blueprintPinned);
  };
  $("#btn-sound").onclick = (e) => {
    soundOn = !soundOn;
    e.currentTarget.setAttribute("aria-pressed", soundOn);
    if (soundOn) chime();
  };
  const swatches = $("#swatches");
  for (const [key, shell] of Object.entries(SHELLS)) {
    const b = document.createElement("button");
    b.className = "swatch";
    b.title = shell.label;
    b.setAttribute("aria-label", `${shell.label} shell`);
    b.style.setProperty("--c", `#${shell.body.toString(16).padStart(6, "0")}`);
    b.style.setProperty("--k", `#${shell.key.toString(16).padStart(6, "0")}`);
    b.setAttribute("aria-pressed", key === "stock");
    b.onclick = () => {
      device.setShell(key);
      $$(".swatch", swatches).forEach((s) => s.setAttribute("aria-pressed", s === b));
    };
    swatches.append(b);
  }

  // Performance library.
  const library = $("#library");
  const tabs = $("#library-tabs");
  let current = 0;
  const renderLibrary = () => {
    $$("button", tabs).forEach((t) => t.setAttribute("aria-selected", +t.dataset.state === current));
    library.replaceChildren(
      ...avatar.clips[current].map((clip) => {
        const b = document.createElement("button");
        b.className = "clip";
        b.dataset.id = `${clip.state}:${clip.index}`;
        b.textContent = clip.name;
        b.onclick = () => playClip(clip);
        return b;
      }),
    );
  };
  STATES.forEach((name, s) => {
    const t = document.createElement("button");
    t.role = "tab";
    t.dataset.state = s;
    t.className = `tab tab-${name}`;
    t.textContent = `${STATE_LABEL[s]} · ${avatar.clips[s].length}`;
    t.onclick = () => { current = s; renderLibrary(); };
    tabs.append(t);
  });
  renderLibrary();

  function playClip(clip) {
    clearTimers();
    const s = avatar.status;
    if (clip.state === 0) calm({ active: Math.max(1, s.active) });
    if (clip.state === 1) calm({ active: 0, done: Math.max(1, s.done) });
    if (clip.state === 2) calm({ blocked: 1, active: Math.max(1, s.active) });
    if (clip.state === 3) {
      set({ listening: true });
      later(() => { if (!talking) { set({ listening: false }); avatar.releaseVoice(); } }, clip.duration + 1400);
    }
    avatar.play(clip.state, clip.index);
  }

  // --- HUD + render loop ---------------------------------------------------
  let shownMode = "";
  let shownClip = "";
  function hud() {
    const mode = avatar.mode;
    const clip = avatar.clip;
    const clipText = clip ? clip.name : mode === "sleeping" ? "zzz…" : mode === "offline" ? "waiting for USB" : "…";
    if (mode !== shownMode) {
      shownMode = mode;
      $("#hud-mode").textContent = MODE_LABEL[mode] ?? mode;
      document.documentElement.style.setProperty("--mood", COLORS[mode] ?? "#3b82f6");
      device.setGlow(COLORS[mode] ?? "#3b82f6");
    }
    if (clipText !== shownClip) {
      shownClip = clipText;
      $("#hud-clip").textContent = clipText;
      $$(".clip.playing").forEach((c) => c.classList.remove("playing"));
      if (clip) $(`.clip[data-id="${clip.state}:${clip.index}"]`)?.classList.add("playing");
    }
  }

  let visible = true;
  new IntersectionObserver(([entry]) => (visible = entry.isIntersecting)).observe(stage);
  // Reveal after the first real frame: fade over from the CSS eyes, dolly in,
  // and open the eyes near the end of the zoom.
  let revealed = false;
  function wake() {
    revealed = true;
    stage.classList.add("ready");
    device.wakeZoom?.(WAKE_FROM, WAKE_MS);
    setTimeout(() => {
      sleepy = false;
      avatar.awake = !!CHAPTERS[names[active]]?.awake;
    }, matchMedia("(prefers-reduced-motion: reduce)").matches ? 400 : WAKE_MS * WAKE_AT);
  }
  function loop(now) {
    requestAnimationFrame(loop);
    if (!visible || document.hidden) return;
    avatar.tick(now);
    hud();
    device.render(now);
    if (!revealed) wake();
  }
  sync();
  requestAnimationFrame(loop);
}

// --- Static page bits --------------------------------------------------------
$$("pre[data-copy]").forEach((pre) => {
  const b = document.createElement("button");
  b.className = "copy-btn";
  b.textContent = "Copy";
  b.onclick = async () => {
    await navigator.clipboard.writeText(pre.querySelector("code").textContent);
    b.textContent = "Copied!";
    setTimeout(() => (b.textContent = "Copy"), 1400);
  };
  pre.append(b);
});
$$("[data-tabs]").forEach((group) => {
  const buttons = $$("[role=tab]", group);
  buttons.forEach((b) => b.addEventListener("click", () => {
    buttons.forEach((x) => x.setAttribute("aria-selected", x === b));
    $$("[data-panel]", group).forEach((p) => (p.hidden = p.dataset.panel !== b.dataset.target));
  }));
});

main();
