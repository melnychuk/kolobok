#include <M5Unified.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <math.h>

#include "avatar_frame.h"
#include "usb_presence.h"
#include "power_nap.h"
#include "frame_profile.h"
#include "framebuffer_raster.h"
#include "native_scan.h"
#include "sound_startle.h"

#ifndef STICK_S3_PROFILE
#define STICK_S3_PROFILE 0
#endif
#ifndef STICK_S3_PROFILE_BUILD_ID
#define STICK_S3_PROFILE_BUILD_ID 0
#endif
// Opt-in: keep the microphone open while idle and let sudden loud noises
// startle the avatar. Levels are analyzed on-device only; no audio leaves RAM.
#ifndef STICK_S3_AMBIENT_STARTLE
#define STICK_S3_AMBIENT_STARTLE 0
#endif
// Theme on first boot (0 dark, 1 light). A short click on the side button
// switches it afterwards and the choice is kept across restarts.
#ifndef STICK_S3_LIGHT_MODE
#define STICK_S3_LIGHT_MODE 0
#endif

namespace {

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint16_t>(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
}

using avatar_frame::kScreenWidth;
using avatar_frame::kScreenHeight;
using avatar_frame::kBlack;
using Face = avatar_behavior::Mode;
constexpr uint32_t kFrameMs = 20;

// Everything a frame shows; the rest of this file is the hardware around it.
avatar_frame::Avatar avatar;
Preferences settings;
bool settingsOpen = false;
bool themePressArmed = false;
uint32_t napEndedAt = 0;

bool micPending = false;
bool holdHandled = false;
uint32_t voiceStartedAt = 0;
int16_t micSamples[1280];
int rotation = 1;
bool attentionChimePlaying = false;
uint8_t attentionChimeStep = 0;
uint32_t lastMessageAt = 0;
usb_presence::Tracker usbPresence;
power_nap::Nap nap;
bool napSupported = false;  // StickS3 with M5PM1: wake buttons and VBUS probe known.
uint32_t napStartedAt = 0;
uint32_t nextAttentionChimeAt = 0;
uint32_t manualRotationUntil = 0;
uint32_t lastImuAt = 0;
float flatTurnDegrees = 0;

// Two full frames: render into one while the other is still being sent to the
// panel by SPI DMA (~13ms at 40MHz). Falls back to one synchronous frame if
// the second internal DMA-capable buffer cannot be allocated.
M5Canvas canvases[2] = {M5Canvas(&M5.Display), M5Canvas(&M5.Display)};
M5Canvas* frameCanvas = &canvases[0];
bool doubleBuffered = false;
// Preferred instead: render landscape into canvases[0], then copy it into
// this portrait buffer in the panel's own scan order (native_scan.h) and send
// that by DMA while the next frame renders. The panel stays at rotation 0;
// `rotation` only picks the copy's direction. Same memory as two landscape
// frames, and a mid-refresh frame splits along one straight line instead of
// shearing diagonally through every moving edge.
M5Canvas scanout(&M5.Display);
bool nativeScan = false;
// Native scan sends from a task on core 0 while core 1 renders the next frame.
// The ESP32-S3 SPI carries at most 32KB per transaction, and M5GFX busy-waits
// through the first one of each 64,800-byte frame (6.5ms at 40MHz): on the
// render core that wait was most of the frame's presentation cost.
TaskHandle_t presenter = nullptr;
SemaphoreHandle_t scanoutIdle = nullptr;  // Given when a transfer has finished reading `scanout`.
bool transferOpen = false;
M5Canvas& frame() { return *frameCanvas; }
#if STICK_S3_AMBIENT_STARTLE
// Ambient level listening: two 32ms blocks alternate so capture is continuous.
// The microphone shares the ES8311 codec/I2S clocks with the speaker, so it
// pauses for the attention chime, dictation, and unplugged/battery naps.
constexpr size_t kAmbientBlock = 512;
constexpr uint32_t kAmbientRate = 16000;
int16_t ambientBuffers[2][kAmbientBlock];
uint8_t ambientQueued = 0, ambientHead = 0;
bool ambientRunning = false;
uint32_t ambientResumeAt = 0;
uint32_t lastHandlingAt = 0;  // Button clicks/handling are heard; never startle at them.
sound_startle::Detector hearing;
#endif
uint32_t nextFrameAt = 0;
uint8_t brightness = 0;

#if STICK_S3_PROFILE
frame_profile::Sample timingSample;
uint32_t timingStart=0,timingMark=0,timingPreviousStart=0,timingSequence=0;
uint32_t timingDropped=0,timingVoiceSkipped=0,timingObserver=0;
bool timingHasPrevious=false,timingPsramFallback=false;
void profileBegin(uint32_t now) {
  timingStart=micros();timingMark=timingStart;
  timingSample=frame_profile::Sample{};
  timingSample.us[frame_profile::interval]=timingHasPrevious?
      frame_profile::elapsed(timingStart,timingPreviousStart):0;
  timingPreviousStart=timingStart;timingHasPrevious=true;
  timingSample.sequence=timingSequence++;timingSample.uptime_ms=now;
  timingSample.build_id=STICK_S3_PROFILE_BUILD_ID;
  timingSample.period_us=kFrameMs*1000;
}
void profileMark(frame_profile::Metric metric) {
  const uint32_t now=micros();
  timingSample.us[metric]=frame_profile::elapsed(now,timingMark);timingMark=now;
}
void profileFinish() {
  // Blocking presentation time: waiting for the PREVIOUS frame's DMA plus
  // queueing this one. With double buffering the transfer itself overlaps the
  // next render and is no longer serialized into this stage.
  profileMark(frame_profile::flush);
  timingSample.us[frame_profile::total]=frame_profile::elapsed(timingMark,timingStart);
  const uint32_t observerStart=micros();
  timingSample.state=static_cast<uint8_t>(avatar.face);
  timingSample.flags=(avatar_scene::mouthVisibility(avatar.scene)>0?1:0)|(avatar.scene.corners>=.35f?2:0)|
                     (avatar.scene.sleep>=.5f?4:0)|(timingPsramFallback?8:0);
  timingSample.dropped=timingDropped;timingSample.voice_skipped=timingVoiceSkipped;
  timingSample.previous_observer_us=timingObserver;
  timingSample.cpu_mhz=getCpuFrequencyMhz();
  if(avatar.voiceRecording) { ++timingVoiceSkipped; }
  else if(Serial && Serial.availableForWrite()>=static_cast<int>(frame_profile::packet_size)) {
    uint8_t packet[frame_profile::packet_size];frame_profile::encode(timingSample,packet);
    if(Serial.write(packet,sizeof(packet))!=sizeof(packet))++timingDropped;
  } else { ++timingDropped; }
  // Admission is drop-on-full, but HWCDC also takes locks. Report all observer
  // time on the next sample instead of pretending telemetry is cost-free.
  timingObserver=frame_profile::elapsed(micros(),observerStart);
}
#define STICK_PROFILE_MARK(metric) profileMark(frame_profile::metric)
#else
#define STICK_PROFILE_MARK(metric) do {} while (0)
#endif

constexpr uint8_t kAwakeBrightness = 90;
constexpr uint8_t kSleepBrightness = 20;

// The frame's pixels: the canvas being drawn (avatar_frame.h's Target). Its
// buffer is written directly when its layout is known; the M5GFX API is the
// fallback.
struct CanvasTarget {
  framebuffer_raster::View pixels;
  // The four status rows, typed RGB565 (no byte-order ambiguity) for the API
  // path and in the sprite's swapped storage order for direct copies.
  lgfx::rgb565_t rows[4][kScreenWidth];
  uint16_t stored[4][kScreenWidth];

