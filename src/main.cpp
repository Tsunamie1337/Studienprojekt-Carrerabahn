#include <Arduino.h>

// --- Pins ---
const int PIN_GAS        = 3;   // ADC  - Gelb
const int PIN_BTN_IN     = 0;   // Input - Grün
const int PWM_PIN        = 4;   // PWM Ausgang zur Bahn
const int PIN_BTN_OUT    = 6;   // BC547 Basis

// --- PWM ---
const int PWM_CHANNEL   = 0;    // PWM_CHANNEL: PWM-Kanal (ESP32-spezifisch)
const int PWM_FREQ_HZ   = 5000; // TODO! Warum gehen nur 5K??
const int PWM_RES_BITS  = 12;   // Auflösung des PWM bits (siehe kluge Mathe)
const int PWM_MAX       = (1 << PWM_RES_BITS) - 1;  // PWM_MAX: Maximalwert für PWM (4095 bei 12 Bit)
const int PWM_VREF_MV   = 3300; // PWM_VREF_MV: Referenzspannung des ADC/PWM in mV (meist 3.3V → 3300mV)
const int PWM_TARGET_MV = 2000; // PWM_TARGET_MV: Ziel-Maximalwert für die Bahn (z.B. 2V → 2000mV)
const int PWM_MAX_2V    = (PWM_MAX * PWM_TARGET_MV) / PWM_VREF_MV; // PWM_MAX_2V: Maximaler PWM-Wert, der 2V entspricht (Skalierung von 3.3V auf 2V)

// --- Kalibrierung ---
int adc_kein_gas = 0;     // Durch messen gefunden (No press)
int adc_vollgas  = 2010;  // Durch messen gefunden (Max press)

// --- Button Verhalten ---
const float DEADZONE           = 0.01f; // PWM_MAX_2V: Maximaler PWM-Wert, der 2V entspricht (Skalierung von 3.3V auf 2V)
const unsigned long BTN_MS     = 80;    // BTN_MS: Wie lange der Spurwechsel-Transistor aktiviert bleibt (in ms)
const unsigned long STATUS_MS  = 2000;  // STATUS_MS: Intervall für Statusausgabe auf die serielle Konsole (in ms)

// --- State ---
bool     letzter_btn_zustand            = HIGH; // letzter_btn_zustand: Merkt sich den letzten Zustand des Spurwechsel-Tasters
unsigned long letzter_wechsel           = 0;    // letzter_wechsel: Zeitstempel des letzten Zustandswechsels (Entprellung)
unsigned long letzter_status            = 0;    // letzter_status: Zeitstempel der letzten Statusausgabe
const unsigned long SPURWECHSEL_COOLDOWN_MS = 500; // SPURWECHSEL_COOLDOWN_MS: Mindestzeit zwischen zwei Spurwechseln
unsigned long letzter_spurwechsel       = 0;    // letzter_spurwechsel: Zeitstempel des letzten Spurwechsels (Cooldown)

// ───────────────────────────────────────────────────────

// Liest alle verfügbaren Zeichen von der seriellen Schnittstelle und gibt sie als String zurück (ohne Zeilenumbrüche, alles klein)
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

// Wartet, bis eine Eingabe über die serielle Schnittstelle erfolgt, und gibt diese zurück
String warte_auf_eingabe() {
  while (true) {
    delay(50);
    if (Serial.available()) {
      String s = serial_lesen();
      if (s.length() > 0) return s;
    }
  }
}

// Liest den Mittelwert von mehreren ADC-Messungen am Gaspin (für stabile Werte)
// samples: Anzahl der Messungen, delay_ms: Wartezeit zwischen Messungen
int lese_adc_mittelwert(int samples = 20, int delay_ms = 10) {
  long summe = 0;
  for (int i = 0; i < samples; i++) {
    summe += analogRead(PIN_GAS);
    delay(delay_ms);
  }
  return (int)(summe / samples);
}

// Führt die Kalibrierung für die Gasstellung durch:
// 1. Nutzer lässt Gas los und bestätigt → adc_kein_gas wird gemessen
// 2. Nutzer drückt Vollgas und bestätigt → adc_vollgas wird gemessen
// 3. Werte werden übernommen, wenn Spreizung ausreichend
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

// Gibt die aktuelle Gasstellung in Prozent (0-100%) zurück, basierend auf den kalibrierten ADC-Werten
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
// Solange der Button gedrückt ist bleibt der Transistor durchgehend aktiv.
// Entprellung per Zeitfenster und Cooldown verhindern Mehrfachauslösung.
void handle_button() {
  bool aktuell = digitalRead(PIN_BTN_IN);
  unsigned long jetzt = millis();

  if (aktuell != letzter_btn_zustand && (jetzt - letzter_wechsel) > 20) {
    letzter_btn_zustand = aktuell;
    letzter_wechsel = jetzt;

    if (aktuell == LOW && (jetzt - letzter_spurwechsel) > SPURWECHSEL_COOLDOWN_MS) {
      // Button gedrückt → Transistor AUS (invertierte Logik)
      Serial.println("Spurwechsel → Transistor aus");
      digitalWrite(PIN_BTN_OUT, LOW);
      letzter_spurwechsel = jetzt;
    }

    if (aktuell == HIGH) {
      // Button losgelassen → Transistor AN (invertierte Logik)
      digitalWrite(PIN_BTN_OUT, HIGH);
      Serial.println("Transistor an");
    }
  }
}

// ───────────────────────────────────────────────────────

// Initialisiert alle Pins, PWM, serielle Schnittstelle und gibt Startmeldung aus
void setup() {
  Serial.begin(115200);
  delay(200);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(PIN_BTN_IN,  INPUT_PULLUP);
  pinMode(PIN_BTN_OUT, OUTPUT);
  digitalWrite(PIN_BTN_OUT, HIGH);

  ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(PWM_PIN, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);

  Serial.println("Carrera Controller bereit.");
  Serial.printf("Kein Gas=%d  Vollgas=%d\n", adc_kein_gas, adc_vollgas);
  Serial.println("Kalibrieren: 'c' senden");
}

// Hauptprogrammschleife:
// - Prüft auf serielle Kommandos (z.B. Kalibrierung)
// - Liest und verarbeitet Gasstellung
// - Steuert PWM-Ausgang
// - Handhabt Spurwechsel-Taster
// - Gibt regelmäßig Status aus
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