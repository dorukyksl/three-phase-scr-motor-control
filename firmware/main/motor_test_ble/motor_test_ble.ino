/* ===================================================================
   B6 TAM KONTROLLU DOGRULTUCU  —  ATESLEME KONTROLCUSU  (v6 / BLE)

   POTANSIYOMETRE YOK.  Alpha yalnizca seri komutla belirlenir:
   USB seri monitorden ya da HM-10 uzerinden iOS uygulamasindan.

   v4'e gore YENI
     - HM-10 BLE modulu (donanim UART, D0/D1) uzerinden telemetri + komut
     - 5 Hz kompakt CSV telemetri  ->  iOS uygulamasi
     - insan okunur mesajlar '#' ile basliyor (uygulama bunlari yok sayar)
     - HEARTBEAT EMNIYETI: uygulama baglantiyi kaybederse atesleme kapanir
     - GUVENLI KONUM SARTI: hedef alpha 140'in altindayken 'E' reddedilir

   v4'ten devralinan
     - komutasyon centigi gurultusu senkronu bozmuyor (IV_MIN filtresi)
     - gecersiz sektor kodu senkronu bozmuyor, sadece sayiliyor
     - 32 bit bolmeler kesme disinda

   -------------------------------------------------------------------
   !! SOFTWARESERIAL KULLANILMADI !!
   SoftwareSerial gonderim/alim boyunca kesmeleri kapatir (9600'de
   ~1 ms/karakter).  Bu sure boyunca Timer1 tikleri ve ZCD kenarlari
   kaybolur -> atesleme zamanlamasi bozulur.  HM-10 bu yuzden DONANIM
   UART'ina baglandi; o kesme tabanlidir ve ana donguyu bloklamaz.
   -------------------------------------------------------------------

   HM-10 BAGLANTISI            (BAUD 9600 - modulun fabrika ayari)
       HM-10 VCC   -> Arduino 3.3V  (breakout kartlarda 5V da olur)
       HM-10 GND   -> Arduino GND
       HM-10 TXD   -> Arduino D0 (RX)   ARAYA JUMPER KOY - yuklerken cikar
       HM-10 RXD   <- 1k -+- Arduino D1 (TX)      gerilim bolucu:
                          |                        5V x 2/3 = 3.33V
                         2k
                          |
                         GND
       HM-10 STATE / EN  -> bos birak

   GIRIS  : ZCD_R=A1(PC1)  ZCD_S=A2(PC2)  ZCD_T=A3(PC3)   -> PCINT1
            TAKO =A0(PC0)  SE2662-6L analog cikisi        -> ADC
   CIKIS  : T1=D5 T2=D6 T3=D7 T4=D8 T5=D9 T6=D10          -> Timer1

   TELEMETRI (5 Hz, satir sonu \n)
       S,<alpha>,<hedef>,<Vteo*10>,<frek*100>,<senkron>,<ates>,
         <sektor>,<gurultu>,<hata>,<gecersizkomut>,<mod>,<doluluk>,
         <rpm>,<takoADC>
       ornek:  S,95,20,322,5000,1,1,3,12,0,0,0,0,1470,291
       sektor -1 = senkron yok      mod: 0=FAZ  1=BURST
       BURST modunda ortalama cikis = Vteo x doluluk/100

   KOMUT — HER KOMUT \n ILE BITMELI, satirin TAMAMI eslesmeli
          (buyuk/kucuk harf farketmez, hem USB hem BLE)
       A<sayi>  alpha hedefi (20..150)
       <sayi>   ayni sey (A'sIz)
       E        ATESLEME AC     (hedef alpha >= 140 olmali)
       D        ATESLEME KAPAT  (aninda)
       B<sayi>  BURST modu + doluluk (0..100)   -- YALNIZCA OMIK YUK
       F        FAZ kontrolu moduna don
       H        heartbeat       (ilk H'den sonra emniyet devreye girer)
       ?        hemen bir telemetri satiri gonder

   GUVENLIK
       - DPDT FIRE_5V anahtari HALA birincil donanim kilididir.
         BLE onu gecemez; uygulama sadece bir kolaylik katmanidir.
       - acilista ATESLEME KAPALI, alpha = 150 (V_dc = 0)
       - hedef alpha < 140 iken 'E' REDDEDILIR
       - heartbeat aktifken 3 sn komut gelmezse ATESLEME KAPANIR
       - 12 gecerli kenar gormeden atesleme baslamaz
       - 25 ms kenar gelmezse tum cikislar LOW
   =================================================================== */

