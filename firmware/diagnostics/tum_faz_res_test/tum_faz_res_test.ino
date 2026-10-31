/* ===================================================================
   B6 TAM KONTROLLU DOGRULTUCU  —  ATESLEME KONTROLCUSU  v4
   Tam kopru  /  omik yuk (100 ohm)

   v4 DEGISIKLIGI  —  gurultu bagisikligi
     Yuksek akimda (dusuk alpha) komutasyon centikleri ZCD hattinda
     sahte kenar uretebiliyor.  v3'te bu "ariza" sayilip senkron
     sifirlaniyordu -> 12 kenar boyunca atesleme duruyor -> cikis
     salinim yapiyordu.
     v4'te sahte kenarlar SAYILIYOR ama YOK SAYILIYOR; senkron ve
     zamanlama bozulmuyor.  Gercek senkron kaybi (kenar kacirma,
     sira bozulmasi, faz kaybi) hala yakalaniyor.

   BAGLANTI
     Trafo 2L1 -> KART2 klemens R
     Trafo 2L3 -> KART2 klemens S          (etiketlere gore!)
     Trafo 2L2 -> KART2 klemens T
     Trafo N   -> SADECE ZCD kartina.  KART 2'ye BAGLANMAZ.
     KART2 DC+ --> 100 ohm --> KART2 DC-

     ZCD_R=A1  ZCD_S=A2  ZCD_T=A3
     Arduino D5..D10 -> ULN2003 giris 1..6
     ULN cikis 16,15,14,13,12,11 -> MOC1..MOC6 pin 2
     Arduino 5V --[DPDT SAFE/ARMED]--> KART2 klemens kutup 1 (FIRE_5V)

   SEKTOR KODU (R<<2 | S<<1 | T)
       010 -> S1 -> T1 R+ (D5)      101 -> S4 -> T4 R- (D8)
       011 -> S2 -> T2 T- (D6)      100 -> S5 -> T5 T+ (D9)
       001 -> S3 -> T3 S+ (D7)      110 -> S6 -> T6 S- (D10)

   BEKLENEN CIKIS  (47 V yildiz, V_LL = 81.4 V, omik yuk)
       alpha <= 60 :  V_dc = 110 * cos(alpha)
       alpha >  60 :  V_dc = 110 * [1 + cos(alpha + 60)]

   KOMUT (115200, New Line)   <sayi>=alpha(20..150)   e=ates AC   d=ates KAPAT

   120 ->   0 V     60 ->  55.0 V
       105 -> 3.7 V     45 ->  77.8 V
        90 ->14.7 V     30 ->  95.3 V
        75 ->32.2 V     20 -> 103.4 V 
   =================================================================== */

#define TICK_US        50                   // Timer1 tik suresi
#define T_NOM          (20000/TICK_US)      // 400 tik = 20 ms

const uint16_t PULSE_DEG   = 70;    // 60'tan genis: omik yukte iki SCR gerekli
const uint16_t ALPHA_MIN   = 20;
const uint16_t ALPHA_MAX   = 150;
const uint16_t SYNC_NEEDED = 12;    // kac gecerli kenar sonra senkron
const uint16_t LOST_TICKS  = 500;   // 25 ms kenar yoksa senkron kaybi
const uint16_t RAMP_TICKS  = 4000;  // 50 ms'de 1 derece -> 20 der/sn

const uint16_t IV_MIN      = 40;    // 2 ms = 36 der.  Altindaki kenar = GURULTU
const uint16_t IV_MAX      = 110;   // 5.5 ms.  Ustunde kenar kacirilmis demek
/* gercek sektor araliklari 55-79 tik (49-71 der) arasindadir */

/* ---------------- global ---------------- */
volatile uint16_t g_tick = 0;
volatile uint16_t g_on[6], g_off[6];
volatile bool     g_armed[6] = {0,0,0,0,0,0};
volatile bool     g_fire  = false;

volatile uint8_t  g_state = 0xFF;
volatile int8_t   g_sec   = -1;
volatile uint16_t g_lastEdge = 0;
volatile uint16_t g_period  = T_NOM;
volatile int16_t  g_dcorr   = 6;
volatile uint16_t g_sync    = 0;
volatile uint16_t g_fault   = 0;     // GERCEK senkron kaybi
volatile uint16_t g_noise   = 0;     // yok sayilan sahte kenar
volatile uint16_t g_ivS = 55, g_ivL = 79;

