/* ===================================================================
   B6 TAM KONTROLLU DOGRULTUCU  —  ATESLEME KONTROLCUSU  (v5)

   v4'e gore YENI
     - A0'a POTANSIYOMETRE ile alpha kontrolu
     - uc katmanli gurultu bastirma: 100nF + ustel filtre + 2 derece olu bant
     - GUVENLI KONUM KILIDI: pot dusuk cikis ucunda gorulmeden atesleme acilmaz
     - POT / SERI mod gecisi

   v4'ten devralinan
     - komutasyon centigi gurultusu senkronu bozmuyor (IV_MIN filtresi)
     - gecersiz sektor kodu senkronu bozmuyor, sadece sayiliyor
     - 32 bit bolmeler kesme disinda

   GIRIS  : ZCD_R=A1(PC1)  ZCD_S=A2(PC2)  ZCD_T=A3(PC3)   -> PCINT1
            POT=A0(PC0)                                    -> ADC (kesme YOK)
   CIKIS  : T1=D5 T2=D6 T3=D7 T4=D8 T5=D9 T6=D10          -> Timer1

   POT BAGLANTISI
       uc 1 (sol)   -> Arduino GND
       uc 2 (orta)  -> Arduino A0     + 100nF A0-GND arasi
       uc 3 (sag)   -> Arduino 5V     (FIRE_5V'den DEGIL)
       saga cevir = alpha kuculur = cikis artar
       ters geliyorsa uc 1 ile uc 3'u takas et

   SEKTOR KODU (R<<2 | S<<1 | T)
       010 -> S1 -> T1 R+        101 -> S4 -> T4 R-
       011 -> S2 -> T2 T-        100 -> S5 -> T5 T+
       001 -> S3 -> T3 S+        110 -> S6 -> T6 S-
       000 / 111 = GECERSIZ

   GUVENLIK
       - acilista ATESLEME KAPALI, alpha = 120 (V_dc = 0)
       - POT MODUNDA: pot en alta getirilmeden 'e' REDDEDILIR
       - 12 gecerli kenar gormeden atesleme baslamaz
       - yanlis sira / cok uzun aralik -> senkron sifirlanir
       - kisa aralik / gecersiz kod = gurultu -> yok sayilir, senkron korunur
       - 25 ms kenar gelmezse -> tum cikislar LOW
       - alpha rampali degisir (RAMP_TICKS)

   KOMUT (Seri Monitor 115200, satir sonu = New Line)
       p        POT modu   (varsayilan)
       <sayi>   SERI modu + alpha hedefi (20..150)
       e        ATESLEME AC
       d        ATESLEME KAPAT  (aninda, rampa yok)
   =================================================================== */

#define TICK_US        50                   // Timer1 tik suresi
#define T_NOM          (20000/TICK_US)      // 400 tik = 20 ms

const uint16_t PULSE_DEG   = 70;    // gate darbe genisligi (60+ = kesisme)
const uint16_t ALPHA_MIN   = 20;
const uint16_t ALPHA_MAX   = 150;
const uint16_t SYNC_NEEDED = 12;    // kac gecerli kenar sonra senkron
const uint16_t LOST_TICKS  = 500;   // 25 ms kenar yoksa ariza
const uint16_t RAMP_TICKS  = 4000;  // 200 ms'de 1 derece -> 5 der/sn
const uint16_t IV_MIN      = 40;    // < 36 derece = centik gurultusu
const uint16_t IV_MAX      = 110;   // > 99 derece = gercek ariza

/* ---------------- POTANSIYOMETRE ---------------- */
#define POT_PIN            A0
const uint16_t POT_MS         = 25;   // okuma periyodu, ms
const uint8_t  POT_DEADBAND   = 2;    // derece — bunun altinda hedef degismez
const uint16_t POT_SAFE_ALPHA = 140;  // kilit acilma esigi (dusuk cikis ucu)

bool     g_potMode = true;            // true = POT, false = SERI
bool     g_potSafe = false;           // pot guvenli konumda gorulduyse true
uint16_t g_potFilt = 0;               // ustel filtre birikimi (deger << 3)
uint32_t g_potLast = 0;
uint16_t g_potAdc  = 0;               // rapor icin