#define TICK_US        50                   // Timer1 tik suresi
#define T_NOM          (20000/TICK_US)      // 400 tik = 20 ms

const uint16_t PULSE_DEG   = 70;    // gate darbe genisligi (60+ = kesisme)
const uint16_t ALPHA_MIN   = 20;
const uint16_t ALPHA_MAX   = 150;
const uint16_t SAFE_ALPHA  = 140;   // bu acinin altindayken atesleme acilamaz
const uint16_t SYNC_NEEDED = 12;    // kac gecerli kenar sonra senkron
const uint16_t LOST_TICKS  = 500;   // 25 ms kenar yoksa ariza
const uint16_t RAMP_TICKS  = 4000;  // 200 ms'de 1 derece -> 5 der/sn
const uint16_t IV_MIN      = 40;    // < 36 derece = centik gurultusu
const uint16_t IV_MAX      = 110;   // > 99 derece = gercek ariza

/* ---------------- BLE / telemetri ---------------- */
const uint32_t BAUD          = 9600;   // HM-10 fabrika ayari
const uint16_t TELEM_MS      = 200;    // 5 Hz
const uint32_t HB_TIMEOUT_MS = 3000;   // heartbeat emniyeti

bool     g_hbActive  = false;          // ilk 'H' gelince true
uint32_t g_lastHb    = 0;
uint32_t g_telemLast = 0;

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

volatile uint16_t g_alpha = 150;       // aktif alpha (rampali)
volatile uint16_t g_dly   = 0;
volatile uint16_t g_wid   = 0;

volatile uint32_t g_usS1 = 0;
volatile uint16_t g_Tus  = 20000;      // periyot, us

uint16_t g_alphaTarget = 150;

/* ---------------- BURST FIRING (tam dalga paketi) ----------------

   Faz kontrolunde her yarim dalga kirpilir.  Burst firing'de dalga hic
   kirpilmaz: TAM sebeke cevrimleri ya tamamen iletilir ya tamamen
   bloklanir.  Ortalama cikis  =  V_dc(alpha) x doluluk.

   ON cevrimlerini bloga toplamak yerine sigma-delta ile esit dagitiyoruz;
   ayni doluluk daha az titresim (flicker) uretir.
        her cevrim:  acc += duty;  acc >= 100 ise -> acc -= 100, bu cevrim ON

   !! YALNIZCA OMIK YUK !!  Motorda burst firing, burst frekansinda tork
   darbeleri uretir; mil ve kayis icin zararlidir.                        */
const uint8_t MODE_PHASE = 0;
const uint8_t MODE_BURST = 1;

uint8_t g_ctrlMode = MODE_PHASE;
uint8_t g_duty     = 0;                 // %0..100, yalniz burst modunda
volatile uint8_t g_burstAcc = 0;
volatile bool    g_burstOn  = true;     // bu sebeke cevrimi ateslenecek mi

/* ---------------- DEVIR OLCUMU  (analog takogeneratör) ---------------

   SE2662-6L'in analog gerilim cikisi kullaniliyor; 25 pinli D-sub'a
   gerek yok.  Cikis devirle DOGRUSAL:

        olculen:  0.17V/190   0.62V/636   1.50V/1552
                  2.42V/2480  3.18V/3294  dev/dk
        orijinden gecen en kucuk kareler:  R2 = 0.99989
        en buyuk sapma 18 dev/dk

        n = TACHO_RPM_PER_VOLT * V_tako          (0.969 V / 1000 dev/dk)

   IKINCI ASAMA — ADC yolu dogrulamasi:
        gercek   uygulama   fark
         199       201      +1.0 %
        1560      1603      +2.8 %
        2534      2607      +2.9 %
        3330      3424      +2.8 %
   Sabit %2.83 yuksek okuma.  Ilk kalibrasyon multimetreyle DOGRUDAN
   takodan yapilmisti (ADC yoktu), yani 1032 rpm/V saglam.  Hata yalnizca
   ADC yolunda ciktigina gore kaynak referans gerilimi:
        VREF = 5.00 * 0.97245 = 4.862 V
   USB'den beslenen Uno'da polyfuse ve guc yolu MOSFET'i uzerinde
   dusen gerilim bu farki aciklar.

   Duzeltmeden sonra kalan hata: >1500 dev/dk'da %0.1'in altinda.

   NOT: Arduino'yu farkli beslersen (baska USB portu/kablosu, jak girisi)
   bu ray degisir ve olcek kayar.  O zaman 5V pinini multimetreyle olcup
   ADC_VREF'e yaz — baska hicbir yere dokunma.                          */
