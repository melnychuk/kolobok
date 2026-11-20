#pragma once

// The one screen-side convention for the two task counters, shared by
// firmware, native previews/tests and the desktop header widget.
//
// Active work (working + blocked, blue/coral) sits on the LEFT and unread
// completions (green) on the RIGHT. With a single counter the face fills the
// opposite side. Everything that points at a counter - backgrounds, label
// positions, glances, reaches, dodges, exits and the step-over - derives its
// direction from here; never hard-code a side for a status.
//
// Per-status arrays keep their slot order (0 done, 1 active) whatever the
// layout. STICK_S3_DONE_SIDE=-1 restores the original done-left layout; it
// exists so regression builds can prove the convention is applied
// symmetrically. Production builds use the default.
#ifndef STICK_S3_DONE_SIDE
#define STICK_S3_DONE_SIDE 1
#endif

namespace status_sides {
constexpr int kDoneSlot=0,kActiveSlot=1;
// Screen direction from the center: -1 left, +1 right.
constexpr int kDone=STICK_S3_DONE_SIDE<0?-1:1;
constexpr int kActive=-kDone;
constexpr int direction(bool done) { return done?kDone:kActive; }
constexpr int slotDirection(int slot) { return slot==kDoneSlot?kDone:kActive; }
// A few performances were authored with completions on the left and active
// work on the right. They are mirrored when the layout is the other way round.
constexpr bool kMirrorAuthored=kDone>0;
// Center x of something `inset` pixels in from the edge on side `sign`.
inline float fromEdge(float sign,float inset,float width=240) {
 return sign<0?inset:width-inset;
}
} // namespace status_sides
