// The menu bar chapter's fake macOS menu bar: BAA's own frames (media/baa,
// rendered by menu-bar's SiteSprites test) acting out key states, timed like
// the app (menu-bar/Sources/BAA): blinks, the request pulse, and a new
// completion's charge, fright and green wipe, after which the avatar comes
// back happy. Times are ms into one loop.

const LOOP = 15000;
// Frame widths in pixels (2× art, 36 px tall).
const WIDTH = {
  "idle": 40, "idle-blink": 40, "working-1": 68, "working-1-blink": 68, "working-2": 68,
  "attention": 64, "attention-peak": 64, "charge": 68, "fright": 68,
  "wipe-1": 68, "wipe-2": 68, "wipe-3": 68, "wipe-4": 68, "gone": 50, "done": 50,
};
const BLINK = 140, PULSE = 650;
const FLICKERS = [0, 200, 340, 450, 530, 590, 640]; // Celebration.flickers
const CHARGE = 720, FRIGHT = 280, WIPE = 240, GONE = 180;

// [ms, frame, caption]. Reduced motion keeps the states, not the motion.
function timeline(reduceMotion) {
  const steps = [];
  const at = (ms, frame, caption) => steps.push([ms, frame, caption]);
  const blink = (ms, frame) => { if (!reduceMotion) { at(ms, `${frame}-blink`); at(ms + BLINK, frame); } };
  at(0, "idle", "All quiet");
  blink(1300, "idle");
  at(2600, "working-1", "Working");
  blink(3700, "working-1");
  at(4600, "working-2");
  at(6800, "attention", "Needs you");
  if (!reduceMotion) {
    for (let ms = 6800 + PULSE, peak = true; ms < 9400; ms += PULSE, peak = !peak) {
      at(ms, peak ? "attention-peak" : "attention");
    }
  }
  at(9400, "working-1", "Working");
  const done = 11000;
  if (reduceMotion) {
    at(done, "done", "Done");
  } else {
    FLICKERS.forEach((ms, n) => at(done + ms, n % 2 ? "working-1" : "charge"));
    at(done + CHARGE, "fright"); // eyes fly open just before the flash
    // Green wipes across the frightened face in quarters, then all green.
    [1, 2, 3, 4].forEach((q) => at(done + CHARGE + FRIGHT + (q - 1) * WIPE / 4, `wipe-${q}`, q === 1 ? "Done" : undefined));
    at(done + CHARGE + FRIGHT + WIPE, "gone");
    at(done + CHARGE + FRIGHT + WIPE + GONE, "done");
  }
  return steps;
}

export function menubarScene(figure) {
  const icon = figure.querySelector(".mb-baa");
  const caption = figure.querySelector(".mb-state");
  const src = (frame) => `media/baa/${frame}.png`;
  let loaded = false;

  function show(frame, text, reduceMotion) {
    icon.src = src(frame);
    icon.width = WIDTH[frame];
    if (!text || caption.textContent === text) return;
    caption.textContent = text;
    if (!reduceMotion) {
      caption.animate([{ opacity: 0, transform: "translateY(6px)" }, { opacity: 1, transform: "none" }],
        { duration: 320, easing: "ease-out" });
    }
  }

  return {
    // later/every are the chapter's timers, cleared when the chapter changes.
    play({ later, every, reduceMotion }) {
      if (!loaded) {
        Object.keys(WIDTH).forEach((frame) => { new Image().src = src(frame); });
        loaded = true;
      }
      const steps = timeline(reduceMotion);
      const run = () => steps.forEach(([ms, frame, text]) => later(() => show(frame, text, reduceMotion), ms));
      run();
      every(run, LOOP);
    },
  };
}