/* ---------------- global ---------------- */
volatile uint16_t g_tick = 0;
volatile uint16_t g_on[6], g_off[6];
volatile bool     g_armed[6] = {0,0,0,0,0,0};
volatile bool     g_fire  = false;

volatile uint8_t  g_state = 0xFF;
volatile int8_t   g_sec   = -1;
volatile uint16_t g_lastEdge = 0;
volatile uint16_t g_period  = T_NOM;   // tik
volatile int16_t  g_dcorr   = 6;       // olu bant duzeltmesi, tik
volatile uint16_t g_sync    = 0;
volatile uint16_t g_fault   = 0;       // gercek ariza sayaci
volatile uint16_t g_noise   = 0;       // yok sayilan gurultu sayaci
volatile uint16_t g_ivS = 55, g_ivL = 79;

volatile uint16_t g_alpha = 120;       // aktif alpha (rampali)
volatile uint16_t g_dly   = 0;         // atesleme gecikmesi, tik
volatile uint16_t g_wid   = 0;         // darbe genisligi, tik

volatile uint32_t g_usS1 = 0;          // micros() capraz kontrol
volatile uint16_t g_Tus  = 20000;      // periyot, us

uint16_t g_alphaTarget = 120;          // rampanin hedefi

const char* SNAME[6] = { "T1 R+","T2 T-","T3 S+","T4 R-","T5 T+","T6 S-" };

/* ---------------- yardimci ---------------- */
int8_t decode(uint8_t c){
  switch(c){
    case 0b010: return 0;   case 0b011: return 1;   case 0b001: return 2;
    case 0b101: return 3;   case 0b100: return 4;   case 0b110: return 5;
    default   : return -1;
  }
}

void disarmAll(){ for (uint8_t i=0;i<6;i++) g_armed[i] = false; }

/* teorik cikis — omik yuk, 47 V yildiz (V_LL = 81,4 V) */
float vdcTheory(uint16_t a){
  float r = a * PI / 180.0f;
  if (a <= 60) return 110.0f * cos(r);
  return 110.0f * (1.0f + cos(r + PI/3.0f));
}

/* ---------------- Timer1: 50 us tik + cikis surucusu ---------------- */
ISR(TIMER1_COMPA_vect){
  g_tick++;
  uint8_t m = 0;
  if (g_fire){
    for (uint8_t i=0;i<6;i++)
      if (g_armed[i] &&
          (int16_t)(g_tick - g_on [i]) >= 0 &&
          (int16_t)(g_tick - g_off[i]) <  0) m |= (1<<i);
  }
  PORTD = (PORTD & ~0xE0) | ((m & 0x07) << 5);   // D5,D6,D7
  PORTB = (PORTB & ~0x07) | ((m >> 3) & 0x07);   // D8,D9,D10
}

/* ---------------- PCINT1: ZCD kenarlari (bolme YOK) ---------------- */
ISR(PCINT1_vect){
  uint8_t p  = PINC;
  uint8_t st = (((p>>1)&1)<<2) | (((p>>2)&1)<<1) | ((p>>3)&1);
  if (st == g_state) return;

  uint8_t  old = g_state;
  uint16_t now = g_tick;
  g_state = st;

  int8_t s = decode(st);
  if (s < 0){ g_noise++; return; }               // gecersiz kod -> YOK SAY

  if (g_sec >= 0){
    uint16_t iv = now - g_lastEdge;

    if (iv < IV_MIN){ g_noise++; return; }       // centik gurultusu -> YOK SAY

    if (iv > IV_MAX || s != (g_sec + 1) % 6){    // GERCEK ariza
      g_sync = 0; g_fault++; disarmAll();
      g_sec = s; g_lastEdge = now; return;
    }

    if (iv < 67) g_ivS = (g_ivS*3 + iv) >> 2;    // kisa aralik  60-2*teta
    else         g_ivL = (g_ivL*3 + iv) >> 2;    // uzun aralik  60+2*teta
    g_period = (g_ivS + g_ivL) * 3;              // 3x(kisa+uzun) = 360 derece
    g_dcorr  = ((int16_t)g_ivL - (int16_t)g_ivS) >> 2;
    if (g_sync < SYNC_NEEDED) g_sync++;
  }
  g_sec = s;
  g_lastEdge = now;

  // --- periyodu micros() ile de olc (capraz kontrol, S1'den S1'e) ---
  if (s == 0){
    uint32_t u = micros();
    uint32_t d = u - g_usS1;
    if (g_usS1 && d > 15000UL && d < 25000UL) g_Tus = (uint16_t)d;
    g_usS1 = u;
  }

  // --- gercek sifir gecisi: dusen kenar -dt, yukselen kenar +dt ---
  uint8_t  chg = old ^ st;
  bool     falling = (old & chg) != 0;
  uint16_t t0 = falling ? (now - g_dcorr) : (now + g_dcorr);

  if (g_sync >= SYNC_NEEDED){
    g_on [s] = t0 + g_dly;                       // hazir degerler, bolme yok
    g_off[s] = t0 + g_dly + g_wid;
    g_armed[s] = true;
  } else disarmAll();
}

