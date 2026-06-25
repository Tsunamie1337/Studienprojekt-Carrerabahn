#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// --- Pins ---
const int PIN_GAS        = 3;   // ADC-Eingang vom Handregler
const int PIN_BTN_IN     = 0;   // Spurwechsel-Taster Eingang
const int PWM_PIN        = 4;   // PWM to the track
const int PIN_BTN_OUT    = 6;   // BC547 -> Base

// --- WiFi Access Point ---
const char* AP_SSID     = "Carrera-Controller";  // Network name
const char* AP_PASSWORD = "MySecureExamplePW123";           // Password (min. 8 char)

// --- PWM ---
const int PWM_CHANNEL   = 0;
const int PWM_FREQ_HZ   = 5000;
const int PWM_RES_BITS  = 12;
const int PWM_MAX       = (1 << PWM_RES_BITS) - 1;
const int PWM_VREF_MV   = 3300;
const int PWM_TARGET_MV = 2000;
const int PWM_MAX_2V    = (PWM_MAX * PWM_TARGET_MV) / PWM_VREF_MV;

// --- Handregler-Kalibrierung ---
int adc_kein_gas = 3;      // Trigger losgelassen
int adc_vollgas  = 2027;   // Trigger voll gedrueckt
const float DEADZONE = 0.03f;

// --- Button ---
const unsigned long BTN_MS                  = 80;
const unsigned long SPURWECHSEL_COOLDOWN_MS = 500;
unsigned long letzter_spurwechsel           = 0;
bool letzter_btn_zustand                    = HIGH;
unsigned long letzter_wechsel               = 0;

// --- Serial / Status ---
const unsigned long STATUS_INTERVAL_MS = 2000;
unsigned long letztes_status_ms       = 0;

// --- State ---
int  aktueller_speed    = 0;   // 0-100%
int  aktueller_gaswert  = 0;   // 0-100%
bool spurwechsel_aktiv  = false;
unsigned long letztes_heartbeat_ms = 0;

enum class Steuerquelle {
  HANDCONTROLLER,
  WEBHOOK
};

Steuerquelle aktive_steuerquelle = Steuerquelle::HANDCONTROLLER;
unsigned long letzter_webhook_ms = 0;
const unsigned long WEBHOOK_PRIORITY_HOLD_MS = 4000;

// --- Webserver on Port 80 ---
WebServer server(80);

// ───────────────────────────────────────────────────────

String serial_lesen() {
  String input = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') continue;
    input += c;
  }
  input.trim();
  input.toLowerCase();
  return input;
}

String warte_auf_eingabe() {
  while (true) {
    delay(50);
    if (Serial.available()) {
      String s = serial_lesen();
      if (s.length() > 0) return s;
    }
  }
}

int lese_adc_mittelwert(int samples = 20, int delay_ms = 10) {
  long summe = 0;
  for (int i = 0; i < samples; i++) {
    summe += analogRead(PIN_GAS);
    delay(delay_ms);
  }
  return (int)(summe / samples);
}

void kalibrierung_durchfuehren() {
  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║       KALIBRIERUNG GESTARTET         ║");
  Serial.println("╚══════════════════════════════════════╝");

  int neuer_kein_gas = 0;
  int neuer_vollgas  = 0;

  while (true) {
    Serial.println("\n1) Trigger LOSLASSEN, dann 'y' senden");
    String antwort = warte_auf_eingabe();
    if (antwort != "y") {
      Serial.println("  Bitte 'y' eingeben.");
      continue;
    }
    neuer_kein_gas = lese_adc_mittelwert();
    Serial.printf("  Kein Gas ADC: %d\n", neuer_kein_gas);

    Serial.println("\n2) Trigger VOLL DRUECKEN, dann 'y' senden");
    antwort = warte_auf_eingabe();
    if (antwort != "y") {
      Serial.println("  Bitte 'y' eingeben.");
      continue;
    }
    neuer_vollgas = lese_adc_mittelwert();
    Serial.printf("  Vollgas ADC:  %d\n", neuer_vollgas);

    if (abs(neuer_kein_gas - neuer_vollgas) < 100) {
      Serial.println("\n⚠  Spreizung zu klein – bitte wiederholen.");
      return;
    }
    break;
  }

  while (true) {
    Serial.println("\n┌──────────────────────────────────────┐");
    Serial.println("│       KALIBRIERUNG ABGESCHLOSSEN     │");
    Serial.println("├──────────────────────────────────────┤");
    Serial.printf("│  Kein Gas  → ADC: %-5d               │\n", neuer_kein_gas);
    Serial.printf("│  Vollgas   → ADC: %-5d               │\n", neuer_vollgas);
    Serial.printf("│  Spreizung:       %-5d counts         │\n", abs(neuer_kein_gas - neuer_vollgas));
    Serial.println("└──────────────────────────────────────┘");
    Serial.println("  Werte uebernehmen? [y / n]");

    String antwort = warte_auf_eingabe();
    if (antwort == "y") {
      adc_kein_gas = neuer_kein_gas;
      adc_vollgas  = neuer_vollgas;
      Serial.println("✔  Werte uebernommen!\n");
      break;
    } else if (antwort == "n") {
      Serial.println("↺  Wiederhole...");
      kalibrierung_durchfuehren();
      return;
    } else {
      Serial.println("  Bitte 'y' oder 'n' eingeben.");
    }
  }
}

int lies_gas_prozent() {
  int roh    = analogRead(PIN_GAS);
  int spanne = adc_vollgas - adc_kein_gas;
  if (spanne == 0) return 0;

  float normiert = (float)(roh - adc_kein_gas) / (float)spanne;
  if (normiert < DEADZONE)          normiert = 0.0f;
  if (normiert > (1.0f - DEADZONE)) normiert = 1.0f;
  normiert = constrain(normiert, 0.0f, 1.0f);

  return (int)(normiert * 100.0f);
}

