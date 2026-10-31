//
//  ContentView.swift
//  DC Motor Regulator
//
//  Uc fazli tam kontrollu B6 dogrultucunun mobil kontrol paneli.
//  Kartta potansiyometre yok — hiz yalnizca buradan ayarlanir.
//

import SwiftUI

struct ContentView: View {
    @State private var ble = BLEManager()

    /// Ana kontrol degiskeni: hedef tetikleme acisi (20°...150°).
    /// Hiz yuzdesi bundan turetilir — tersi degil.
    @State private var targetAlpha: Double = 150

    /// Burst modunda doluluk orani (%0..100).
    @State private var dutyPct: Double = 0

    /// Olcum tablosu icin sik kullanilan acilar.
    private let presets = [150, 120, 105, 90, 75, 60, 45, 30]

    /// alpha 150° = %0 (durma),  alpha 20° = %100 (tam)
    private var speedPct: Double { (150.0 - targetAlpha) / 130.0 * 100.0 }

    /// Kaydiriciyi yuzde ekseninde surer, ama gercek deger alpha olarak saklanir.
    private var speedBinding: Binding<Double> {
        Binding(
            get: { speedPct },
            set: { newPct in
                targetAlpha = (150.0 - newPct / 100.0 * 130.0).rounded()
            }
        )
    }

    /// Hedef alpha'yi degistirir ve karta gonderir.
    private func setAlpha(_ value: Int, immediate: Bool = false) {
        targetAlpha = Double(min(150, max(20, value)))
        ble.setAlpha(Int(targetAlpha), force: immediate)
    }

    private var t: Telemetry { ble.telemetry }
    private var isLive: Bool {
        guard let last = ble.lastPacketAt else { return false }
        return Date().timeIntervalSince(last) < 2.0
    }

