#include <Arduino.h>

// --- Pins ---
const int PIN_GAS        = 3;
const int PIN_BTN_IN     = 0;
const int PWM_PIN        = 4;
const int PIN_BTN_OUT    = 6;

// --- PWM ---
const int PWM_CHANNEL   = 0;
const int PWM_FREQ_HZ   = 5000;
const int PWM_RES_BITS  = 12;
const int PWM_MAX       = (1 << PWM_RES_BITS) - 1;
const int PWM_VREF_MV   = 3300;
const int PWM_TARGET_MV = 2000;
const int PWM_MAX_2V    = (PWM_MAX * PWM_TARGET_MV) / PWM_VREF_MV;

// --- Handregler-Kalibrierung ---
int adc_kein_gas = 3;
int adc_vollgas  = 2027;
const float DEADZONE = 0.03f;

// --- Button ---
// ԣŴ��  GE+�NDERT: BTN_MS entfernt (war nur f++r altes delay-Protokoll relevant)
const unsigned long SPURWECHSEL_COOLDOWN_MS = 500;
unsigned long letzter_spurwechsel           = 0;
bool letzter_btn_zustand                    = HIGH;
unsigned long letzter_wechsel               = 0;

// --- Serial / Status ---
const unsigned long STATUS_INTERVAL_MS = 2000;
unsigned long letztes_status_ms        = 0;

// --- State ---
int  aktueller_speed    = 0;
int  aktueller_gaswert  = 0;

// ԣŴ��  GE+�NDERT: Heartbeat-Intervall angepasst
unsigned long letztes_heartbeat_ms = 0;

enum class Steuerquelle {
  SOFTWARE,
  HANDCONTROLLER
};

Steuerquelle aktive_steuerquelle        = Steuerquelle::SOFTWARE;
// ԣŴ��  GE+�NDERT: letzter_serieller_speed_wert bleibt f++r Deduplizierung
int letzter_serieller_speed_wert        = -1;

// ���������������������������������������������������������������������������������������������������������������������������������������������������������������������

// ԣŴ��  GE+�NDERT: serial_lesen() entfernt ��� Protokoll ist jetzt frame-basiert
// ԣŴ��  GE+�NDERT: warte_auf_eingabe() bleibt nur f++r Kalibrierung

