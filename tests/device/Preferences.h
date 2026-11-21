// Host stand-in: settings are not kept between runs.
#pragma once
#include <map>
#include <string>
class Preferences {
 public:
  bool begin(const char*, bool) { return true; }
  bool getBool(const char* key, bool fallback) const {
    const auto at = values_.find(key);
    return at == values_.end() ? fallback : at->second;
  }
  void putBool(const char* key, bool value) { values_[key] = value; }

 private:
  std::map<std::string, bool> values_;
};
