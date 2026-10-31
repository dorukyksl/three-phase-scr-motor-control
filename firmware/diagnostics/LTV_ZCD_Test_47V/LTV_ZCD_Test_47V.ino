/* ===================================================================
   ZCD KARTI - KARAKTERIZASYON            (Fiziksel Adim 3.2)

   Kart : LTV-844 + 3 x (15k + 1N4007), faz-notr, 47 V yildiz
   Pin  : ZCD_R=A1  ZCD_S=A2  ZCD_T=A3

   OLCTUKLERI
     A) Her kanal : tLOW, tHIGH, periyot, frekans, olu bant
     B) Faz iliskisi : R'nin dusen kenarina gore S ve T gecikmesi
     C) 6 sektorlu mutlak faz kodu (B6'nin zamanlama cekirdegi)
   =================================================================== */

const uint8_t PN[3] = { A1, A2, A3 };
const char*   NM[3] = { "ZCD_R", "ZCD_S", "ZCD_T" };
const uint8_t NS    = 16;

// ---- sektor kodu (R<<2 | S<<1 | T) -> hangi tristor ateslenecek ----
const __FlashStringHelper* sectorName(uint8_t c) {
  switch (c) {
    case 0b010: return F("S1 -> T1  R+");
    case 0b011: return F("S2 -> T2  T-");
    case 0b001: return F("S3 -> T3  S+");
    case 0b101: return F("S4 -> T4  R-");
    case 0b100: return F("S5 -> T5  T+");
    case 0b110: return F("S6 -> T6  S-");
    default:    return F("GECERSIZ !!");
  }
}

// ---------- A) tek kanal olcumu ----------
bool measChan(uint8_t p, float &tH, float &tL) {
  uint32_t sH = 0, sL = 0; uint8_t ok = 0;
  for (uint8_t i = 0; i < NS; i++) {
    uint32_t h = pulseIn(p, HIGH, 60000UL);
    uint32_t l = pulseIn(p, LOW , 60000UL);
    if (h && l) { sH += h; sL += l; ok++; }
  }
  if (ok < NS / 2) return false;
  tH = sH / (float)ok / 1000.0f;
  tL = sL / (float)ok / 1000.0f;
  return true;
}

// ---------- R'nin dusen kenarini bekle ----------
bool waitFallR(uint32_t &t) {
  uint32_t g = micros();
  while (digitalRead(A1) == LOW ) { if (micros() - g > 60000UL) return false; }
  while (digitalRead(A1) == HIGH) { if (micros() - g > 60000UL) return false; }
  t = micros();
  return true;
}

// ---------- B) faz iliskisi ----------
bool measPhase(float &dS, float &dT) {
  uint32_t tR;
  if (!waitFallR(tR)) return false;
  uint32_t eS = 0, eT = 0;
  bool pS = digitalRead(A2), pT = digitalRead(A3);
  while (micros() - tR < 25000UL) {
    bool cS = digitalRead(A2), cT = digitalRead(A3);
    if (pS && !cS && !eS) eS = micros();
    if (pT && !cT && !eT) eT = micros();
    pS = cS; pT = cT;
  }
  if (!eS || !eT) return false;
  dS = (eS - tR) / 1000.0f;
  dT = (eT - tR) / 1000.0f;
  return true;
}

// ---------- C) sektor dizisi ----------
void showSectors() {
  uint32_t t0;
  if (!waitFallR(t0)) { Serial.println(F("  sektor olcumu basarisiz")); return; }

  uint8_t  code[12]; uint32_t tt[12]; uint8_t n = 0;
  uint8_t  prev = 0xFF;
  while (micros() - t0 < 21000UL && n < 12) {
    uint8_t st = (digitalRead(A1) << 2) | (digitalRead(A2) << 1) | digitalRead(A3);
    if (st != prev) { code[n] = st; tt[n] = micros() - t0; n++; prev = st; }
  }

  Serial.println(F("   t(ms)   R S T    sektor"));
  for (uint8_t i = 0; i < n; i++) {
    float ms = tt[i] / 1000.0f;
    Serial.print(F("   "));
    if (ms < 10) Serial.print(' ');
    Serial.print(ms, 2);
    Serial.print(F("    "));
    Serial.print((code[i] >> 2) & 1); Serial.print(' ');
    Serial.print((code[i] >> 1) & 1); Serial.print(' ');
    Serial.print( code[i]       & 1);
    Serial.print(F("    "));
    Serial.println(sectorName(code[i]));
  }
}

void setup() {
  for (uint8_t i = 0; i < 3; i++) pinMode(PN[i], INPUT);
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("   ZCD KARAKTERIZASYONU      (Adim 3.2)"));
  Serial.println(F("================================================"));
}

void loop() {
  Serial.println(F("\n------------------------------------------------"));

  // ---- A ----
  Serial.println(F("[A] KANAL OLCUMLERI"));
  for (uint8_t i = 0; i < 3; i++) {
    float tH, tL;
    if (!measChan(PN[i], tH, tL)) {
      Serial.print(F("  ")); Serial.print(NM[i]);
      Serial.println(F("  : SINYAL YOK"));
      continue;
    }
    float T  = tH + tL;
    float th = 4.5f * (tH - tL);
    Serial.print(F("  ")); Serial.print(NM[i]);
    Serial.print(F("  tL=")); Serial.print(tL, 2);
    Serial.print(F("  tH=")); Serial.print(tH, 2);
    Serial.print(F("  T=" )); Serial.print(T , 2);
    Serial.print(F("  f=" )); Serial.print(1000.0f / T, 2);
    Serial.print(F("Hz  olu bant=+/-")); Serial.print(th, 1);
    Serial.println(F(" der"));
    if (T < 15.0f) Serial.println(F("     >> UYARI: T~10ms  DIYOT YOK veya KISA"));
    if (th > 15.0f) Serial.println(F("     >> NOT: olu bant genis, 6k8 dusunulebilir"));
  }

  // ---- B ----
  Serial.println(F("\n[B] FAZ ILISKISI"));
  float dS, dT;
  if (measPhase(dS, dT)) {
    Serial.print(F("  R->S = ")); Serial.print(dS, 2);
    Serial.print(F(" ms     R->T = ")); Serial.print(dT, 2); Serial.println(F(" ms"));
    Serial.println(F("  beklenen (R-S-T): 6.67 ve 13.33 ms"));
    if (dS > 5.5 && dS < 7.8 && dT > 12.2 && dT < 14.5)
      Serial.println(F("  >> FAZ SIRASI R-S-T : DOGRU"));
    else if (dS > 12.2 && dS < 14.5 && dT > 5.5 && dT < 7.8)
      Serial.println(F("  >> FAZ SIRASI TERS  : S ve T kablolarini takas et"));
    else
      Serial.println(F("  >> BEKLENMEDIK : bir diyot ters olabilir"));
  } else Serial.println(F("  olcum basarisiz"));

  // ---- C ----
  Serial.println(F("\n[C] SEKTOR DIZISI (bir cevrim)"));
  showSectors();

  delay(4000);
}