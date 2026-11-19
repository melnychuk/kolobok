#pragma once

#include <stdint.h>

// Distinguishes "the USB host went away" from "the bridge stopped talking".
// A sustained loss of USB start-of-frame packets means the Stick was unplugged
// (or is on a charger without a host): the avatar falls asleep. A bridge that
// stops heartbeats on a still-plugged Stick (Herdr errors) keeps the offline
// face, so sleep never masquerades as a healthy "no tasks" idle.
namespace usb_presence {

// HWCDC::isPlugged() is a SOF watchdog that can flap false for a few ms on a
// healthy link; require a sustained loss before treating it as an unplug.
constexpr uint32_t kUnplugMs = 1200;
// After boot or a replug, stay asleep while the bridge reconnects instead of
// flashing the offline face; fall back to offline if no host message arrives.
constexpr uint32_t kHostGraceMs = 10000;

struct Tracker {
  bool rawPlugged = false;
  bool unplugged = true;  // No host has been seen since boot.
  bool grace = false;
  uint32_t rawSince = 0;
  uint32_t pluggedAt = 0;

  // Returns true exactly once per sustained unplug, so the caller can drop the
  // bridge immediately rather than waiting for the heartbeat timeout.
  bool update(bool plugged, uint32_t now) {
    if (plugged != rawPlugged) {
      rawPlugged = plugged;
      rawSince = now;
    }
    if (grace && now - pluggedAt >= kHostGraceMs) grace = false;
    if (rawPlugged) {
      if (unplugged) {
        unplugged = false;
        grace = true;
        pluggedAt = now;
      }
      return false;
    }
    if (!unplugged && now - rawSince >= kUnplugMs) {
      unplugged = true;
      grace = false;
      return true;
    }
    return false;
  }

  // A host message proves the bridge is back; end the reconnect grace.
  void hostSeen() { grace = false; }

  // Whether a disconnected avatar should sleep rather than show offline.
  bool sleepy(uint32_t now) const {
    return unplugged || (grace && now - pluggedAt < kHostGraceMs);
  }
};

}  // namespace usb_presence
