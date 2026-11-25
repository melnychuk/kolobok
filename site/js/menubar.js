// The menu bar chapter's fake macOS menu bar: Kolobok's own frames (media/kolobok,
// rendered by menu-bar's SiteSprites test) acting out key states, timed like
// the app (menu-bar/Sources/Kolobok): blinks, the request pulse, and a new
// completion's celebration, frame for frame (media/kolobok/celebrate.js). At the
// end a click opens Kolobok's agents menu, as macOS draws it, and the pointer
// heads for the finished agent. Times are ms into one loop.
import CELEBRATE from "../media/kolobok/celebrate.js";

const LOOP = 19500;
// State frame widths in pixels (2× art, 36 px tall); the celebration's are in its manifest.
const WIDTH = {
  "idle": 40, "idle-blink": 40, "working-1": 68, "working-1-blink": 68, "working-2": 68,
  "attention": 64, "attention-peak": 64, "done": 50,
};
CELEBRATE.forEach(({ frame, width }) => { WIDTH[frame] = width; });
const BLINK = 140, PULSE = 650;
const DONE = 11000;
const CELEBRATION = CELEBRATE[CELEBRATE.length - 1].ms;
// The burst covers the face about a second in: that's when it's done.
const BURST = CELEBRATE.find(({ ms }) => ms >= 1000)?.ms ?? 0;
const CLICK = DONE + CELEBRATION + 1100, HOVER = CLICK + 900, CLOSE = LOOP - 900;

// [ms, action, caption]. Reduced motion keeps the states, not the motion.
function timeline(reduceMotion) {
  const steps = [];
  const at = (ms, action, caption) => steps.push([ms, action, caption]);
  const frame = (name) => ({ frame: name });
  const blink = (ms, name) => { if (!reduceMotion) { at(ms, frame(`${name}-blink`)); at(ms + BLINK, frame(name)); } };
  at(0, { frame: "idle", menu: "closed" }, "All quiet");
  blink(1300, "idle");
  at(2600, frame("working-1"), "Working");
  blink(3700, "working-1");
  at(4600, frame("working-2"));
  at(6800, frame("attention"), "Needs you");
  if (!reduceMotion) {
    for (let ms = 6800 + PULSE, peak = true; ms < 9400; ms += PULSE, peak = !peak) {
      at(ms, frame(peak ? "attention-peak" : "attention"));
    }
  }
  at(9400, frame("working-1"), "Working");
  if (reduceMotion) {
    at(DONE, frame("done"), "Done");
  } else {
    CELEBRATE.forEach(({ frame: name, ms }) => at(DONE + ms, frame(name)));
    at(DONE + BURST, {}, "Done");
  }
  at(CLICK - (reduceMotion ? 0 : 700), { pointer: "icon" });
  at(CLICK, { menu: "open" }, "One click away");
  at(HOVER, { pointer: "row", menu: "hover" });
  at(CLOSE, { menu: "closed", pointer: "away" });
  return steps.sort((a, b) => a[0] - b[0]);
}

export function menubarScene(figure) {
  const screen = figure.querySelector(".mb-screen");
  const item = figure.querySelector(".mb-item");
  const icon = figure.querySelector(".mb-kolobok");
  const caption = figure.querySelector(".mb-state");
  const menu = figure.querySelector(".mb-menu");
  const target = menu.querySelector(".mb-row.is-done");
  const pointer = figure.querySelector(".mb-pointer");
  const src = (frame) => `media/kolobok/${frame}.png`;
  let loaded = false;
  // Only Apple's fonts have its logo (U+F8FF); elsewhere keep the drawn apple.
  const platform = navigator.userAgentData?.platform || navigator.platform || navigator.userAgent;
  screen.classList.toggle("apple-glyph", /mac|iphone|ipad|ipod/i.test(platform));

  function show(frame) {
    icon.src = src(frame);
    icon.width = WIDTH[frame];
  }

  function say(text, reduceMotion) {
    if (!text || caption.textContent === text) return;
    caption.textContent = text;
    if (!reduceMotion) {
      caption.animate([{ opacity: 0, transform: "translateY(6px)" }, { opacity: 1, transform: "none" }],
        { duration: 320, easing: "ease-out" });
    }
  }

  // The menu hangs from the icon's left edge, as macOS places it, shifted
  // left if it would run off the screen.
  function place() {
    const box = screen.getBoundingClientRect();
    const at = item.getBoundingClientRect();
    const scale = box.width / screen.offsetWidth || 1; // the stage may be scaled
    const left = (at.left - box.left) / scale;
    menu.style.left = `${Math.max(6, Math.min(left, screen.offsetWidth - menu.offsetWidth - 6))}px`;
  }

  // The pointer, in screen pixels: on the icon, on the finished agent, or off to the side.
  function aim(where, reduceMotion) {
    const box = screen.getBoundingClientRect();
    const scale = box.width / screen.offsetWidth || 1;
    const spot = (el, x, y) => {
      const r = el.getBoundingClientRect();
      return [(r.left - box.left + r.width * x) / scale, (r.top - box.top + r.height * y) / scale];
    };
    const [x, y] = where === "icon" ? spot(item, 0.55, 0.6)
      : where === "row" ? spot(target, 0.35, 0.6)
      : [screen.offsetWidth * 0.62, screen.offsetHeight * 0.72];
    pointer.classList.toggle("is-shown", where !== "away");
    const to = `translate(${x.toFixed(1)}px, ${y.toFixed(1)}px)`;
    if (reduceMotion || !pointer.style.transform) {
      pointer.style.transform = to;
      return;
    }
    pointer.animate([{ transform: pointer.style.transform }, { transform: to }],
      { duration: 520, easing: "cubic-bezier(.3, .1, .2, 1)" });
    pointer.style.transform = to;
  }

  function setMenu(state) {
    const open = state !== "closed";
    if (open && !menu.classList.contains("is-open")) place();
    menu.classList.toggle("is-open", open);
    item.classList.toggle("is-open", open);
    screen.classList.toggle("menu-open", open);
    target.classList.toggle("is-hover", state === "hover");
  }

  function act([, { frame, menu: state, pointer: where }, text], reduceMotion) {
    if (frame) show(frame);
    if (where) aim(where, reduceMotion);
    if (state) setMenu(state);
    say(text, reduceMotion);
  }

  return {
    // later/every are the chapter's timers, cleared when the chapter changes.
    play({ later, every, reduceMotion }) {
      if (!loaded) {
        Object.keys(WIDTH).forEach((frame) => { new Image().src = src(frame); });
        loaded = true;
      }
      const steps = timeline(reduceMotion);
      const run = () => steps.forEach((step) => later(() => act(step, reduceMotion), step[0]));
      run();
      every(run, LOOP);
    },
  };
}
