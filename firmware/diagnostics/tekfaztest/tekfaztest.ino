/* ===================================================================
   TEK FAZLI  -  TEK SCR TESTI                 (Adim 4)

   DEVRE
     Trafo 2L1 (R) --------------------> KART 2 klemens R
     KART 2 klemens DC+ --> 100 ohm ---> Trafo yildiz noktasi (N)
     KART 2 klemens S / T / DC-  : BOS
     ZCD karti : R, S, T, N bagli kalsin (senkron icin gerekli)

     Arduino D5 ---> ULN2003 pin 1
     ULN2003 pin 16 ---> KART 2 MOC1 pin 2   (Dupont "R+")
     Arduino 5V --[DPDT SAFE/ARMED]--> KART 2 klemens kutup 1 (FIRE_5V)
     ZCD_R=A1  ZCD_S=A2  ZCD_T=A3

   CALISMA
     Tek fazli yarim dalga kontrollu dogrultucu.  Sadece D3 (R+) devrede.
     Senkron : ZCD_R'nin DUSEN kenari = R'nin pozitif sifir gecisi
     Olu bant duzeltmesi : 300 us  (ZCD testinde olculdu)

   BEKLENEN     V_dc = (Vm / 2*pi) * (1 + cos alpha),  Vm = 66.5 V
       alpha=150 -> 1.4 V     alpha=60 -> 15.9 V
       alpha=120 -> 5.3 V     alpha=45 -> 18.1 V
       alpha= 90 -> 10.6 V    alpha=30 -> 19.7 V
                              alpha=20 -> 20.5 V

   KOMUT (Seri Monitor 115200, satir sonu = New Line)
       <sayi>  alpha (0..170 derece)
       e       ATESLEME AC
       d       ATESLEME KAPAT

   GUVENLIK
       Acilista atesleme KAPALI, alpha = 150.
       ZCD sinyali kesilirse cikis LOW'a duser ve uyari basar.
       Durdurma sirasi:  d  ->  DPDT SAFE  ->  trafo salteri OFF
   =================================================================== */

const uint8_t  PIN_ZCD_R = A1;
const uint8_t  PIN_T1    = 5;        // MOC1 (R+) -> ULN pin 1
const uint16_t OFFSET_DEG  = 0;

const uint16_t PULSE_DEG = 30;       // gate darbe genisligi
const int32_t  DEAD_US   = 300;      // olu bant duzeltmesi
const uint16_t ALPHA_MAX = 170;

uint16_t g_alpha  = 150;             // guvenli yuksek baslangic
uint32_t g_period = 20000;           // us
bool     g_fire   = false;
uint32_t g_edges  = 0;               // gorulen kenar sayisi

/* ---- ZCD_R'nin dusen kenarini bekle ---- */
bool waitFall(uint32_t &t){
  uint32_t g = micros();
  while (digitalRead(PIN_ZCD_R) == LOW ){ if (micros() - g > 60000UL) return false; }
  while (digitalRead(PIN_ZCD_R) == HIGH){ if (micros() - g > 60000UL) return false; }
  t = micros();
  return true;
}

/* ---- seri komut ---- */
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
  digitalWrite(PIN_T1, LOW);         // guvenli acilis
  pinMode(PIN_T1, OUTPUT);
  digitalWrite(PIN_T1, LOW);
  pinMode(PIN_ZCD_R, INPUT);         // harici 10k pull-up var

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("   TEK FAZLI - TEK SCR TESTI   (D3 = R+)"));
  Serial.println(F("================================================"));
  Serial.println(F(" alpha=150 ile basliyor, ATESLEME KAPALI"));
  Serial.println(F(" komut: <sayi>  e=ates AC  d=ates KAPAT"));
  Serial.println(F(" beklenen: V_dc = 10.58 * (1 + cos alpha)"));
  Serial.println(F("------------------------------------------------"));
}

void loop(){
  handleSerial();

  uint32_t t0;
  if (!waitFall(t0)){                         // ZCD yok -> guvenli duruma gec
    digitalWrite(PIN_T1, LOW);
    Serial.println(F("!! ZCD SINYALI YOK - atesleme durdu"));
    delay(500);
    return;
  }
  g_edges++;

  static uint32_t prev = 0;
  if (prev){
    uint32_t p = t0 - prev;
    if (p > 15000UL && p < 25000UL) g_period = p;   // gecerli periyot
  }
  prev = t0;

  if (!g_fire){
    digitalWrite(PIN_T1, LOW);
  } else {
    uint32_t tz   = t0 - DEAD_US;                          // gercek sifir gecisi
    uint32_t tOn  = tz  + (uint32_t)g_alpha   * g_period / 360UL;
    uint32_t tOff = tOn + (uint32_t)PULSE_DEG * g_period / 360UL;

    if ((int32_t)(tOn - micros()) > 0){
      while ((int32_t)(micros() - tOn ) < 0) { }
      digitalWrite(PIN_T1, HIGH);
      while ((int32_t)(micros() - tOff) < 0) { }
      digitalWrite(PIN_T1, LOW);
    }
  }

  static uint32_t last = 0;
  if (millis() - last >= 1000){
    last = millis();
    Serial.print(F("T="));      Serial.print(g_period / 1000.0, 2);
    Serial.print(F("ms  f="));  Serial.print(1000000.0 / g_period, 2);
    Serial.print(F("Hz  alpha=")); Serial.print(g_alpha);
    Serial.print(g_fire ? F("  ATES:ACIK") : F("  ATES:KAPALI"));
    Serial.print(F("  kenar=")); Serial.println(g_edges);
  }
}