  // Installed M5GFX 0.2.32 uses packed swap565 storage at rotation zero.
  // Guard every assumption; unsupported layouts retain the public API path.
  void begin() {
    pixels.pixels=nullptr;
    if(frame().getRotation()==0 && frame().getColorDepth()==lgfx::rgb565_2Byte &&
       frame().width()==kScreenWidth && frame().height()==kScreenHeight &&
       frame().bufferLength()>=kScreenWidth*kScreenHeight*2) {
      pixels.pixels=static_cast<uint16_t*>(frame().getBuffer());
      pixels.width=kScreenWidth;pixels.height=kScreenHeight;
      pixels.stride=kScreenWidth;pixels.swapped=true;
    }
  }

  void ink(int x,int y,uint16_t color,float alpha) {
    if(pixels.pixels)pixels.ink(x,y,color,alpha);
    else if(alpha>=1)frame().drawPixel(x,y,color);
    else if(alpha>0)frame().drawPixel(x,y,avatar_frame::blend565(frame().readPixel(x,y),color,alpha));
  }

  void run(int x,int y,int length,uint16_t color) {
    if(pixels.pixels) {
      uint16_t* row=pixels.pixels+y*pixels.stride+x;
      // Black is 0 in either byte order: clear the opaque run in place.
      if(color==kBlack) { memset(row,0,length*sizeof(uint16_t)); return; }
      const uint16_t value=pixels.swapped?framebuffer_raster::swap(color):color;
      for(int i=0;i<length;++i)row[i]=value;
    } else frame().drawFastHLine(x,y,length,color);
  }

