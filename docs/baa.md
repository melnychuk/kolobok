# BAA: Bar for Agent Activity

BAA puts a miniature of the Stick's avatar in the macOS menu bar, with its own
pixel art drawn for the menu bar at 1× and 2×. It shows every agent Herdr runs,
whatever the kind (pi, Claude, Codex, Copilot…), exactly as the Stick's bridge
reports them, so the menu bar and the Stick always agree. You don't need a
Stick: the bridge can run without one. BAA needs macOS 14.4+ with Swift 5.9+ to
build.

## Preview the states

From the checkout:

```sh
swift run --package-path menu-bar BAA --demo
```

Click the avatar and choose a state from **Preview**. The avatar and the menu
update immediately; demo mode never contacts the bridge or Herdr.

As on the Stick, the face acts out the status and colored sides show it: active
work on the left in blue, unread completions on the right in green. The sides
wrap the face's outline, separated from it by a one-point moat, and are two
points shorter than the face, so it stands a point proud of them above and
below.

| State | Menu-bar cue |
| --- | --- |
| No agents / Idle | Open eyes; the menu distinguishes the two |
| Working | Focused eyes looking left at a blue side with the active count (working and blocked agents, as on the Stick) |
| Needs attention | Wide eyes and a yellow side with a black `!` instead of the count |
| Done | Happy closed arches looking right at a green side, regardless of how many tasks completed |
| Working + done | Focused face between the blue and green sides |
| Mixed | Yellow `!` side and green side; the menu still lists every agent |
| Herdr unavailable | Closed eyes, face at half strength |
| No bridge | Puzzled face, one eye squinting: BAA can't know what the agents are doing |

The face is a template image, so macOS colors it for the menu bar. The active
count is white on blue, and the black `!` sits on a side that steps between
the Stick's request yellow and orange. Open eyes blink every few seconds.

A new completion plays a short version of the Stick's transfer cue: the blue
side charges with green light, flickering faster and faster; the face jumps,
eyes flying open in fright; a green starburst spins out from the charged side
along the sides' 14-point band, its rays cutting the face away above and below
the band while the eyes squeeze shut, until the face is gone and the band is
one solid green bar. Then the face peeks back in from the left, one eye on the
green, blinks, crouches and hops home onto the bar, landing squashed and happy.
If work is still active, the face drops in from above instead of crossing the
blue: it peeks down at its home, blinks, braces and falls onto the bar, which
blends from the blue side into green (with both sides showing, the charge
blends from blue into green too). It all takes about 2.5 seconds. Without
active work before it, the face is startled straight away. None of this
animates if macOS **Reduce motion** is on.

The product site's menu bar chapter replays these frames, rendered by a test
that's skipped unless asked for. After changing the art or the celebration,
regenerate them on a Mac:

```sh
BAA_SPRITES_DIR=site/media/baa swift test --package-path menu-bar --filter SiteSprites
```

Click the avatar for a standard macOS menu of your agents, grouped under their
Herdr spaces in Herdr's order. Each takes one line: a pane name you set, or
else the agent's kind, then in grey its status (and its tab, when a space has
several agents), after a dot in the Stick's status color. Agents Herdr runs on
another machine say so in their space's header. To quit, right-click (or Control-click) it
and choose **Quit BAA**, or press Ctrl-C in the launching terminal. Nothing is
installed or started at login.

## Show live agents

BAA has no Herdr logic of its own. It reads the Mac bridge
(`bridge/macos_herdr_bridge.py`), the same process that drives the Stick, so
both count agents the same way, including Herdr's own unread state. For Herdr
Desktop, run the bridge with its Desktop source (add `--no-usb` without a
Stick; see [Mac / Herdr integration](macos-herdr.md) for the LaunchAgent):

```sh
python3 bridge/macos_herdr_bridge.py --herdr-source desktop --no-usb
swift run --package-path menu-bar BAA
```

The bridge publishes its status on a Unix socket in the per-user temporary
directory (`$(getconf DARWIN_USER_TEMP_DIR)stick-s3/bridge.sock`): one socket
file when it starts, then everything stays in memory, with no status files
written while it runs. Only your user can connect. BAA reconnects on its own;
if the bridge stops or stalls for more than 12 seconds, the face turns puzzled.

Choose an agent to show it in Herdr. BAA asks the bridge, which checks the agent
against a fresh Herdr snapshot, focuses it and brings a running Herdr forward;
it never launches Herdr, sends terminal input, or approves prompts. Only one
focus runs at a time, shared with the Stick's button. BAA doesn't read
transcripts; the bridge passes it each agent's kind, its terminal or tab name
and its status, and keeps them out of its logs.

## The same avatar in Omarchy's bar

On Linux, the Omarchy Agents bar widget can show BAA's avatar in place of its
robot glyph, drawn from the same pixels: `omarchy/baa/BaaIcon.qml` draws it on
a Canvas with `baa-avatar.mjs`, a JavaScript port of the app's drawing and
celebration. The art, eye placements and timings come straight from the Swift
in `omarchy/baa/baa-art.mjs`; re-export it after changing them:

```sh
python3 scripts/export-baa-art.py
node --test tests/baa-omarchy.test.mjs   # the port against the app's own frames
scripts/install-baa-omarchy.sh           # copy it where the widget loads it
```

The install script only copies files into `~/.local/share/stick-s3/baa`; the
widget picks them up the next time the shell loads it, and keeps its glyph
without them or on a vertical bar. The face takes the bar's foreground color,
the side's label the bar's font, and the counts are the widget's own.

Omarchy draws the compact avatar, two points shorter than the menu bar's so
it sits among the bar's other icons: a 14 px face over 12 px sides with 9 px
digits, filling the 16 px icon canvas of Omarchy's default 26 px bar at rest
and using the pixel above and below only while it jumps. On a bar under 22 px
it keeps to the face's rows: the fright's jump becomes a flinch and nothing
stretches. The eyes are the menu bar's, a point higher in the shorter face,
as when it squashes.
