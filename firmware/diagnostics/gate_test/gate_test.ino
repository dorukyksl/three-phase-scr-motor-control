/* ===================================================================
   TANI TESTI  —  GATE SUREKLI ACIK               (Adim 4 / teshis)

   AMAC
     Zamanlama kodunu tamamen devreden cikarip donanimi tek basina
     test etmek. ZCD yok, alpha yok, kesme yok.
     MOC1 (R+) surekli tetikli tutulur -> D3 her pozitif yarim dalgada
     mumkun olan en erken anda iletime girer -> alpha ~ 0 -> tam cikis.

   DEVRE (degistirme, mevcut kurulum aynen kalsin)
     Trafo 2L1 (R) ------> KART 2 klemens R
     KART 2 DC+ --------> 100 ohm ayarli direnc ------> Trafo yildiz (N)
     KART 2 S / T / DC-   : BOS
     Arduino D5 --------> ULN2003 pin 1
     ULN2003 pin 16 ----> KART 2 MOC1 pin 2  (Dupont "R+")
     Arduino 5V --[DPDT]-> KART 2 klemens kutup 1 (FIRE_5V)
     ZCD karti bagli kalabilir - bu testte kullanilmiyor.

   BEKLENEN SONUC
     DC+ <-> N   (DC V)   = ~21 V
     akim (100 ohm)       = ~210 mA
     direncte guc         = ~4.5 W        (500 W parcada hicbir sey)
     SCR'de guc           = ~0.18 W

   KOMUT   (Seri Monitor, 115200, satir sonu = New Line)
     e  ->  gate ACIK
     d  ->  gate KAPALI

   GUVENLIK
     Acilista gate KAPALI. "e" yazana kadar hicbir sey atesleneme.
     Durdurma:  d  ->  DPDT SAFE  ->  trafo salteri OFF
   =================================================================== */

const uint8_t PIN_T1 = 5;        // MOC1 (R+) surucusu  -> ULN pin 1
bool g_on = false;               // acilista KAPALI

void setGate(bool on) {
  g_on = on;
  digitalWrite(PIN_T1, on ? HIGH : LOW);
}

void setup() {
  // Guvenli acilis: once seviyeyi yaz, sonra cikis yap
  digitalWrite(PIN_T1, LOW);
  pinMode(PIN_T1, OUTPUT);
  digitalWrite(PIN_T1, LOW);

  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("   TANI TESTI  -  GATE SUREKLI ACIK"));
  Serial.println(F("================================================"));
  Serial.println(F(" Beklenen :  DC+ <-> N  =  ~21 V (DC)"));
  Serial.println(F("             akim       =  ~210 mA"));
  Serial.println(F("------------------------------------------------"));
  Serial.println(F(" GATE SU AN KAPALI."));
  Serial.println(F(" Baslatmak icin  e  yaz + Enter"));
  Serial.println(F(" Durdurmak icin  d  yaz + Enter"));
  Serial.println(F("------------------------------------------------"));
}

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'e') { setGate(true);  Serial.println(F(">> GATE ACIK   - simdi olc")); }
    if (c == 'd') { setGate(false); Serial.println(F(">> GATE KAPALI - 0 V bekleniyor")); }
  }
}

void loop() {
  handleSerial();

  static uint32_t last = 0;
  if (millis() - last >= 2000) {
    last = millis();
    Serial.print(F("gate = "));
    Serial.println(g_on ? F("ACIK") : F("KAPALI"));
  }
}