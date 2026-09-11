#pragma once
#include "humsienk.h"
#if defined(ARDUINO_ARCH_ESP32)
namespace humsienk {
// One process-lifetime NimBLE owner. Do not combine with Bluedroid or another
// BLE owner. Call from one application task, never from a BLE callback.
class Esp32Battery {
 public:
  Error begin();
  Error discover(Candidate* out, std::size_t capacity, std::size_t& count,
                 std::uint32_t scanMs = 10000);
  Error connect(const Target& target);
  void disconnect();
  bool connected() const;
  Error readSnapshot(Snapshot& out);
  Error readProduct(Product& out);
  Error readRaw(ReadPoint point, Response& out);
  Esp32Battery(const Esp32Battery&) = delete;
  Esp32Battery& operator=(const Esp32Battery&) = delete;
 private:
  Esp32Battery() = default;
  friend Esp32Battery& battery();
};
Esp32Battery& battery();
}
#endif
