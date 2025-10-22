#pragma once
// esp32.h — HTTP JSON endpoint /state untuk ESP32/ESP32-C3
// Header-only. Tidak perlu .cpp terpisah.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>

namespace Net {

// Callback types
typedef float (*ReadR0Func)();
typedef bool (*RecalibrateFunc)(unsigned long samples, unsigned long intervalMs, unsigned long warmupMs);

struct Config {
  const char* ssid      = "YourWiFi";
  const char* pass      = "YourPass";
  const char* hostname  = "esp32c3";    // mDNS hostname (esp32c3.local)
  const char* ap_ssid   = "ESP32C3-AP";
  const char* ap_pass   = "pass12345";   // min 8 char
  bool        enable_ap_fallback = true; // SoftAP jika STA gagal
  bool        enable_cors = true;        // Access-Control-Allow-Origin: *
  uint32_t    sta_timeout_ms = 8000;     // tunggu koneksi STA

  // Optional: Static IP settings for STA mode (use DHCP if 0.0.0.0)
  IPAddress   sta_ip   = IPAddress(0,0,0,0);
  IPAddress   sta_gw   = IPAddress(0,0,0,0);
  IPAddress   sta_sn   = IPAddress(0,0,0,0);
  IPAddress   sta_dns1 = IPAddress(0,0,0,0);
  IPAddress   sta_dns2 = IPAddress(0,0,0,0);

  // Optional: SoftAP IP settings (defaults to 192.168.4.1/24)
  IPAddress   ap_ip    = IPAddress(192,168,4,1);
  IPAddress   ap_gw    = IPAddress(192,168,4,1);
  IPAddress   ap_sn    = IPAddress(255,255,255,0);

  // Optional callbacks for MQ135 integration
  ReadR0Func      readR0      = nullptr;
  RecalibrateFunc recalibrate = nullptr;

  // MQTT Configuration
  const char* mqtt_server   = nullptr;           // MQTT broker address (e.g., "broker.hivemq.com")
  uint16_t    mqtt_port     = 1883;              // MQTT broker port
  const char* mqtt_user     = nullptr;           // MQTT username (nullptr if no auth)
  const char* mqtt_pass     = nullptr;           // MQTT password
  const char* mqtt_client_id = "esp32c3-sensor"; // MQTT client ID
  const char* mqtt_topic    = "kosan/room204/sensors"; // MQTT topic for publishing
  const char* room_id       = "204";             // Room identifier for JSON payload
  uint32_t    mqtt_interval_ms = 5000;           // Publish interval in ms (default 5s)
};

// cache nilai terakhir
inline volatile float gT = NAN, gH = NAN, gHI = NAN, gCO2 = NAN;
inline volatile uint32_t gTS = 0;

// MQTT state
inline WiFiClient& wifiClient() { static WiFiClient c; return c; }
inline PubSubClient& mqttClient() { static PubSubClient c(wifiClient()); return c; }
inline uint32_t gLastMqttAttempt = 0;
inline uint32_t gLastMqttPublish = 0;
inline const char* gRoomId = "204";
inline const char* gMqttTopic = "kosan/room204/sensors";
inline uint32_t gMqttInterval = 5000;

// internal server
inline WebServer& server() {
  static WebServer s(80);
  return s;
}

// Forward declaration for helper returning active IP
inline IPAddress ip();

// Stored callbacks
inline ReadR0Func& cbReadR0() { static ReadR0Func f = nullptr; return f; }
inline RecalibrateFunc& cbRecal() { static RecalibrateFunc f = nullptr; return f; }

inline void _setCors() {
  if (!server().hasHeader("Access-Control-Allow-Origin")) {
    server().sendHeader("Access-Control-Allow-Origin", "*");
  }
  server().sendHeader("Cache-Control", "no-cache");
}

inline void _handleRoot() {
  _setCors();
  server().send(200, "text/plain", "OK. GET /state for JSON.\n");
}

inline void _handleState() {
  _setCors();
  char buf[192];
  auto nz = [](float v){ return isfinite(v) ? v : 0.0f; };
  snprintf(buf, sizeof(buf),
    "{\"t\":%.1f,\"h\":%.1f,\"hi\":%.1f,\"co2\":%.0f,\"ts\":%lu}",
    nz(gT), nz(gH), nz(gHI), nz(gCO2), (unsigned long)gTS);
  server().send(200, "application/json", buf);
}

inline void _handleNet() {
  _setCors();
  bool sta = (WiFi.getMode() & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  bool ap  = (WiFi.getMode() & WIFI_MODE_AP);
  String mode = sta ? "STA" : (ap ? "AP" : "NONE");
  IPAddress ipaddr = ip();
  String ssid = sta ? WiFi.SSID() : (ap ? WiFi.softAPSSID() : String(""));
  long rssi = sta ? WiFi.RSSI() : 0;
  String mac = sta ? WiFi.macAddress() : WiFi.softAPmacAddress();
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"mode\":\"%s\",\"ssid\":\"%s\",\"rssi\":%ld,\"ip\":\"%s\",\"mac\":\"%s\"}",
           mode.c_str(), ssid.c_str(), rssi, ipaddr.toString().c_str(), mac.c_str());
  server().send(200, "application/json", buf);
}

inline void _handleMqR0() {
  _setCors();
  float r0 = NAN;
  if (cbReadR0()) r0 = cbReadR0()();
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"r0\":%.3f}", isfinite(r0) ? r0 : 0.0f);
  server().send(cbReadR0() ? 200 : 501, "application/json", buf);
}

