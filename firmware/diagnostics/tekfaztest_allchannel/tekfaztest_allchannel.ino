/* ===================================================================
   TEK FAZLI  -  TEK KANAL TESTI                    (Adim 4 / v3)

   v3 DEGISIKLIGI
     OFFSET >= 180 olan kanallar ZCD_R'nin YUKSELEN kenarindan
     referanslanir (o kenar 180 dereceye denk gelir). Boylece
     efektif aci hicbir zaman 360'a yaklasmaz -> darbe kirpilmaz.

   ---------------------------------------------------------------
   KANAL TABLOSU  -  sadece asagidaki iki sabiti degistir
   ---------------------------------------------------------------
     Kanal  SCR       PIN_T1  OFFSET_DEG  Trafodan  Yuk
     T1     D3  R+      5         0          R      DC+ -> yuk -> N
     T2     D6  T-      6        60          T      N -> yuk -> DC-
     T3     D5  S+      7       120          S      DC+ -> yuk -> N
     T4     D4  R-      8       180          R      N -> yuk -> DC-
     T5     D2  T+      9       240          T      DC+ -> yuk -> N
     T6     D1  S-     10       300          S      N -> yuk -> DC-

     Negatif grupta yuk DC- ile N arasina baglanir, DC+ bosta.
     Olcum: kirmizi prob N'de, siyah DC-'de.
   ---------------------------------------------------------------

   BEKLENEN    V_dc = 10.58 * (1 + cos alpha)
       150 -> 1.4    120 -> 5.3    90 -> 10.6
        60 -> 15.9    45 -> 18.1   30 -> 19.7

   KOMUT   <sayi>=alpha(0..170)   e=ates AC   d=ates KAPAT
   =================================================================== */

/* ============ KANALA GORE DEGISTIRILECEK IKI SABIT ============ */
const uint8_t  PIN_T1     = 10;     // <<< kanal tablosuna bak
const uint16_t OFFSET_DEG = 300;    // <<< kanal tablosuna bak
/* ============================================================== */

/* --- offset'e gore otomatik: hangi kenar, hangi efektif aci --- */
const bool     USE_RISING = (OFFSET_DEG >= 180);
const uint16_t EFF_OFFSET = USE_RISING ? (OFFSET_DEG - 180) : OFFSET_DEG;

const uint8_t  PIN_ZCD_R  = A1;
const uint16_t PULSE_DEG  = 30;
const int32_t  DEAD_US    = 300;
const uint16_t ALPHA_MAX  = 170;
const uint32_t GUARD_US   = 500;    // emniyet agi (normalde devreye girmez)

uint16_t g_alpha  = 150;
uint32_t g_period = 20000;
bool     g_fire   = false;
uint32_t g_edges  = 0;
uint32_t g_shots  = 0;

/* ---- referans kenari bekle (dusen veya yukselen) ---- */
bool waitEdge(uint32_t &t){
  uint32_t g = micros();
  if (USE_RISING){
    while (digitalRead(PIN_ZCD_R) == HIGH){ if (micros()-g > 60000UL) return false; }
    while (digitalRead(PIN_ZCD_R) == LOW ){ if (micros()-g > 60000UL) return false; }
  } else {
    while (digitalRead(PIN_ZCD_R) == LOW ){ if (micros()-g > 60000UL) return false; }
    while (digitalRead(PIN_ZCD_R) == HIGH){ if (micros()-g > 60000UL) return false; }
  }
  t = micros();
  return true;
}

void handleSerial(){
  static uint16_t n = 0; static bool has = false;
  while (Serial.available()){
    char c = Serial.read();
    if (c >= '0' && c <= '9'){ n = n*10 + (c - '0'); has = true; }
    else {
      if (has){
        if (n <= ALPHA_MAX){ g_alpha = n; Serial.print(F(">> alpha = ")); Serial.println(n); }
        else                Serial.println(F(">> alpha araligi 0..170"));
        n = 0; has = false;
      }
      if (c == 'e'){ g_fire = true;  Serial.println(F(">> ATESLEME ACIK")); }
      if (c == 'd'){ g_fire = false; Serial.println(F(">> ATESLEME KAPALI")); }
    }
  }
}

void setup(){
  digitalWrite(PIN_T1, LOW);
  pinMode(PIN_T1, OUTPUT);
  digitalWrite(PIN_T1, LOW);
  pinMode(PIN_ZCD_R, INPUT);

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("   TEK FAZLI - TEK KANAL TESTI  v3"));
  Serial.println(F("================================================"));
  Serial.print  (F(" PIN_T1     = D")); Serial.println(PIN_T1);
  Serial.print  (F(" OFFSET_DEG = "));  Serial.println(OFFSET_DEG);
  Serial.print  (F(" referans   = "));  Serial.println(USE_RISING ? F("YUKSELEN kenar") : F("DUSEN kenar"));
  Serial.print  (F(" efektif offset = ")); Serial.println(EFF_OFFSET);
  Serial.println(F(" alpha=150, ATESLEME KAPALI"));
  Serial.println(F("------------------------------------------------"));
}

void loop(){
  handleSerial();

  uint32_t t0;
  if (!waitEdge(t0)){
    digitalWrite(PIN_T1, LOW);
    Serial.println(F("!! ZCD SINYALI YOK - atesleme durdu"));
    delay(500);
    return;
  }
  g_edges++;

  static uint32_t prev = 0;
  if (prev){
    uint32_t p = t0 - prev;
    if (p > 15000UL && p < 25000UL) g_period = p;
  }
  prev = t0;

  if (!g_fire){
    digitalWrite(PIN_T1, LOW);
  } else {
    // gercek sifir gecisi: dusen kenarda -dt, yukselen kenarda +dt
    uint32_t tz  = USE_RISING ? (t0 + DEAD_US) : (t0 - DEAD_US);

    uint16_t ang = (EFF_OFFSET + g_alpha) % 360;
    uint32_t tOn = tz + (uint32_t)ang * g_period / 360UL;

    if ((int32_t)(tOn - micros()) < 0) tOn = micros();

    uint32_t tOff = tOn + (uint32_t)PULSE_DEG * g_period / 360UL;
    uint32_t tLim = t0 + g_period - GUARD_US;
    if ((int32_t)(tOff - tLim) > 0) tOff = tLim;

    if ((int32_t)(tOff - tOn) > 0){
      while ((int32_t)(micros() - tOn ) < 0) { }
      digitalWrite(PIN_T1, HIGH);
      while ((int32_t)(micros() - tOff) < 0) { }
      digitalWrite(PIN_T1, LOW);
      g_shots++;
    }
  }

  static uint32_t last = 0;
  if (millis() - last >= 1000){
    last = millis();
    Serial.print(F("T="));       Serial.print(g_period / 1000.0, 2);
    Serial.print(F("ms  f="));   Serial.print(1000000.0 / g_period, 2);
    Serial.print(F("Hz  a="));   Serial.print(g_alpha);
    Serial.print(F("  aci="));   Serial.print((EFF_OFFSET + g_alpha) % 360);
    Serial.print(g_fire ? F("  ATES:ACIK") : F("  ATES:KAPALI"));
    Serial.print(F("  kenar=")); Serial.print(g_edges);
    Serial.print(F("  ates="));  Serial.println(g_shots);
  }
}