String warte_auf_eingabe() {
  String buf = "";
  while (true) {
    delay(50);
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        buf.trim();
        if (buf.length() > 0) return buf;
        buf = "";
      } else {
        buf += c;
      }
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
  Serial.println("\n������������������������������������������������������������������������������������������������������������������������");
  Serial.println("���       KALIBRIERUNG GESTARTET         ���");
  Serial.println("������������������������������������������������������������������������������������������������������������������������");

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
      Serial.println("\n���  Spreizung zu klein ��� bitte wiederholen.");
      return;
    }
    break;
  }

  while (true) {
    Serial.println("\n������������������������������������������������������������������������������������������������������������������������");
    Serial.println("���       KALIBRIERUNG ABGESCHLOSSEN     ���");
    Serial.println("������������������������������������������������������������������������������������������������������������������������");
    Serial.printf("���  Kein Gas  ��� ADC: %-5d               ���\n", neuer_kein_gas);
    Serial.printf("���  Vollgas   ��� ADC: %-5d               ���\n", neuer_vollgas);
    Serial.printf("���  Spreizung:       %-5d counts         ���\n", abs(neuer_kein_gas - neuer_vollgas));
    Serial.println("������������������������������������������������������������������������������������������������������������������������");
    Serial.println("  Werte uebernehmen? [y / n]");

    String antwort = warte_auf_eingabe();
    if (antwort == "y") {
      adc_kein_gas = neuer_kein_gas;
      adc_vollgas  = neuer_vollgas;
      Serial.println("ԣ�  Werte uebernommen!\n");
      break;
    } else if (antwort == "n") {
      Serial.println("��  Wiederhole...");
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

const char* steuerquelle_als_text(Steuerquelle quelle) {
  return quelle == Steuerquelle::HANDCONTROLLER ? "handcontroller" : "software";
}

// ԣŴ��  GE+�NDERT: Protokoll ��� sendet "Vxxx\n" (zero-padded, 3 Ziffern)
void sende_fahrdaten(int speed) {
  speed = constrain(speed, 0, 100);
  if (speed == letzter_serieller_speed_wert) return;
  letzter_serieller_speed_wert = speed;
  Serial.printf("V%03d\n", speed);
}

void setSpeed(int percent) {
  percent = constrain(percent, 0, 100);
  aktueller_speed = percent;
  int pwm_val = map(percent, 0, 100, 0, PWM_MAX_2V);
  ledcWrite(PWM_CHANNEL, pwm_val);
}

void updateHandreglerSpeed() {
  if (aktive_steuerquelle == Steuerquelle::SOFTWARE) return;
  aktueller_gaswert = lies_gas_prozent();
  setSpeed(aktueller_gaswert);
  sende_fahrdaten(aktueller_gaswert);
}

void doSpurwechsel() {
  unsigned long jetzt = millis();
  if ((jetzt - letzter_spurwechsel) < SPURWECHSEL_COOLDOWN_MS) return;
  letzter_spurwechsel = jetzt;

  // Diagnostic: report PIN_BTN_OUT and PWM duty before/after pulse
  int btn_before = digitalRead(PIN_BTN_OUT);
  int pwm_val_before = map(aktueller_speed, 0, 100, 0, PWM_MAX_2V);
  Serial.printf("# DBG L start t=%lu btn_before=%d pwm_val=%d speed=%d\n", jetzt, btn_before, pwm_val_before, aktueller_speed);

  Serial.printf("# L start t=%lu\n", jetzt);
  // Try to reduce motor load briefly to help the mechanical switch
  int pwm_current_val = map(aktueller_speed, 0, 100, 0, PWM_MAX_2V);
  int reduced_percent = min(20, aktueller_speed); // do not increase speed
  int pwm_reduced_val = map(reduced_percent, 0, 100, 0, PWM_MAX_2V);

  if (pwm_reduced_val < pwm_current_val) {
    Serial.printf("# DBG L reduce pwm from %d to %d\n", pwm_current_val, pwm_reduced_val);
    ledcWrite(PWM_CHANNEL, pwm_reduced_val);
  }

  digitalWrite(PIN_BTN_OUT, LOW);
  delay(80);
  digitalWrite(PIN_BTN_OUT, HIGH);

  // keep reduced PWM briefly so mechanism finishes under lower load
  delay(120);

  if (pwm_reduced_val < pwm_current_val) {
    ledcWrite(PWM_CHANNEL, pwm_current_val);
    Serial.printf("# DBG L restore pwm to %d\n", pwm_current_val);
  }

  int btn_after = digitalRead(PIN_BTN_OUT);
  int pwm_val_after = map(aktueller_speed, 0, 100, 0, PWM_MAX_2V);
  Serial.printf("# L end t=%lu btn_after=%d pwm_val=%d speed=%d\n", millis(), btn_after, pwm_val_after, aktueller_speed);
}

void handle_button_input() {
  bool aktuell = digitalRead(PIN_BTN_IN);
  unsigned long jetzt = millis();

  if (aktuell != letzter_btn_zustand && (jetzt - letzter_wechsel) > 20) {
    letzter_btn_zustand = aktuell;
    letzter_wechsel = jetzt;

    if (aktuell == LOW && (jetzt - letzter_spurwechsel) > SPURWECHSEL_COOLDOWN_MS) {
      digitalWrite(PIN_BTN_OUT, LOW);
      letzter_spurwechsel = jetzt;
      // ԣŴ��  GE+�NDERT: Sendet "L\n" statt Klartext-Log f++r Spurwechsel-Event
      Serial.println("L");
    }

    if (aktuell == HIGH) {
      digitalWrite(PIN_BTN_OUT, HIGH);
    }
  }
}

// ԣŴ��  GE+�NDERT: handle_serial_command() komplett ersetzt durch frame_parser()
//              Liest exakt 4 Bytes pro Frame: 1 Typ-Byte + 3 Daten-Bytes + '\n'
void frame_parser() {
  // Puffer h+�lt einen unvollst+�ndigen Frame zwischen loop()-Aufrufen
  static char buf[8];
  static int  buf_len = 0;

  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n') {
      buf[buf_len] = '\0';

      if (buf_len == 4 && buf[0] == 'V') {
        // Geschwindigkeits-Frame: "Vxxx"
        int speed = (buf[1] - '0') * 100
                  + (buf[2] - '0') * 10
                  + (buf[3] - '0');

        if (speed >= 0 && speed <= 100 && aktive_steuerquelle == Steuerquelle::SOFTWARE) {
          aktueller_gaswert = speed;
          setSpeed(speed);
          Serial.printf("# RX V%03d accepted mode=software\n", speed);
        } else {
          Serial.printf("# RX V%03d ignored mode=%s\n", speed, steuerquelle_als_text(aktive_steuerquelle));
        }

      } else if (buf_len == 1 && buf[0] == 'L') {
        // Lane-Switch-Frame: "L"
        Serial.printf("# RX L mode=%s\n", steuerquelle_als_text(aktive_steuerquelle));
        doSpurwechsel();

      } else if (buf_len == 1 && buf[0] == 'C') {
        // Modus: Handcontroller
        aktive_steuerquelle = Steuerquelle::HANDCONTROLLER;
        letzter_serieller_speed_wert = -1;
        Serial.println("# MODE HANDCONTROLLER");

      } else if (buf_len == 1 && buf[0] == 'S') {
        // Modus: Software
        aktive_steuerquelle = Steuerquelle::SOFTWARE;
        Serial.println("# MODE SOFTWARE");

      } else if (buf_len == 1 && buf[0] == 'K') {
        // Kalibrierung
        Serial.println("# RX K");
        kalibrierung_durchfuehren();
      }

      buf_len = 0;

    } else {
      if (buf_len < (int)(sizeof(buf) - 1)) {
        buf[buf_len++] = c;
      } else {
        // Puffer-+�berlauf ��� Frame verwerfen
        buf_len = 0;
      }
    }
  }
}

