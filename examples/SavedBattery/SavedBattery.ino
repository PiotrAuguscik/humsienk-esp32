#include <humsienk/esp32.h>
#include <Preferences.h>

auto& bms = humsienk::battery();
humsienk::Target target;
bool ready = false;

void setup() {
  Serial.begin(115200);
  if (bms.begin() != humsienk::Error::Ok) return;
  Preferences settings;
  if (!settings.begin("humsienk", false)) return;
  const String address = settings.getString("address", "");
  address.toCharArray(target.address, sizeof(target.address));
  target.type = humsienk::AddressType(settings.getUChar("type", 255));
  if (!humsienk::validTarget(target)) {
    humsienk::Candidate candidates[8];
    size_t count = 0;
    if (bms.discover(candidates, 8, count) != humsienk::Error::Ok || count != 1) {
      Serial.println("Select one battery before saving its address.");
      settings.end();
      return;
    }
    target = candidates[0].target;
  }
  const auto error = bms.connect(target);
  if (error == humsienk::Error::Ok) {
    // Persist only after GATT + product/version/CRC validation.
    settings.putString("address", target.address);
    settings.putUChar("type", uint8_t(target.type));
    ready = true;
  } else Serial.println(humsienk::errorName(error));
  settings.end();
}

void loop() {
  if (!ready) { delay(1000); return; }
  if (!bms.connected() && bms.connect(target) != humsienk::Error::Ok) {
    delay(10000);
    return;
  }
  humsienk::Snapshot data;
  const auto error = bms.readSnapshot(data);
  if (error == humsienk::Error::Ok)
    Serial.printf("SOC %u%%, battery %.2f V\n", data.socPercent, data.voltageV);
  else Serial.println(humsienk::errorName(error));
  delay(10000);
}