  void backdrop(int x,int y,uint16_t color) {
    rows[y][x]=lgfx::rgb565_t(color);
    stored[y][x]=framebuffer_raster::swap(color);
  }

  void fillBackdrop() {
    // Direct row copies into the checked packed sprite; 135 pushImage calls each
    // paid clipping/pixel-copy setup. The API path remains the fallback.
    if(pixels.pixels && pixels.swapped && pixels.stride==kScreenWidth) {
      for(int y=0;y<kScreenHeight;++y)
        memcpy(pixels.pixels+y*kScreenWidth,stored[y&3],sizeof(stored[0]));
      return;
    }
    for(int y=0;y<kScreenHeight;++y) frame().pushImage(0,y,kScreenWidth,1,rows[y&3]);
  }

#if STICK_S3_PROFILE
  void mark(frame_profile::Metric metric) { profileMark(metric); }
#else
  void mark(frame_profile::Metric) {}
#endif
} canvasTarget;

void sendFrame(char kind, const uint8_t* data = nullptr, uint16_t length = 0) {
  const uint8_t header[] = {'S', '3', 'V', '1', static_cast<uint8_t>(kind),
                            static_cast<uint8_t>(length & 255), static_cast<uint8_t>(length >> 8)};
  Serial.write(header, sizeof(header));
  if (length) Serial.write(data, length);
}

#if STICK_S3_AMBIENT_STARTLE
void stopAmbient(bool restoreSpeaker) {
  if (!ambientRunning) return;
  M5.Mic.end();  // Drops pending ambient blocks; nothing is retained.
  ambientRunning = false;
  ambientQueued = 0;
  if (restoreSpeaker) M5.Speaker.begin();
}

bool queueAmbient() {
  const uint8_t slot = (ambientHead + ambientQueued) & 1;
  if (!M5.Mic.record(ambientBuffers[slot], kAmbientBlock, kAmbientRate, false)) return false;
  ++ambientQueued;
  return true;
}

void updateAmbient(uint32_t now) {
  if (M5.BtnA.isPressed() || M5.BtnB.isPressed() || M5.BtnA.wasReleased() || M5.BtnB.wasReleased())
    lastHandlingAt = now;
  if (avatar.voiceRecording || attentionChimePlaying || usbPresence.unplugged) {
    stopAmbient(!avatar.voiceRecording);
    ambientResumeAt = now + 350;  // Let the chime/handling noise die away.
    return;
  }
  if (static_cast<int32_t>(now - ambientResumeAt) < 0) return;
  if (!ambientRunning) {
    M5.Speaker.end();
    if (!M5.Mic.begin()) {
      M5.Speaker.begin();
      ambientResumeAt = now + 5000;
      return;
    }
    ambientRunning = true;
    ambientQueued = 0;
    ambientHead = 0;
    hearing.reset(now);
  }
  // The capture task fills requests in order: the oldest finishes first.
  while (ambientQueued > 0 && M5.Mic.isRecording() < ambientQueued) {
    const bool handled = now - lastHandlingAt < 700;
    const float strength = hearing.feed(ambientBuffers[ambientHead], kAmbientBlock, kAmbientRate, now, handled);
    if (strength > avatar.pendingStartle) avatar.pendingStartle = strength;
    ambientHead ^= 1;
    --ambientQueued;
  }
  while (ambientQueued < 2 && queueAmbient()) {}
  if (ambientQueued == 0) {  // The capture port failed; retry later.
    stopAmbient(true);
    ambientResumeAt = now + 5000;
  }
}
#else
void stopAmbient(bool) {}
void updateAmbient(uint32_t) {}
#endif

void stopVoice(bool aborted = false) {
  M5.Mic.end();
  avatar.voiceRecording = false;
  micPending = false;
  sendFrame(aborted ? 'X' : 'E');
  if (!aborted) avatar.buttonCue.notify(millis(), true);
  M5.Speaker.begin();
#if STICK_S3_AMBIENT_STARTLE
  ambientResumeAt = millis() + 600;  // Release click and handling noise.
#endif
}

void updateVoice() {
  // Unplugged there is no host to receive audio or focus requests; queued frames
  // would be replayed on replug. Buttons then only wake the face (power_nap).
  const bool hostless = usbPresence.unplugged;
  if (M5.BtnA.pressedFor(300) && !holdHandled && !hostless) {
    holdHandled = true;
    stopAmbient(false);
    M5.Speaker.end();
    attentionChimePlaying = false;
    if (M5.Mic.begin()) {
      avatar.voiceRecording = true;
      voiceStartedAt = millis();
      avatar.voiceLevel.reset();
      avatar.buttonCue.active = false;
      sendFrame('B');
    } else {
      sendFrame('X');
      M5.Speaker.begin();
    }
  }
  if (avatar.voiceRecording) {
    if (micPending && !M5.Mic.isRecording()) {
      avatar.voiceLevel.feed(micSamples, sizeof(micSamples) / sizeof(micSamples[0]), millis());
      sendFrame('A', reinterpret_cast<uint8_t*>(micSamples), sizeof(micSamples));
      micPending = false;
    }
    if (!M5.BtnA.isPressed() || millis() - voiceStartedAt > 60000) {
      stopVoice();
    } else if (!micPending) {
      micPending = M5.Mic.record(micSamples, 1280, 16000, false);
      if (!micPending) stopVoice(true);
    }
  }
  if (M5.BtnA.wasReleased()) {
    if (!holdHandled && !hostless) {
      const char message[] = "BUTTON|focus";
      sendFrame('T', reinterpret_cast<const uint8_t*>(message), sizeof(message) - 1);
      avatar.buttonCue.notify(millis());
      avatar.lastTaskAt = millis();
    }
    holdHandled = false;
  }
}

// Close the open transfer: waits for the previous frame's DMA to finish.
void finishTransfer() {
  if (!transferOpen) return;
  if (presenter) xSemaphoreTake(scanoutIdle, portMAX_DELAY);
  else M5.Display.endWrite();
  transferOpen = false;
}

void presenterLoop(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    M5.Display.startWrite();
    scanout.pushSprite(0, 0);
    M5.Display.endWrite();  // Waits for the DMA tail.
    xSemaphoreGive(scanoutIdle);
  }
}

