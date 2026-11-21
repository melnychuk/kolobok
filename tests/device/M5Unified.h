// Host stand-ins for the parts of Arduino, M5Unified and ESP-IDF that
// firmware/firmware.ino uses, so device_frames.cpp (beside this) can run the real
// firmware loop and hash its frames. Inputs (clock, serial, buttons) are
// scripted by the harness through the `device_shim` namespace. Nothing here
// models timing, DMA or the panel; a frame is the sprite as pushSprite sees it.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <cmath>
#include <string>
#include <deque>
#include <algorithm>
#include <type_traits>
#include <vector>

using std::abs;

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

namespace device_shim {
inline uint32_t clock_ms = 0;
inline uint32_t random_state = 0x2545F491u;
inline std::deque<char> serial_in;
// Called with the sprite's pixels each time the firmware presents a frame.
inline void (*on_present)(const uint16_t* pixels, int width, int height) = nullptr;
inline uint8_t brightness = 0;
// Real time for micros() when set (stage timing in STICK_S3_PROFILE builds).
inline uint32_t (*real_micros)() = nullptr;
struct Button {
  bool pressed = false, was_pressed = false, was_released = false, was_clicked = false;
  uint32_t pressed_since = 0;
};
inline Button button_a, button_b;
inline void set_brightness(uint8_t value) { brightness = value; }
// 0: the front button A, 1: the side button B.
inline Button& button(int which) { return which ? button_b : button_a; }
}  // namespace device_shim

using device_shim::set_brightness;

inline uint32_t millis() { return device_shim::clock_ms; }
inline uint32_t micros() {
  return device_shim::real_micros ? device_shim::real_micros() : device_shim::clock_ms * 1000u;
}
inline void delay(uint32_t ms) { device_shim::clock_ms += ms; }
// Deterministic stand-in for Arduino's random(max).
inline long random(long limit) {
  uint32_t& s = device_shim::random_state;
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return limit > 0 ? static_cast<long>(s % static_cast<uint32_t>(limit)) : 0;
}
inline uint32_t esp_random() { return static_cast<uint32_t>(random(0x7fffffff)) * 2654435761u; }
inline uint32_t getCpuFrequencyMhz() { return 240; }
// As arduino-esp32 defines them: max/min are std::max/std::min (the firmware
// passes matching types), constrain is the classic macro.
using std::max;
using std::min;
template <class T, class L, class H>
inline typename std::common_type<T, L, H>::type constrain(T value, L low, H high) {
  return value < low ? low : (value > high ? high : value);
}

class String {
 public:
  String() = default;
  String(const char* text) : value_(text) {}
  String(std::string text) : value_(std::move(text)) {}
  bool startsWith(const char* prefix) const { return value_.rfind(prefix, 0) == 0; }
  int indexOf(char c, int from = 0) const {
    const size_t at = value_.find(c, static_cast<size_t>(from));
    return at == std::string::npos ? -1 : static_cast<int>(at);
  }
  String substring(int from) const { return String(value_.substr(static_cast<size_t>(from))); }
  String substring(int from, int to) const {
    return String(value_.substr(static_cast<size_t>(from), static_cast<size_t>(to - from)));
  }
  long toInt() const { return strtol(value_.c_str(), nullptr, 10); }
  int length() const { return static_cast<int>(value_.size()); }
  bool operator==(const char* other) const { return value_ == other; }
  String& operator+=(char c) { value_ += c; return *this; }
  String& operator=(const char* text) { value_ = text; return *this; }

 private:
  std::string value_;
};

struct SerialPort {
  explicit operator bool() const { return true; }
  int available() const { return static_cast<int>(device_shim::serial_in.size()); }
  int read() {
    if (device_shim::serial_in.empty()) return -1;
    const char c = device_shim::serial_in.front();
    device_shim::serial_in.pop_front();
    return static_cast<unsigned char>(c);
  }
  size_t write(const uint8_t*, size_t length) { return length; }
  int availableForWrite() const { return 0; }
};
inline SerialPort Serial;

// ESP-IDF sleep and GPIO: never reached (no PMIC, so no nap).
enum gpio_num_t { GPIO_NUM_11 = 11, GPIO_NUM_12 = 12 };
enum gpio_int_type_t { GPIO_INTR_LOW_LEVEL = 4 };
enum esp_sleep_source_t { ESP_SLEEP_WAKEUP_ALL = 0, ESP_SLEEP_WAKEUP_GPIO = 7 };
inline void gpio_wakeup_enable(gpio_num_t, gpio_int_type_t) {}
inline void gpio_wakeup_disable(gpio_num_t) {}
inline void esp_sleep_enable_gpio_wakeup() {}
inline void esp_sleep_enable_timer_wakeup(uint64_t) {}
inline void esp_light_sleep_start() {}
inline esp_sleep_source_t esp_sleep_get_wakeup_cause() { return ESP_SLEEP_WAKEUP_ALL; }
inline void esp_sleep_disable_wakeup_source(esp_sleep_source_t) {}

