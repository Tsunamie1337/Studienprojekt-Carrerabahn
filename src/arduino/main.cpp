#include <Arduino.h>

// --- Pins ---
const int PIN_GAS        = 3;   // ADC  - Gelb
const int PIN_BTN_IN     = 0;   // Input - Grün
const int PWM_PIN        = 4;   // PWM Ausgang zur Bahn
const int PIN_BTN_OUT    = 6;   // BC547 Basis — HIGH = AN, LOW = AUS

// --- PWM ---
const int PWM_CHANNEL   = 0;
const int PWM_FREQ_HZ   = 5000;
const int PWM_RES_BITS  = 12;
const int PWM_MAX       = (1 << PWM_RES_BITS) - 1;
const int PWM_VREF_MV   = 3300;
const int PWM_TARGET_MV = 2000;
const int PWM_MAX_2V    = (PWM_MAX * PWM_TARGET_MV) / PWM_VREF_MV;

// --- Kalibrierung ---
int adc_kein_gas = 0;
int adc_vollgas  = 2010;

// --- Button Verhalten ---
const float DEADZONE           = 0.01f;
const unsigned long BTN_MS     = 80;
const unsigned long STATUS_MS  = 2000;

// --- State ---
bool          letzter_btn_zustand       = HIGH;
unsigned long letzter_wechsel           = 0;
unsigned long letzter_status            = 0;
const unsigned long SPURWECHSEL_COOLDOWN_MS = 500;
unsigned long letzter_spurwechsel       = 0;

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
  Serial.println(  "║       KALIBRIERUNG GESTARTET         ║");
  Serial.println(  "╚══════════════════════════════════════╝");

  int neuer_kein_gas = 0;
  int neuer_vollgas  = 0;

  while (true) {
    Serial.println("\n1) Trigger LOSLASSEN, dann 'y' senden");
    String antwort = warte_auf_eingabe();
    if (antwort != "y") { Serial.println("  Bitte 'y' eingeben."); continue; }
    neuer_kein_gas = lese_adc_mittelwert();
    Serial.printf("  Kein Gas ADC: %d\n", neuer_kein_gas);

    Serial.println("\n2) Trigger VOLL DRUECKEN, dann 'y' senden");
    antwort = warte_auf_eingabe();
    if (antwort != "y") { Serial.println("  Bitte 'y' eingeben."); continue; }
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
    Serial.println(  "│       KALIBRIERUNG ABGESCHLOSSEN     │");
    Serial.println(  "├──────────────────────────────────────┤");
    Serial.printf(   "│  Kein Gas  → ADC: %-5d               │\n", neuer_kein_gas);
    Serial.printf(   "│  Vollgas   → ADC: %-5d               │\n", neuer_vollgas);
    Serial.printf(   "│  Spreizung:       %-5d counts         │\n", abs(neuer_kein_gas - neuer_vollgas));
    Serial.println(  "└──────────────────────────────────────┘");
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

// ───────────────────────────────────────────────────────

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

// Überwacht den Spurwechsel-Taster und spiegelt dessen Zustand direkt auf den Transistor.
// HIGH = Transistor AN, LOW = Transistor AUS
void handle_button() {
  bool aktuell = digitalRead(PIN_BTN_IN);
  unsigned long jetzt = millis();

  if (aktuell != letzter_btn_zustand && (jetzt - letzter_wechsel) > 20) {
    letzter_btn_zustand = aktuell;
    letzter_wechsel = jetzt;

    if (aktuell == LOW && (jetzt - letzter_spurwechsel) > SPURWECHSEL_COOLDOWN_MS) {
      // Button gedrückt → Transistor AN
      digitalWrite(PIN_BTN_OUT, HIGH);
      Serial.println("Spurwechsel → Transistor an");
      letzter_spurwechsel = jetzt;
    }

    if (aktuell == HIGH) {
      // Button losgelassen → Transistor AUS
      digitalWrite(PIN_BTN_OUT, LOW);
      Serial.println("Transistor aus");
    }
  }
}

// ───────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(200);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(PIN_BTN_IN,  INPUT_PULLUP);
  pinMode(PIN_BTN_OUT, OUTPUT);
  digitalWrite(PIN_BTN_OUT, LOW);   // Ruhezustand: Transistor AUS

  ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(PWM_PIN, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);

  Serial.println("Carrera Controller bereit.");
  Serial.printf("Kein Gas=%d  Vollgas=%d\n", adc_kein_gas, adc_vollgas);
  Serial.println("Kalibrieren: 'c' senden");
}

void loop() {
  if (Serial.available()) {
    String cmd = serial_lesen();
    if (cmd == "c") {
      kalibrierung_durchfuehren();
    }
  }

  handle_button();

  int gas     = lies_gas_prozent();
  int pwm_val = map(gas, 0, 100, 0, PWM_MAX_2V);
  ledcWrite(PWM_CHANNEL, pwm_val);

  if (millis() - letzter_status >= STATUS_MS) {
    Serial.printf("Gas: %d%%  PWM: %d\n", gas, pwm_val);
    letzter_status = millis();
  }
}
