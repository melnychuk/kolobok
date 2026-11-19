#include <stdio.h>
#include <stdlib.h>

#include "../firmware/power_nap.h"

using power_nap::Nap;
using power_nap::kPeekMs;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

struct Probe {
  bool powered;
  int* calls;
  bool operator()() const { ++*calls; return powered; }
};

static void pluggedNeverNapsOrProbes() {
  Nap n; int calls = 0;
  for (uint32_t now = 0; now < 60000; now += 2)
    CHECK(!n.update(now, false, false, true, Probe{false, &calls}));
  CHECK(calls == 0);                          // No PMIC traffic while plugged in.
  n.poke(100);
  CHECK(!n.update(200, false, false, true, Probe{false, &calls}));
  CHECK(!n.peek(200));                        // A host clears any peek.
}

static void napsOnlyOnceAsleepAndUnpowered() {
  Nap n; int calls = 0;
  CHECK(!n.update(0, true, false, false, Probe{false, &calls}));
  CHECK(calls == 0);                          // Eyes still closing: no probe.
  CHECK(!n.update(10, true, true, true, Probe{false, &calls}));
  CHECK(calls == 0);                          // Recording/held button: no nap.
  CHECK(!n.update(20, true, false, true, Probe{true, &calls}));
  CHECK(calls == 1 && !n.napping);            // Charger power: stay awake.
  CHECK(n.update(30, true, false, true, Probe{false, &calls}));
  CHECK(n.napping);
  // Keeps napping across timer wakes even though the face is not re-rendered.
  CHECK(n.update(1030, true, false, false, Probe{false, &calls}));
  // VBUS returning ends the nap before the SOF tracker notices.
  CHECK(!n.update(2030, true, false, false, Probe{true, &calls}));
  CHECK(!n.napping);
}

static void buttonPeeksThenNapsAgain() {
  Nap n; int calls = 0;
  CHECK(n.update(0, true, false, true, Probe{false, &calls}));
  n.poke(500);                                // Button woke the light sleep.
  CHECK(!n.napping && n.peek(500));
  // Face is awake during the peek even though it would otherwise be asleep.
  for (uint32_t now = 500; now < 500 + kPeekMs; now += 20) {
    CHECK(n.peek(now));
    CHECK(!n.update(now, true, false, true, Probe{false, &calls}));
  }
  CHECK(!n.peek(500 + kPeekMs));
  // Peek over; the eyes must close again before the panel goes dark.
  CHECK(!n.update(500 + kPeekMs, true, false, false, Probe{false, &calls}));
  CHECK(n.update(500 + kPeekMs + 2100, true, false, true, Probe{false, &calls}));
}

static void unplugStaysAwakeThenNaps() {
  Nap n; int calls = 0;
  n.poke(1000);                               // Firmware pokes on the unplug edge.
  for (uint32_t now = 1000; now < 1000 + kPeekMs; now += 20) {
    CHECK(n.peek(now));                       // Awake offline face, not asleep.
    CHECK(!n.update(now, true, false, true, Probe{false, &calls}));
  }
  CHECK(calls == 0);
  CHECK(!n.peek(1000 + kPeekMs));
  CHECK(!n.update(1000 + kPeekMs, true, false, false, Probe{false, &calls}));
  CHECK(n.update(1000 + kPeekMs + 2100, true, false, true, Probe{false, &calls}));
}

static void heldOrRepeatedButtonsExtendPeek() {
  Nap n; int calls = 0;
  n.poke(0);
  for (uint32_t now = 0; now < 20000; now += 20) {
    n.poke(now);                              // Held: poked every loop.
    CHECK(!n.update(now, true, true, true, Probe{false, &calls}));
  }
  CHECK(n.peek(20000 + kPeekMs - 50));        // Stays up until kPeekMs after release.
  CHECK(!n.peek(20000 + kPeekMs));
  CHECK(calls == 0);
}

static void peekAcrossMillisWrap() {
  Nap n; int calls = 0;
  const uint32_t start = 0xFFFFFFFFu - 1000;
  n.poke(start);
  CHECK(n.peek(start + 2000));                // Wrapped, still within kPeekMs.
  CHECK(!n.update(start + 2000, true, false, true, Probe{false, &calls}));
  CHECK(!n.peek(start + kPeekMs));
  CHECK(n.update(start + kPeekMs, true, false, true, Probe{false, &calls}));
}

int main() {
  pluggedNeverNapsOrProbes();
  napsOnlyOnceAsleepAndUnpowered();
  buttonPeeksThenNapsAgain();
  unplugStaysAwakeThenNaps();
  heldOrRepeatedButtonsExtendPeek();
  peekAcrossMillisWrap();
  if (failures) return EXIT_FAILURE;
  puts("Power nap checks passed");
  return EXIT_SUCCESS;
}
