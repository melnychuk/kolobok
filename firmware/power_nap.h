#pragma once

#include <stdint.h>

// Battery nap for an unplugged Stick. Once the disconnected avatar has visibly
// fallen asleep, the caller turns the panel and backlight off and light-sleeps
// the ESP32-S3, waking on a button or a slow timer that re-checks host power.
// A button press while unplugged "peeks": the panel wakes and shows the offline
// face for a few seconds, then the avatar dozes off and naps again.
//
// Napping is never allowed while the Stick has USB power (host or charger),
// while recording, while a button is held, or while the face is still awake.
// Plugged-in behavior (bridge heartbeats, offline face) is unchanged.
namespace power_nap {

// How long the face stays awake after an unplug or the last button activity
// before the eyes close and the Stick naps again.
constexpr uint32_t kPeekMs = 5000;
// Light-sleep timer: how often a napping Stick re-checks USB power.
constexpr uint32_t kPollMs = 1000;
// Eyes must be this closed (ui.sleep) before the panel goes dark.
constexpr float kAsleep = .95f;
// ST7789 needs 120ms between sleep-in and sleep-out commands.
constexpr uint32_t kMinPanelSleepMs = 120;

struct Nap {
  bool napping = false;
  bool peeking = false;
  uint32_t peekUntil = 0;

  // Button activity (or the unplug itself) on an unplugged Stick: stay awake briefly.
  void poke(uint32_t now) {
    napping = false;
    peeking = true;
    peekUntil = now + kPeekMs;
  }

  // Whether the face should be shown awake despite the disconnect.
  bool peek(uint32_t now) const {
    return peeking && static_cast<int32_t>(peekUntil - now) > 0;
  }

  // Returns whether the Stick should be napping now. `hostPower` is a probe for
  // USB VBUS (true = powered or unknown); it is called lazily, only when a nap
  // would otherwise start or continue, so the PMIC is not polled every frame.
  template <class Probe>
  bool update(uint32_t now, bool unplugged, bool busy, bool faceAsleep, Probe hostPower) {
    if (!unplugged) {  // A host is back: resume normal operation for good.
      napping = peeking = false;
      return false;
    }
    if (peeking && !peek(now)) peeking = false;
    if (busy || peeking) {
      napping = false;
      return false;
    }
    if (!napping && !faceAsleep) return false;
    napping = !hostPower();
    return napping;
  }
};

}  // namespace power_nap