// Queue the finished frame and, when double buffered, return while DMA sends
// it; the next frame renders into the other buffer. A frame buffer is never
// written while DMA reads it: the previous transfer completes before its
// buffer comes back around. Native scan refills `scanout` only after that.
void presentFrame() {
  finishTransfer();
  if (nativeScan) {
    native_scan::transpose(static_cast<const uint16_t*>(frame().getBuffer()), kScreenWidth, kScreenHeight,
                           static_cast<uint16_t*>(scanout.getBuffer()), rotation);
    transferOpen = true;
    if (presenter) {
      xTaskNotifyGive(presenter);
    } else {
      M5.Display.startWrite();
      scanout.pushSprite(0, 0);
    }
    return;
  }
  M5.Display.startWrite();
  transferOpen = true;
  frame().pushSprite(0, 0);
  if (doubleBuffered) frameCanvas = frameCanvas == &canvases[0] ? &canvases[1] : &canvases[0];
  else finishTransfer();
}

void renderFrame(uint32_t now) {
#if STICK_S3_PROFILE
  profileBegin(now);
#endif
  canvasTarget.begin();
  avatar.unplugged = usbPresence.unplugged;
  avatar.drowsy = usbPresence.sleepy(now) && !nap.peek(now);
  const float seconds = avatar.update(now);
  const uint8_t nextBrightness = lroundf(avatar_frame::lerp(kAwakeBrightness, kSleepBrightness, avatar.ui.sleep));
  if (nextBrightness != brightness) {
    brightness = nextBrightness;
    M5.Display.setBrightness(brightness);
  }
  STICK_PROFILE_MARK(update);
  avatar.draw(canvasTarget, now, seconds);
  presentFrame();
#if STICK_S3_PROFILE
  profileFinish();
#endif
}

