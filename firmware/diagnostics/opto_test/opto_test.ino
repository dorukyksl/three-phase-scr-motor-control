/* ===================================================================
   MOC KARTI  -  ASAMA 1 TESTI
   Her kanali sirayla 3 saniye ACIK tutar.
   O sirada ilgili 220 ohm'un IKI UCU arasindaki gerilimi olc.
   Beklenen ~2.6 V   ->   I_F = V / 220 = ~11.9 mA
   UYARI: MOC cikislari (pin 4/6) bagli OLMAMALI. Sadece 5 V.
   =================================================================== */

const uint8_t  PN[6] = { 5, 6, 7, 8, 9, 10 };
const char*    NM[6] = { "T1 R+", "T2 T-", "T3 S+", "T4 R-", "T5 T+", "T6 S-" };
const uint16_t HOLD_MS = 3000;

void allOff(){ for (uint8_t i = 0; i < 6; i++) digitalWrite(PN[i], LOW); }

void setup(){
  for (uint8_t i = 0; i < 6; i++){ digitalWrite(PN[i], LOW); pinMode(PN[i], OUTPUT); }
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\n=== MOC KARTI - ASAMA 1 TESTI ==="));
  Serial.println(F("Her kanal 3 sn ACIK kalir."));
  Serial.println(F("O kanalin 220 ohm'unun IKI UCU arasini olc -> ~2.6 V"));
  Serial.println(F("I_F = V / 220\n"));
  delay(1500);
}

void loop(){
  for (uint8_t i = 0; i < 6; i++){
    allOff();
    digitalWrite(PN[i], HIGH);
    Serial.print(F("KANAL ")); Serial.print(i + 1);
    Serial.print(F("   ")); Serial.print(NM[i]);
    Serial.print(F("   (D")); Serial.print(PN[i]);
    Serial.println(F(")  ACIK - simdi olc"));
    delay(HOLD_MS);
  }
  allOff();
  Serial.println(F("--- tur bitti ---\n"));
  delay(1500);
}