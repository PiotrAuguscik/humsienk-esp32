#include <humsienk/esp32.h>

auto& bms = humsienk::battery();
humsienk::Target target;
bool haveTarget = false;

void setup() {
  Serial.begin(115200);
  auto error = bms.begin();
  if (error != humsienk::Error::Ok) {
    Serial.println(humsienk::errorName(error));
    return;
  }
  humsienk::Candidate candidates[8];
  size_t count = 0;
  error = bms.discover(candidates, 8, count);
  if (error != humsienk::Error::Ok) {
    Serial.println(humsienk::errorName(error));
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    Serial.printf("%s type=%u %s RSSI=%d\n", candidates[i].target.address,
                  unsigned(candidates[i].target.type), candidates[i].name, candidates[i].rssi);
  }
  // Never silently select among multiple batteries.
  if (count != 1) {
    Serial.println("Expected one candidate. Select your battery explicitly.");
    return;
  }
  target = candidates[0].target;
  haveTarget = true;
}

void loop() {
  if (!haveTarget) { delay(1000); return; }
  if (!bms.connected()) {
    const auto error = bms.connect(target);
    if (error != humsienk::Error::Ok) {
      Serial.println(humsienk::errorName(error));
      delay(10000);
      return;
    }
  }
  humsienk::Snapshot data;
  const auto error = bms.readSnapshot(data);
  if (error == humsienk::Error::Ok) {
    Serial.printf("SOC %u%% | %.2f V | %+.1f A\n", data.socPercent, data.voltageV, data.currentA);
    Serial.printf("Cells %.3f–%.3f V | remaining %.1f Ah\n",
                  data.minCellV(), data.maxCellV(), data.remainingCapacityAh);
    for (unsigned i = 0; i < data.cellCount; ++i)
      Serial.printf("Cell %u: %u mV\n", i + 1, data.cellMillivolts[i]);
    for (unsigned i = 0; i < data.temperatureCount; ++i)
      Serial.printf("Sensor %u: %.1f C\n", i + 1, data.temperaturesC[i]);
    if (data.experimental.present)
      Serial.printf("Experimental SOH: %u%%\n", data.experimental.sohPercent);
  } else {
    Serial.println(humsienk::errorName(error));
  }
  // Application owns sampling cadence. Session stays connected between reads.
  delay(10000);
}