void startAttentionChime() {
  attentionChimePlaying = true;
  attentionChimeStep = 0;
  nextAttentionChimeAt = millis();
}

void updateAttentionChime() {
  if (!attentionChimePlaying ||
      static_cast<int32_t>(millis() - nextAttentionChimeAt) < 0) {
    return;
  }

  if (attentionChimeStep == 0) {
    M5.Speaker.tone(1047, 70);
    attentionChimeStep = 1;
    nextAttentionChimeAt = millis() + 105;
  } else if (attentionChimeStep == 1) {
    M5.Speaker.tone(1568, 110);
    attentionChimeStep = 2;
    nextAttentionChimeAt = millis() + 130;
  } else {
    attentionChimePlaying = false;
  }
}

// Side button B: a short click switches dark/light mode and remembers it.
// A press that wakes the Stick from its battery nap only wakes it.
void updateTheme(uint32_t now) {
  if (M5.BtnB.wasPressed()) themePressArmed = now - napEndedAt > 400;
  if (!M5.BtnB.wasClicked() || !themePressArmed) return;
  themePressArmed = false;
  avatar.setTheme(!avatar.lightMode);
  if (settingsOpen) settings.putBool("light", avatar.lightMode);
}

void processLine(const String& line) {
  if (!line.startsWith("C|")) return;
  int cursor = 2;
  int separator = line.indexOf('|', cursor);
  if (separator < 0) return;
  const int nextActiveCount = line.substring(cursor, separator).toInt();
  cursor = separator + 1;
  separator = line.indexOf('|', cursor);
  if (separator < 0) return;
  const int nextAttentionCount = line.substring(cursor, separator).toInt();
  cursor = separator + 1;
  separator = line.indexOf('|', cursor);
  const int nextDoneCount = line.substring(cursor, separator < 0 ? line.length() : separator).toInt();
  // Missing/unknown presence means hidden (compatible with legacy hosts).
  avatar.quakeVisible = separator >= 0 && line.substring(separator + 1) == "1";
  if (avatar.report(nextActiveCount, nextAttentionCount, nextDoneCount, millis())) startAttentionChime();
  lastMessageAt = millis();
  usbPresence.hostSeen();
}


uint32_t lastImuPollAt = 0;
constexpr uint32_t kImuPollMs = 40;  // Orientation needs ~25Hz, not every loop pass.

void updateOrientation() {
  const uint32_t now = millis();
  if (now - lastImuPollAt < kImuPollMs) return;
  lastImuPollAt = now;
  if (now < manualRotationUntil || !M5.Imu.update()) {
    return;
  }

  const auto data = M5.Imu.getImuData();
#if STICK_S3_AMBIENT_STARTLE
  // Picking the Stick up or tapping it is loud at the microphone.
  if (fabsf(data.gyro.x) + fabsf(data.gyro.y) + fabsf(data.gyro.z) > 45.0f) lastHandlingAt = now;
#endif
  const float elapsedSeconds = lastImuAt ? (now - lastImuAt) / 1000.0f : 0;
  lastImuAt = now;
  int nextRotation = rotation;
  const float horizontal = abs(data.accel.x) > abs(data.accel.y)
                               ? data.accel.x
                               : data.accel.y;
  if (horizontal > 0.55f) nextRotation = 1;
  if (horizontal < -0.55f) nextRotation = 3;

  if (abs(data.accel.z) > 0.72f && abs(data.gyro.z) > 18.0f) {
    flatTurnDegrees += data.gyro.z * elapsedSeconds;
    if (abs(flatTurnDegrees) > 105.0f) {
      nextRotation = rotation == 1 ? 3 : 1;
      flatTurnDegrees = 0;
      manualRotationUntil = now + 1200;
    }
  } else if (abs(data.gyro.z) < 8.0f) {
    flatTurnDegrees *= 0.92f;
  }
  if (nextRotation != rotation) {
    rotation = nextRotation;
    // Native scan keeps the panel at rotation 0; the next copy turns instead.
    if (!nativeScan) {
      finishTransfer();  // Never change MADCTL while a frame is in flight.
      M5.Display.setRotation(rotation);
    }
  }
}

}  // namespace

