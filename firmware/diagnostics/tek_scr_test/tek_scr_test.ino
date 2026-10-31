/* ===================================================================
   ADIM 4  —  TEK SCR ATESLEMESI
   Tek fazli yarim dalga kontrollu dogrultucu:  R fazi -> D3 -> yuk -> N
   Ates : sadece D5 (T1 = R+).  Diger bes kanal kullanilmaz.
   Sync : ZCD_R (A1) dusen kenari = R'nin pozitif sifir gecisi
   Komut: <sayi>=alpha(0..170)  e=ATES AC  d=ATES KAPAT
   =================================================================== */

const uint8_t PIN_ZCD_R = A1;
const uint8_t PIN_T1    = 5;

uint16_t g_alpha  = 150;      // derece — guvenli yuksek baslangic
uint16_t g_pulse  = 30;       // gate darbe genisligi, derece
uint32_t g_period = 20000;    // us
const int32_t DEAD_US = 300;  // olu bant duzeltmesi (ZCD testinden)
bool     g_fire   = false;

bool waitFall(uint32_t &t){
  uint32_t g = micros();
  while (digitalRead(PIN_ZCD_R) == LOW ){ if (micros()-g > 60000UL) return false; }
  while (digitalRead(PIN_ZCD_R) == HIGH){ if (micros()-g > 60000UL) return false; }
  t = micros();  return true;
}

void cmd(){
  static uint16_t n = 0; static bool has = false;
  while (Serial.available()){
    char c = Serial.read();
    if (c>='0' && c<='9'){ n = n*10 + (c-'0'); has = true; }
    else {
      if (has){
        if (n <= 170){ g_alpha = n; Serial.print(F(">> alpha=")); Serial.println(n); }
        else Serial.println(F(">> 0..170"));
        n = 0; has = false;
      }
      if (c=='e'){ g_fire = true;  Serial.println(F(">> ATES ACIK")); }
      if (c=='d'){ g_fire = false; Serial.println(F(">> ATES KAPALI")); }
    }
  }
}

void setup(){
  digitalWrite(PIN_T1, LOW);  pinMode(PIN_T1, OUTPUT);  digitalWrite(PIN_T1, LOW);
  pinMode(PIN_ZCD_R, INPUT);
  Serial.begin(115200); delay(300);
  Serial.println(F("\n=== ADIM 4 : TEK SCR ATESLEMESI ==="));
  Serial.println(F("alpha=150 ile basliyor, ATES KAPALI"));
  Serial.println(F("komut: <sayi>  e  d\n"));
}

void loop(){
  cmd();

  uint32_t t0;
  if (!waitFall(t0)){                       // ZCD yok -> guvenli duruma gec
    digitalWrite(PIN_T1, LOW);
    Serial.println(F("!! ZCD SINYALI YOK - atesleme durdu"));
    delay(500);  return;
  }

  static uint32_t prev = 0;
  if (prev){ uint32_t p = t0 - prev; if (p > 15000 && p < 25000) g_period = p; }
  prev = t0;

  if (!g_fire){ digitalWrite(PIN_T1, LOW); }
  else {
    uint32_t tz    = t0 - DEAD_US;
    uint32_t tOn   = tz + (uint32_t)g_alpha * g_period / 360UL;
    uint32_t tOff  = tOn + (uint32_t)g_pulse * g_period / 360UL;
    if ((int32_t)(tOn - micros()) > 0){
      while ((int32_t)(micros() - tOn ) < 0) { }
      digitalWrite(PIN_T1, HIGH);
      while ((int32_t)(micros() - tOff) < 0) { }
      digitalWrite(PIN_T1, LOW);
    }
  }

  static uint32_t last = 0;
  if (millis() - last > 1000){
    last = millis();
    Serial.print(F("T=")); Serial.print(g_period/1000.0,2);
    Serial.print(F("ms  alpha=")); Serial.print(g_alpha);
    Serial.println(g_fire ? F("  ATES:ACIK") : F("  ATES:KAPALI"));
  }
}