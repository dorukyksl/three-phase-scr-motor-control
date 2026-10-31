//
//  Telemetry.swift
//  DC Motor Regulator
//
//  Arduino'dan gelen CSV telemetri satirinin modeli ve cozumleyicisi.
//
//  Satir formati (5 Hz, \n ile biter):
//    S,<alpha>,<hedef>,<Vteo*10>,<frek*100>,<senkron>,<ates>,<sektor>,
//      <gurultu>,<hata>,<gecersizkomut>,<mod>,<doluluk>,<rpm>,<takoADC>
//  Ornek:
//    S,95,20,322,5000,1,1,3,12,0,0,0,0,1470,291
//

import Foundation

/// Cikisin nasil kisildigini belirleyen yontem.
enum ControlMode: Int, Equatable {
    /// Her yarim dalga kirpilir — alpha ile ayarlanir.
    case phase = 0
    /// Tam sebeke cevrimleri ya tamamen iletilir ya bloklanir.
    /// Ortalama cikis = V_dc(alpha) x doluluk.  YALNIZCA OMIK YUK.
    case burst = 1

    var label: String { self == .phase ? "Faz" : "Burst" }
}

/// Arduino'nun 5 Hz gonderdigi anlik durum.
struct Telemetry: Equatable {
    /// Su anki tetikleme acisi (derece). Rampa yuzunden hedefe yavas yaklasir.
    var alpha: Int
    /// Rampanin ulasmaya calistigi aci (derece).
    var targetAlpha: Int
    /// TEORIK dogru akim gerilimi (V). Olculen degil — alpha'dan hesaplanir.
    var vdcTheoretical: Double
    /// Sebeke frekansi (Hz), Arduino'nun micros() ile olctugu.
    var frequency: Double
    /// ZCD senkronizasyonu saglandi mi?
    var isSynced: Bool
    /// Atesleme acik mi?
    var isFiring: Bool
    /// Su an hangi 60 derecelik sektordeyiz (0...5). Senkron yoksa nil.
    var sector: Int?
    /// Yok sayilan sahte ZCD kenari sayisi (komutasyon centikleri).
    var noiseCount: Int
    /// Gercek senkron kaybi sayisi.
    var faultCount: Int
    /// Arduino'nun taniyamayip attigi komut satiri sayisi.
    /// HM-10 gevezeligi buraya dusuyorsa hat gurultulu demektir.
    var badCommandCount: Int = 0
    /// Faz kontrolu mu, burst firing mi?
    var controlMode: ControlMode = .phase
    /// Burst modunda doluluk orani, %0..100.
    var duty: Int = 0
    /// SE2662-6L takogeneratöründen olculen mil hizi (dev/dk).
    var rpm: Int = 0
    /// Takonun ham ADC okumasi (0...1023) — tani icin.
    var tachoAdc: Int = 0

    /// Takogeneratör gerilimi (V), ham ADC'den geri hesaplanir.
    var tachoVolts: Double { Double(tachoAdc) * 5.0 / 1023.0 }

    /// Motorun etiket devri. Ustunde calismak mekanik olarak risklidir.
    static let ratedRpm = 3000

    /// Sektorun okunabilir adi — atesle­nen tristor.
    var sectorName: String {
        guard let s = sector, (0...5).contains(s) else { return "—" }
        return ["T1 R+", "T2 T−", "T3 S+", "T4 R−", "T5 T+", "T6 S−"][s]
    }

    /// Rampa hedefe ulasmis mi?
    var isRamping: Bool { alpha != targetAlpha }

    /// Yuke gercekte uygulanan ortalama gerilim.
    /// Burst modunda tam dalga paketleri doluluk oraninda uygulandigi icin
    /// ortalama, faz kontrolu degerinin doluluk katidir.
    var effectiveVdc: Double {
        controlMode == .burst ? vdcTheoretical * Double(duty) / 100.0
                              : vdcTheoretical
    }

    /// Arduino'nun atesleme izni verecegi guvenli aci esigi.
    static let safeAlpha = 140

    /// Su anki hedefle 'E' komutu kabul edilir mi?
    var canEnableFiring: Bool { targetAlpha >= Telemetry.safeAlpha }

    /// Baglanti yokken gosterilecek bos durum.
    static let placeholder = Telemetry(
        alpha: 150, targetAlpha: 150, vdcTheoretical: 0, frequency: 0,
        isSynced: false, isFiring: false, sector: nil,
        noiseCount: 0, faultCount: 0, badCommandCount: 0,
        controlMode: .phase, duty: 0, rpm: 0, tachoAdc: 0
    )
}

extension Telemetry {

    /// Bir telemetri satirini cozumler. Satir "S," ile baslamiyorsa
    /// (ornegin '#' ile baslayan insan okunur mesaj) nil doner.
    static func parse(_ line: String) -> Telemetry? {
        let trimmed = line.trimmingCharacters(in: .whitespacesAndNewlines)
        guard trimmed.hasPrefix("S,") else { return nil }

        let f = trimmed.split(separator: ",", omittingEmptySubsequences: false)
        // "S" + 9 zorunlu alan = 10 parca.  11. alan (gecersiz komut)
        // eski surumlerde yok — varsa okunur, yoksa 0 kabul edilir.
        guard f.count >= 10 else { return nil }

        guard
            let alpha   = Int(f[1]),
            let target  = Int(f[2]),
            let vdc10   = Int(f[3]),
            let freq100 = Int(f[4]),
            let sync    = Int(f[5]),
            let fire    = Int(f[6]),
            let sec     = Int(f[7]),
            let noise   = Int(f[8]),
            let fault   = Int(f[9])
        else { return nil }

        return Telemetry(
            alpha: alpha,
            targetAlpha: target,
            vdcTheoretical: Double(vdc10) / 10.0,
            frequency: Double(freq100) / 100.0,
            isSynced: sync == 1,
            isFiring: fire == 1,
            sector: (0...5).contains(sec) ? sec : nil,
            noiseCount: noise,
            faultCount: fault,
            badCommandCount: f.count >= 11 ? (Int(f[10]) ?? 0) : 0,
            controlMode: f.count >= 12 ? (ControlMode(rawValue: Int(f[11]) ?? 0) ?? .phase) : .phase,
            duty: f.count >= 13 ? (Int(f[12]) ?? 0) : 0,
            rpm:  f.count >= 14 ? (Int(f[13]) ?? 0) : 0,
            tachoAdc: f.count >= 15 ? (Int(f[14]) ?? 0) : 0
        )
    }
}
