/* =================================================================
   B6 TAM KONTROLLU DOGRULTUCU  —  ATESLEME ZINCIRI TESTI
   Fiziksel Adim 2   |   SADECE 5 V.  GUC KATI BAGLI DEGIL.
   ================================================================= */

const uint8_t firePin[6]  = {  5,      6,      7,      8,      9,     10   };
const char*   fireName[6] = { "T1 R+","T2 T-","T3 S+","T4 R-","T5 T+","T6 S-" };

/* ---- TEST MODU SEC ----
   0 = YAVAS  : saniyede bir adim, LED'leri gozle takip et
   1 = GERCEK : 50 Hz, her 60 derecede bir (3333 us) - osiloskop icin
   2 = TUT    : tek kanali surekli ACIK tut (akim olcumu icin)      */
#define TEST_MODE   2
#define HOLD_CH     0          // MODE 2 icin kanal: 0..5  (0 = T1)

const uint16_t SLOW_ON_MS  = 400;
const uint16_t SLOW_OFF_MS = 600;
const uint16_t STEP_US     = 3333;   // 60 derece @ 50 Hz
const uint16_t PULSE_US    = 1000;   // test darbe genisligi

void setup() {
  for (uint8_t i = 0; i < 6; i++) {
    digitalWrite(firePin[i], LOW);   // once LOW yaz...
    pinMode(firePin[i], OUTPUT);     // ...sonra cikis yap (guvenli acilis)
  }
  Serial.begin(115200);
  Serial.println(F("=== Atesleme zinciri testi ==="));
  Serial.print  (F("Mod: ")); Serial.println(TEST_MODE);
  Serial.println(F("UYARI: guc kati bagli OLMAMALI"));
  delay(1000);
}

void loop() {
#if TEST_MODE == 2
  digitalWrite(firePin[HOLD_CH], HIGH);
  Serial.print(F("SUREKLI ACIK -> ")); Serial.println(fireName[HOLD_CH]);
  delay(2000);

#else
  for (uint8_t i = 0; i < 6; i++) {
  #if TEST_MODE == 0
    Serial.print(i + 1); Serial.print(F(" -> ")); Serial.println(fireName[i]);
    digitalWrite(firePin[i], HIGH);
    delay(SLOW_ON_MS);
    digitalWrite(firePin[i], LOW);
    delay(SLOW_OFF_MS);
  #else
    digitalWrite(firePin[i], HIGH);
    delayMicroseconds(PULSE_US);
    digitalWrite(firePin[i], LOW);
    delayMicroseconds(STEP_US - PULSE_US);
  #endif
  }
#endif
}