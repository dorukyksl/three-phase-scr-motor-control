//
//  BLEManager.swift
//  DC Motor Regulator
//
//  HM-10 (CC2541) seffaf UART koprusu ile CoreBluetooth baglantisi.
//
//  HM-10 servis/karakteristik UUID'leri sabittir:
//      servis          FFE0
//      karakteristik   FFE1   (write + notify)
//
//  NOT: BLE bildirimleri 20 baytlik paketler halinde gelir; bir telemetri
//  satiri birden fazla pakete bolunur.  Bu yuzden gelen veri tampona
//  yazilir ve satir sonuna (\n) gore ayristirilir.
//

import Foundation
import CoreBluetooth

/// Taramada bulunan bir cihaz.
struct DiscoveredDevice: Identifiable, Equatable {
    let id: UUID
    let name: String
    let rssi: Int
    let advertisesUART: Bool
}

@Observable
final class BLEManager: NSObject {

    // MARK: - HM-10 sabitleri
    private static let serviceUUID = CBUUID(string: "FFE0")
    private static let charUUID    = CBUUID(string: "FFE1")

    // MARK: - Disari acilan durum
    enum ConnectionState: Equatable {
        case poweredOff
        case unauthorized
        case idle
        case scanning
        case connecting(String)
        case connected(String)

        var isConnected: Bool { if case .connected = self { return true }; return false }

        var label: String {
            switch self {
            case .poweredOff:          return "Bluetooth kapalı"
            case .unauthorized:        return "Bluetooth izni yok"
            case .idle:                return "Bağlı değil"
            case .scanning:            return "Taranıyor…"
            case .connecting(let n):   return "\(n) — bağlanıyor…"
            case .connected(let n):    return n
            }
        }
    }

    private(set) var state: ConnectionState = .idle
    private(set) var devices: [DiscoveredDevice] = []
    private(set) var telemetry: Telemetry = .placeholder
    /// Arduino'dan gelen '#' ile baslayan son mesaj (uyari / hata).
    private(set) var lastMessage: String = ""
    /// Son telemetri satirinin geldigi an — veri akiyor mu anlamak icin.
    private(set) var lastPacketAt: Date?

    // --- tani: BLE'den gercekte ne geliyor? ---
    /// Baglantidan bu yana FFE1'den alinan toplam bayt.
    private(set) var rxByteCount: Int = 0
    /// Cozumlensin cozumlenmesin, alinan son tam satir (ham).
    private(set) var lastRawLine: String = ""
    /// Cozumlenemeyen satir sayisi.
    private(set) var badLineCount: Int = 0

    // MARK: - Ic durum
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var uartChar: CBCharacteristic?
    private var rxBytes: [UInt8] = []
    private var heartbeatTimer: Timer?
    private var lastAlphaSentAt: Date = .distantPast
    private var pendingAlpha: Int?
    private var lastDutySentAt: Date = .distantPast
    private var pendingDuty: Int?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    // MARK: - Tarama / baglanti

    func startScan() {
        guard central.state == .poweredOn else { return }
        devices.removeAll()
        state = .scanning
        // nil ile tararız: bazı HM-10 klonları FFE0'ı reklamda yayınlamıyor.
        central.scanForPeripherals(withServices: nil,
                                   options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
    }

    func stopScan() {
        central.stopScan()
        if case .scanning = state { state = .idle }
    }

    func connect(_ device: DiscoveredDevice) {
        guard let p = central.retrievePeripherals(withIdentifiers: [device.id]).first else { return }
        central.stopScan()
        peripheral = p
        p.delegate = self
        state = .connecting(device.name)
        central.connect(p, options: nil)
    }

    /// Once ateşlemeyi kapatip sonra baglantiyi keser.
    func disconnect() {
        if state.isConnected { send("D\n") }
        stopHeartbeat()
        if let p = peripheral { central.cancelPeripheralConnection(p) }
    }

    // MARK: - Komutlar

    /// Ateşlemeyi ac. Arduino guvenli konum sarti saglanmazsa reddeder.
    func enableFiring()  { send("E\n") }
    /// Ateşlemeyi aninda kapat.
    func disableFiring() { send("D\n") }
    /// Hemen bir telemetri satiri iste.
    func requestTelemetry() { send("?\n") }

    /// Hedef tetikleme acisini ayarla (20...150) ve UYGULAMA moduna gec.
    /// Slider surtunurken saniyede ~7 komuttan fazlasini gondermez.
    func setAlpha(_ value: Int, force: Bool = false) {
        let clamped = min(150, max(20, value))
        pendingAlpha = clamped
        let elapsed = Date().timeIntervalSince(lastAlphaSentAt)
        if force || elapsed > 0.15 {
            flushAlpha()
        }
    }

    /// Slider birakildiginda son degeri kesin gonder.
    func commitAlpha() { flushAlpha() }

    /// BURST moduna gec ve doluluk oranini ayarla (%0..100).
    func setBurst(_ duty: Int, force: Bool = false) {
        let clamped = min(100, max(0, duty))
        pendingDuty = clamped
        if force || Date().timeIntervalSince(lastDutySentAt) > 0.15 { flushDuty() }
    }

    /// Doluluk slider'i birakildiginda son degeri kesin gonder.
    func commitDuty() { flushDuty() }

    /// Faz kontrolu moduna don.
    func usePhaseMode() { send("F\n") }

    private func flushDuty() {
        guard let d = pendingDuty else { return }
        send("B\(d)\n")
        pendingDuty = nil
        lastDutySentAt = Date()
    }

    private func flushAlpha() {
        guard let a = pendingAlpha else { return }
        send("A\(a)\n")
        pendingAlpha = nil
        lastAlphaSentAt = Date()
    }

    private func send(_ text: String) {
        guard let p = peripheral, let c = uartChar,
              let data = text.data(using: .ascii) else { return }
        let type: CBCharacteristicWriteType =
            c.properties.contains(.writeWithoutResponse) ? .withoutResponse : .withResponse
        p.writeValue(data, for: c, type: type)
    }

    // MARK: - Heartbeat
    //
    // Arduino'da 3 saniyelik emniyet var: ilk 'H' geldikten sonra komut
    // akisi kesilirse ateşleme kapanir.  Uygulama bagliyken saniyede bir
    // 'H' gondererek bunu besliyoruz; uygulama kapanirsa motor duruyor.

    private func startHeartbeat() {
        stopHeartbeat()
        heartbeatTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            self?.send("H\n")
        }
    }

