//  UDPClient.swift (revised)
//  iOS UDP Delay Measurement Client – now supports optional local BIND
//  2025‑04‑19

import Foundation
import Network

// MARK: - Packet Types
enum PacketType: UInt16 {
    case initPacket        = 0
    case initResponse      = 1
    case initAck           = 2
    case serverDelay       = 3
    case disconnect        = 4
    case serverDisconnect  = 5
    case delayResult       = 6
}

// MARK: - Packet 0 (Init)
struct Packet0 {
    let type: UInt16 = PacketType.initPacket.rawValue
    let nameLength: UInt16
    let name: String

    func toData() -> Data {
        var data = Data()
        var t = type.littleEndian
        data.append(Data(bytes: &t, count: 2))

        var n = nameLength.littleEndian
        data.append(Data(bytes: &n, count: 2))

        let bytes = [UInt8](name.utf8)
        data.append(contentsOf: bytes)
        if bytes.count < 10 {                    // 10 byte 고정 필드
            data.append(contentsOf: [UInt8](repeating: 0, count: 10 - bytes.count))
        }
        return data
    }
}

// MARK: - Packet 2 (Ack)
struct Packet2 {
    let type: UInt16 = PacketType.initAck.rawValue
    let clientID: UInt32
    let res: UInt8  = 0

    func toData() -> Data {
        var data = Data()
        var t = type.littleEndian
        data.append(Data(bytes: &t, count: 2))
        data.append(contentsOf: [0, 0])          // padding

        var cid = clientID.littleEndian
        data.append(Data(bytes: &cid, count: 4))
        data.append(contentsOf: [res])
        return data
    }
}

// MARK: - Packet 3 (공용 송수신)
struct Packet3 {
    var type: UInt16 = PacketType.serverDelay.rawValue
    var clientID: UInt32
    var seqNum: UInt32
    var timeClientSend: UInt64
    var timeServerRecv: UInt64
    var timeServerSend: UInt64
    var timeClientRecv: UInt64

    func toData() -> Data {
        var d = Data()
        var t = type.littleEndian
        d.append(Data(bytes: &t, count: 2))
        d.append(contentsOf: [0, 0])

        var cid = clientID.littleEndian
        d.append(Data(bytes: &cid, count: 4))

        var sn = seqNum.littleEndian
        d.append(Data(bytes: &sn, count: 4))

        var ts0 = timeClientSend.littleEndian;
        d.append(Data(bytes: &ts0, count: 8))
        var ts1 = timeServerRecv.littleEndian;
        d.append(Data(bytes: &ts1, count: 8))
        var ts2 = timeServerSend.littleEndian;
        d.append(Data(bytes: &ts2, count: 8))
        var ts3 = timeClientRecv.littleEndian;
        d.append(Data(bytes: &ts3, count: 8))

        return d
    }

    init?(data: Data) {
        guard data.count >= 44 else { return nil }

        let raw = data.withUnsafeBytes { $0 }

        self.clientID       = UInt32(littleEndian: raw.load(fromByteOffset: 4,  as: UInt32.self))
        self.seqNum         = UInt32(littleEndian: raw.load(fromByteOffset: 8,  as: UInt32.self))
        self.timeClientSend = UInt64(littleEndian: raw.load(fromByteOffset: 16, as: UInt64.self))
        self.timeServerRecv = UInt64(littleEndian: raw.load(fromByteOffset: 24, as: UInt64.self))
        self.timeServerSend = UInt64(littleEndian: raw.load(fromByteOffset: 32, as: UInt64.self))
        self.timeClientRecv = UInt64(littleEndian: raw.load(fromByteOffset: 40, as: UInt64.self))
    }

    init(clientID: UInt32, seqNum: UInt32, timeClientSend: UInt64) {
        self.clientID = clientID
        self.seqNum = seqNum
        self.timeClientSend = timeClientSend
        self.timeServerRecv = 0
        self.timeServerSend = 0
        self.timeClientRecv = 0
    }
}


// MARK: - Packet 6 (Delay Result)
struct Packet6 {
    let type: UInt16 = PacketType.delayResult.rawValue
    let clientID: UInt32
    let seqNum: UInt32
    let delay: UInt64

    func toData() -> Data {
        var d = Data()
        var t = type.littleEndian ; d.append(Data(bytes: &t, count: 2))
        d.append(contentsOf: [0, 0])

        var cid = clientID.littleEndian ; d.append(Data(bytes: &cid, count: 4))
        var sn  = seqNum.littleEndian   ; d.append(Data(bytes: &sn,  count: 4))
        var dl  = delay.littleEndian    ; d.append(Data(bytes: &dl,  count: 8))
        return d
    }
}

// MARK: - UDPClient (로컬 BIND 지원)
final class UDPClient: ObservableObject {

    // MARK: Internal State
    private let connection: NWConnection
    private var clientID: UInt32 = 0
    private var seqNum:  UInt32  = 0
    private let clientName: String
    private var timer: DispatchSourceTimer?