namespace lgfx {
struct rgb565_t {
  uint16_t raw = 0;
  rgb565_t() = default;
  rgb565_t(uint16_t value) : raw(value) {}
  operator uint16_t() const { return raw; }
};
enum color_depth_t { rgb565_2Byte = 16 };
}  // namespace lgfx

namespace m5 {
enum class board_t { board_unknown, board_M5StickS3 };
struct M5PM1_Class {
  static constexpr uint8_t vin = 1, vinout = 2;
  bool readRegister(uint8_t, uint8_t*, size_t) { return false; }
};
struct Power_Class {
  enum pmic_t { pmic_unknown, pmic_m5pm1 };
  M5PM1_Class M5pm1;
  pmic_t getType() const { return pmic_unknown; }
};
}  // namespace m5

struct ImuData {
  struct { float x = 0, y = 0, z = 0; } accel, gyro;
};

struct Display_Class {
  void setRotation(int) {}
  void setBrightness(uint8_t value) { set_brightness(value); }
  void fillScreen(uint16_t) {}
  void startWrite() {}
  void endWrite() {}
  void waitDMA() {}
  void sleep() {}
  void wakeup() {}
};

using ShimButton = device_shim::Button;
inline const ShimButton& button_state(int which) { return device_shim::button(which); }

struct Button_Class {
  int which = 0;
  bool isPressed() const { return button_state(which).pressed; }
  bool pressedFor(uint32_t ms) const {
    return button_state(which).pressed && millis() - button_state(which).pressed_since >= ms;
  }
  bool wasPressed() const { return button_state(which).was_pressed; }
  bool wasReleased() const { return button_state(which).was_released; }
  bool wasClicked() const { return button_state(which).was_clicked; }
};
inline Button_Class make_button(int which) {
  Button_Class button;
  button.which = which;
  return button;
}

struct Speaker_Class {
  void setVolume(uint8_t) {}
  void tone(float, uint32_t) {}
  bool begin() { return true; }
  void end() {}
};

struct MicConfig { int task_pinned_core = 1; };
struct Mic_Class {
  bool begin() { return true; }
  void end() {}
  // Every block arrives at once, silent: dictation shows its face, no voice.
  bool record(int16_t* data, size_t count, uint32_t, bool) {
    memset(data, 0, count * sizeof(int16_t));
    return true;
  }
  size_t isRecording() const { return 0; }
  MicConfig config() const { return {}; }
  void config(const MicConfig&) {}
};

struct Imu_Class {
  bool update() { return false; }
  ImuData getImuData() const { return {}; }
};

struct M5Config {
  uint32_t serial_baudrate = 0;
  m5::board_t fallback_board = m5::board_t::board_unknown;
  bool internal_spk = false, internal_mic = false;
};

struct M5Unified_Class {
  Display_Class Display;
  Speaker_Class Speaker;
  Mic_Class Mic;
  Imu_Class Imu;
  m5::Power_Class Power;
  Button_Class BtnA = make_button(0);
  Button_Class BtnB = make_button(1);
  M5Config config() const { return {}; }
  void begin(const M5Config&) {}
  void update() {}
  m5::board_t getBoard() const { return m5::board_t::board_unknown; }
};
inline M5Unified_Class M5;

class M5Canvas {
 public:
  explicit M5Canvas(Display_Class*) {}
  void setColorDepth(int) {}
  void setTextWrap(bool) {}
  void setPsram(bool) {}
  void* createSprite(int width, int height) {
    width_ = width; height_ = height;
    pixels_.assign(static_cast<size_t>(width * height), 0);
    return pixels_.data();
  }
  void deleteSprite() { pixels_.clear(); width_ = height_ = 0; }
  int getRotation() const { return 0; }
  lgfx::color_depth_t getColorDepth() const { return lgfx::rgb565_2Byte; }
  int width() const { return width_; }
  int height() const { return height_; }
  size_t bufferLength() const { return pixels_.size() * sizeof(uint16_t); }
  void* getBuffer() { return pixels_.data(); }
  // The fallback API path (unused while the packed buffer is available).
  // Storage is byte-swapped RGB565, as M5GFX keeps it.
  static uint16_t swap(uint16_t v) { return static_cast<uint16_t>((v << 8) | (v >> 8)); }
  void drawPixel(int x, int y, uint16_t color) {
    if (x >= 0 && y >= 0 && x < width_ && y < height_) pixels_[y * width_ + x] = swap(color);
  }
  uint16_t readPixel(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_ ? swap(pixels_[y * width_ + x]) : 0;
  }
  void drawFastHLine(int x, int y, int length, uint16_t color) {
    for (int i = 0; i < length; ++i) drawPixel(x + i, y, color);
  }
  void pushImage(int x, int y, int w, int h, const lgfx::rgb565_t* data) {
    for (int j = 0; j < h; ++j)
      for (int i = 0; i < w; ++i) drawPixel(x + i, y + j, data[j * w + i]);
  }
  void pushSprite(int, int) {
    if (device_shim::on_present) device_shim::on_present(pixels_.data(), width_, height_);
  }

 private:
  std::vector<uint16_t> pixels_;
  int width_ = 0, height_ = 0;
};