    private func stopHeartbeat() {
        heartbeatTimer?.invalidate()
        heartbeatTimer = nil
    }

    // MARK: - Gelen veriyi satirlara ayir

    /// Gelen baytlari tamponlar ve satir sonlarina gore ayirir.
    ///
    /// DIKKAT — burada String tamponu KULLANILMAZ.  Arduino `println` ile
    /// satirlari "\r\n" olarak bitirir; Swift'te "\r\n" TEK bir Character
    /// (grapheme cluster) oldugu icin `firstIndex(of: "\n")` bunu asla
    /// bulamaz ve hicbir satir ayristirilamaz.  Bayt duzeyinde calisiyoruz;
    /// satir sonundaki '\r' zaten handle(line:) icinde kirpiliyor.
    private func ingest(_ data: Data) {
        rxByteCount += data.count
        rxBytes.append(contentsOf: data)

        // Tampon kacak olursa siniri koru
        if rxBytes.count > 4096 { rxBytes.removeFirst(rxBytes.count - 1024) }

        while let idx = rxBytes.firstIndex(of: 0x0A) {          // '\n'
            let lineBytes = rxBytes[..<idx]
            rxBytes.removeFirst(idx + 1)
            if let line = String(bytes: lineBytes, encoding: .utf8) {
                handle(line: line)
            }
        }
    }

    private func handle(line: String) {
        let t = line.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !t.isEmpty else { return }
        lastRawLine = t

        if let parsed = Telemetry.parse(t) {
            telemetry = parsed
            lastPacketAt = Date()
        } else if t.hasPrefix("#") {
            lastMessage = String(t.dropFirst()).trimmingCharacters(in: .whitespaces)
        } else {
            badLineCount += 1
        }
    }
}

// MARK: - CBCentralManagerDelegate

extension BLEManager: CBCentralManagerDelegate {

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            state = .idle
            startScan()
        case .unauthorized:
            state = .unauthorized
        default:
            state = .poweredOff
            peripheral = nil
            uartChar = nil
            stopHeartbeat()
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {

        let uuids = (advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID]) ?? []
        let hasUART = uuids.contains(Self.serviceUUID)
        let advName = advertisementData[CBAdvertisementDataLocalNameKey] as? String
        let name = advName ?? peripheral.name ?? ""

        // Isimsiz ve UART yayinlamayan cihazlari listeye alma — ekrani bogar.
        guard hasUART || !name.isEmpty else { return }

        let device = DiscoveredDevice(id: peripheral.identifier,
                                      name: name.isEmpty ? "Bilinmeyen" : name,
                                      rssi: RSSI.intValue,
                                      advertisesUART: hasUART)

        if let i = devices.firstIndex(where: { $0.id == device.id }) {
            devices[i] = device
        } else {
            devices.append(device)
        }
        // HM-10 olma ihtimali yuksek olanlar once
        devices.sort { ($0.advertisesUART ? 0 : 1, -$0.rssi) < ($1.advertisesUART ? 0 : 1, -$1.rssi) }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.discoverServices([Self.serviceUUID])
    }

    func centralManager(_ central: CBCentralManager,
                        didFailToConnect peripheral: CBPeripheral, error: Error?) {
        state = .idle
        lastMessage = "Bağlanılamadı: \(error?.localizedDescription ?? "bilinmeyen hata")"
    }

    func centralManager(_ central: CBCentralManager,
                        didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        self.peripheral = nil
        uartChar = nil
        rxBytes.removeAll()
        telemetry = .placeholder
        lastPacketAt = nil
        rxByteCount = 0
        badLineCount = 0
        lastRawLine = ""
        stopHeartbeat()
        state = .idle
        if error != nil { lastMessage = "Bağlantı koptu — ateşleme 3 sn içinde kapanır" }
    }
}

// MARK: - CBPeripheralDelegate

extension BLEManager: CBPeripheralDelegate {

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let svc = peripheral.services?.first(where: { $0.uuid == Self.serviceUUID }) else {
            lastMessage = "Bu cihazda HM-10 UART servisi (FFE0) yok"
            central.cancelPeripheralConnection(peripheral)
            return
        }
        peripheral.discoverCharacteristics([Self.charUUID], for: svc)
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard let ch = service.characteristics?.first(where: { $0.uuid == Self.charUUID }) else {
            lastMessage = "FFE1 karakteristiği bulunamadı"
            central.cancelPeripheralConnection(peripheral)
            return
        }
        uartChar = ch
        peripheral.setNotifyValue(true, for: ch)
        state = .connected(peripheral.name ?? "HM-10")
        startHeartbeat()
        requestTelemetry()
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard let data = characteristic.value else { return }
        ingest(data)
    }
}