#define TACHO_PIN   A0
const float    TACHO_RPM_PER_VOLT = 1032.0f;   // takonun kendi sabiti (DMM ile)
const float    ADC_VREF           = 4.862f;    // olculen 5V rayi
const uint16_t TACHO_MS           = 25;        // okuma periyodu

/* Asiri hiz koruması — KAPALI (kullanici istegi).
   Motor etiketi 3000 dev/dk; olcumlerde alpha=45'te 3294 gorulmustu.
   Acmak istersen esik degeri yaz, ornegin 3400.  Trip, tek bir gurultu
   sicramasindan tetiklenmesin diye ust uste TRIP_COUNT olcum boyunca
   asilmayi bekler (8 x 25 ms = 200 ms).                                */
const uint16_t RPM_TRIP   = 0;        // 0 = koruma yok
const uint8_t  TRIP_COUNT = 8;

/* Gurultu bastirma:
   ADC_OVERSAMPLE  her okumada kac ornek ortalanacak (rastgele gurultuyu
                   karekok kadar azaltir: 8 ornek -> 2.8 kat)
   RPM_DEADBAND    bu degerin altini SIFIR say.  Motor dururken birkac
                   milivoltluk ADC gurultusu 5-10 dev/dk gibi gorunuyor. */
const uint8_t  ADC_OVERSAMPLE = 8;
const uint16_t RPM_DEADBAND   = 30;

uint16_t g_tachoFilt = 0;      // ustel filtre birikimi (deger << 3)
uint16_t g_tachoAdc  = 0;
uint32_t g_tachoLast = 0;
uint16_t g_rpm = 0;
bool     g_overspeed = false;
uint8_t  g_tripCnt = 0;

/* komut satiri tamponu — satir tabanli ayristirici icin */
char     g_cmdBuf[20];
uint8_t  g_cmdLen  = 0;
uint16_t g_badCmd  = 0;      // taninmayan satir sayaci

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

/* teorik cikis — omik yuk, 47 V yildiz (V_LL = 81,4 V)
   Ikinci bolge formulu yalnizca 60 < a <= 120 icin gecerlidir.
   120 derecenin ustunde cikis sifirdir; formul kullanilirsa
   yanlislikla tekrar yukselen bir deger dondurur (a=150 -> 14.7 V). */
float vdcTheory(uint16_t a){
  if (a >= 120) return 0.0f;
  float r = a * PI / 180.0f;
  if (a <= 60) return 110.0f * cos(r);
  return 110.0f * (1.0f + cos(r + PI/3.0f));
}

/* atesleme acilabilir mi? — cikis sifira yakin olmali */
bool canEnable(){ return g_alphaTarget >= SAFE_ALPHA; }

/* ---------------- Timer1: 50 us tik + cikis surucusu ---------------- */
ISR(TIMER1_COMPA_vect){
  g_tick++;
  uint8_t m = 0;
  if (g_fire && g_burstOn){          // burst modunda OFF cevrimlerde susar
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
    g_period = (g_ivS + g_ivL) * 3;
    g_dcorr  = ((int16_t)g_ivL - (int16_t)g_ivS) >> 2;
    if (g_sync < SYNC_NEEDED) g_sync++;
  }
  g_sec = s;
  g_lastEdge = now;

  if (s == 0){                                   // YENI SEBEKE CEVRIMI
    uint32_t u = micros();                       // micros() capraz kontrol
    uint32_t d = u - g_usS1;
    if (g_usS1 && d > 15000UL && d < 25000UL) g_Tus = (uint16_t)d;
    g_usS1 = u;

    if (g_ctrlMode == MODE_BURST){               // sigma-delta doluluk
      g_burstAcc += g_duty;
      if (g_burstAcc >= 100){ g_burstAcc -= 100; g_burstOn = true; }
      else                    g_burstOn = false;
    } else {
      g_burstOn = true;
    }
  }

  uint8_t  chg = old ^ st;
  bool     falling = (old & chg) != 0;
  uint16_t t0 = falling ? (now - g_dcorr) : (now + g_dcorr);

  if (g_sync >= SYNC_NEEDED){
    g_on [s] = t0 + g_dly;
    g_off[s] = t0 + g_dly + g_wid;
    g_armed[s] = true;
  } else disarmAll();
}

