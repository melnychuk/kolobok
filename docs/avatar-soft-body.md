# Soft-body outline: rim contact, check swells and sorting blobs

## Feedback

Three things made the outline look unlike one soft body:

1. **Rugged lower rim.** After a hop, startle or transfer the rim landed as a
   flat middle with two pointed horns, and short ripples lingered.
2. **Angular sides during checks.** A counter check pushed a capsule "hand"
   (radius 30, only 4–18px out) through the side and wrapped it in a convex
   envelope (the [shoulder-junction fix](avatar-shoulder-join.md)). A convex
   envelope can only bridge with straight tangent lines, and on the gently
   curved side those lines were long: the side became a `<` with its point at
   the numeral.
3. **Sorting blobs looked like sticks.** Each blob was a numeral-wide semicircle
   lowered under existing rim columns, so it had parallel vertical walls and a
   7px fillet. After set-down the blobs stretched to about 30px on a 30px
   width: pegs.

## Causes

- The rim's contact pinned nodes 13–18 to the support each substep (restitution
  −0.18) while nodes 12 and 19 swung free, so the outline folded at the
  supported/free boundary. The rim had no damping of short wavelengths, so a
  node-scale zigzag took about 1.5s to fade (the side membranes already had
  curvature viscosity for exactly this).
- Body motion excited the rim with a V-shaped profile (`|u-.5|`), a fold at the
  middle.
- The check and the blobs were geometry added on top of the physics, so neither
  had any give, and both were built from shapes with corners or walls.

## Changes

**Lower rim (`face_physics.h`).** 64 nodes instead of 32, with spring and
curvature-viscosity coefficients scaled by the squared node density so the
continuous rim keeps its wave speed. Short ripples are damped by viscosity on
curvature velocity, as on the sides. Contact is a soft, near critically damped
penalty floor weighted by a smooth patch across the middle of the body, instead
of pinned nodes. Body motion excites the rim with a rounded profile. Kick and
check-excitation positions are now given along the rim, independent of node
count.

**Membranes carry authored shapes (`Surface::follow`).** Each frame the scene's
authored deformations (check swells, sorting sags) are sampled at the membrane
nodes. When the shape changes, the rigid outline takes it at once and the skin
keeps `kShapeDrag` (0.6) of its old position, then its springs pull it along:
the same rule the sides already used for body shape changes. A reach has weight,
lags a little, and wobbles briefly when it lets go.

**Check swell (`avatar_scene.h`, `pokeSwells`).** The capsule, shoulder fillet
and convex envelope are gone. A check now swells the body toward its numeral as
a smooth bell (`(1-t²)³`). In vertical layouts it swells the facing side; in
the paired layout it swells the lower rim. Reflow blends the two. The reach is
measured from the rigid outline (no feedback from the membrane), with the old
4–18px (side) and 4–14px (rim) limits. The bell is wide, 40+5×reach rows on the
sides and 30+3×reach columns on the rim. A narrow bell added at the oval's
widest point concentrates the bend at the numeral and leaves straight flanks,
which looks angular again. It is part of the left/right/bottom edges, so the
raster, contact and eye clipping use it directly. Zero strength is exactly no
swell.

**Sorting blobs (`lobeMask`, `lobeSag`).** Each blob is a pseudopod. The rim
around it sags (a bell of 25% of the drop, as wide as 2×radius+drop, carried by
the rim membrane so it springs back on release). The blob keeps the numeral-wide
round tip that grips the numeral. 64° from straight down, the tip circle hands
over to a concave skirt that leaves along the circle's tangent and meets the rim
tangentially (cubic Hermite, 2.2/tan 64° wide per pixel of height). A long blob
widens into the body instead of hanging on walls. A small fillet softens the
crease while only the tip cap shows and fades out as the skirt appears; the
shape grows continuously. Everything is measured from the local rim and only
lowers existing columns, so a blob never floats or detaches.

## Results

Native fixtures (`tests/preview_avatar.cpp`, before → after):

- Rim bumpiness (largest second difference over 4px, over each fixture's
  timeline): new active count 0.97→0.58, completion 1.88→0.70, counter checks
  0.64→0.38, sorting 0.55→0.34, startle 1.63→0.83. The remaining values are
  broad, smooth sloshes; the horns are gone.
- Vertical checks: the side's largest 12-row change of direction was 11.8–12.4°
  with the capsule. With the swell it stays at the oval's own value. Over
  9,728 check frames the worst is 10.68° both with and without the swell.
- All 160 check performances still reach their numeral; the smallest numeral
  travel rose from 1.03 to 1.53px.
- Host timings (`STICK_S3_PROFILE`): update +0.4µs and mask +0.2µs per frame.
  The ESP32 cost is not measured.

Tests:

- `tests/test_face_physics.cpp`: a landing keeps the rim's node-scale bend
  small, zigzags die within 0.2s while broad sloshes survive, and `follow` lags
  by `kShapeDrag` then settles.
- `tests/test_check_swell.cpp` (replaces `test_shoulder_join.cpp`): every check
  performance on both sides, with and without the kiss, two strip widths and
  three cadences, played through the membranes as in the frame. It checks that
  only the facing side moves, outward, with no more bend than the oval, that the
  follow-through settles before the next check, and that reflow is continuous.
- `tests/test_avatar_poke.cpp`: outward-only, smooth (small second differences)
  swells on every layout, exact zero strength, raster/coverage identity, and
  real contact in all 160 performances, now with membrane follow.
- `tests/test_counter_flight.cpp`: blobs grip the numeral, never float, have no
  walls (neighbouring columns step no more than the flank slope) and are
  pseudopods (wider than 1.4× the tip width where they leave the body), and they
  grow continuously.
- `tests/test_device_frames.py`: the recorded frame hash is updated after
  looking at the session's frames.

Not checked: the Stick's frame timing and how the motion feels on the device.
