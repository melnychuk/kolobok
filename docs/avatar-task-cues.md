# Noticeable task-count cues: arrival drop and completion flip

Status: merged in `50a9f03` on top of the `9bf10aa` counter-label layout (all 26
native suites, 76 Python tests and the O2 build pass: 771,327 program / 67,640
global bytes). Not physically timed. Real task counts still update immediately; these
cues only stage how numerals, colours and acting reveal a snapshot.

The previous count cues (a 620ms lift-only pop plus a 1.2s facial reaction)
were almost invisible on the device. `firmware/task_cues.h` adds two authored,
event-only performances shared by firmware, native previews and the header.
Directions (dodge away from active, exit/re-entry through the active side,
step over the done side) come from `firmware/status_sides.h`: active is on the
left and done on the right.

## Arrival — the active count rises (2.4s)

| ms | numeral | character |
|---|---|---|
| 0–280 | the new letters slide down into view at the top edge | eyes snap up toward them (the active side) |
| 280–860 | they teeter there (small wobble); old letters slide over to make room | scared: tall narrow eyes, raised lower lids, worried slant, short shiver, body shrinks |
| 800–930 | — | **jerks aside**: whole character dodges 20px away, wincing |
| 860–1180 | falls with gravity, three speed lines | peeks back up at it |
| 1180 | lands with a hop, dust puffs, rim thud | winces shut |
| 1400–2400 | settles | relieved look at the newcomer, then hands back to normal acting |

Roman numerals are not positional digits, so the drop depends on the strings:

- **Extension** in the same font (`I→II`, `II→III`, `V→VI`, `X→XI`, …): the old
  letters slide left (Roman reads left to right on either side) by exactly the new letters' advance and only the appended
  letters fall into the gap. After landing the two parts tile the new label
  exactly (glyph advances are additive).
- **Rewrite** (`III→IV`, `VIII→IX`, `IX→X`, or a fitted-font change): the whole new
  numeral falls onto the old one and knocks it down off the screen.
- **First arrival** (`0→I`): the layout opens the blue strip immediately (as
  before); the numeral then drops into it.

A rise while a numeral is still in the air reveals the current count (no queued
replay); a rise after landing starts a fresh drop from the landed count.

## Transfer — active→done with no existing done label (4.15s)

| ms | effect |
|---|---|
| 0–1050 | the blue backdrop **charges with green light**: accelerating pulses toward energetic lime, crackling light motes, the active numeral trembles; the character looks at it, increasingly uneasy, leaning back |
| 1050–1190 | cartoon **starburst** (ten rotating rays) grows from the numeral; eyes squeeze shut |
| 1190–1300 | full-screen pale flash. Under cover the snapshot swaps, the face leaves offscreen and the layout reflows unseen |
| 1300–1680 | flash fades to a full green screen |
| 1330–2180 | the new done numeral **arcs across and flips** (one full card turn: edge-on twice, mirrored back face between) with a light trail and landing ring |
| 2300–2950 | the face **creeps back in through the active side** (now the left), wary, near eye first |
| 2950–3300 | spots the numeral and brightens |
| 3300–3700 | crouch, **hop**, squash landing (eyes carry the jump; the tall body is mostly offscreen), rim thud |
| 3700–4150 | happy curved closure, then the usual completion cycle starts (celebration window extended past the return) |

The green wash stays until the face is home when no active count remains (its
body then covers the unused side); with a remaining active count it fades after
the landing and reveals the normal green/blue gradient. Paired completions with
an existing done label keep the deployed **completion sorting** unchanged.

## Split completions (two polls) — firmware + header

The transfer needs the done rise and the active fall in ONE snapshot. On the
Stick a completion was observed arriving over two 2s polls instead, so the
ordinary paired reflow played (the face lifted off a green I, no flash). The
cause is not logged (the bridge records no count transitions); Herdr status
passing through `unknown`, or a completion sequence lagging the status, would
both produce it.

`task_cues::CompletionPairing` (`avatar_frame::Avatar::report`, shared by the
firmware and the desktop avatar; gallery modes 30/31) holds the FIRST half for up to
`kWindow` = 4.6s (two polls plus write jitter) when no done label exists yet:

- active falls with done still zero (including I -> 0), or
- done rises from zero with active unchanged.

The presented counts stay the old ones (labels, layout, face selection and
reactions all follow them), so when the complementary half arrives the cues see
one full edge and the transfer plays from the ORIGINAL counts (e.g. II, not I).
Any other change, a revert, a request/recording/button cue (immediate release)
or the window expiring presents the snapshot normally with its usual reaction.
Counts are delayed, never dropped, invented or reordered. Trade-off: an active
fall that is not a completion (e.g. a viewed task) or a done rise while the
active count is unchanged now shows up to 4.6s later. Paired layouts (an
existing done label) are untouched: completion sorting still owns them.

`tests/test_task_cues.cpp` covers both orders, I -> 0, repeated heartbeats,
expiry/`due`, unready release, reverts, unrelated changes, the original window
for a further fall, the gallery fixtures (no green before the flash, old counts
until cover) and the header (both orders transfer; a lone half shows late; a
request releases at once without a cue).

## Paired arrival makes room

