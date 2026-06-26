#include <Arduino.h>

// --- Pins ---
const int PIN_GAS        = 3;   // ADC-Eingang vom Handregler
const int PIN_BTN_IN     = 0;   // Spurwechsel-Taster Eingang
const int PWM_PIN        = 4;   // PWM to the track
const int PIN_BTN_OUT    = 6;   // BC547 -> Base

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
  SERIAL
};

Steuerquelle aktive_steuerquelle = Steuerquelle::HANDCONTROLLER;

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
  Serial.println("Serielle Steuerung aktiv. WLAN/Webserver deaktiviert.");
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
  if (aktive_steuerquelle == Steuerquelle::SERIAL) {
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

void handle_serial_command(String cmd) {
  cmd.trim();
  cmd.toLowerCase();

  if (cmd.length() == 0) {
    return;
  }

  if (cmd == "c") {
    kalibrierung_durchfuehren();
    return;
  }

  if (cmd == "h") {
    aktive_steuerquelle = Steuerquelle::HANDCONTROLLER;
    Serial.println("OK HANDCONTROLLER");
    return;
  }

  if (cmd == "l" || cmd == "lane" || cmd == "spur" || cmd == "spurwechsel") {
    aktive_steuerquelle = Steuerquelle::SERIAL;
    doSpurwechsel();
    Serial.println("OK LANE");
    return;
  }

  int speed = -1;
  if (cmd.startsWith("s:" ) || cmd.startsWith("s=")) {
    speed = cmd.substring(2).toInt();
  } else if (cmd.startsWith("speed:" ) || cmd.startsWith("speed=")) {
    speed = cmd.substring(6).toInt();
  } else if (cmd.startsWith("s ") || cmd.startsWith("speed ")) {
    speed = cmd.substring(cmd.indexOf(' ') + 1).toInt();
  } else if (cmd.startsWith("s")) {
    speed = cmd.substring(1).toInt();
  }

  if (speed >= 0 && speed <= 100) {
    aktive_steuerquelle = Steuerquelle::SERIAL;
    aktueller_gaswert = speed;
    setSpeed(speed);
    Serial.printf("OK SPEED %d%%\n", speed);
    return;
  }

  Serial.printf("ERR UNKNOWN COMMAND: %s\n", cmd.c_str());
}

void printHeartbeat() {
  unsigned long jetzt = millis();
  if ((jetzt - letztes_heartbeat_ms) < 5000) {
    return;
  }

  letztes_heartbeat_ms = jetzt;
  Serial.printf(
    "Heartbeat: %lu ms | gas=%d%% | speed=%d%% | source=%s | freeHeap=%u\n",
    jetzt,
    aktueller_gaswert,
    aktueller_speed,
    aktive_steuerquelle == Steuerquelle::SERIAL ? "serial" : "handcontroller",
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
  Serial.println("Serielle Befehle: s<0-100>, l, h, c");

  startAccessPoint();
}

void loop() {
  if (Serial.available()) {
    String cmd = serial_lesen();
    handle_serial_command(cmd);
  }

  updateHandreglerSpeed();
  handle_button_input();
  printHeartbeat();

  unsigned long jetzt = millis();
  if (jetzt - letztes_status_ms >= STATUS_INTERVAL_MS) {
    Serial.printf("Gas: %d%%  PWM: %d\n", aktueller_gaswert, map(aktueller_speed, 0, 100, 0, PWM_MAX_2V));
    letztes_status_ms = jetzt;
  }
}