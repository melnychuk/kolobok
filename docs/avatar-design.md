# Avatar acting and fluid-field design

**Status sides:** active work (blue) is on the LEFT and
unread completions (green) on the RIGHT. With only active tasks the face shifts
right; with only completions it shifts left; paired counters sit bottom-left
(active) and bottom-right (done) under a centred face. `firmware/status_sides.h`
is the single mapping for firmware, header widget, previews and tests; status-
directed authored clips keep their original keys and are mirrored on sampling
(`authoredDoneLeft`), never the framebuffer, so Roman numerals stay readable.
`-DSTICK_S3_DONE_SIDE=-1` rebuilds the original done-left layout for regression
comparison. Requests temporarily cover the counters with their warm field.

Width-aware [numeral accommodation](avatar-counter-space.md) shifts first and
eases body/eyes to 94% for five-character SIDE labels only. Bottom numerals grow
inward at normal font size, reducing only for real collisions; the avatar stays
full-size and centred. Native regressions and O2 compile pass.

Current staged behavior/transition integration is documented in
[Avatar continuity](avatar-continuity.md), including source-pose/tangent capture,
interruptible doze/wake/recovery, target-relative gaze, per-eye shape controls,
seeded quiet phrasing and sparse event-only completion glints. Status notes
below are snapshots of their respective revisions.

The retreat crease on both completed-only left and active-only right edges is
corrected in the [shoulder-junction patch](avatar-shoulder-join.md).

## Goal

This is a small character communicating task state, not an eye screensaver.
Thinking, completion, and asking for input need different intentions, timing,
eye silhouettes, and whole-face reactions. The face stays black, counts remain
solid black Roman numerals on colored backgrounds, and no microphone or agent
input is used by development/review tools.

## Research consulted

These are animation-practice references, not claims that gaze direction is a
scientific detector of thought, honesty, memory, or emotion.