With both counters, the wide face covered the whole active top corner, so the
new numeral teetered and fell over the black face and the authored 20px jerk
barely moved it. `Arrival::room()` now measures how far the resting body (layout
only, neutral pose, no flow) reaches into the falling piece's column above its
resting place, plus `kClear` = 16px, capped at `kMaxDodge` = 96px (raised from
88px when active moved to the left: an appended letter such as II->III now
falls on the face side of the label, so `fallInner()` needs ~91px there). The face
backs away from the corner while the numeral slides in (0.04-0.4s) and the
authored 20px jerk still follows as it lets go; the return is unchanged. Single-
counter layouts need no extra room and keep the exact 20px jerk. Because the
body can clear further than the eyes may follow, `Arrival::eyeReturn()` gives
back up to the extra dodge so the outer eye stays on screen (>= 3px).
`pairedArrivalRoom` checks modes 21/23/26 at 16/33/50ms cadences: the falling
piece box never overlaps face coverage, bounded frame steps (authored jerk
only), eyes on screen and exact return to zero.

Physical timing unverified.

## Product semantics preserved

- Status sides from `status_sides.h` (blue-left/green-right), black solid
  interiors and status-matched outlines.
  Resting labels are never scaled; only the transient flip squeezes/mirrors, and
  it grows a fresh 2px outline around the squeezed interior (all outlines before
  all fills).
- Recovery/startup/listening returns are baselines. Requests, listening,
  offline, button cues and recording cancel without replay; cancelled transfers
  ease the face back (no teleport) and fade the wash.
- No task mutation, acknowledgement, sound or synthetic live events. Aggregate
  counts only.
- Sorting, checks, pops and contact springs pause during both cues; the
  director yields (external cue) and rebases on the final pose.

## Implementation notes

- `Scene::away`: whole-character translation (eyes, body, mouth). Zero keeps
  every established raster exact. Hidden uses 700px so even the 3x unused-side
  stretch stays offscreen.
- `status_background` gains a trailing `wash` colour/presence in the exact-keyed
  row cache. Exactly-zero wash is bit-identical; a full wash floods the backdrop.
- `count_raster::renderScaled`: box-filtered horizontal scale (negative mirrors)
  using bounded static scratch (no ESP32 stack spike); falls back for oversized
  labels.
- The starburst fills interior spans directly and evaluates only its spiky
  fringe per pixel (Chebyshev `cos 5θ`, no `atan2`), for about four frames.
- Firmware keeps the OLD layout through the charge (`displayCount` before the
  cover) so the face does not reflow early.

## Verification

- `tests/test_task_cues.cpp` (also under ASan/UBSan): baselines, heartbeat,
  readiness, midair supersede, restart after landing, one impact/home thud,
  bounded dodge, Roman extension tiling and rewrite knock-out for many counts,
  monotonic fall, fully visible teeter, transfer edge rules (no old done label
  only), old counts until cover, glow/wash/label-alpha timelines, offscreen
  masks/eyes in every layout and stretch, careful monotonic entry at three
  cadences, eased cancellation, full card turn and exact landing, ≥95% burst
  coverage, scaled-label outline order/width/mirroring, and the full header
  compositor (no idle triggers, hidden face, celebration after return,
  request/listening/offline suppression).
- `test_render_cache.cpp` covers the two new cache-key members and zero/full wash.
- All 25 native suites, 76 Python tests, normal O2 ESP32 build
  (769,743 program bytes, 67,528 globals) pass. Versus `bcd14d0`
  (751,475 / 51,368): +18.3KB flash, +16.2KB static RAM for the bounded flip
  scratch and the second (flying) label cache; 260KB remain free.
- Native gallery: `scripts/preview-avatar.py` event modes 20–26.

No physical appearance, smoothness or frame-time measurement exists for this
revision. The burst/flip frames add per-pixel work for a few frames; any
existing measured occupancy envelope must keep rejecting this source.

## Step over (opening the last unread completion)

Done-only -> zero with no active work used to fade the green strip through a
muddy dark green while the face reflowed. `task_cues::StepOver` replaces that
fade (1.5s, eyes-only acting, 90ms body lag):

1. 0–220ms: the face eyes the numeral and winds up away from it.
2. 220–700ms: a rounded black foot (`Scene::stepSide/stepProgress`,
   `avatar_scene::stepMask`) leads out of the body, its toe lifting over the
   numeral's height mid-stride, and plants 8px past the screen edge. The
   numeral is *covered* by the foot (`Mask::stepCoverage`), not drawn above it.
3. At 700ms the plant thuds; only now does `displayCount` drop the old count,
   so the strip, layout and green backdrop were held until fully covered. The
   (hidden) backdrop then leaves and the body follows home with a hop, a
   second settle thud at 1020ms and a happy squint.

The foot is unioned with the base mask like the shoulder fillet; every row
attaches to opaque base coverage, eyes still clip to the base, and zero
progress is bit-identical. Requests, listening, offline, button cues and any
newer snapshot cancel it without replay; the foot then retracts (120ms ease).
Startup/recovery is a baseline. Paired layouts, done->0 with work remaining and
not-yet-settled labels keep the old departure fade.

`tests/test_step_over.cpp`: trigger policy/baselines/cancellation, exact zero
parity, monotonic advance, attachment, raster/coverage agreement, full left-half
coverage when planted over 12 flow phases, and the full header compositor: no
frame ever shows a partially faded green on an exposed pixel (the old path had
five such frames). Gallery event mode 29.

Physical appearance and frame timing are unverified; the foot adds per-pixel coverage work for about half a second only
during the cue.