volatile uint16_t g_alpha = 120;     // aktif alpha (rampali)
volatile uint16_t g_dly   = 0;       // atesleme gecikmesi, tik
volatile uint16_t g_wid   = 0;       // darbe genisligi, tik

volatile uint32_t g_usS1 = 0;
volatile uint16_t g_Tus  = 20000;

uint16_t g_alphaTarget = 120;

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

/* teorik cikis - omik yuk, 47 V yildiz */
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
  PORTD = (PORTD & ~0xE0) | ((m & 0x07) << 5);   // T1,T2,T3 -> D5,D6,D7
  PORTB = (PORTB & ~0x07) | ((m >> 3) & 0x07);   // T4,T5,T6 -> D8,D9,D10
}

/* ---------------- PCINT1: ZCD kenarlari ---------------- */
ISR(PCINT1_vect){
  uint8_t p  = PINC;
  uint8_t st = (((p>>1)&1)<<2) | (((p>>2)&1)<<1) | ((p>>3)&1);   // R S T
  if (st == g_state) return;

  uint8_t  old = g_state;
  uint16_t now = g_tick;
  g_state = st;                        // pin durumu her zaman guncellenir

  int8_t s = decode(st);

  /* --- gecersiz kod (000 / 111): yok say, senkronu BOZMA --- */
  if (s < 0){
    g_noise++;
    return;                            // g_lastEdge guncellenmez ->
  }                                    // kalici ise loop'taki bekci yakalar

  if (g_sec >= 0){
    uint16_t iv = now - g_lastEdge;

    /* --- sahte kenar (komutasyon centigi): yok say --- */
    if (iv < IV_MIN){
      g_noise++;
      return;                          // g_sec ve g_lastEdge degismez
    }

    /* --- gercek senkron kaybi --- */
    if (iv > IV_MAX || s != (g_sec + 1) % 6){
      g_sync = 0; g_fault++; disarmAll();
      g_sec = s; g_lastEdge = now;
      return;
    }

    /* --- normal kenar: periyot ve olu bant guncelle --- */
    if (iv < 67) g_ivS = (g_ivS*3 + iv) >> 2;    // kisa aralik  60 - 2*teta
    else         g_ivL = (g_ivL*3 + iv) >> 2;    // uzun aralik  60 + 2*teta
    g_period = (g_ivS + g_ivL) * 3;              // 3x(kisa+uzun) = 360 derece
    g_dcorr  = ((int16_t)g_ivL - (int16_t)g_ivS) >> 2;
    if (g_sync < SYNC_NEEDED) g_sync++;
  }

  g_sec = s;
  g_lastEdge = now;

  if (s == 0){                          // micros() ile capraz kontrol
    uint32_t u = micros();
    uint32_t d = u - g_usS1;
    if (g_usS1 && d > 15000UL && d < 25000UL) g_Tus = (uint16_t)d;
    g_usS1 = u;
  }

  /* gercek sifir gecisi: dusen kenar -dt, yukselen kenar +dt */
  uint8_t  chg = old ^ st;
  bool     falling = (old & chg) != 0;
  uint16_t t0 = falling ? (now - g_dcorr) : (now + g_dcorr);

  if (g_sync >= SYNC_NEEDED){
    g_on [s] = t0 + g_dly;              // hazir degerler, ISR'da bolme yok
    g_off[s] = t0 + g_dly + g_wid;
    g_armed[s] = true;
  } else disarmAll();
}