bool usbHostPresent() {
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  return HWCDC::isPlugged();  // USB host start-of-frame activity.
#else
  return true;  // No SOF signal: rely on the heartbeat timeout (offline face).
#endif
}

// USB VBUS (host or charger) from the StickS3 PMIC. Unknown counts as powered,
// so a failed read never puts a possibly connected Stick to sleep.
bool hostPowerPresent() {
  uint8_t source = 0;
  if (!M5.Power.M5pm1.readRegister(0x04, &source, 1)) return true;  // PWR_SRC
  return source & (m5::M5PM1_Class::vin | m5::M5PM1_Class::vinout);
}

constexpr gpio_num_t kWakeButtons[] = {GPIO_NUM_11, GPIO_NUM_12};  // Front A, side B.

void enterNap(uint32_t now) {
  napStartedAt = now;
  attentionChimePlaying = false;
  stopAmbient(false);
  M5.Speaker.end();
  finishTransfer();  // Release the panel bus; waits for any in-flight frame.
  M5.Display.waitDMA();
  M5.Display.sleep();  // Backlight off, ST7789 sleep-in; panel RAM is kept.
}

void leaveNap() {
  const uint32_t slept = millis() - napStartedAt;
  if (slept < power_nap::kMinPanelSleepMs) delay(power_nap::kMinPanelSleepMs - slept);
  // Shows the retained sleeping frame first, so waking animates from it.
  M5.Display.wakeup();
  M5.Speaker.begin();
  lastImuAt = 0;  // Do not integrate gyro rate across the nap.
  napEndedAt = millis();
  themePressArmed = false;
}

// Light-sleeps until a button or the poll timer; true when a button woke it.
// IDF detaches the USB-Serial/JTAG pads during light sleep and restores them.
bool lightNap() {
  for (const gpio_num_t pin : kWakeButtons) gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(power_nap::kPollMs) * 1000);
  esp_light_sleep_start();
  const bool button = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  for (const gpio_num_t pin : kWakeButtons) gpio_wakeup_disable(pin);
  return button;
}

// Returns true while napping, after sleeping for this loop iteration.
bool updateNap(uint32_t now) {
  if (!napSupported) return false;
  const bool unplugged = usbPresence.unplugged;
  const bool held = M5.BtnA.isPressed() || M5.BtnB.isPressed();
  if (unplugged && (held || M5.BtnA.wasReleased() || M5.BtnB.wasReleased())) nap.poke(now);
  const bool wasNapping = nap.napping;
  const bool asleep = avatar.face == Face::sleeping && avatar.ui.sleep >= power_nap::kAsleep;
  if (!nap.update(now, unplugged, held || avatar.voiceRecording, asleep, hostPowerPresent)) {
    if (wasNapping) leaveNap();
    return false;
  }
  if (!wasNapping) enterNap(now);
  if (lightNap()) {
    nap.poke(millis());
    leaveNap();
  }
  return true;
}

