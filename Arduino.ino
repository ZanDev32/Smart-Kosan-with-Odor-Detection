// Display and DHT headers
#include "SSH1106.h"
#include "DHT22.h"
#include "esp32c3.h" 

// MQ135 configuration MUST be defined BEFORE including MQ135.h
#undef MQ135_BOARD
#define MQ135_BOARD "ESP-32"
#undef MQ135_VOLTAGE_RESOLUTION
#define MQ135_VOLTAGE_RESOLUTION 3.3f
#undef MQ135_ADC_BIT_RESOLUTION
#define MQ135_ADC_BIT_RESOLUTION 12
#undef MQ135_ANALOG_PIN
#define MQ135_ANALOG_PIN 2   // GPIO2 (ADC)
#undef MQ135_DIGITAL_PIN
#define MQ135_DIGITAL_PIN 1  // GPIO1 (Digital DOUT)

#include "MQ135.h"

DHT22Sensor dht22;
SH1106Display oled;
MQ135Sensor mq135;

void setup() {
  Serial.begin(9600);
  delay(2000);
  
#ifdef ESP32
  // Ensure ADC resolution matches our MQ135 configuration on ESP32-C3
  analogReadResolution(MQ135_ADC_BIT_RESOLUTION);
  // Use full-scale attenuation for up to ~3.3V input range on ESP32-C3 ADC
  analogSetPinAttenuation(MQ135_ANALOG_PIN, ADC_11db);
#endif
  
  dht22.begin();

  if (!oled.begin()) {
    Serial.println(F("SH1106 allocation failed"));
    while (1);
  }
  oled.clear();

  // Initialize MQ-135 (place in clean air during calibration)
  Serial.println(F("Calibrating MQ-135 in clean air..."));
  // Warm-up the heater for more stable R0 (initial burn-in may need much longer)
  const unsigned long warmupMs = 5000; // 5s warm-up (increase for better stability)
  unsigned long wstart = millis();
  while (millis() - wstart < warmupMs) {
    mq135.update();
    delay(50);
  }
  mq135.begin(100, 100); // 100 samples, 100ms interval
  Serial.print(F("MQ-135 R0 = ")); Serial.println(mq135.getR0(), 3);

  Net::Config cfg;
  cfg.ssid = "morning";
  cfg.pass = "mieayam9";
  cfg.hostname = "smart-kosan-odor-detection"; // mDNS: http://smart-kosan-odor-detection.local/
  // cfg.sta_ip = IPAddress(192,168,23,230);
  // cfg.sta_gw = IPAddress(192,168,1,1);
  // cfg.sta_sn = IPAddress(255,255,255,0);
  // cfg.sta_dns1 = IPAddress(192,168,1,1);
  
  // MQTT Configuration (uncomment and configure to enable MQTT)
  // cfg.mqtt_server = "broker.hivemq.com";  // or your MQTT broker IP/domain
  // cfg.mqtt_port = 1883;
  // cfg.mqtt_user = nullptr;  // set if broker requires auth
  // cfg.mqtt_pass = nullptr;
  // cfg.mqtt_client_id = "esp32c3-room204";
  // cfg.mqtt_topic = "kosan/room204/sensors";
  // cfg.room_id = "204";
  // cfg.mqtt_interval_ms = 5000; // publish every 5 seconds
  
  Net::begin(cfg);
  // Provide MQ135 callbacks for HTTP endpoints
  Net::cbReadR0() = []() -> float { return mq135.getR0(); };
  Net::cbRecal() = [](unsigned long samples, unsigned long intervalMs, unsigned long warmupMs) -> bool {
    // Warm-up before recalibration
    unsigned long ws = millis();
    while (millis() - ws < warmupMs) {
      mq135.update();
      delay(50);
    }
    // Perform calibration with requested parameters
    mq135.begin(samples, intervalMs);
    Serial.print(F("[NET] Recalibrated R0 = ")); Serial.println(mq135.getR0(), 3);
    return true;
  };
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP: "); Serial.println(WiFi.localIP());  // your ESP IP
  } else {
    Serial.println("WiFi connect failed");
    WiFi.disconnect(true, true);    // clear cached creds/NVS
  }
}

void loop() {
  delay(2500);
  Net::handle();
  
  // Read sensor data
  float humidity = dht22.readHumidity();
  float temperature = dht22.readTemperature();
  // Update MQ135 first, then read a gas value (CO2 approximation)
  mq135.update();
  // Simple moving average over N samples to reduce spikes
  static const int N = 5;
  static float buf[N];
  static int idx = 0;
  static int filled = 0;
  float co2_raw = mq135.readCO2();
  buf[idx] = co2_raw;
  idx = (idx + 1) % N;
  if (filled < N) filled++;
  float sum = 0;
  for (int i = 0; i < filled; ++i) sum += buf[i];
  float co2ppm = sum / filled;
  // Guard against invalid values from regression at extremes (0 or saturated ADC)
  if (!isfinite(co2ppm) || co2ppm <= 0 || co2ppm > 50000) {
    co2ppm = NAN;
  }

  // Validate readings
  if (!dht22.isValidReading(temperature, humidity)) {
    Serial.println(F("DHT22 read failed"));
    return;
  }

  // Calculate heat index
  float heatIndex = DHT22Sensor::computeHeatIndex(temperature, humidity);

  // Print to Serial
  Serial.print(F("Humidity: "));    Serial.print(humidity, 1);      Serial.print("%    ");
  Serial.print(F("Temp: "));        Serial.print(temperature, 1);   Serial.print("°C   ");
  Serial.print(F("Heat index: "));  Serial.print(heatIndex, 1);     Serial.print("°C   ");
  Serial.print(F("CO2: "));
  if (isfinite(co2ppm)) {
    Serial.print(co2ppm, 0); Serial.println(F(" ppm"));
  } else {
    Serial.println(F("ERR"));
  }

  // Display on OLED (environment + gas)
  oled.displayDHT22(temperature, humidity, heatIndex);
  oled.displayMQ135(co2ppm);
  oled.show();

  // Update network state cache for /state endpoint
  Net::update(temperature, humidity, heatIndex, isfinite(co2ppm) ? co2ppm : 0);
  
  // Publish to MQTT if configured (uses stored cfg values from setup)
  // Will auto-reconnect and respect interval
  static const char* mqttClientId = "esp32c3-room204";
  static const char* mqttUser = nullptr;
  static const char* mqttPass = nullptr;
  Net::publishMqtt(mqttClientId, mqttUser, mqttPass);
}