    var body: some View {
        NavigationStack {
            Group {
                if ble.state.isConnected {
                    dashboard
                } else {
                    connectScreen
                }
            }
            .navigationTitle("DC Motor Regülatörü")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    if ble.state.isConnected {
                        Button("Ayır", role: .destructive) { ble.disconnect() }
                    }
                }
            }
        }
    }

    // MARK: - Bağlantı ekranı

    private var connectScreen: some View {
        VStack(spacing: 0) {
            List {
                Section {
                    ForEach(ble.devices) { d in
                        Button { ble.connect(d) } label: {
                            HStack {
                                Image(systemName: d.advertisesUART ? "antenna.radiowaves.left.and.right" : "dot.radiowaves.right")
                                    .foregroundStyle(d.advertisesUART ? .green : .secondary)
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(d.name).font(.body.weight(.medium))
                                    if d.advertisesUART {
                                        Text("HM-10 UART (FFE0)").font(.caption).foregroundStyle(.green)
                                    }
                                }
                                Spacer()
                                Text("\(d.rssi) dBm").font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                            }
                        }
                        .tint(.primary)
                    }
                } header: {
                    Text(ble.state.label)
                } footer: {
                    Text("HM-10 genellikle **HMSoft**, **BT05** ya da **JDY-** ile başlayan bir adla görünür. Yeşil anten simgesi UART servisini yayınladığını gösterir.")
                }
            }

            if !ble.lastMessage.isEmpty {
                Text(ble.lastMessage)
                    .font(.footnote)
                    .foregroundStyle(.orange)
                    .padding(.horizontal)
                    .padding(.bottom, 8)
            }

            Button {
                ble.startScan()
            } label: {
                Label("Yeniden tara", systemImage: "arrow.clockwise")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .padding()
        }
    }

    // MARK: - Kontrol paneli

    private var dashboard: some View {
        ScrollView {
            VStack(spacing: 16) {
                statusStrip
                primaryReadout
                rpmCard
                controlCard
                if t.isFiring { stopCard } else { armCard }
                diagnosticsCard
                safetyNote
            }
            .padding()
        }
        .background(Color(.systemGroupedBackground))
        .onAppear {
            targetAlpha = Double(t.targetAlpha)
            dutyPct = Double(t.duty)
        }
    }

    // MARK: Durum şeridi

    private var statusStrip: some View {
        HStack(spacing: 8) {
            chip(t.isSynced ? "SENKRON" : "SENKRON YOK",
                 color: t.isSynced ? .green : .orange,
                 system: t.isSynced ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")

            chip(t.isFiring ? "ATEŞLEME AÇIK" : "ATEŞLEME KAPALI",
                 color: t.isFiring ? .red : .secondary,
                 system: t.isFiring ? "bolt.fill" : "bolt.slash")

            chip(isLive ? String(format: "%.2f Hz", t.frequency) : "veri yok",
                 color: isLive ? .blue : .secondary,
                 system: "waveform.path.ecg")
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private func chip(_ text: String, color: Color, system: String) -> some View {
        Label(text, systemImage: system)
            .font(.caption.weight(.semibold))
            .padding(.horizontal, 10).padding(.vertical, 6)
            .background(color.opacity(0.15), in: Capsule())
            .foregroundStyle(color)
    }

    // MARK: Ana gösterge

    private var primaryReadout: some View {
        VStack(spacing: 4) {
            Text("\(t.effectiveVdc, specifier: "%.1f")")
                .font(.system(size: 68, weight: .bold, design: .rounded))
                .monospacedDigit()
                .contentTransition(.numericText())
            Text("VOLT  ·  TEORİK")
                .font(.caption.weight(.bold))
                .foregroundStyle(.secondary)
                .kerning(1.5)

            if t.controlMode == .burst {
                Text("\(t.vdcTheoretical, specifier: "%.1f") V × %\(t.duty) doluluk")
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.orange)
                    .padding(.top, 2)
            }

            Text("α = \(t.alpha)°" + (t.isRamping ? "  →  \(t.targetAlpha)°" : ""))
                .font(.headline.monospacedDigit())
                .foregroundStyle(t.isRamping ? .orange : .primary)
                .padding(.top, 8)

            Text("Sektör \(t.sectorName)")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 20)
        .background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 16))
        .overlay(alignment: .topTrailing) {
            Text("hesaplanan\ndeğer")
                .font(.caption2)
                .multilineTextAlignment(.trailing)
                .foregroundStyle(.tertiary)
                .padding(12)
        }
    }

    // MARK: Devir göstergesi

    private var rpmCard: some View {
        HStack(alignment: .center, spacing: 16) {
            Image(systemName: "gauge.with.needle")
                .font(.system(size: 30))
                .foregroundStyle(t.rpm > 0 ? .green : .secondary)
                .frame(width: 44)

            VStack(alignment: .leading, spacing: 2) {
                Text("Mil hızı").font(.caption.weight(.semibold)).foregroundStyle(.secondary)
                Text(t.rpm > 0 ? "\(t.rpm)" : "—")
                    .font(.system(size: 40, weight: .bold, design: .rounded))
                    .monospacedDigit()
                    .contentTransition(.numericText())
                    .foregroundStyle(t.rpm > Telemetry.ratedRpm ? .orange
                                     : (t.rpm > 0 ? .primary : .secondary))
            }

            Spacer()

            VStack(alignment: .trailing, spacing: 2) {
                Text("dev/dk").font(.subheadline.weight(.semibold)).foregroundStyle(.secondary)
                Text(String(format: "%.2f V tako", t.tachoVolts))
                    .font(.caption2.monospacedDigit()).foregroundStyle(.tertiary)
                if t.rpm > Telemetry.ratedRpm {
                    Label("etiket üstü", systemImage: "exclamationmark.triangle.fill")
                        .font(.caption2.weight(.semibold))
                        .foregroundStyle(.orange)
                }
            }
        }
        .padding()
        .background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 16))
    }

    // MARK: Hız kontrolü

    private var controlCard: some View {
        VStack(alignment: .leading, spacing: 16) {

            // ---- Tetikleme açısı: doğrudan derece kontrolü ----
            // ---- Kontrol yöntemi ----
            Picker("", selection: Binding(
                get: { t.controlMode },
                set: { newMode in
                    if newMode == .phase { ble.usePhaseMode() }
                    else { ble.setBurst(Int(dutyPct), force: true) }
                })
            ) {
                Text("Faz kontrolü").tag(ControlMode.phase)
                Text("Burst firing").tag(ControlMode.burst)
            }
            .pickerStyle(.segmented)

            if t.controlMode == .burst {
                VStack(alignment: .leading, spacing: 10) {
                    HStack(alignment: .firstTextBaseline) {
                        Text("Doluluk").font(.headline)
                        Spacer()
                        Text("%\(Int(dutyPct))")
                            .font(.title2.bold().monospacedDigit())
                    }

                    Slider(value: $dutyPct, in: 0...100, step: 1) { editing in
                        if !editing { ble.commitDuty() }
                    }
                    .tint(t.isFiring ? .red : .orange)
                    .onChange(of: dutyPct) { _, new in ble.setBurst(Int(new)) }

                    Text("100 çevrimin %\(Int(dutyPct)) kadarı iletilir, kalanı bloklanır. Dalga hiç kırpılmaz.")
                        .font(.caption).foregroundStyle(.secondary)

                    Label("Yalnızca omik yük. Motorda tork darbeleri üretir.",
                          systemImage: "exclamationmark.triangle.fill")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.orange)
                }
                .padding(.vertical, 4)

                Divider()
            }

            HStack(alignment: .firstTextBaseline) {
                Text("Tetikleme açısı").font(.headline)
                Spacer()
                Text("%\(Int(speedPct.rounded())) hız")
                    .font(.subheadline.monospacedDigit())
                    .foregroundStyle(.secondary)
            }

            HStack(spacing: 14) {
                stepButton("minus", enabled: Int(targetAlpha) > 20) {
                    setAlpha(Int(targetAlpha) - 1, immediate: true)
                }

                VStack(spacing: 0) {
                    Text("\(Int(targetAlpha))°")
                        .font(.system(size: 44, weight: .bold, design: .rounded))
                        .monospacedDigit()
                        .contentTransition(.numericText())
                    Text("hedef α")
                        .font(.caption2.weight(.semibold))
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity)

                stepButton("plus", enabled: Int(targetAlpha) < 150) {
                    setAlpha(Int(targetAlpha) + 1, immediate: true)
                }
            }

            Slider(value: speedBinding, in: 0...100) { editing in
                if !editing { ble.commitAlpha() }
            }
            .tint(t.isFiring ? .red : .accentColor)
            .onChange(of: targetAlpha) { _, new in
                ble.setAlpha(Int(new))
            }

            HStack {
                Text("α 150° · durma").font(.caption2).foregroundStyle(.secondary)
                Spacer()
                Text("α 20° · tam").font(.caption2).foregroundStyle(.secondary)
            }

            // ---- Hazır açılar ----
            VStack(alignment: .leading, spacing: 8) {
                Text("Hazır açılar")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)

                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 8) {
                        ForEach(presets, id: \.self) { p in
                            Button {
                                setAlpha(p, immediate: true)
                            } label: {
                                Text("\(p)°")
                                    .font(.subheadline.weight(.semibold).monospacedDigit())
                                    .frame(minWidth: 46)
                                    .padding(.vertical, 8)
                            }
                            .buttonStyle(.bordered)
                            .tint(Int(targetAlpha) == p ? .accentColor : .secondary)
                        }
                    }
                    .padding(.horizontal, 1)
                }
            }

            Label("α rampalı değişir: 5°/sn. Hedef anında değişir, motor yavaş takip eder.",
                  systemImage: "info.circle")
                .font(.caption).foregroundStyle(.secondary)
        }
        .padding()
        .background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 16))
    }

    private func stepButton(_ icon: String, enabled: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: icon)
                .font(.title3.weight(.bold))
                .frame(width: 52, height: 52)
        }
        .buttonStyle(.bordered)
        .disabled(!enabled)
    }

    // MARK: Ateşleme aç / kapat

    private var armCard: some View {
        VStack(spacing: 10) {
            Button {
                ble.enableFiring()
            } label: {
                Label("ATEŞLEMEYİ AÇ", systemImage: "bolt.fill")
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 6)
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .disabled(!t.isSynced || !t.canEnableFiring)

            if !t.isSynced {
                Text("Senkronizasyon yok — trafoyu aç ve ZCD kartını kontrol et.")
                    .font(.caption).foregroundStyle(.orange)
                    .multilineTextAlignment(.center)
            } else if !t.canEnableFiring {
                Text("Hızı %0'a indirmeden Arduino ateşlemeyi kabul etmez.")
                    .font(.caption).foregroundStyle(.orange)
                    .multilineTextAlignment(.center)
            } else {
                Text("Hazır. Trafo şalteri açık ve DPDT anahtarı ARMED konumunda olmalı.")
                    .font(.caption).foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
            }
        }
        .padding()
        .background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 16))
    }

    private var stopCard: some View {
        Button(role: .destructive) {
            ble.disableFiring()
        } label: {
            Label("DURDUR", systemImage: "stop.fill")
                .font(.title3.bold())
                .frame(maxWidth: .infinity)
                .padding(.vertical, 12)
        }
        .buttonStyle(.borderedProminent)
        .tint(.red)
        .controlSize(.large)
    }

    // MARK: Tanılama

    private var diagnosticsCard: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Tanılama").font(.headline)

            row("Şebeke frekansı", String(format: "%.2f Hz", t.frequency))
            row("Aktif α", "\(t.alpha)°")
            row("Hedef α", "\(t.targetAlpha)°")
            row("Sektör", t.sectorName)
            row("Kontrol yöntemi", t.controlMode.label)
            if t.controlMode == .burst { row("Doluluk", "%\(t.duty)") }
            row("Mil hızı", t.rpm > 0 ? "\(t.rpm) dev/dk" : "—",
                tint: t.rpm > Telemetry.ratedRpm ? .orange : nil)
            row("Tako gerilimi", String(format: "%.3f V", t.tachoVolts))
            row("Tako ham ADC", "\(t.tachoAdc) / 1023")
            row("Yok sayılan gürültü", "\(t.noiseCount)")
            row("Senkron kaybı (hata)", "\(t.faultCount)",
                tint: t.faultCount > 0 ? .orange : nil)

            Divider()

            Text("BLE hattı").font(.caption.weight(.semibold)).foregroundStyle(.secondary)
            row("Alınan bayt", "\(ble.rxByteCount)",
                tint: ble.rxByteCount == 0 ? .red : .green)
            row("Çözümlenemeyen satır", "\(ble.badLineCount)",
                tint: ble.badLineCount > 0 ? .orange : nil)
            row("Arduino'nun attığı komut", "\(t.badCommandCount)",
                tint: t.badCommandCount > 0 ? .orange : nil)
            if ble.lastRawLine.isEmpty {
                Text("Henüz hiç satır gelmedi")
                    .font(.caption.monospaced())
                    .foregroundStyle(.red)
            } else {
                Text(ble.lastRawLine)
                    .font(.caption.monospaced())
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
            }

            if !ble.lastMessage.isEmpty {
                Divider()
                Label(ble.lastMessage, systemImage: "text.bubble")
                    .font(.caption).foregroundStyle(.orange)
            }
        }
        .padding()
        .background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 16))
    }

    private func row(_ k: String, _ v: String, tint: Color? = nil) -> some View {
        HStack {
            Text(k).font(.subheadline).foregroundStyle(.secondary)
            Spacer()
            Text(v).font(.subheadline.monospacedDigit().weight(.medium))
                .foregroundStyle(tint ?? .primary)
        }
    }

    // MARK: Güvenlik notu

    private var safetyNote: some View {
        VStack(alignment: .leading, spacing: 8) {
            Label("Güvenlik", systemImage: "exclamationmark.shield.fill")
                .font(.subheadline.bold())
                .foregroundStyle(.orange)
            Text("""
            Karttaki **DPDT FIRE_5V anahtarı** birincil donanım kilididir; bu uygulama onu geçemez. \
            Acil durumda **trafo şalterini** kes.

            Uygulama bağlantısı koparsa Arduino ateşlemeyi **3 saniye içinde** kendiliğinden kapatır.

            Ekrandaki gerilim **ölçülen değil, α'dan hesaplanan teorik** değerdir.
            """)
            .font(.caption)
            .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding()
        .background(Color.orange.opacity(0.10), in: RoundedRectangle(cornerRadius: 16))
    }
}

#Preview {
    ContentView()
}