/* ---------------- kurulum ---------------- */
void setup(){
  DDRD |=  0xE0;  PORTD &= ~0xE0;      // D5,D6,D7 cikis, LOW
  DDRB |=  0x07;  PORTB &= ~0x07;      // D8,D9,D10 cikis, LOW
  DDRC &= ~0x0F;  PORTC &= ~0x0F;      // A0..A3 giris, dahili pull-up KAPALI

  Serial.begin(115200);
  delay(300);

  // ADC'yi isit ve filtreyi mevcut pot konumuyla doldur
  analogRead(POT_PIN);
  delay(5);
  g_potFilt = (uint16_t)analogRead(POT_PIN) << 3;

  cli();
  TCCR1A = 0;
  TCCR1B = (1<<WGM12) | (1<<CS11);     // CTC, bolucu 8 -> 2 MHz
  OCR1A  = (TICK_US * 2) - 1;          // 50 us
  TIMSK1 = (1<<OCIE1A);
  PCICR  |= (1<<PCIE1);
  PCMSK1 |= (1<<PCINT9)|(1<<PCINT10)|(1<<PCINT11);   // A0 = PCINT8 ACILMIYOR
  sei();

  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("   B6 ATESLEME KONTROLCUSU  v5  (POT)"));
  Serial.println(F("================================================"));
  Serial.println(F(" komut : p=POT modu  <sayi>=SERI modu alpha(20..150)"));
  Serial.println(F("         e=ates AC   d=ates KAPAT"));
  Serial.println(F(" acilis: alpha=120  ATESLEME KAPALI  POT modu"));
  Serial.println(F(" KILIT : pot en alta getirilmeden 'e' reddedilir"));
  Serial.println(F("------------------------------------------------"));
}

/* ---------------- potansiyometre okuma ---------------- */
void readPot(){
  if (millis() - g_potLast < POT_MS) return;
  g_potLast = millis();

  // ustel filtre:  zaman sabiti = 8 x 25 ms = 200 ms
  uint16_t raw = analogRead(POT_PIN);
  g_potFilt = g_potFilt - (g_potFilt >> 3) + raw;
  uint16_t adc = g_potFilt >> 3;
  g_potAdc = adc;

  // ADC 0 -> alpha 150 (cikis 0)      ADC 1023 -> alpha 20 (cikis maks)
  uint16_t a = ALPHA_MAX - (uint16_t)(((uint32_t)adc * (ALPHA_MAX - ALPHA_MIN)) / 1023UL);
  if (a < ALPHA_MIN) a = ALPHA_MIN;
  if (a > ALPHA_MAX) a = ALPHA_MAX;

  // guvenli konum kilidi: pot bir kez dusuk cikis ucunda gorulmeli
  if (a >= POT_SAFE_ALPHA) g_potSafe = true;

  if (!g_potMode) return;

  // olu bant: kucuk oynamalarda hedefi degistirme
  int16_t d = (int16_t)a - (int16_t)g_alphaTarget;
  if (d > (int16_t)POT_DEADBAND || d < -(int16_t)POT_DEADBAND) g_alphaTarget = a;
}