/* ---------------- kurulum ---------------- */
void setup(){
  DDRD |=  0xE0;  PORTD &= ~0xE0;      // D5,D6,D7 cikis LOW
  DDRB |=  0x07;  PORTB &= ~0x07;      // D8,D9,D10 cikis LOW
  DDRC &= ~0x0E;  PORTC &= ~0x0E;      // A1,A2,A3 giris, dahili pull-up KAPALI

  Serial.begin(115200);
  delay(300);

  cli();
  TCCR1A = 0;
  TCCR1B = (1<<WGM12) | (1<<CS11);     // CTC, bolucu 8 -> 2 MHz
  OCR1A  = (TICK_US * 2) - 1;          // 50 us
  TIMSK1 = (1<<OCIE1A);
  PCICR  |= (1<<PCIE1);
  PCMSK1 |= (1<<PCINT9)|(1<<PCINT10)|(1<<PCINT11);
  sei();

  Serial.println();
  Serial.println(F("================================================"));
  Serial.println(F("   B6 TAM KOPRU  -  ATESLEME KONTROLCUSU  v4"));
  Serial.println(F("================================================"));
  Serial.println(F(" komut : <sayi>=alpha(20..150)  e=ates AC  d=ates KAPAT"));
  Serial.println(F(" acilis: alpha=120 (V_dc=0)   ATESLEME KAPALI"));
  Serial.println(F(" gurultu = yok sayilan sahte kenar (zararsiz)"));
  Serial.println(F(" hata    = gercek senkron kaybi (0 kalmali)"));
  Serial.println(F("------------------------------------------------"));
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
          g_alphaTarget = n;
          Serial.print(F(">> alpha hedef = ")); Serial.print(n);
          Serial.print(F("   teorik V_dc = ")); Serial.print(vdcTheory(n),1);
          Serial.println(F(" V"));
        } else Serial.println(F(">> alpha araligi 20..150"));
        n = 0; has = false;
      }
      if (c == 'e'){ g_fire = true;  Serial.println(F(">> ATESLEME ACIK")); }
      if (c == 'd'){ g_fire = false; Serial.println(F(">> ATESLEME KAPALI")); }
    }
  }
}

/* ---------------- alpha rampasi + gecikme hesabi ---------------- */
void updateTiming(uint16_t tk){
  static uint16_t lastRamp = 0;

  if ((uint16_t)(tk - lastRamp) >= RAMP_TICKS){       // 50 ms'de 1 derece
    lastRamp = tk;
    uint16_t a; cli(); a = g_alpha; sei();
    if      (a < g_alphaTarget) a++;
    else if (a > g_alphaTarget) a--;
    cli(); g_alpha = a; sei();
  }

  uint16_t per, alp;
  cli(); per = g_period; alp = g_alpha; sei();
  uint16_t d = ((uint32_t)(30 + alp) * per) / 360UL;  // 30 = dogal komutasyon
  uint16_t w = ((uint32_t) PULSE_DEG * per) / 360UL;
  cli(); g_dly = d; g_wid = w; sei();
}

/* ---------------- ana dongu ---------------- */
void loop(){
  handleSerial();

  uint16_t tk, le;
  cli(); tk = g_tick; le = g_lastEdge; sei();

  updateTiming(tk);

  if ((uint16_t)(tk - le) > LOST_TICKS){              // kenar kaybi bekcisi
    cli();
    g_sync = 0; g_sec = -1; g_state = 0xFF; disarmAll();
    sei();
  }

  static uint16_t last = 0;
  if ((uint16_t)(tk - last) >= 10000){                // ~500 ms
    last = tk;

    uint16_t per, syn, flt, noi, alp, tus; int16_t dc; int8_t sec;
    cli();
      per = g_period; syn = g_sync; flt = g_fault; noi = g_noise;
      dc  = g_dcorr;  sec = g_sec;  alp = g_alpha;   tus = g_Tus;
    sei();

    float Tus = tus / 1000.0f;

    Serial.print(F("T_us="));   Serial.print(Tus, 2);
    Serial.print(F("ms f="));   Serial.print(1000.0f / Tus, 2);
    Serial.print(F("Hz dt="));  Serial.print(dc * TICK_US / 1000.0f, 2);
    Serial.print(F("ms a="));   Serial.print(alp);
    if (alp != g_alphaTarget){ Serial.print(F("->")); Serial.print(g_alphaTarget); }
    Serial.print(F(" Vteo=")); Serial.print(vdcTheory(alp), 1);
    Serial.print(F("V "));
    Serial.print(syn >= SYNC_NEEDED ? F("SENKRON") : F("SENKRON-YOK"));
    Serial.print(g_fire ? F(" ATES:ACIK") : F(" ATES:KAPALI"));
    Serial.print(F(" gurultu=")); Serial.print(noi);
    Serial.print(F(" hata="));    Serial.println(flt);
  }
}