void setup() {
  auto config = M5.config();
  config.serial_baudrate = 115200;
  config.fallback_board = m5::board_t::board_M5StickS3;
  config.internal_spk = true;
  config.internal_mic = true;
  M5.begin(config);
#if STICK_S3_AMBIENT_STARTLE
  {  // Keep the capture task off the render core (loopTask, priority 1, core 1).
    auto micConfig = M5.Mic.config();
    micConfig.task_pinned_core = 0;
    M5.Mic.config(micConfig);
  }
#endif
  napSupported = M5.getBoard() == m5::board_t::board_M5StickS3 &&
                 M5.Power.getType() == m5::Power_Class::pmic_m5pm1;

  M5.Speaker.setVolume(128);
  M5.Display.setRotation(rotation);
  M5.Display.setBrightness(kAwakeBrightness);
  settingsOpen = settings.begin("stick-s3", false);
  avatar.setTheme(settingsOpen ? settings.getBool("light", STICK_S3_LIGHT_MODE != 0) : STICK_S3_LIGHT_MODE != 0);
  M5.Display.fillScreen(avatar.palette->body);
  avatar.eyeLife.seed = esp_random() | 1u;  // xorshift state must be nonzero.
  avatar.eyeLife.microSeed = esp_random() | 1u;
  avatar.counterChecks.seed = esp_random() | 1u;
  avatar.random = [](long limit) -> long { return random(limit); };

  for (M5Canvas& canvas : canvases) {
    canvas.setColorDepth(16);
    canvas.setTextWrap(false);
    canvas.setPsram(false);
  }
  bool firstInPsram = false;
  if (!canvases[0].createSprite(kScreenWidth, kScreenHeight)) {
#if STICK_S3_PROFILE
    timingPsramFallback=true;
#endif
    firstInPsram = true;
    canvases[0].setPsram(true);
    canvases[0].createSprite(kScreenWidth, kScreenHeight);
  }
  // Overlap needs both frames in internal DMA-capable RAM (setPsram(false)
  // allocates only there); otherwise keep the synchronous single buffer.
  scanout.setColorDepth(16);
  scanout.setPsram(false);
  nativeScan = !firstInPsram && canvases[0].getBuffer() != nullptr &&
               canvases[0].getRotation() == 0 &&
               canvases[0].bufferLength() >= kScreenWidth * kScreenHeight * 2 &&
               scanout.createSprite(kScreenHeight, kScreenWidth) != nullptr &&
               scanout.bufferLength() >= kScreenWidth * kScreenHeight * 2;
  if (nativeScan) {
    M5.Display.setRotation(0);
    // Same priority as the render loop (core 1); audio tasks (priority 2)
    // still preempt it. Without the task, frames are sent from the loop.
    scanoutIdle = xSemaphoreCreateBinary();
    if (scanoutIdle &&
        xTaskCreatePinnedToCore(presenterLoop, "scanout", 4096, nullptr, 1, &presenter, 0) != pdPASS) {
      presenter = nullptr;
      vSemaphoreDelete(scanoutIdle);
      scanoutIdle = nullptr;
    }
  } else {
    scanout.deleteSprite();
    doubleBuffered = !firstInPsram && canvases[1].createSprite(kScreenWidth, kScreenHeight) != nullptr;
    if (!doubleBuffered) canvases[1].deleteSprite();
  }
  nextFrameAt = millis();
  renderFrame(millis());
}

void loop() {
  M5.update();

  static String input;
  while (Serial.available()) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\n') {
      processLine(input);
      input = "";
    } else if (value != '\r' && input.length() < 160) {
      input += value;
    }
  }

  // A sustained USB unplug drops the bridge at once instead of after the
  // heartbeat timeout, so the avatar falls asleep about a second after unplug.
  const bool unpluggedNow = usbPresence.update(usbHostPresent(), millis());
  // Like a button wake, stay awake (offline face) briefly after an unplug.
  if (unpluggedNow && napSupported) nap.poke(millis());
  if (avatar.releasePairing(millis())) startAttentionChime();
  if (avatar.bridgeConnected && (unpluggedNow || (lastMessageAt && millis() - lastMessageAt > 8000)))
    avatar.disconnect();

  // On battery, a settled sleeping face turns the panel off and light-sleeps.
  if (updateNap(millis())) return;

  updateVoice();
  updateTheme(millis());
  updateOrientation();
  updateAmbient(millis());  // Before the chime: it hands the codec back to the speaker.
  if (!avatar.voiceRecording) updateAttentionChime();

  // Fixed-cadence deadlines instead of "33ms since the last start": when a
  // frame runs late the next one is not pushed later as well, and a long
  // stall resynchronises instead of bursting catch-up frames.
  const uint32_t now = millis();
  const int32_t late = static_cast<int32_t>(now - nextFrameAt);
  if (late >= 0) {
    nextFrameAt = late >= static_cast<int32_t>(kFrameMs) ? now + kFrameMs : nextFrameAt + kFrameMs;
    renderFrame(now);
  } else if (-late > 2) {
    delay(1);
  }
}