/* ---------------- devir hesabi (takogeneratör ADC) ---------------- */
void updateRpm(){
  if (millis() - g_tachoLast < TACHO_MS) return;
  g_tachoLast = millis();

  uint16_t sum = 0;                              // asiri ornekleme
  for (uint8_t i = 0; i < ADC_OVERSAMPLE; i++) sum += analogRead(TACHO_PIN);
  uint16_t raw = sum / ADC_OVERSAMPLE;

  g_tachoFilt = g_tachoFilt - (g_tachoFilt >> 3) + raw;   // 8 x 25ms = 200ms
  g_tachoAdc  = g_tachoFilt >> 3;

  float v = g_tachoAdc * (ADC_VREF / 1023.0f);
  float r = v * TACHO_RPM_PER_VOLT;
  uint16_t rpm = (r < 0.0f) ? 0 : (uint16_t)r;
  g_rpm = (rpm < RPM_DEADBAND) ? 0 : rpm;        // olu bant

  /* --- asiri hiz koruması (gurultuye karsi geciktirmeli) --- */
  if (RPM_TRIP && g_fire && g_rpm > RPM_TRIP){
    if (++g_tripCnt >= TRIP_COUNT){
      g_fire = false;
      g_overspeed = true;
      g_tripCnt = 0;
      Serial.print(F("# ASIRI HIZ: ")); Serial.print(g_rpm);
      Serial.println(F(" dev/dk -> ATESLEME KAPATILDI"));
    }
  } else {
    g_tripCnt = 0;
  }
}

/* ---------------- kurulum ---------------- */
void setup(){
  DDRD |=  0xE0;  PORTD &= ~0xE0;      // D5,D6,D7 cikis, LOW
  DDRB |=  0x07;  PORTB &= ~0x07;      // D8,D9,D10 cikis, LOW
  DDRC &= ~0x0E;  PORTC &= ~0x0E;      // A1,A2,A3 giris, pull-up KAPALI
  DDRC &= ~0x01;  PORTC &= ~0x01;      // A0 giris (tako), pull-up KAPALI

  Serial.begin(BAUD);
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
  Serial.println(F("# B6 ATESLEME KONTROLCUSU v6 (BLE, pot yok)"));
  Serial.println(F("# komut: A<sayi> E D H ?"));
  Serial.println(F("# acilis: alpha=150  ATESLEME KAPALI"));
}

/* ---------------- telemetri ---------------- */
void sendTelemetry(){
  uint16_t syn, flt, nse, alp, tus; int8_t sec;
  cli();
    syn = g_sync; flt = g_fault; nse = g_noise;
    sec = g_sec;  alp = g_alpha; tus = g_Tus;
  sei();

  // senkron yokken g_Tus baslangic degerinde kalir; 50.00 Hz gostermek
  // yaniltici olur -> senkron saglanana kadar 0 bildir
  bool synced = (syn >= SYNC_NEEDED);
  float f = (synced && tus > 0) ? (1000000.0f / tus) : 0.0f;

  Serial.print(F("S,"));
  Serial.print(alp);                              Serial.print(',');
  Serial.print(g_alphaTarget);                    Serial.print(',');
  Serial.print((int16_t)(vdcTheory(alp)*10.0f));  Serial.print(',');
  Serial.print((uint16_t)(f*100.0f));             Serial.print(',');
  Serial.print(synced ? 1 : 0);                   Serial.print(',');
  Serial.print(g_fire ? 1 : 0);                   Serial.print(',');
  Serial.print(sec);                              Serial.print(',');
  Serial.print(nse);                              Serial.print(',');
  Serial.print(flt);                              Serial.print(',');
  Serial.print(g_badCmd);                         Serial.print(',');
  Serial.print(g_ctrlMode);                       Serial.print(',');
  Serial.print(g_duty);                           Serial.print(',');
  Serial.print(g_rpm);                            Serial.print(',');
  Serial.println(g_tachoAdc);
}

/* ---------------- komut isleme (USB + BLE ayni UART) ----------------

   SATIR TABANLI ve TAM ESLESMELI.

   Onceki surumde her karakter tek basina komut sayiliyordu.  HM-10 BLE
   baglantisi kopukken UART'a AT modu cevaplari doktugu icin icindeki
   herhangi bir 'E' harfi ATESLEMEYI ACIYORDU.  Artik komut ancak
   satir sonuna kadar biriktirilip TAMAMI eslesirse calisir; taninmayan
   satirlar sayilip atilir.                                            */

