#include <stdio.h>
#include <stdlib.h>

#include "../firmware/usb_presence.h"

using usb_presence::Tracker;
using usb_presence::kHostGraceMs;
using usb_presence::kUnplugMs;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

// Feed a constant plug state at a fixed loop period; count unplug edges.
static int run(Tracker& t, bool plugged, uint32_t& now, uint32_t ms, uint32_t step = 2) {
  int edges = 0;
  for (uint32_t elapsed = 0; elapsed < ms; elapsed += step, now += step)
    edges += t.update(plugged, now);
  return edges;
}

static void bootPluggedSleepsUntilHostOrGrace() {
  Tracker t; uint32_t now = 0;
  CHECK(t.sleepy(now));                       // Before the first sample: no host yet.
  CHECK(run(t, true, now, 4000) == 0);
  CHECK(t.sleepy(now));                       // Bridge still reconnecting: asleep.
  t.hostSeen();
  CHECK(!t.sleepy(now));                      // Later heartbeat loss -> offline face.

  Tracker slow; now = 0;
  run(slow, true, now, kHostGraceMs - 10);
  CHECK(slow.sleepy(now));
  run(slow, true, now, 20);
  CHECK(!slow.sleepy(now));                   // Plugged, no bridge: honest offline.
}

static void bootOnBatteryStaysAsleep() {
  Tracker t; uint32_t now = 0;
  CHECK(run(t, false, now, 60000) == 0);      // Never had a host: no edge to report.
  CHECK(t.sleepy(now));
}

static void sustainedUnplugReportsOnce() {
  Tracker t; uint32_t now = 1000;
  run(t, true, now, 500); t.hostSeen();
  CHECK(!t.sleepy(now));
  CHECK(run(t, false, now, kUnplugMs - 20) == 0);
  CHECK(!t.sleepy(now));                      // Debounce not yet elapsed.
  CHECK(run(t, false, now, 40) == 1);         // Edge after ~1.2s.
  CHECK(t.sleepy(now));
  CHECK(run(t, false, now, 30000) == 0);      // Exactly once.
  CHECK(t.sleepy(now));
}

static void flappingLinkNeverSleeps() {
  Tracker t; uint32_t now = 0;
  run(t, true, now, 200); t.hostSeen();
  int edges = 0;
  // SOF watchdog glitches: up to 1s false every ~3s, healthy otherwise.
  for (int i = 0; i < 200; ++i) {
    edges += run(t, false, now, 5 + (i * 37) % (kUnplugMs - 200));
    edges += run(t, true, now, 3000);
  }
  CHECK(edges == 0);
  CHECK(!t.sleepy(now));
}

static void replugGraceThenHost() {
  Tracker t; uint32_t now = 0;
  run(t, true, now, 100); t.hostSeen();
  CHECK(run(t, false, now, 5000) == 1);
  CHECK(t.sleepy(now));
  run(t, true, now, 1500);                    // Replugged; bridge not yet back.
  CHECK(t.sleepy(now));                       // Stays asleep instead of offline.
  t.hostSeen();
  CHECK(!t.sleepy(now));
  // A replug without any bridge falls back to offline after the grace.
  CHECK(run(t, false, now, 2000) == 1);
  run(t, true, now, kHostGraceMs + 10);
  CHECK(!t.sleepy(now));
}

static void millisWrap() {
  Tracker t; uint32_t now = 0xFFFFFFFFu - 3000;
  run(t, true, now, 100); t.hostSeen();
  CHECK(run(t, false, now, 2000, 1) == 1);    // Unplug detected across the wrap.
  CHECK(t.sleepy(now));
  now = 0xFFFFFFFFu - 2000;
  Tracker g; run(g, true, now, 1000, 1);      // Replug grace spans the wrap.
  CHECK(g.sleepy(now));
  run(g, true, now, kHostGraceMs, 1);
  CHECK(!g.sleepy(now));
  // Grace must not reappear after a full 2^32 ms cycle.
  uint32_t later = g.pluggedAt + 5;
  CHECK(!g.sleepy(later));
}

int main() {
  bootPluggedSleepsUntilHostOrGrace();
  bootOnBatteryStaysAsleep();
  sustainedUnplugReportsOnce();
  flappingLinkNeverSleeps();
  replugGraceThenHost();
  millisWrap();
  if (failures) return EXIT_FAILURE;
  puts("USB presence sleep/offline checks passed");
  return EXIT_SUCCESS;
}