inline void _handleMqRecalibrate() {
  _setCors();
  if (!cbRecal()) {
    server().send(501, "application/json", "{\"ok\":false,\"err\":\"recalibrate not available\"}");
    return;
  }
  unsigned long s = server().hasArg("s") ? strtoul(server().arg("s").c_str(), nullptr, 10) : 100;
  unsigned long i = server().hasArg("i") ? strtoul(server().arg("i").c_str(), nullptr, 10) : 100;
  unsigned long w = server().hasArg("w") ? strtoul(server().arg("w").c_str(), nullptr, 10) : 3000;
  bool ok = cbRecal()(s, i, w);
  float r0 = cbReadR0() ? cbReadR0()() : NAN;
  char buf[160];
  snprintf(buf, sizeof(buf), "{\"ok\":%s,\"r0\":%.3f,\"s\":%lu,\"i\":%lu,\"w\":%lu}",
           ok ? "true" : "false", isfinite(r0) ? r0 : 0.0f, s, i, w);
  server().send(ok ? 200 : 500, "application/json", buf);
}

inline void _handleMqtt() {
  _setCors();
  bool connected = mqttClient().connected();
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"connected\":%s,\"broker\":\"%s\",\"topic\":\"%s\",\"lastPublish\":%lu,\"interval\":%lu}",
           connected ? "true" : "false",
           mqttClient().connected() ? "connected" : "disconnected",
           gMqttTopic,
           (unsigned long)gLastMqttPublish,
           (unsigned long)gMqttInterval);
  server().send(200, "application/json", buf);
}

inline void begin(const Config& cfg = Config{}) {
  // Mode STA
  WiFi.mode(WIFI_STA);

  auto ipSet = [](const IPAddress& ip){ return ip != IPAddress(0,0,0,0); };

  // Apply static IP if provided (must be before WiFi.begin)
  if (ipSet(cfg.sta_ip) && ipSet(cfg.sta_gw) && ipSet(cfg.sta_sn)) {
    bool ok = WiFi.config(cfg.sta_ip, cfg.sta_gw, cfg.sta_sn,
                          ipSet(cfg.sta_dns1) ? cfg.sta_dns1 : IPAddress(0,0,0,0),
                          ipSet(cfg.sta_dns2) ? cfg.sta_dns2 : IPAddress(0,0,0,0));
    if (!ok) {
      Serial.println(F("[NET] WiFi.config (STA) failed"));
    }
  }
  // Set hostname if provided
  if (cfg.hostname && cfg.hostname[0]) {
    WiFi.setHostname(cfg.hostname);
  }
  WiFi.begin(cfg.ssid, cfg.pass);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < cfg.sta_timeout_ms) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    // connected as STA
    Serial.print(F("[NET] STA IP: ")); Serial.println(WiFi.localIP());
    // Start mDNS (wait for IP to be fully configured)
    delay(100);
    if (cfg.hostname && cfg.hostname[0]) {
      if (MDNS.begin(cfg.hostname)) {
        MDNS.addService("http", "tcp", 80);
        Serial.print(F("[NET] mDNS started: http://")); Serial.print(cfg.hostname); Serial.println(F(".local/"));
        Serial.print(F("[NET] Hostname: ")); Serial.println(cfg.hostname);
      } else {
        Serial.println(F("[NET] ERROR: mDNS begin failed!"));
      }
    } else {
      Serial.println(F("[NET] WARNING: No hostname set for mDNS"));
    }
  } else if (cfg.enable_ap_fallback) {
    // fallback SoftAP
    WiFi.mode(WIFI_AP);
    // Optionally set AP IP before starting AP
    if (cfg.ap_ip != IPAddress(0,0,0,0) && cfg.ap_sn != IPAddress(0,0,0,0)) {
      if (!WiFi.softAPConfig(cfg.ap_ip, cfg.ap_gw, cfg.ap_sn)) {
        Serial.println(F("[NET] softAPConfig failed"));
      }
    }
    WiFi.softAP(cfg.ap_ssid, cfg.ap_pass);
    // Set AP hostname if available
    if (cfg.hostname && cfg.hostname[0]) {
#if defined(ESP32)
      WiFi.softAPsetHostname(cfg.hostname);
#endif
      if (MDNS.begin(cfg.hostname)) {
        MDNS.addService("http", "tcp", 80);
        Serial.print(F("[NET] mDNS(AP): http://")); Serial.print(cfg.hostname); Serial.println(F(".local/"));
      }
    }
    Serial.print(F("[NET] SoftAP IP: ")); Serial.println(WiFi.softAPIP());
  } else {
    Serial.println(F("[NET] STA failed and AP fallback disabled"));
  }

  // Initialize MQTT if configured
  if (cfg.mqtt_server && cfg.mqtt_server[0]) {
    gRoomId = cfg.room_id;
    gMqttTopic = cfg.mqtt_topic;
    gMqttInterval = cfg.mqtt_interval_ms;
    mqttClient().setServer(cfg.mqtt_server, cfg.mqtt_port);
    Serial.print(F("[NET] MQTT broker: ")); Serial.print(cfg.mqtt_server);
    Serial.print(":"); Serial.println(cfg.mqtt_port);
  }

  // routes
  server().on("/", [](){ _handleRoot(); });
  server().on("/state", HTTP_GET, [](){ _handleState(); });
  server().on("/net", HTTP_GET, [](){ _handleNet(); });
  server().on("/mq/r0", HTTP_GET, [](){ _handleMqR0(); });
  server().on("/mq/recalibrate", HTTP_GET, [](){ _handleMqRecalibrate(); });
  server().on("/mqtt", HTTP_GET, [](){ _handleMqtt(); });

  // preflight CORS (opsional)
  server().onNotFound([](){
    if (server().method() == HTTP_OPTIONS) {
      _setCors();
      server().sendHeader("Access-Control-Allow-Methods", "GET,OPTIONS");
      server().sendHeader("Access-Control-Allow-Headers", "Content-Type");
      server().send(204);
    } else {
      server().send(404, "text/plain", "Not found");
    }
  });

  server().begin();
}