    // MARK: Init
    init(
        remoteHost: String,
        remotePort: UInt16,
        localPort:  UInt16? = nil,
        localAddress: String? = nil,
        name: String = "iOS_Client"
    ) {
        clientName = name

        // 1️⃣ NWParameters
        let params = NWParameters.udp
        params.allowLocalEndpointReuse = true

        // 2️⃣ Optional local bind
        if let lPort = localPort {
            let hostPart: NWEndpoint.Host
            if let lAddr = localAddress {
                if let v4 = IPv4Address(lAddr) { hostPart = .ipv4(v4) }
                else if let v6 = IPv6Address(lAddr) { hostPart = .ipv6(v6) }
                else { fatalError("Invalid localAddress \(lAddr)") }
            } else {
                hostPart = .ipv4(IPv4Address("0.0.0.0")!)
            }
            let localEP = NWEndpoint.hostPort(
                host: hostPart,
                port: .init(rawValue: lPort)!
            )
            params.requiredLocalEndpoint = localEP
        }

        // 3️⃣ Remote endpoint
        let remoteHostPart: NWEndpoint.Host
        if let v4 = IPv4Address(remoteHost) {
            remoteHostPart = .ipv4(v4)
        } else if let v6 = IPv6Address(remoteHost) {
            remoteHostPart = .ipv6(v6)
        } else {
            remoteHostPart = .name(remoteHost, nil)
        }
        let remoteEP = NWEndpoint.hostPort(
            host: remoteHostPart,
            port: .init(rawValue: remotePort)!
        )

        // 4️⃣ Connection
        connection = NWConnection(to: remoteEP, using: params)
    }

    // MARK: Public API
    func start() {
        print("▶️ UDPClient.start()  localBind=\(connection.parameters.requiredLocalEndpoint != nil)")

        connection.stateUpdateHandler = { [weak self] state in
            guard let self = self else { return }
            print("🔀 NW state → \(state)")

            switch state {
            case .ready:
                self.sendInit()
                self.receiveLoop()
            case .failed(let err):
                print("❌ failed:", err)
            default:
                break
            }
        }
        connection.start(queue: .main)
    }

    func disconnect() {
        timer?.cancel()
        connection.cancel()
    }

    // MARK: - Packet I/O
    private func sendInit() {
        let pkt = Packet0(
            nameLength: UInt16(clientName.utf8.count),
            name: clientName
        )
        connection.send(content: pkt.toData(),
        completion: .contentProcessed{ err in
            if let e = err { print("Init send err:", e) }
            else           { print("➡️ Sent init packet") }
        })
    }

    private func receiveLoop() {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 1024) { [weak self] data, _, _, err in
            guard let self = self else { return }
            if let d = data { self.handle(d) }
            if err == nil   { self.receiveLoop() }
        }
    }

    private func handle(_ data: Data) {
        guard data.count >= 2 else { return }
        let kind = UInt16(littleEndian: data.withUnsafeBytes {
            $0.load(fromByteOffset: 0, as: UInt16.self)
        })
        guard let type = PacketType(rawValue: kind) else { return }

        switch type {

        case .initResponse:
            let ok = data[2] == 0
            if ok {
                clientID = UInt32(littleEndian: data.withUnsafeBytes {
                    $0.load(fromByteOffset: 4, as: UInt32.self)
                })
                print("🔗 Connected  clientID=\(clientID)")

                let ack = Packet2(clientID: clientID)
                connection.send(content: ack.toData(), completion: .contentProcessed { _ in })
                startDelayTimer()
            } else {
                print("🚫 init refused")
            }

        case .serverDelay:
            if let p = Packet3(data: data) {
                let now   = DispatchTime.now().uptimeNanoseconds
                let delay = now - p.timeClientSend
                print("⏱ seq=\(p.seqNum)  delay=\(delay) ns")

                let res = Packet6(clientID: p.clientID, seqNum: p.seqNum, delay: delay)
                connection.send(content: res.toData(), completion: .contentProcessed { _ in })
            }

        case .serverDisconnect:
            print("🔴 server disconnect")
            disconnect()

        default:
            break
        }
    }

    private func startDelayTimer() {
        let src = DispatchSource.makeTimerSource(queue: .global())
        src.schedule(deadline: .now() + 1, repeating: 1)

        src.setEventHandler(qos: .unspecified, flags: []) { [weak self] in
            guard let self = self else { return }

            let now = DispatchTime.now().uptimeNanoseconds
            let pkt = Packet3(
                clientID: self.clientID,
                seqNum:   self.seqNum,
                timeClientSend: now
            )

            self.connection.send(
                content: pkt.toData(),
                completion: .contentProcessed { _ in }
            )
            self.seqNum += 1
        }

        src.resume()
        timer = src
    }

}

/* ─────────────────────────────────────────────
   사용 예시:

   let client = UDPClient(
       remoteHost: "192.168.205.181",
       remotePort: 12345,
       localPort: 12346           // 필요시
   )
   client.start()
   ───────────────────────────────────────────── */