bool startAccessPoint() {
  IPAddress localIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  const uint8_t apChannel = 6;

  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  if (!WiFi.softAPConfig(localIP, gateway, subnet)) {
    Serial.println("Fehler: softAPConfig fehlgeschlagen");
    return false;
  }

  if (!WiFi.softAP(AP_SSID, AP_PASSWORD, apChannel, false, 4)) {
    Serial.println("Fehler: softAP konnte nicht gestartet werden");
    return false;
  }

  Serial.printf("Chip:         %s Rev.%d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("Access Point: %s\n", AP_SSID);
  Serial.printf("Kanal:        %u\n", apChannel);
  Serial.printf("IP Adresse:   %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("AP MAC:       %s\n", WiFi.softAPmacAddress().c_str());
  return true;
}

// Sets the speed per PWM
void setSpeed(int percent) {
  percent = constrain(percent, 0, 100);
  aktueller_speed = percent;
  int pwm_val = map(percent, 0, 100, 0, PWM_MAX_2V);
  ledcWrite(PWM_CHANNEL, pwm_val);
}

void updateHandreglerSpeed() {
  if (aktive_steuerquelle == Steuerquelle::WEBHOOK &&
      (millis() - letzter_webhook_ms) < WEBHOOK_PRIORITY_HOLD_MS) {
    return;
  }

  aktive_steuerquelle = Steuerquelle::HANDCONTROLLER;
  aktueller_gaswert = lies_gas_prozent();
  setSpeed(aktueller_gaswert);
}

// Lane switch
void doSpurwechsel() {
  unsigned long jetzt = millis();
  if ((jetzt - letzter_spurwechsel) < SPURWECHSEL_COOLDOWN_MS) return;
  letzter_spurwechsel = jetzt;

  Serial.println("Spurwechsel ausgelöst");
  digitalWrite(PIN_BTN_OUT, LOW);   // Transistor ON (invertierte Logik)
  delay(BTN_MS);
  digitalWrite(PIN_BTN_OUT, HIGH);  // Transistor OFF
}

void handle_button_input() {
  bool aktuell = digitalRead(PIN_BTN_IN);
  unsigned long jetzt = millis();

  if (aktuell != letzter_btn_zustand && (jetzt - letzter_wechsel) > 20) {
    letzter_btn_zustand = aktuell;
    letzter_wechsel = jetzt;

    if (aktuell == LOW && (jetzt - letzter_spurwechsel) > SPURWECHSEL_COOLDOWN_MS) {
      Serial.println("Spurwechsel -> Transistor aus");
      digitalWrite(PIN_BTN_OUT, LOW);
      letzter_spurwechsel = jetzt;
    }

    if (aktuell == HIGH) {
      digitalWrite(PIN_BTN_OUT, HIGH);
      Serial.println("Transistor an");
    }
  }
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
  aktive_steuerquelle = Steuerquelle::WEBHOOK;
  letzter_webhook_ms = millis();
  Serial.printf("Speed gesetzt: %d%%\n", speed);
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /laneswitch
void handle_spurwechsel() {
  doSpurwechsel();
  server.send(200, "application/json", "{\"ok\":true}");
}

// GET /status
void handle_status() {
  StaticJsonDocument<128> doc;
  doc["speed"]  = aktueller_speed;
  doc["gas"]    = aktueller_gaswert;
  doc["source"] = aktive_steuerquelle == Steuerquelle::WEBHOOK ? "webhook" : "handcontroller";
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
    "Heartbeat: %lu ms | gas=%d%% | speed=%d%% | AP=%s | IP=%s | freeHeap=%u\n",
    jetzt,
    aktueller_gaswert,
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

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(PIN_GAS, INPUT);
  pinMode(PIN_BTN_IN, INPUT_PULLUP);

  // PWM
  pinMode(PWM_PIN, OUTPUT);
  ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(PWM_PIN, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);

  // Transistor
  pinMode(PIN_BTN_OUT, OUTPUT);
  digitalWrite(PIN_BTN_OUT, HIGH);  // AUS (invertierte Logik)

  Serial.println("Carrera Handregler bereit.");
  Serial.printf("Standardwerte: Kein Gas=%d  Vollgas=%d\n", adc_kein_gas, adc_vollgas);
  Serial.println("Zum Kalibrieren: 'c' senden");

  // Start Access Point
  if (!startAccessPoint()) {
    Serial.println("AP-Start abgebrochen. Bitte Board-Auswahl, Verkabelung und Serial-Log pruefen.");
  }

  // Registrier endpoints
  server.on("/speed",       HTTP_POST, handle_speed);
  server.on("/spurwechsel", HTTP_POST, handle_spurwechsel);
  server.on("/status",      HTTP_GET,  handle_status);
  server.onNotFound(handle_not_found);

  server.begin();
  Serial.println("Webserver gestartet.");
}

void loop() {
  if (Serial.available()) {
    String cmd = serial_lesen();
    if (cmd == "c") {
      kalibrierung_durchfuehren();
    } else if (cmd == "h") {
      aktive_steuerquelle = Steuerquelle::HANDCONTROLLER;
      Serial.println("Steuerquelle: Handcontroller");
    }
  }

  updateHandreglerSpeed();
  server.handleClient();  // Handle incomin requests
  handle_button_input();
  printHeartbeat();

  unsigned long jetzt = millis();
  if (jetzt - letztes_status_ms >= STATUS_INTERVAL_MS) {
    Serial.printf("Gas: %d%%  PWM: %d\n", aktueller_gaswert, map(aktueller_speed, 0, 100, 0, PWM_MAX_2V));
    letztes_status_ms = jetzt;
  }
}