void printHeartbeat() {
  unsigned long jetzt = millis();
  if ((jetzt - letztes_heartbeat_ms) < 5000) return;
  letztes_heartbeat_ms = jetzt;
  Serial.printf(
    "# HB %lu gas=%d spd=%d src=%s heap=%u\n",
    jetzt, aktueller_gaswert, aktueller_speed,
    steuerquelle_als_text(aktive_steuerquelle),
    ESP.getFreeHeap()
  );
}

// ���������������������������������������������������������������������������������������������������������������������������������������������������������������������

void setup() {
  Serial.begin(115200);
  unsigned long serialStart = millis();
  while (!Serial && (millis() - serialStart) < 4000) delay(10);

  Serial.println("# ESP32-C3 Boot");

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(PIN_GAS,     INPUT);
  pinMode(PIN_BTN_IN,  INPUT_PULLUP);
  pinMode(PWM_PIN,     OUTPUT);
  pinMode(PIN_BTN_OUT, OUTPUT);

  ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(PWM_PIN, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);

  digitalWrite(PIN_BTN_OUT, HIGH);

  Serial.println("# Carrera bereit");
}

void loop() {
  // ԣŴ��  GE+�NDERT: Serial.available()-Block ruft jetzt frame_parser() auf
  frame_parser();

  updateHandreglerSpeed();
  handle_button_input();
  printHeartbeat();

  unsigned long jetzt = millis();
  if (jetzt - letztes_status_ms >= STATUS_INTERVAL_MS) {
    // ԣŴ��  GE+�NDERT: Statuszeile mit '#'-Prefix damit Python sie ignoriert
    Serial.printf("# gas=%d%% spd=%d%%\n", aktueller_gaswert, aktueller_speed);
    letztes_status_ms = jetzt;
  }
}
