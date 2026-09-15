#pragma once
#include <stdint.h>

namespace cxnthermal {
enum class Source : uint8_t { None, Module, Esp32 };

// Pure debounce/latch logic kept independent from Arduino so it can be host-tested.
struct Guard {
  uint8_t moduleCount = 0;
  uint8_t esp32Count = 0;
  bool latched = false;
  bool reported = false;
  Source source = Source::None;

  void reset() { moduleCount = esp32Count = 0; latched = reported = false; source = Source::None; }
  bool update(bool enabled, bool moduleValid, float moduleTemp, uint8_t moduleLimit,
              bool esp32Valid, float esp32Temp, uint8_t esp32Limit) {
    if (!enabled) { moduleCount = esp32Count = 0; return false; }
    moduleCount = moduleValid && moduleTemp >= moduleLimit ? (moduleCount < 2 ? moduleCount + 1 : 2) : 0;
    esp32Count = esp32Valid && esp32Temp >= esp32Limit ? (esp32Count < 2 ? esp32Count + 1 : 2) : 0;
    if (!latched && (moduleCount >= 2 || esp32Count >= 2)) {
      latched = true;
      source = moduleCount >= 2 ? Source::Module : Source::Esp32;
    }
    if (latched && !reported) { reported = true; return true; }
    return false;
  }
  bool fanFull(bool enabled) const { return enabled && (moduleCount || esp32Count || latched); }
};
}
