#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// --- Pins ---
const int PWM_PIN        = 4;   // PWM Ausgang zur Bahn
const int PIN_BTN_OUT    = 6;   // BC547 Basis

// --- WiFi Access Point ---
const char* AP_SSID     = "Carrera-Controller";  // Netzwerkname
const char* AP_PASSWORD = "carrera123";           // Passwort (min. 8 Zeichen)

// --- PWM ---
const int PWM_CHANNEL   = 0;
const int PWM_FREQ_HZ   = 5000;
const int PWM_RES_BITS  = 12;
const int PWM_MAX       = (1 << PWM_RES_BITS) - 1;
const int PWM_VREF_MV   = 3300;
const int PWM_TARGET_MV = 2000;
const int PWM_MAX_2V    = (PWM_MAX * PWM_TARGET_MV) / PWM_VREF_MV;

// --- Button ---
const unsigned long BTN_MS                  = 80;
const unsigned long SPURWECHSEL_COOLDOWN_MS = 500;
unsigned long letzter_spurwechsel           = 0;

// --- State ---
int  aktueller_speed    = 0;   // 0-100%
bool spurwechsel_aktiv  = false;
unsigned long letztes_heartbeat_ms = 0;

// --- Webserver auf Port 80 ---
WebServer server(80);

// ───────────────────────────────────────────────────────

bool startAccessPoint() {
  IPAddress localIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);

  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  if (!WiFi.softAPConfig(localIP, gateway, subnet)) {
    Serial.println("Fehler: softAPConfig fehlgeschlagen");
    return false;
  }

  if (!WiFi.softAP(AP_SSID, AP_PASSWORD, 1, false, 4)) {
    Serial.println("Fehler: softAP konnte nicht gestartet werden");
    return false;
  }

  Serial.printf("Chip:         %s Rev.%d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("Access Point: %s\n", AP_SSID);
  Serial.printf("IP Adresse:   %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("AP MAC:       %s\n", WiFi.softAPmacAddress().c_str());
  return true;
}

// Setzt die Geschwindigkeit per PWM
void setSpeed(int percent) {
  percent = constrain(percent, 0, 100);
  aktueller_speed = percent;
  int pwm_val = map(percent, 0, 100, 0, PWM_MAX_2V);
  ledcWrite(PWM_CHANNEL, pwm_val);
}

// Löst einen Spurwechsel aus
void doSpurwechsel() {
  unsigned long jetzt = millis();
  if ((jetzt - letzter_spurwechsel) < SPURWECHSEL_COOLDOWN_MS) return;
  letzter_spurwechsel = jetzt;

  Serial.println("Spurwechsel ausgelöst");
  digitalWrite(PIN_BTN_OUT, LOW);   // Transistor AN (invertierte Logik)
  delay(BTN_MS);
  digitalWrite(PIN_BTN_OUT, HIGH);  // Transistor AUS
}

// ── HTTP Handler ────────────────────────────────────────

// POST /speed  body: {"value": 75}
void handle_speed() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Kein Body\"}");
    return;
  }

  StaticJsonDocument<64> doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"error\":\"Ungültiges JSON\"}");
    return;
  }

  int speed = doc["value"] | -1;
  if (speed < 0 || speed > 100) {
    server.send(400, "application/json", "{\"error\":\"Wert muss 0-100 sein\"}");
    return;
  }

  setSpeed(speed);
  Serial.printf("Speed gesetzt: %d%%\n", speed);
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /spurwechsel
void handle_spurwechsel() {
  doSpurwechsel();
  server.send(200, "application/json", "{\"ok\":true}");
}

// GET /status
void handle_status() {
  StaticJsonDocument<128> doc;
  doc["speed"]  = aktueller_speed;
  doc["ip"]     = WiFi.softAPIP().toString();

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// 404 Handler
void handle_not_found() {
  server.send(404, "application/json", "{\"error\":\"Nicht gefunden\"}");
}

void printHeartbeat() {
  unsigned long jetzt = millis();
  if ((jetzt - letztes_heartbeat_ms) < 5000) {
    return;
  }

  letztes_heartbeat_ms = jetzt;
  Serial.printf(
    "Heartbeat: %lu ms | speed=%d%% | AP=%s | IP=%s | freeHeap=%u\n",
    jetzt,
    aktueller_speed,
    WiFi.softAPSSID().c_str(),
    WiFi.softAPIP().toString().c_str(),
    ESP.getFreeHeap()
  );
}

// ───────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  unsigned long serialStart = millis();
  while (!Serial && (millis() - serialStart) < 4000) {
    delay(10);
  }
  Serial.println();
  Serial.println("=== ESP32-C3 Boot ===");

  // PWM
  pinMode(PWM_PIN, OUTPUT);
  ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(PWM_PIN, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);

  // Transistor
  pinMode(PIN_BTN_OUT, OUTPUT);
  digitalWrite(PIN_BTN_OUT, HIGH);  // AUS (invertierte Logik)

  // Access Point starten
  if (!startAccessPoint()) {
    Serial.println("AP-Start abgebrochen. Bitte Board-Auswahl, Verkabelung und Serial-Log pruefen.");
  }

  // Endpoints registrieren
  server.on("/speed",       HTTP_POST, handle_speed);
  server.on("/spurwechsel", HTTP_POST, handle_spurwechsel);
  server.on("/status",      HTTP_GET,  handle_status);
  server.onNotFound(handle_not_found);

  server.begin();
  Serial.println("Webserver gestartet.");
}

void loop() {
  server.handleClient();  // Eingehende Requests verarbeiten
  printHeartbeat();
}