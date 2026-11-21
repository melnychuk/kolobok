// Runs the real firmware loop (firmware/firmware.ino) on the host and prints a
// hash of every frame it presents, over a scripted session that touches each
// major behaviour: offline, connect, arrival, transfer, paired counts, a
// request, button tap, dictation, theme switch, startle, step-over, dozing and
// disconnect. A refactor of the frame code must keep every hash.
//
//   g++ -std=c++17 -O2 -ffp-contract=off -Itests/device -Ifirmware \
//       tests/device/device_frames.cpp -o /tmp/device_frames
//   /tmp/device_frames            # "<ms> <hash>" per frame, then "total <hash>"
//   /tmp/device_frames --ppm MS   # the frame presented at or after MS, as PPM
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "M5Unified.h"
#include "../../firmware/firmware.ino"

namespace {
uint64_t total = 1469598103934665603ull;
uint32_t dumpAt = 0;
bool dumping = false, dumped = false;
uint32_t frames = 0;

uint64_t fnv(uint64_t hash, const void* data, size_t length) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
  return hash;
}

void present(const uint16_t* pixels, int width, int height) {
  ++frames;
  if (dumping) {
    if (dumped || millis() < dumpAt) return;
    dumped = true;
    printf("P6\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; ++i) {
      const uint16_t c = static_cast<uint16_t>((pixels[i] << 8) | (pixels[i] >> 8));  // Stored swapped.
      const uint8_t rgb[3] = {static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31),
                              static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63),
                              static_cast<uint8_t>((c & 31) * 255 / 31)};
      fwrite(rgb, 1, 3, stdout);
    }
    return;
  }
  uint64_t hash = fnv(1469598103934665603ull, pixels, static_cast<size_t>(width * height) * 2);
  hash = fnv(hash, &device_shim::brightness, 1);
  total = fnv(total, &hash, sizeof(hash));
  printf("%u %016llx\n", millis(), static_cast<unsigned long long>(hash));
}

void send(const char* line) {
  for (const char* c = line; *c; ++c) device_shim::serial_in.push_back(*c);
  device_shim::serial_in.push_back('\n');
}

struct Counts { int active, attention, done; };
}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && strcmp(argv[1], "--ppm") == 0) {
    dumping = true;
    dumpAt = static_cast<uint32_t>(strtoul(argv[2], nullptr, 10));
  }
  device_shim::on_present = present;
  setup();
  // The host reports counts on change and as a heartbeat every second.
  const struct { uint32_t at; Counts counts; } reports[] = {
      {500, {0, 0, 0}},   {2000, {1, 0, 0}},  {5000, {3, 0, 0}},  {8000, {2, 0, 1}},
      {12000, {2, 0, 3}}, {16000, {3, 1, 3}}, {20000, {3, 0, 3}}, {30000, {0, 0, 1}},
      {33000, {0, 0, 0}},
  };
  constexpr uint32_t kHeartbeatsEnd = 41000, kEnd = 52000;
  Counts current{0, 0, 0};
  bool connected = false;
  uint32_t lastReport = 0;
  for (uint32_t ms = 1; ms <= kEnd; ++ms) {
    device_shim::clock_ms = ms;
    for (const auto& report : reports) {
      if (report.at != ms) continue;
      current = report.counts;
      connected = true;
    }
    if (connected && ms <= kHeartbeatsEnd && (ms - lastReport >= 1000 || [&] {
          for (const auto& report : reports) if (report.at == ms) return true;
          return false;
        }())) {
      char line[48];
      snprintf(line, sizeof(line), "C|%d|%d|%d|0", current.active, current.attention, current.done);
      send(line);
      lastReport = ms;
    }
    auto& a = device_shim::button(0);
    auto& b = device_shim::button(1);
    // A short tap on A (focus), then a 1.5s hold (dictation).
    if (ms == 22000 || ms == 24000) { a.pressed = a.was_pressed = true; a.pressed_since = ms; }
    if (ms == 22100 || ms == 25500) { a.pressed = false; a.was_released = true; }
    // A click on B switches to the light theme.
    if (ms == 27000) { b.pressed = b.was_pressed = true; b.pressed_since = ms; }
    if (ms == 27080) { b.pressed = false; b.was_released = b.was_clicked = true; }
    // A loud noise from the left.
    if (ms == 29000) avatar.pendingStartle = .8f;
    loop();
    a.was_pressed = a.was_released = a.was_clicked = false;
    b.was_pressed = b.was_released = b.was_clicked = false;
  }
  if (!dumping) printf("total %016llx frames %u\n", static_cast<unsigned long long>(total), frames);
  return dumping && !dumped ? 1 : 0;
}
