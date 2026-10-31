/* ===================================================================
   ZCD KARTI  —  KABLOLAMA TESTI          (Fiziksel Adim 3.1)

   AMAC : Kontrol tarafinin (10k pull-up + header + Arduino kablosu)
          dogru baglandigini, guc tarafina hic dokunmadan dogrulamak.

   SART : TRAFO BAGLI DEGIL.  Kartin R/S/T/N klemenslerinde
          hicbir kablo yok.  Devrede sadece 5 V var.

   BAGLANTI
        ZCD header        Arduino Uno
        ----------        -----------
        5V           -->  5V
        GND          -->  GND
        ZCD_R        -->  A1
        ZCD_S        -->  A2
        ZCD_T        -->  A3

   BEKLENEN : R=1  S=1  T=1   ve  gerilimler ~5.0 V
   SEBEP    : AC yok -> LTV LED'leri sonuk -> fototransistorler kesimde
              -> 10k pull-up'lar hatlari 5 V'a cekiyor.
   =================================================================== */

const uint8_t PIN_R = A1;
const uint8_t PIN_S = A2;
const uint8_t PIN_T = A3;

const float V_MIN = 4.3;      // saglikli HIGH icin alt sinir
const float V_MAX = 5.3;

uint16_t okCount   = 0;
uint16_t failCount = 0;
bool     reported  = false;

float volts(uint8_t pin) {
  return analogRead(pin) * 5.0f / 1023.0f;
}

void setup() {
  // DIKKAT: INPUT_PULLUP KULLANMA. Harici 10k pull-up var;
  // dahili pull-up acilirsa eksik/kopuk harici direnci gizler.
  pinMode(PIN_R, INPUT);
  pinMode(PIN_S, INPUT);
  pinMode(PIN_T, INPUT);

  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("   ZCD KARTI - KABLOLAMA TESTI   (Adim 3.1)"));
  Serial.println(F("================================================"));
  Serial.println(F(" UYARI : TRAFO BAGLI OLMAMALI. Sadece 5 V."));
  Serial.println(F(" BEKLENEN : R=1 S=1 T=1  ve  ~5.0 V"));
  Serial.println(F("------------------------------------------------"));
  delay(1000);
}

void loop() {
  uint8_t r = digitalRead(PIN_R);
  uint8_t s = digitalRead(PIN_S);
  uint8_t t = digitalRead(PIN_T);

  float vr = volts(PIN_R);
  float vs = volts(PIN_S);
  float vt = volts(PIN_T);

  Serial.print(F("R="));  Serial.print(r);
  Serial.print(F(" ("));  Serial.print(vr, 2); Serial.print(F("V)"));
  Serial.print(F("   S=")); Serial.print(s);
  Serial.print(F(" ("));  Serial.print(vs, 2); Serial.print(F("V)"));
  Serial.print(F("   T=")); Serial.print(t);
  Serial.print(F(" ("));  Serial.print(vt, 2); Serial.print(F("V)"));
  Serial.print(F("   |  "));

  bool levelsOk = (r == 1 && s == 1 && t == 1);
  bool voltsOk  = (vr > V_MIN && vr < V_MAX) &&
                  (vs > V_MIN && vs < V_MAX) &&
                  (vt > V_MIN && vt < V_MAX);

  if (levelsOk && voltsOk) {
    okCount++; failCount = 0;
    Serial.print(F("OK  ")); Serial.println(okCount);

    if (okCount == 20 && !reported) {
      reported = true;
      Serial.println(F("------------------------------------------------"));
      Serial.println(F(" >> GECTI"));
      Serial.println(F("    Pull-up'lar, header ve Arduino kablosu dogru."));
      Serial.println(F("    TEST 3.2'ye gecebilirsin (trafo baglanacak)."));
      Serial.println(F("------------------------------------------------"));
    }
  } else {
    failCount++; okCount = 0; reported = false;
    Serial.println(F("HATA"));

    if (failCount == 3) {
      Serial.println(F("------------------------------------------------"));
      Serial.println(F(" >> KALDI - kontrol listesi:"));

      if (r == 0 || vr < V_MIN) Serial.println(F("    R : LTV pin16 / 10k / ZCD_R teli"));
      if (s == 0 || vs < V_MIN) Serial.println(F("    S : LTV pin14 / 10k / ZCD_S teli"));
      if (t == 0 || vt < V_MIN) Serial.println(F("    T : LTV pin12 / 10k / ZCD_T teli"));

      if (r == 0 && s == 0 && t == 0)
        Serial.println(F("    Hepsi 0 -> 5V / GND ters baglanmis olabilir"));

      if ((vr > 2.0 && vr < 4.3) || (vs > 2.0 && vs < 4.3) || (vt > 2.0 && vt < 4.3))
        Serial.println(F("    2-4 V arasi -> gerilim bolunuyor, yanlis tel/direnc"));

      Serial.println(F("    Olcum: header 5V<->pin16 = 10k ?"));
      Serial.println(F("           header 5V<->GND   = OL ?"));
      Serial.println(F("------------------------------------------------"));
    }
  }

  delay(500);
}