void execCommand(){
  g_cmdBuf[g_cmdLen] = '\0';

  /* --- tek karakterli komutlar: satirin TAMAMI o karakter olmali --- */
  if (g_cmdLen == 1){
    switch (g_cmdBuf[0]){
      case 'E': case 'e':
        if (!canEnable()) Serial.println(F("# RED: once alpha 150'ye getir"));
        else { g_fire = true; g_overspeed = false;
               Serial.println(F("# ATESLEME ACIK")); }
        return;

      case 'D': case 'd':
        g_fire = false;
        Serial.println(F("# ATESLEME KAPALI"));
        return;

      case 'H': case 'h':                 // YALNIZCA 'H' heartbeat besler
        g_hbActive = true;
        g_lastHb = millis();
        return;

      case 'F': case 'f':                 // FAZ kontrolu moduna don
        g_ctrlMode = MODE_PHASE;
        g_burstOn  = true;
        Serial.println(F("# FAZ kontrolu modu"));
        return;

      case '?':
        sendTelemetry();
        return;
    }
  }

  /* --- B<yuzde> : BURST modu + doluluk (0..100) --- */
  if (g_cmdBuf[0] == 'B' || g_cmdBuf[0] == 'b'){
    const char* q = g_cmdBuf + 1;
    if (*q){
      uint16_t n = 0; bool ok = true;
      for (; *q; q++){
        if (*q < '0' || *q > '9'){ ok = false; break; }
        n = n * 10 + (uint16_t)(*q - '0');
        if (n > 999){ ok = false; break; }
      }
      if (ok && n <= 100){
        g_ctrlMode = MODE_BURST;
        g_duty = (uint8_t)n;
        Serial.print(F("# BURST modu, doluluk = %")); Serial.println(n);
        return;
      }
    }
    Serial.println(F("# HATA: B0..B100"));
    return;
  }

  /* --- A<sayi> veya <sayi> : tum karakterler rakam olmali --- */
  const char* p = g_cmdBuf;
  if (*p == 'A' || *p == 'a') p++;
  if (*p >= '0' && *p <= '9'){
    uint16_t n = 0;
    bool ok = true;
    for (const char* q = p; *q; q++){
      if (*q < '0' || *q > '9'){ ok = false; break; }
      n = n * 10 + (uint16_t)(*q - '0');
      if (n > 9999){ ok = false; break; }
    }
    if (ok){
      if (n >= ALPHA_MIN && n <= ALPHA_MAX){
        g_alphaTarget = n;
        Serial.print(F("# alpha hedef = ")); Serial.println(n);
      } else {
        Serial.println(F("# HATA: alpha 20..150 olmali"));
      }
      return;
    }
  }

  g_badCmd++;                              // taninmadi -> sessizce at
}

void handleSerial(){
  while (Serial.available()){
    char c = Serial.read();

    if (c == '\n' || c == '\r'){           // satir bitti
      if (g_cmdLen > 0){ execCommand(); g_cmdLen = 0; }
      continue;
    }
    if (g_cmdLen < sizeof(g_cmdBuf) - 1) g_cmdBuf[g_cmdLen++] = c;
    // tasma: fazla karakterler atilir, satir zaten taninmayacak
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

  uint16_t per, alp;
  cli(); per = g_period; alp = g_alpha; sei();
  uint16_t d = ((uint32_t)(30 + alp) * per) / 360UL;
  uint16_t w = ((uint32_t) PULSE_DEG * per) / 360UL;
  cli(); g_dly = d; g_wid = w; sei();
}

/* ---------------- ana dongu ---------------- */
void loop(){
  handleSerial();

  uint16_t tk, le;
  cli(); tk = g_tick; le = g_lastEdge; sei();

  updateTiming(tk);

  // kenar kaybi bekcisi
  if ((uint16_t)(tk - le) > LOST_TICKS){
    cli();
    g_sync = 0; g_sec = -1; g_state = 0xFF; disarmAll();
    sei();
  }

  // BLE heartbeat emniyeti — sadece ilk 'H' geldikten sonra aktif
  if (g_hbActive && g_fire && (millis() - g_lastHb > HB_TIMEOUT_MS)){
    g_fire = false;
    g_hbActive = false;
    Serial.println(F("# EMNIYET: baglanti koptu, atesleme kapatildi"));
  }

  updateRpm();

  // telemetri
  if (millis() - g_telemLast >= TELEM_MS){
    g_telemLast = millis();
    sendTelemetry();
  }
}