1. Ferdinand Engländer, [Making Eyes that See](https://www.animatorisland.com/making-eyes-that-see/).
   Use the whole eye area to clarify gaze; lids, surrounding skin, and face
   deformation support one intention. We preserve the pupil-less design and
   therefore communicate gaze through paired eye position, shape, and tilt.
2. Engländer, [Eyes Q&A: Eyeball inside the head + Eye motions](https://www.animatorisland.com/eyes-qa-eyeball-inside-the-head-eye-motions/).
   Eyes move quickly, stop, and sometimes make smaller scans; perpetual floating
   looks unmotivated. Eyes often lead the head, and a meaningful blink should
   follow the main action rather than compete with it. Holds and broken rhythms
   are intentional acting tools.
3. Engländer, [Emotional Build Up](https://www.animatorisland.com/emotional-build-up/).
   Establish a mood, vary intensity, let viewers recognize it, and give them
   breaks. Motions should follow a motivation, not be arbitrary variation.
4. Don Kim / Animation Mentor, [Tutorial: How to Master the 12 Principles of Animation](https://www.animationmentor.com/blog/tutorial-12-principles-of-animation/).
   Anticipation, staging, squash/stretch, overlap, arcs, secondary action, and
   timing inform the authored beats and counter-safe composition.
5. Nathaniel Seymour / Animation Mentor, [Animating Caution in Character Acting with Key Poses](https://www.animationmentor.com/blog/tutorial-animating-caution-in-character-acting/).
   Particularly relevant: a character with only eyes communicates partial
   expressions through clear key poses; eyes register first, body reacts later,
   and camera-view playback is essential before polishing secondary motion.
6. Patricio Gonzalez Vivo and Jen Lowe, [The Book of Shaders: Noise](https://thebookofshaders.com/11/)
   and [fBM / Domain Warping](https://thebookofshaders.com/13/).
   Noise with continuous coordinates, layered octaves, and a warped domain
   creates flowing, spatially coherent motion instead of obvious sine wobble.

## Shared implementation

- `firmware/eye_acting.h`: 30 authored sequences: nine thinking and seven each
  for completion, requests and listening. Each has an
  anticipation, signature gesture, readable hold, follow-through, and settle.
- `firmware/eye_raster.h`: paired rounded-eye distance fields, curved upper lids,
  cheek/smile masks, real unilateral winks, happy closed arches, antialiasing.
- `firmware/eye_life.h`: hold-only micro-saccades and procedural blinks plus an
  underdamped eye-size spring, layered over authored acting (see below).
- `firmware/mouth_raster.h`: a small antialiased contour morphing between a quiet
  line, pursed focus, positive smile and open insight oval. Shared with previews.
- `firmware/fluid_face.h`: advected 3D value noise, quintic lattice interpolation,
  domain warp, and two rotated octave samples. This is a **CPU shader analogue**,
  not a GPU shader or Navier–Stokes simulation. It has no random contour
  destinations and no per-frame random shape jitter.
- `firmware/face_physics.h`: spring-driven translation, impact squash, and a
  32-point damped spring-chain rim. Fixed 120Hz steps make motion predictable
  across frame rates. Central contact points sit on a flat surface; the ends
  remain free to flap, overhang the numerals, and send rounded travelling waves.
- `firmware/avatar_scene.h`: shared full silhouette mask, quartic lower profile,
  body scaling, gaze composition, the low request dome, and eye rendering.
  Only the contact points constrain the rim; there is no counter-safe border.
- `firmware/avatar_interaction.h`: short-press wink, dictation-release nod,
  happy completed-side / focused active-side check-ins, and mask-contact numeral
  springs. Static overlaps remain intentional; advancing contacts transfer small
  impulses, with bounded travel and damped return. These are not task events.
- `firmware/count_pop.h`: snapshot-edge counter anticipation, a brief smaller-font
  preparation, 10px hop and damped rebound; first arrivals wait for readable labels.
  Real font sizes preserve letter proportions, and identical heartbeats are inert.
- `firmware/count_raster.h`: two-pixel background-colored glyph outlines followed by solid black
  interiors. Numerals render last, over both face and eyes, so brief overlaps
  remain readable without immobilizing a whole band of the face.
- `firmware/status_background.h`: shared green-to-blue (coral for requests)
  smoothstep gradient, with fixed Bayer dithering for RGB565.
- `tests/preview_avatar.cpp`: native RGB565 frame renderer using these same
  headers; `scripts/preview-avatar.py` generates GIFs and timestamped storyboards.

Eye transitions use short authored saccades and quintic easing, with genuine
holds. Positional travel (eye x/y and body lean/lift) now finishes 25% earlier
within each key interval; expression shapes, blinks, clip durations and holds
keep their authored clocks. The body samples the same sequence **90ms later**
and adds its authored lean, lift, compression/stretch, and flow pressure. A
160ms expression entry blend prevents shape pops, with position arriving at
120ms. Each state uses a size-derived no-repeat bag (nine thinking, seven
otherwise), with no immediate repeat across bag boundaries. The bag uses 16-bit
masks; previews, export inventory and all-clip tests use `clipCount(state)`. Layout side shifts use a
150ms easing constant (previously 200ms), and upward wide-layout reflow uses
210ms (previously 280ms). Translation springs run at 1.3x frequency with matching
damping scaling; the rim, fluid field, sleep fades and counter-pop clocks are
unchanged.
Spring translation follows those goals, with partial inertial coupling to the
eyes. Shape changes and count events excite local rim impulses; cubic sampling
makes asymmetric cartoon crests rather than a repeating sine-bend template.
Active-count increases cue a rightward glance, upward excited release, strong
happy eyes and a brief curved closure, then ease back over about 1.2 seconds.
The body and vertical-layout smile follow 90ms behind the eyes. Decreases retain
a quieter acknowledgement; new completions get the same happy release to the left.
Changed positive numerals independently prepare in a smaller fitting font, pop to
full size and hop 10px with a small rebound over 620ms. New labels wait until
readable before starting their pop; identical heartbeats never retrigger it.
Requesting input cancels facial cues and listening/sleep/offline suppress them.
The counters still come from aggregate status snapshots, not individual task IDs.

The fluid clock is integrated using elapsed time and emotional flow speed;
changing speed never multiplies absolute time and jumps the field. Deep focus
has slow, restrained flow. Completion can be buoyant. Input requests have the
highest flow rate and strongest engagement, with pauses to avoid fatigue.

## Slightly shorter eyes

Eye rendering now uses 81% of the original awake height (two successive 10%
trims: typical 56px → 50.4px → 45.36px),
without changing width, gaze centers/gap, body geometry, mouth anchors or authored
expression/hold/blink clocks. `avatar_scene::renderedEyeHeight()` keeps requests
at their 48px open-eye floor and eases back to the original sleep-line size.
Happy curved closures and unilateral winks remain readable in the native gallery.

The subsequent exact eye-kernel optimization is tested against the frozen prior
kernel **at the same new height**; it is not pixel-identical to the intentionally
larger old eyes. The current callback suite also covers the 81% height (about
20 million exact callbacks); the earlier 69,768-frame parity run used 90% height.
See [render performance](render-performance.md) for bounded column scratch,
host-only benchmarks and the prepared/not-physically-timed status.

## Snappy saccades, life layer and cartoon blinks

A 2026-11-04 expressiveness pass changes how poses move, not which clips play
or when. Key times, holds, the 160ms entry blend and the 90ms body overlap are
unchanged.

- **Derived per-key easing** (`eye_acting::mixSegment`): a key interval of at
  most 320ms whose eye x/y change is at least 4px is a saccade. Eye x/y use
  `snap()`, `1-(1-s)^2(1-2.6s)` over the first 75% of the interval: fast start,
  ~9% overshoot (≤3px on the largest 28px jump), exact zero-velocity arrival.
  Body lean/lift keep smooth travel and their spring. Short (≤160ms) closing
  lids accelerate shut (`s^2`); openings keep quintic timing. Longer moves and
  holds are unchanged.
- **Life layer** (`firmware/eye_life.h`): only while the authored eyes hold
  (`Player::segment().still`, including the pause after a clip, or idle/offline
  rest) and no cue changed the pose. Fixational micro-saccades of ≤1.8px x /
  1.1px y snap every 0.7–2.2s on an absolute schedule with their own RNG
  stream, so they are identical at any frame rate. They fade in over 120ms and
  out over 50ms. Procedural blinks follow a per-state mood (interval, double-blink
  chance; requests never double-blink). Authored closures reset the timer, so
  clips keep their own blink rhythm. A large (≥14px) short authored saccade
  without a nearby authored closure may blink with the move, anchored to the
  segment start, with a 1.2s refractory. Sleep suppresses blinks; listening uses
  0.6× micro amplitude. The old firmware-only idle blink timer is replaced.
- **Blink envelope**: 70ms accelerating close, 30ms hold, 140ms smooth open;
  one eye lags by 14ms (random side), so closures are organic but still read
  as one blink. It fully closes at 20, 25, 30 and 60fps.
- **Cartoon blink shape** (`avatar_scene::kBlinkSink=.22`, `kBlinkWiden=.06`):
  as a plain closure removes height, the eye sinks by 22% of the lost height
  along its own axis, so the lid comes down and the closed line sits in the
  lower part. It also widens by up to 6%. Happy arches (smile ≥ .5) are
  untouched. `eye_raster::render` defaults to zero, which is the exact frozen
  kernel, so the parity oracles are unchanged.
- **Eye-size spring** (`eye_life::SizeSpring`, 5.5Hz, ζ=.5): replaces the 55ms
  first-order lag for width/height in firmware, previews and the header widget.
  Fixed 240Hz substeps with interpolated targets; ~16% settle on a step, less
  on authored ramps; consistent across 20/30/60fps.

`tests/test_eye_life.cpp` covers curve bounds, every authored segment's
overlap limits, closing ease, segment classification, frame-rate-independent
micro motion, blink spacing/closure/suppression/wink preservation, saccade
blinks, millis wrap, spring bounds/consistency, and blink-shape parity. It also
guards the eye raster workload against the pre-pass baseline over every clip
at 20/25fps (max +3%, mean +3%). Measured: vertical max 6490 vs 6358 (+2.1%,
spring overshoot) and mean 4223 vs 4252; request max 5466 vs 5452 and mean
4195 vs 4227. This is a host pixel-count proxy, not device timing.

No physical timing capture.

## Vertical-layout mouth

Mouth artwork is now **disabled by default** with the shared compile-time flag
`STICK_S3_ENABLE_MOUTH=0` in `firmware/avatar_scene.h`. The implementation is
retained; an explicitly requested build can use `-DSTICK_S3_ENABLE_MOUTH=1`.
`test_mouth.cpp` opts in to preserve acting/raster coverage; `test_mouth_flag.cpp`
and `test_mouth_feature.py` verify both the default-off and enabled builds.
Eye acting, body geometry, task semantics and completion sorting are unchanged.
The following describes the retained optional mouth, not default screen artwork.

The mouth is a secondary action, applying the earlier Animator Island eye/acting
references and Animation Mentor's staging/overlap principles. Eyes lead; mouth
shape follows the existing **90ms-delayed body pose**, rather than a separate
random talking loop. The timing is our implementation choice, not a prescribed
number from those sources. Authored holds remain still and readable.

The refined mouth sits 5px lower than the first pass, with three opaque stroke
rows rather than a hairline. It follows 60% of the eye-center displacement, while
its corner curl/skew follows the delayed acting pose. This applies the eye-led
lip-motion example in Animator Island's Eyes Q&A rather than adding a free-running
wiggle. Neutral uses a rounded line; thinking has visibly pinched upper/lower lips
with a soft crease, travelling left/right with the look. Concentration strengthens
the purse, while insight relaxes it into an open oval. Completion gets a deeper
positive smile and restrained one-sided smirk. Blinks do not flap the lips.
One contour morphs between these shapes without an extra line through an open mouth.

Only the awake **vertical face layout** gets a mouth (not a screen-rotation rule).
It fades out during the first part of reflow into the wide two-counter layout;
the broad input-request layout and sleep are also mouth-free. All mouth pixels
are clipped to the face mask, and numerals still render last. No voice analysis,
lip sync, agent input, or task-status changes are involved.

The six vertical mouth studies cover focus, insight, relief, wink, pursed reading
scans and left/right comparisons. Native
mouth tests cover shape contrast, eye clearance, mask clipping, layout/sleep
visibility, overlap timing and still holds across all thinking/completion clips.
A 168-frame native comparison confirmed the paired/wide layouts remain pixel
identical to the committed eyes-only avatar.

## Nine thinking performances

| Sequence | Eye performance | Matching face performance |
|---|---|---|
| Recall | small anticipation; up-left hold; blink; up-right check | follows the look with a restrained lean and slight lift |
| Compare options | left/right saccades with separate holds; centered squint | leans toward each option, then compresses to concentrate |
| Read and scan | stepped left-to-right reading, lower return, blink | light lateral follow-through; stable, quiet contour |
| Verify a hunch | asymmetric lid, side look, blink, brighter confirmation | a cautious lean, then release; avoids distressed tilt |
| Aha insight | concentration hold, quick upward widening, settle | compression before the eyes open; a delayed upward stretch |
| Calculate | broad/downward focus, double blink, small upward result | contained compression; no sharp contour lip on the blink |
| Deep focus | slow symmetric squint, long still hold, blink/release | low-pressure flow; narrowed, calm body rather than agitation |
| Work through it | tilted asymmetric-lid study, long hold, blink, straighten and small nod | restrained left lean follows the eyes, then a soft lift and settle (3.02s) |
| Connect the dots | lower-left study, diagonal upper-right look, centered concentration hold | gentle opposing leans and lift, then calm compression (3.14s) |

The two added thinking clips retain eyes-only defaults, 160ms entry blending and
90ms body overlap. No existing clips, status priorities, event/check-in clocks
or frame pacing changed. Native galleries include both paired-counter and
active-only studies. These additions are native-tested and O2-compiled.
No physical timing claim.

## Seven positive completion performances

| Sequence | Eye performance | Matching face performance |
|---|---|---|
| Relief | inhale/open, slow blink, soft smile | broadens and settles downward like an exhale |
| Proud of it | upward smiling glance, small tilt, front-facing hold | rises and leans lightly before settling |
| Joyful bounce | squash anticipation, stretch, smaller second bounce, happy hold | follows with a damped compression/stretch rhythm |
| Warm smile | gradual cheek lift, patient hold, soft curved blink | slow, warm expansion with low-energy flow |
| Playful wink | smiling turn, one-eye arch closure, reopen | small lean supports the wink without a droopy head |
| Happy double blink | happy hold, two distinct curved closures, settle | quiet body hold lets the blink rhythm read |
| Ta-da | compress, open/widen, then strong smiling hold | buoyant rise and expansion, then a gentle settle |

There are no sad, angry, frustrated, or disappointed completion scripts. A task
finishing is a positive event. Happy closures stay curved instead of becoming
flat sleepy lines. Foreground numerals retain filled black interiors; a separate
status-gradient-colored outline protects contrast when a free flap passes behind them.

## Seven input-request performances

Input requests preempt completion/thinking immediately, while preserving a short
visual entry blend. They do not interrupt recording/listening. The whole screen becomes a slowly
pulsing orange-yellow field (`request_scene::field`, yellow `#facc15` to amber
`#fb9216`, 3.6s exact-wrap cosine, never a flash) with a large, lightly bobbing
black `!` near the top. The body sits below it: an ellipse dome whose centre
is below the display (`kRequestDome*` in `avatar_scene.h`), so only the top of
the blob shows and the lower rim never does. Done/active numerals, their glints
and colors are hidden visually while requesting (task data is untouched) and
fade back afterwards. Authored request squash/stretch reaches the dome only
partly (35% width, 30% height); a soft constraint keeps 9px of body above the
eyes, so looking up at the `!` stretches the dome instead of clipping the eyes.
With the extra room the body hops (`request_scene::Hops`): a double hop greets
the request 260ms in, then one or two hops every 4.2–8s, each a 90ms crouch,
9px parabolic 300ms jump (second hop 70%) and landing squash, rendered through
`Scene::hop/hopStretch` (zero keeps every raster exact). Button and startle
cues pause hops; ending the request stops them at once. Open eyes stay at least
48px high in the settled request layout. Full closures are brief; there is no
long sleeping pose. `tests/test_request_scene.cpp` covers the field, sign
bounds, dome coverage, hop scheduling and unclipped eyes in every clip/hop phase.

| Sequence | Eye performance | Matching face performance |
|---|---|---|
| Look at me | anticipation, large direct eye contact, quick blink | expansive, forward-facing engagement |
| Double take | purposeful left/right checks, blink, wide front-facing hold | quick lateral follow, then a centered rise |
| Knock knock | two clear vertical engagement beats | delayed compression and bounce like a gentle tap |
| Question tilt | open eyes, one rigid tilt then the other | follows with a questioning lean, not angry lids |
| Peek and open | brief closure, fast full opening, another quick blink | releases into an open body, not a waking-from-sleep performance |
| Check then ask | left/right checks, deliberate direct request | active scan followed by a larger, stable front-facing shape |
| Please | wide upward appeal, brief blink, patient open hold | hopeful lift with a readable pause |

There are no screen flashes or extra repeated chimes. The existing attention
chime behavior is unchanged.

## Listening, button cues and counter contact

Seven listening performances are now authored in the same player: ready to hear,
lean in left/right, a small acknowledgement nod, taking it in, reassuring blink,
and patient listening. They use receptive open-eye holds, restrained turns, short
closures and a separate no-repeat bag. They are clock-authored, not lip sync.
Since 2026-11-19 the eyes themselves become a soundwave while listening (see
"Listening soundwave" below). A short press gets a happy wink toward the done side; release after a
recording gets a small nod. Those acknowledge gestures, not successful desktop
focus/transcription. The 300ms hold threshold and local-only ASR remain unchanged.
Recording hides counters visually without changing the counts/unread set, recenters
the face, and takes priority even without a connected status bridge.

Both counters now motivate occasional down-right curious/focused check-ins and
warm down-left looks, including a happy curved closure. Each press excites only
the nearby free spring rim. Its actual mask is probed against the resting numeral
bounding box; advancing penetration gives a damped spring impulse, not a count
mutation. First/static overlap only establishes a baseline. Counter offsets stay
within ±3px x and -2.5..3px y; existing count-change pops remain independent.
Probe resting bounds so a numeral's own hop cannot kick itself. Requests and
listening preempt check-ins. Black interiors/backdrop-colored outlines remain
foreground; no safe band or collision-driven layout swap is introduced.

`tests/test_avatar_interaction.cpp` checks no-repeat listening, entry
continuity, short closures, cue suppression/wrap, side-specific emotions,
fixed-step spring consistency, bounded travel/settling, a forced real mask
overlap, and actual contacts during the authored mixed check. Gallery additions
are seven listening GIFs plus mixed-check, tap and release GIFs. It includes the
pre-existing round-3 renderer working-tree edits; those are not part of the
interaction commit. No new physical frame timing was measured, and physical
click/hold/microphone feedback remains a human check. Older renderer parity
claims below describe the original 21 scripts, not a claim that these new
interaction timelines are pixel-identical.

### Single-counter contact extension

Completed-only and active-only layouts now inspect only their visible numeral.
They retain the existing check clocks and 90ms body follow-through; the tall face
reaches sideways rather than looking into an empty bottom corner. Completed-side
checks keep their happy smile/curved closure; active-side checks stay focused.
Changing counter presence resets the check softly, and requests, listening,
button cues and count reactions keep priority.

Contact probes blend the actual sideways mask penetration in vertical layouts
with the existing top-edge penetration in corner layouts. Resting numeral bounds,
static-overlap baselines, bounded spring travel, foreground background-colored
outlines and black interiors remain unchanged. No task data is mutated.
Regression fixtures cover both sides with `I`, `III` and long Romans, real contact,
settling, absent-side suppression and presence transitions. Four additional
synthetic event GIFs (10–13) cover completed-only / active-only and narrow `I`.
Native tests and an O2 Arduino build passed for the initial extension.

### Local pokes and occasional completed-side kisses

The tall face now uses less whole-body lean/expansion and grows a compact rounded
tip at the actual resting numeral; paired layouts grow a local lower-rim tip.
The press keeps the previous check clocks and 90ms eye/body overlap, with a small
inward recoil. A polynomial bump avoids extra noise/trig in the silhouette work;
it is absent at zero strength. Both mask envelopes include the protrusion, so
it cannot be clipped away by the original ellipse's horizontal support.

Every third completed check adds smiling curved eye closures and a narrower
kiss-shaped tip. Vertical layouts briefly fade the normal mouth out and show
tiny sideways puckered lips just inside the tip; the mouth never sweeps across
the eyes. Paired layouts remain mouth-free and convey the kiss through rim/eyes.
No hearts, sounds, task acknowledgements or microphone analysis are added.

Contact now averages advancing penetration over the resting bounds instead of
using the deepest sample capped at glyph width/height. The old cap could hide a
new press when an animation already overlapped the whole numeral. Static overlap
still cannot drive perpetual movement, and ±3px/-2.5..3px spring limits remain.
`tests/test_avatar_poke.cpp` checks 140 real-contact performances (all seven
clips, both sides/layouts and five fitted labels), saturation regressions,
localized shape/AA spans, zero/tiny strength, kiss placement/clipping, priority
and wrap. Event GIFs 14–15 add vertical kiss and paired kiss-shaped-rim
fixtures; the gallery now has 12 event GIFs. All 12 strict native suites and 55
host tests pass. No physical FPS or button/microphone check was performed;
historical occupancy envelopes still do not match this revision.

### Solid-black connected hands refinement

Physical feedback identified unwanted white lip detail and clipped/detached
extrusions in the first poke deployment. The correction removes the extra lips;
kisses remain affectionate eye closures plus narrower black contour acting.
Eyes and the regular mouth clip to the original body coverage only, and the
mouth rests during presses, so the hand contains no pale artwork.

A prepared rounded extrusion now starts in an opaque part of the actual body
and is unioned with its coverage, instead of modifying two intersecting body
limits. It can extend past both old side/bottom envelopes without being chopped
off or leaving a disconnected island. Reflow blends the root/direction; a root
search handles rim dips and narrow support. Opaque/interior classification and
base-mask spans stay optimized; square roots are limited to extrusion fringe
pixels and geometric contact cross-sections. No clocks or task state changed.

Numerals probe the exact rounded extension, not its candidate rectangle.
Advancing contact transfers a stronger bounded impulse; existing ±3px/-2.5..3px
return and independent count hops remain. All 140 authored-layout/label cases
now assert >1px travel (minimum 1.05px natively). Added regressions flood-fill
every visible hand pixel to its opaque body root across 42 reflow/body/rim
stress cases, check union span/AA parity, continuous zero/tiny strength and
absence of white artwork. This is not a physical FPS measurement.

### Broad resistant-body touch

Further device feedback found the rounded hand too finger-like. Contact now uses
an embedded 30px-radius shoulder (27px for affectionate checks), rather than the
13px/9px finger. Outward contour reach is capped at 18px sideways and 14px down,
previously 32px/26px; center travel remains below 20px/16px. The radius is always
larger than the extension travel, producing a wide shallow body bulge instead
of a small tentacle. The existing authored press/hold/release and 90ms body delay,
whole-body lean, rim kick and fixed-step physics are unchanged: no body slam.

Black-only union coverage, opaque attachment, facial-artwork exclusion and
foreground numeral outlines remain. A slightly firmer bounded counter impulse
keeps every tested number responsive despite the shorter reach; 140 fixtures
retain >1px motion (minimum 1.03px natively). Tests additionally enforce broad
radius-to-travel proportions and reduced reach through reflow and authored
poses. All 12 native suites, 55 host tests, sanitizer checks and 16 QEMU
functional tests passed. No new physical FPS, button/microphone or full
production GDMA guest measurement was performed; earlier build IDs/envelopes
remain historical.

### Paired numeral flight and strict active looks

When both counters are settled/readable, an occasional visual duet lifts the
numerals 32px and glides them inward before a small landing rebound. A 360ms
stagger makes the lead readable; active leads the first flight, done the next.
Each label's 2.5-second excursion fits inside a 2.95-second tracking performance,
starting after 7.6 seconds of uninterrupted eligibility and repeating every
16.8 seconds. Fitted full-label widths limit inward travel to preserve a 10px
resting separation, even for XCIX+. Neither numeral crosses to the opposite
status side, swaps labels, or changes/acknowledges task counts.

Eyes follow the lead and then the follower, with a strict active-side expression,
warm completed-side expression and restrained 90ms-delayed body lean. Active
checks/tracking use .22/.24 upper lids, .94 pose height and no smile; completion
tracking retains a .63 smile and open upper lids. No angry tilt, global eye-size
change, or authored-clock change is added. Foreground black interiors and exact local
background outlines remain unchanged; the wide face stays mouth-free. Contact
checks pause during flight, springs settle without flight-derived collision input,
and landing establishes a new contact baseline. Requests, listening, offline,
button/count cues, unreadable labels, pops and single-counter layouts suppress
flight; reentry waits rather than replaying a cancelled excursion. Existing
single-counter poke/kiss clocks and contact geometry are untouched.

`tests/test_counter_flight.cpp` covers 40 fitted-label pairs, wraparound,
continuous/bounded travel, separation, alternating leads, gaze/body overlap,
settling, cancellation, repeated updates and frame-cadence-independent sampling.
It also rejects historical vertical occupancy for the wide flight layout and
unknown animation fingerprints. Native event fixtures 16–18 show normal Romans,
long Romans and I + I. Strict active/positive done contrast is asserted for both
flight leads and counter check-ins. All 13 strict native suites, 55 Python
tests, sanitizer flight checks and 16 QEMU functional tests pass; changed-path
active LSP probes are clean. No fresh physical smoothness/FPS, button/microphone
measurement or full production GDMA guest run was performed.

### Completion sorting — replacement for idle flight

Pickup now has a reason: an observed aggregate done increase together with an
active decrease, while both old counters are readable. Both OLD numerals lift
simultaneously and gather without crossing status sides. A still apex hold runs
1050–1600ms; both labels change to the exact NEW snapshot at 1400ms, **before**
return travel begins. Thus II done + III active becomes III done + II active
while gathered, then returns to green-right/blue-left. Batched completions use
actual totals rather than an invented one-task transfer. Zero remains hidden.
The full beat is 2.95s, retaining 32px lift, fitted separation, strict active /
warm done eyes and 90ms body overlap. No idle recurrence or alternating lead.

Real task counts update immediately; only their presentation is staged. Sorting
replaces competing count hops/facial cues. Checks pause and spring contact is
rebaselined on landing. Requests, listening, offline or buttons cancel the beat;
a superseding snapshot reveals current values without stale replay or another
midair pickup. First/recovery snapshots are baselines. Done-only increases,
active-only decreases and identical heartbeats never imply a transfer. If no old
done numeral exists, the ordinary completion cue remains the fallback.

Shared firmware/native kernels and the prepared header backend use this logic.
Native modes 16–18 show normal Romans, long Romans and the last active becoming
hidden zero. `test_counter_flight.cpp` tests the still-apex swap before return,
exact snapshot values, priority, cadence, wrap and envelope rejection;
`test_avatar_widget_sort.cpp` tests the full command-driven compositor and
rejects synthetic fixture events in live mode. The normal O2 build uses 704,511
program bytes and 46,688 global bytes. All 14 strict native suites and 63 Python
tests pass, including both mouth flag settings. Earlier sorting-only sanitizer
and 16 QEMU functional tests also pass; these are not physical animation tests.
No fresh physical timing, button/microphone test, full-production QEMU/GDMA run
or task acknowledgment is implied.

### Sorting with blobs

Completion sorting is now carried by the body itself. Before anything moves
(0–380ms) the lower rim droops into a round blob over each numeral and overlaps
its top by `kGrip` = 4px (the label keeps its background outline, so it stays
readable). The authored pickup then runs 380ms later with each numeral hanging
from its blob: grip dip, lift, gather (the two blobs merge into one scalloped
rim), still apex hold with the swap at `kSwap` = 1780ms, return and the small
set-down hop. The blobs carry the numerals all the way back down into their new
places; only after both are set down (`kRelease` = 2880ms) does each blob flow
back into the rim over 380ms, ending the beat at `kEnd` = 3330ms. Numerals are
carried `kCarry` = 14px high while the body leads them up `kRise` = 20px
(physics surface lag included; it is clamped at 22px), so they travel over newly
revealed status background below the face instead of over the black body.
When the last active count becomes zero, its numeral still vanishes at the swap
and that blob lets go there. An earlier arms-and-fists version was rejected as
too human-like.

A blob is a U-shaped lobe — circular tip, radius numeral width/2 + 3px clamped
to 10–16px (a round drop on a narrow I, a middle hold on long Romans) — joined
to the rim by a 7px smooth-max fillet. `avatar_scene::Hands` (reach, x, tip y,
radius per side) is part of the scene and `lobeMask` only lowers existing rim
columns before the poke/step extrusions, so the body stays one piece, the
raster/fast paths are unchanged, and zero reach is exactly the old mask.
`CounterFlight::hands()` uses the same carried positions as the drawn labels in
firmware, native preview and header. `test_counter_flight.cpp` checks zero-reach
exactness, the tip/rim bounds, opaque continuity down to the tip, raster vs
coverage identity, still numerals during the reach, set-down before release and
the empty blob at the swap; `test_avatar_widget_sort.cpp` checks that the full
compositor overlaps each label top only slightly at the swap. Native modes 16–18
show the result.

### Retain the departing background through reflow

When sorting makes the last active count zero, the numeral still disappears at
1400ms, but its blue backdrop now remains through the wide-layout hold and face
reflow. Background presence is independent of numeral visibility: after sorting,
release waits for corner reflow below 5% and the single-side shift above 95%,
then fades over the existing 120ms presence constant. The firmware also waits for
single-layout expansion above 95%. Layout reclamation follows the disappearing
label, not the retained backdrop, so holding blue cannot deadlock the shift.
The organic face need not cover every screen corner to release the backdrop.
Requests/listening/offline clear retention; returning counts cancel it.

Shared background state covers firmware and the native/header compositor.
`test_avatar_widget_sort.cpp` checks zero-at-apex, retained blue until the
shift, eventual removal, priority cancellation and returning counts. All 74
Python tests, 13 strict native suites plus the full header sorting regression,
and the normal O2 Arduino build pass. User feedback confirmed the improved
transition but identified a remaining blue oval-edge sliver. No new timing or
matching occupancy envelope is implied.

### Fill the unused screen edge

The single-counter silhouette now stretches only its unused half horizontally,
up to 3x, following the existing eased side shift and corner reflow. Both row
bounds and the lower envelope use the same stretch; changing only one leaves
an oval-shaped blue sliver. The counter-facing contour, body/eye/mouth anchors,
acting/flight clocks, status colors and backdrop-retention logic are unchanged.
Paired, request and fully sleeping layouts do not stretch, and transitions remain
continuous. The vacated side now reads as a large black body extending offscreen,
not an oval floating against the retained status color.

`test_avatar_edge_fill.cpp` checks a fully opaque 16px unused-edge strip across
6,804 authored frames, both sides, seven clips and two numeral-strip widths.
Python discovery runs this regression; the full sorting compositor also checks
the entire rightmost column after reclamation. The frozen round-3 mask oracle is
unchanged: unaffected edges still compare exactly, while intentionally stretched
edges must only expand. All 15 strict native suites and 75 Python tests pass;
the normal O2 Arduino build compiles. Physical appearance feedback and fresh
timing remain pending; historical occupancy gates remain unchanged.

## Soft vertical sides

Analysis: in single-counter layouts the counter-facing edge was a rigid,
nearly constant-curvature ellipse arc that only slid and inflated. The 32-node
rim is laid along the bottom; sides sampled it at 18%, so in vertical layouts
it contributed ~0.2px, and fluid noise only ±1.8px. The paired layout feels
alive because its visible lower rim gets the full ±18px flaps and support
contact.

`face_physics::Surface` now also carries two 20-node side membranes spanning
rows -20..155, so both free ends sit offscreen. They store horizontal deviation
from the rigid silhouette and are driven by:

- body translation and vertical bob each 120Hz substep; loose upper/lower flesh
  lags more than the core;
- width/height changes once per frame, where the core lags its own shape change
  only partly;
- an outward swell on the cued side of `kick`, giving an elastic lunge with the
  leading side swelling and the trailing side dragging.

The membranes use uniform anchoring, because a stiffer core produced a pointed
tent profile. High tension plus curvature-velocity viscosity damps short
ripples first. `edgeSample` saturates softly at 10px; a hard 20px clamp is only
a safety net, because hard clamps caused visible bounces.

`avatar_scene::softSides` weights the membranes, a settled-drop profile
(narrower shoulders, heavier belly, ±~5px) and a little extra fluid flow. The
weight is `(1-corners)(1-alert)(1-.8 sleep)·side`: exactly zero in paired,
request and no-counter layouts, so those frames are bit-identical to before.
The lower envelope widens by the same weight. Otherwise the rigid oval bottom
intersects the bulging side and leaves a visible corner.

- Across all thinking/done clips, the shape changes ~2x more per frame, with
  smaller peak per-frame jumps than the old rigid snaps (10.2 vs 12.0px).
- Edge curvature stays at the old level (max 0.15px/row² over 6,804
  edge-fill frames).
- Numeral contact springs move more often, from real mask contact and within
  the existing caps.
- Host-only mask build is ~15% slower (~3.5µs); this is not a device FPS
  figure.

Tests:

- `test_face_physics.cpp` covers one-sided swell, priming, sway lag,
  widening lag, soft saturation, bounds, settling and frame-rate consistency.
  It also checks paired masks are unaffected by membrane state.
- `test_avatar_edge_fill.cpp` guards zero lower-envelope cuts and smoothness.
- The round-3 oracle allows bounded soft-side deviation only where
  `softSides>0`.

- All four regions reported `Hash of data verified`.
- The build includes the working-tree power-nap firmware changes.

Physical appearance and timing have not been checked.

## Smoothness and variety pass

An analysis of every clip (DTW over eye-visible pose fields, interior-key
velocity, clip boundaries) drove a series of focused commits. Each is native-
tested and O2-compiled for the Stick; no device timing exists for any of them.

### 1. Clip handoffs, request stretch and exact arrival

- **Clips start from the pose on screen.** `eye_acting::sampleFrom()` replaces
  a clip's authored first key with the actual entry pose and keeps the first
  segment's full authored duration (no separate 160ms blend). Previously every
  clip restarted from neutral `p()`: the completion smile (.45) collapsed and
  regrew every ~4.3s, request eyes shrank from 1.10/1.15 and regrew every
  ~3.2s, and Ready to hear left the listening rest. Completion keys that were
  below the resting smile now keep at least .45; closures there become happy
  arches. Interruptions (requests, listening) still ease from the current pose.
- **Request squash/stretch is visible.** `renderedEyeHeight()` used
  `max(height×.81, 48)` while requests, which made every authored request
  height (.88–1.25 → 35–45px) render at exactly 48px. Requests now scale by
  `kRequestEyeScale = 48/(44×1.15)`: the request rest pose still renders at
  48px, while Knock knock, Look at me and Peek and open squash/stretch over
  ~37–52px. The size target floor is 36px (`kRequestEyeFloor`).
- **No zero-velocity stops mid-move.** Interior keys where a field kept moving
  in the same direction (Aha insight 1900ms, Deep focus 1980ms, Relief 390ms,
  Proud of it 1790ms, Look at me 140ms, Peek and open 280ms, Check then ask
  1050ms) now reverse (settle with overshoot), hold, or carry the value.
- **Eased layout values arrive.** `avatar_scene::approach()` snaps within 1e-4
  of its target. A plain exponential stalls a denormal/ulp away (2.8e-45 and
  0.99999988 on the host), so after any paired/request/sleep transition the
  exact-zero fast paths (lower-rim noise, soft sides) never engaged again and
  exact-keyed background rows kept missing. `SortingDeparture` arrives too.
- **Request `!` clock** uses `(now % 1800)/1000`, an exact period of its bob,
  instead of `now/1000.0f`, which steps visibly after a day of uptime.

`test_avatar.cpp` asserts exact boundary continuity over a minute of each state,
completion smile ≥ .44 and request height ≥ .87 after the first clip, and that
`approach()` arrives. `test_eye_pass.cpp` covers the request scale. The eye
workload guard keeps its baseline and +3% mean; only the request max may reach
+10% because the authored stretch is now visible (mean fell ~8%).

### 2. Long-uptime flow clock, sub-pixel springs, no font flicker

- **Flow clock wraps exactly.** `flowClock` grew forever at ~0.2–1.4/s. As a
  float its per-frame increment quantised after ~2 days and it stopped
  advancing after ~1–2 weeks of plugged-in uptime, freezing the edge flow.
  `fluid_face` now hashes a 16-periodic lattice; every time-dependent lattice
  coordinate moves by a multiple of 16 when the clock advances 8000, so the
  field is periodic and `fluid_face::advance()` wraps there (measured wrap
  mismatch < 5e-4, i.e. ~1e-3px). The lattice repeats spatially only every
  ~440–890px. Noise values changed, so frozen oracles that call the production
  field remain consistent; no reference kernel was edited.
- **Contact springs move sub-pixel.** Numerals were rounded to whole pixels,
  so the ±3px, ~1.6Hz contact spring stepped. `count_raster::Cache::render`
  takes the spring as a separate shift; zero keeps the exact integer path, a
  whole-pixel shift reproduces it one pixel over, and fractions bilinearly
  resample the cached coverage (outlines still precede interiors; interiors
  stay solid). Springs settle to exactly zero, so resting labels stay crisp.
  Count pops and flights keep integer positions (fast, large moves).
- **Count pop is lift-only.** The 90ms smaller-font swap lasted two or three
  frames, jumped sideways with the narrower label and read as a flicker.

### 3. Overlapped presentation (firmware only)

See `docs/render-performance.md`: two internal frame buffers with the SPI DMA
transfer overlapping the next render, direct background/face fills, fixed 33ms
deadlines, a 25Hz IMU poll and no per-frame `String`. The header widget does not
use this path; acting and pixels are unchanged.

### 4. Occasional, varied counter checks and unmetered pauses

Counter checks used to run on a fixed 8.4s cycle (active 1400–3500ms, completed
4400–7000ms), fully replacing the eye/body pose for ~2.8s and partly for ~4.7s
of every cycle. With counters visible the thinking clips were on screen less
than half the time, the life layer was muted during checks, and the same check
repeated every 8.4s. `avatar_interaction::Checks` now schedules live checks:

- One check at a time, beginning only while `Player::resting()` (the clip has
  finished); `Player::update(..., hold)` keeps that resting pose until the check
  and its 90ms body follow-through end, so clips and checks never compete.
- First check 1.4s after counters appear, then random 5.2–12.8s gaps after each
  check. With both counters the side alternates 70% of the time.
- About a third of checks are glances: the same strict/warm look, no shoulder
  press, rim excitation or poke. The first check is always a press. Looks
  jitter by up to ±3px x / ±2.5px y. Every third completed press kisses.
- Clip pauses are drawn per clip: thinking/completion 0.9–2.3s, requests
  0.5–1.0s, listening 1.2–2.6s (were fixed 1.5s/0.7s).

Event previews and contact regressions use `Checks::fixed`, the old
deterministic timeline, so their fixtures and frame times are unchanged.
`test_avatar_interaction.cpp` runs ten simulated minutes of the live scheduler:
starts only at rest, the held clip never restarts mid-check, gaps ≥ length +
5.2s with both short and long gaps, both sides, glances without pokes, first
press, and occasional kisses; single counters check only their side.

### 5. Diversified acting library

Names, counts and states are unchanged; the closest pairs were re-staged so each
clip has its own intention and silhouette of motion:

| State | Clip | Was | Now |
|---|---|---|---|
| Thinking | Aha insight | centred squint, then pop | pondering down under heavy lids, sudden pop up, overshoot settle |
| Thinking | Calculate | centred squint, two blinks, look up | four rhythmic counting saccades across, then a decided nod |
| Thinking | Verify a hunch | side look, blink, look up (≈ Recall) | glance, return, glance again, sure nod; no blink |
| Thinking | Compare options | ends in a centred squint (≈ Deep focus) | ends in a small undecided tilt |
| Completion | Ta-da | squash/stretch, smile hold (≈ bounce) | anticipate away, present toward the completed counter with a tilted big smile, happy closure |
| Completion | Proud of it | left/right tilted smiles (≈ new Ta-da) | chin-up rise and hold, satisfied nod |
| Completion | Warm smile | smile hold + closure (≈ double blink) | slow tilted "melt" with one long happy closure |
| Request | Check then ask | left/right scan (≈ Double take) | look up at the `!` above twice, popping back to the viewer |
| Request | Please | big stare + blink (≈ Look at me) | eager tilted side-to-side wiggle, then a big stare |
| Listening | I follow | one nod | two small nods, no blink |
| Listening | Taking it in | small aside hold | slow downward drift across, blink on the way back |
| Listening | Reassuring blink | ordinary blink | slow happy blink |
| Listening | Patient listening | hold + blink | settle and slow drift, no blink |

Closest-pair DTW distance (eye-visible fields; requests without the invisible
pre-fix height) rose from .40→.45 thinking, .34→.40 completion, .22→.27
requests and .16→.22 listening (listening is intentionally quiet; the mirrored
lean pair is by design). `mixSegment` clamps closures to [0,1]: the quintic ease
can exceed 1 by an ulp, which reopened partial closures to -1e-7.

### 6. Calm completion between celebrations

The completion face used to loop all seven clips, including Joyful bounce and
Ta-da, for as long as unread completions remained. `Player::calm` is now set
once the 3.6s celebration window after an actual completion edge has passed
(firmware `celebrateUntil`; the widget mirrors it). Calm completion skips the
two `lively()` clips (still visiting every other clip without repeats) and adds
1.6s to each rest (2.5–3.9s). A new completion re-enables the full set at once;
the face stays positive throughout. Other states ignore `calm`.

### Status

Physical appearance, cadence and DMA behaviour unverified.

## Visual development and independent critique

A different model family, **Claude Sonnet 5.5**, reviewed the design and first
rendered storyboards. Its still-image critique was treated as advice, not as
proof of playback smoothness. Suggestions implying sadness/frustration on
completion were rejected because they contradict the dashboard semantics.

The first visual pass identified and the second pass revised:

- request squashes/closures that looked sleepy: 48px open-eye floor, shorter
  closures, no lingering one-eye waking pose;
- skeptical request lids: replaced with open rigid tilts;
- shallow happy shapes and flat closure lines: deeper smiles and 4px arches;
- contour lips around deep squashes: reduced warp amplitude and kept the
  counter-safe taper connected;
- single-count request layout seam: full lower status-colored portrait;
- cramped top area: shorter yellow band and a slightly raised request gaze.

All 21 second-pass storyboards were visually reviewed, including one-count and
long-Roman stress fixtures. GIFs preserve the whole frame sequence at ~30fps.
The native preview uses the same animation/player, mask, eye geometry, field,
font masks, soft-body physics, count outlines, and RGB565 colors. The 21 acting
fixtures hold their count layout stable; three extra synthetic event fixtures
exercise count arrival/completion reactions, layout reflow and a vertical new-task
smile, with simplified count-appearance timing. GIF clocks use 30/30/40ms durations to preserve ~30fps.
It is **not** a camera capture or a measurement of on-device frame timing.
USB/layout transitions and physical viewing remain a human device check.

## Reproduce the review

```sh
.venv/bin/python -m pip install -r scripts/requirements-preview.txt
.venv/bin/python scripts/preview-avatar.py
# Prints a private temporary directory containing index.html, 28 acting GIFs,
# 15 event + 6 vertical-mouth GIFs, timestamped contact sheets, and a layout stress sheet.

g++ -std=c++11 -O2 -Wall -Wextra -Werror tests/test_avatar.cpp \
  -o /tmp/stick-s3-acting-test
/tmp/stick-s3-acting-test
g++ -std=c++11 -O2 -Wall -Wextra -Werror tests/test_face_contour.cpp \
  -o /tmp/stick-s3-contour-test
/tmp/stick-s3-contour-test
g++ -std=c++11 -O2 -Wall -Wextra -Werror tests/test_face_physics.cpp \
  -o /tmp/stick-s3-physics-test
/tmp/stick-s3-physics-test
g++ -std=c++11 -O2 -Wall -Wextra -Werror tests/test_mouth.cpp \
  -o /tmp/stick-s3-mouth-test
/tmp/stick-s3-mouth-test
.venv/bin/python -m unittest discover -s tests
```

Tests cover sequence ordering and bounds, seven-item bags, request preemption,
entry continuity, positive completion lids, brief request closures, safe
banner/eye masks, real winks, curved happy closures, continuous bounded fluid
fields, Roman conversion, solid glyph interiors, fitting, and body scaling.
Physics tests cover frame-rate consistency, travelling disturbances, damping,
bounded motion, directed count cues, input priority, and an actual rim overlap
with a still-readable outlined numeral.
Generated review images, binaries, and frame dumps must remain outside Git.

For smoothness diagnosis, use the opt-in [physical frame profiler](frame-profiling.md)
rather than inferring ESP32 frame timing from these host GIFs. It separates render
stages, completed display-call time, frame intervals and observer/loss effects,
with source provenance and distinct physical/emulated/synthetic declarations.

## Listening soundwave

Listening used to look like idling: small receptive eye moves on a black face.
Now the eyes turn into a soundwave (`firmware/listen_wave.h`). Entering
listening, the eyes close into the ordinary blink line, the line breaks into
five rounded bars per eye, and the bars ripple. Sound enters at the inner bars
(next to the gap between the eyes) and travels outward, mirrored, so the voice
seems to radiate from the middle of the face. The authored listening clips keep
leaning, nodding and tilting the whole wave; blinks squash it to 40%. Release
sinks the bars back into the line and the eyes open with the release nod; the
eye raster is then exactly the original one.

On the Stick the wave follows the speaker's actual loudness: each 80ms PCM16
dictation block (already captured for local ASR) becomes eight 10ms RMS levels
with an adaptive noise floor and peak normalization, played back at real pace.
Only those 0..1 levels exist on the device; nothing new is sent, stored or
logged, and nothing about speech content is analyzed. Native previews, the
header renderer and the product site have no microphone, so a seeded
synthetic voice (syllables, words and phrase pauses) drives the same wave.

Tests: `tests/test_listen_wave.cpp`. Not yet physically timed.
