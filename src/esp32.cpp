#include "humsienk/esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_bt.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <cstring>

namespace humsienk {
namespace {
struct Chunk { std::size_t size; std::uint8_t bytes[kMaxFrame]; };
class BleTransport final : public Transport {
 public:
  Error begin() {
    if (initialized_) return Error::Ok;
    if (attempted_ || NimBLEDevice::isInitialized() ||
        esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_IDLE) return Error::Busy;
    // IDF4 radio coexistence can abort when modem sleep is disabled. Policy
    // belongs to the application: refuse instead of changing its Wi-Fi.
    wifi_mode_t mode=WIFI_MODE_NULL;
    auto status=esp_wifi_get_mode(&mode);
    if (status!=ESP_OK && status!=ESP_ERR_WIFI_NOT_INIT) return Error::Transport;
    if (status==ESP_OK && mode!=WIFI_MODE_NULL) {
      wifi_ps_type_t ps=WIFI_PS_NONE;
      if (esp_wifi_get_ps(&ps)!=ESP_OK || ps==WIFI_PS_NONE) return Error::Busy;
    }
    attempted_=true;
    queue_=xQueueCreate(8,sizeof(Chunk));
    if (!queue_) return Error::Overflow;
    if (!NimBLEDevice::init("humsienk-esp32")) return Error::Transport;
    NimBLEDevice::setMTU(247);
    client_=NimBLEDevice::createClient();
    if (!client_) return Error::Transport;
    client_->setConnectTimeout(10000);
    initialized_=true; return Error::Ok;
  }
  Error open(const Target& t) override {
    if (!initialized_) return Error::NotConnected;
    if (!validTarget(t)) return Error::InvalidArgument;
    if (connected() || NimBLEDevice::getScan()->isScanning()) return Error::Busy;
    accepting_=false; clearNotifications(); rx_=tx_=auth_=nullptr;
    // Some HS04 peripherals initiate MTU negotiation themselves. NimBLE 2.x's
    // early automatic exchange can then fail with BLE_HS_EALREADY and discard
    // an otherwise usable connection. Keep fresh GATT discovery and synchronous
    // connect, but let this peripheral negotiate MTU. Frames are reassembled
    // independently of notification size; requests fit the default ATT MTU.
    // https://github.com/h2zero/NimBLE-Arduino/discussions/1074
    if (!client_->connect(NimBLEAddress(std::string(t.address),std::uint8_t(t.type)),
                          true, false, false)) return Error::Transport;
    auto* service=client_->getService(NimBLEUUID(std::uint16_t(0xfff0)));
    if (service) {
      rx_=service->getCharacteristic(NimBLEUUID(std::uint16_t(0xfff1)));
      tx_=service->getCharacteristic(NimBLEUUID(std::uint16_t(0xfff2)));
      auth_=service->getCharacteristic(NimBLEUUID(std::uint16_t(0xfffa)));
    }
    if (!rx_ || !tx_ || !auth_ || !rx_->canNotify() || !tx_->canWrite() ||
        !auth_->canWrite() || !auth_->canRead()) {
      close(); return Error::WrongResponse;
    }
    if (!rx_->subscribe(true,[this](NimBLERemoteCharacteristic*, std::uint8_t* data,
                                   std::size_t size, bool) {
      if (!accepting_.load()) return;
      if (size>kMaxFrame) { overflow_=true; return; }
      Chunk chunk{}; chunk.size=size; std::memcpy(chunk.bytes,data,size);
      if (xQueueSend(queue_,&chunk,0)!=pdTRUE) overflow_=true;
    },true)) { close(); return Error::Transport; }
    accepting_=true; return Error::Ok;
  }
  void close() override {
    accepting_=false;
    if (client_ && client_->isConnected()) {
      client_->disconnect();
      const auto start=millis();
      while (client_->isConnected() && std::uint32_t(millis()-start)<3000) delay(10);
    }
    // Queue and owner live for the process lifetime, including late callbacks.
    clearNotifications();
  }
  bool connected() const override { return client_ && client_->isConnected(); }
  Error write(bool auth, const std::uint8_t* bytes, std::size_t size) override {
    if (!connected()) return Error::NotConnected;
    auto* characteristic=auth ? auth_ : tx_;
    if (!characteristic || !characteristic->writeValue(bytes,size,true)) return Error::Transport;
    return Error::Ok;
  }
  Error receive(std::uint8_t* out, std::size_t capacity, std::size_t& size,
                std::uint32_t timeoutMs) override {
    size=0;
    if (overflow_) return Error::Overflow;
    if (!connected()) return Error::NotConnected;
    Chunk chunk{};
    if (xQueueReceive(queue_,&chunk,pdMS_TO_TICKS(timeoutMs))!=pdTRUE) return Error::Timeout;
    if (overflow_ || chunk.size>capacity) return Error::Overflow;
    std::memcpy(out,chunk.bytes,chunk.size); size=chunk.size; return Error::Ok;
  }
  Error readAuthStatus(std::uint8_t& status) override {
    if (!connected() || !auth_) return Error::NotConnected;
    const auto value=auth_->readValue();
    if (value.size()!=1) return Error::Transport;
    status=value[0]; return Error::Ok;
  }
  void clearNotifications() override { if (queue_) xQueueReset(queue_); overflow_=false; }
  void wait(std::uint32_t ms) override { delay(ms); }
  std::uint32_t nowMs() const override { return millis(); }
 private:
  bool initialized_=false, attempted_=false;
  NimBLEClient* client_=nullptr;
  NimBLERemoteCharacteristic *rx_=nullptr, *tx_=nullptr, *auth_=nullptr;
  QueueHandle_t queue_=nullptr;
  std::atomic<bool> overflow_{false}, accepting_{false};
};
BleTransport& transport() { static BleTransport t; return t; }
Session& session() { static Session s(transport()); return s; }
}
Esp32Battery& battery() { static Esp32Battery b; return b; }
Error Esp32Battery::begin() { return transport().begin(); }
Error Esp32Battery::connect(const Target& t) { return session().connect(t); }
void Esp32Battery::disconnect() { session().disconnect(); }
bool Esp32Battery::connected() const { return session().connected(); }
Error Esp32Battery::readSnapshot(Snapshot& s) { return session().readSnapshot(s); }
Error Esp32Battery::readProduct(Product& p) { return session().readProduct(p); }
Error Esp32Battery::readRaw(ReadPoint p, Response& r) { return session().readRaw(p,r); }
Error Esp32Battery::discover(Candidate* out, std::size_t capacity, std::size_t& count,
                            std::uint32_t scanMs) {
  count=0;
  if (!out || !capacity || capacity>32 || scanMs<1000 || scanMs>30000) return Error::InvalidArgument;
  auto e=begin(); if (e!=Error::Ok) return e;
  auto* scanner=NimBLEDevice::getScan();
  if (transport().connected() || scanner->isScanning()) return Error::Busy;
  scanner->setActiveScan(true); scanner->setMaxResults(32); scanner->clearResults();
  const auto results=scanner->getResults(scanMs,false);
  for (int i=0; i<results.getCount() && count<capacity; ++i) {
    const auto* d=results.getDevice(i);
    const auto name=d->getName();
    if (name.compare(0,2,"HS")!=0 && !d->isAdvertisingService(NimBLEUUID(std::uint16_t(0xfff0)))) continue;
    Candidate c{};
    const auto address=d->getAddress();
    if (address.getType()>1) continue;
    const auto text=address.toString();
    std::strncpy(c.target.address,text.c_str(),sizeof(c.target.address)-1);
    c.target.type=AddressType(address.getType());
    std::strncpy(c.name,name.c_str(),sizeof(c.name)-1); c.rssi=d->getRSSI();
    out[count++]=c;
  }
  scanner->clearResults(); return Error::Ok;
}
}
#endif