/* ---------------- seri komut ---------------- */
void handleSerial(){
  static uint16_t n = 0; static bool has = false;
  while (Serial.available()){
    char c = Serial.read();
    if (c >= '0' && c <= '9'){ n = n*10 + (c-'0'); has = true; }
    else {
      if (has){
        if (n >= ALPHA_MIN && n <= ALPHA_MAX){
          g_potMode = false;                     // sayi girildi -> SERI moda gec
          g_alphaTarget = n;
          Serial.print(F(">> SERI modu, alpha hedef = ")); Serial.println(n);
        } else Serial.println(F(">> alpha araligi 20..150"));
        n = 0; has = false;
      }
      if (c == 'p'){
        g_potMode = true;
        Serial.println(F(">> POT modu"));
      }
      if (c == 'e'){
        if (g_potMode && !g_potSafe){
          Serial.println(F(">> REDDEDILDI: once potu en sola (dusuk cikis) cevir"));
        } else {
          g_fire = true;  Serial.println(F(">> ATESLEME ACIK"));
        }
      }
      if (c == 'd'){
        g_fire = false;
        g_potSafe = false;                       // kilidi yeniden kur
        Serial.println(F(">> ATESLEME KAPALI  (pot kilidi yeniden kuruldu)"));
      }
    }
  }
}

/* ---------------- alpha rampasi + gecikme hesabi ---------------- */
void updateTiming(uint16_t tk){
  static uint16_t lastRamp = 0;

  if ((uint16_t)(tk - lastRamp) >= RAMP_TICKS){
    lastRamp = tk;
    uint16_t a; cli(); a = g_alpha; sei();
    if      (a < g_alphaTarget) a++;
    else if (a > g_alphaTarget) a--;
    cli(); g_alpha = a; sei();
  }

  // bolmeler burada, kesme disinda
  uint16_t per, alp;
  cli(); per = g_period; alp = g_alpha; sei();
  uint16_t d = ((uint32_t)(30 + alp) * per) / 360UL;
  uint16_t w = ((uint32_t) PULSE_DEG * per) / 360UL;
  cli(); g_dly = d; g_wid = w; sei();
}

/* ---------------- ana dongu ---------------- */
void loop(){
  handleSerial();
  readPot();

  uint16_t tk, le;
  cli(); tk = g_tick; le = g_lastEdge; sei();

  updateTiming(tk);

  // kenar kaybi bekcisi
  if ((uint16_t)(tk - le) > LOST_TICKS){
    cli();
    g_sync = 0; g_sec = -1; g_state = 0xFF; disarmAll();
    sei();
  }

  // ~500 ms'de bir rapor
  static uint16_t last = 0;
  if ((uint16_t)(tk - last) >= 10000){
    last = tk;

    uint16_t per, syn, flt, nse, alp, tus; int16_t dc; int8_t sec;
    cli();
      per = g_period; syn = g_sync; flt = g_fault; nse = g_noise;
      dc  = g_dcorr;  sec = g_sec;  alp = g_alpha; tus = g_Tus;
    sei();

    float Ttick = per * TICK_US / 1000.0f;      // tik sayacindan
    float Tus   = tus / 1000.0f;                // micros()'tan

    Serial.print(F("T_us="));   Serial.print(Tus, 2);
    Serial.print(F("ms  f="));  Serial.print(1000.0f / Tus, 2);
    Serial.print(F("Hz  dt=")); Serial.print(dc * TICK_US / 1000.0f, 2);
    Serial.print(F("ms  a="));  Serial.print(alp);
    if (alp != g_alphaTarget){ Serial.print(F("->")); Serial.print(g_alphaTarget); }
    Serial.print(F("  Vteo=")); Serial.print(vdcTheory(alp), 1);
    Serial.print(F("V  "));
    Serial.print(g_potMode ? F("POT(") : F("SERI("));
    Serial.print(g_potAdc); Serial.print(')');
    if (g_potMode && !g_potSafe) Serial.print(F(" KILITLI"));
    Serial.print(F("  sek="));
    if (sec >= 0){ Serial.print('S'); Serial.print(sec+1); Serial.print(' '); Serial.print(SNAME[sec]); }
    else Serial.print(F("--"));
    Serial.print(syn >= SYNC_NEEDED ? F("  SENKRON") : F("  SENKRON-YOK"));
    Serial.print(g_fire ? F("  ATES:ACIK") : F("  ATES:KAPALI"));
    Serial.print(F("  gurultu=")); Serial.print(nse);
    Serial.print(F("  hata="));    Serial.println(flt);
  }
}