// MQTT reconnect helper
inline bool mqttReconnect(const char* clientId, const char* user, const char* pass) {
  if (mqttClient().connected()) return true;
  
  uint32_t now = millis();
  if (now - gLastMqttAttempt < 5000) return false; // retry every 5s
  gLastMqttAttempt = now;
  
  Serial.print(F("[MQTT] Connecting... "));
  bool ok = false;
  if (user && user[0] && pass && pass[0]) {
    ok = mqttClient().connect(clientId, user, pass);
  } else {
    ok = mqttClient().connect(clientId);
  }
  
  if (ok) {
    Serial.println(F("connected"));
  } else {
    Serial.print(F("failed, rc="));
    Serial.println(mqttClient().state());
  }
  return ok;
}

// Format sensor data as JSON string per your spec
inline String formatSensorJson(const char* roomId, float t, float h, float hi, float co2) {
  auto nz = [](float v){ return isfinite(v) ? v : 0.0f; };
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"roomId\":\"%s\",\"timestamp\":%lu,\"temperature\":%.1f,\"humidity\":%.1f,\"heatIndex\":%.1f,\"co2\":%.0f}",
    roomId,
    millis(),
    nz(t), nz(h), nz(hi), nz(co2));
  return String(buf);
}

// Publish sensor data to MQTT if enabled and interval elapsed
inline bool publishMqtt(const char* clientId, const char* user, const char* pass) {
  if (!mqttClient().connected()) {
    if (!mqttReconnect(clientId, user, pass)) return false;
  }
  
  uint32_t now = millis();
  if (now - gLastMqttPublish < gMqttInterval) return false; // respect interval
  
  String json = formatSensorJson(gRoomId, gT, gH, gHI, gCO2);
  bool ok = mqttClient().publish(gMqttTopic, json.c_str());
  if (ok) {
    gLastMqttPublish = now;
    Serial.print(F("[MQTT] Published: "));
    Serial.println(json);
  } else {
    Serial.println(F("[MQTT] Publish failed"));
  }
  return ok;
}

inline void handle() {
  server().handleClient();
  mqttClient().loop(); // MQTT keep-alive
}

// panggil setelah baca sensor
inline void update(float t, float h, float hi, float co2) {
  gT  = t;
  gH  = h;
  gHI = hi;
  gCO2 = co2;
  gTS = millis() / 1000;
}

// util: ambil IP aktif (STA jika ada, else AP)
inline IPAddress ip() {
  if (WiFi.getMode() & WIFI_MODE_STA && WiFi.status() == WL_CONNECTED)
    return WiFi.localIP();
  if (WiFi.getMode() & WIFI_MODE_AP)
    return WiFi.softAPIP();
  return IPAddress(0,0,0,0);
}

} // namespace Net
