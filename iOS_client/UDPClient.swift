// UDPClient.swift
// iOS UDP Delay Measurement Client

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

// MARK: - Packet Structures

/// 초기 연결 요청 (Packet 0)
struct Packet0 {
    let type: UInt16 = PacketType.initPacket.rawValue
    let nameLength: UInt16
    let name: String

    func toData() -> Data {
        var data = Data()
        var t = type.littleEndian
        data.append(Data(bytes: &t, count: 2))
        var nl = nameLength.littleEndian
        data.append(Data(bytes: &nl, count: 2))
        let nameBytes = [UInt8](name.utf8)
        data.append(contentsOf: nameBytes)
        if nameBytes.count < 10 {
            data.append(contentsOf: [UInt8](repeating: 0, count: 10 - nameBytes.count))
        }
        return data
    }
}

/// 초기 연결 응답 후 클라이언트 Ack (Packet 2)
struct Packet2 {
    let type: UInt16 = PacketType.initAck.rawValue
    let clientID: UInt32
    let res: UInt8 = 0

    func toData() -> Data {
        var data = Data()
        var t = type.littleEndian; data.append(Data(bytes: &t, count: 2))
        data.append(contentsOf: [0, 0]) // padding
        var cid = clientID.littleEndian; data.append(Data(bytes: &cid, count: 4))
        data.append(contentsOf: [res])
        return data
    }
}

/// Delay 전송용 Packet 3
struct Packet3Send {
    let type: UInt16 = PacketType.serverDelay.rawValue
    let clientID: UInt32
    let seqNum: UInt32
    let timeClientSend: UInt64

    func toData() -> Data {
        var data = Data()
        var t = type.littleEndian; data.append(Data(bytes: &t, count: 2))
        data.append(contentsOf: [0, 0]) // padding
        var cid = clientID.littleEndian;    data.append(Data(bytes: &cid, count: 4))
        var sn  = seqNum.littleEndian;      data.append(Data(bytes: &sn, count: 4))
        var ts  = timeClientSend.littleEndian; data.append(Data(bytes: &ts, count: 8))
        return data
    }
}

/// Server로부터 받은 Delay 응답 Packet 3
struct Packet3Receive {
    let clientID: UInt32
    let seqNum: UInt32
    let timeClientSend: UInt64
    let timeServerRecv: UInt64
    let timeServerSend: UInt64

    init?(data: Data) {
        guard data.count >= 36 else { return nil }
        // type (0-1), padding (2-3)
        clientID      = UInt32(littleEndian: data.withUnsafeBytes { $0.load(fromByteOffset: 4, as: UInt32.self) })
        seqNum        = UInt32(littleEndian: data.withUnsafeBytes { $0.load(fromByteOffset: 8, as: UInt32.self) })
        timeClientSend = UInt64(littleEndian: data.withUnsafeBytes { $0.load(fromByteOffset: 12, as: UInt64.self) })
        timeServerRecv = UInt64(littleEndian: data.withUnsafeBytes { $0.load(fromByteOffset: 20, as: UInt64.self) })
        timeServerSend = UInt64(littleEndian: data.withUnsafeBytes { $0.load(fromByteOffset: 28, as: UInt64.self) })
    }
}

/// Delay 결과 전송 Packet 6
struct Packet6 {
    let type: UInt16 = PacketType.delayResult.rawValue
    let clientID: UInt32
    let seqNum: UInt32
    let delay: UInt64

    func toData() -> Data {
        var data = Data()
        var t = type.littleEndian; data.append(Data(bytes: &t, count: 2))
        data.append(contentsOf: [0, 0]) // padding
        var cid = clientID.littleEndian; data.append(Data(bytes: &cid, count: 4))
        var sn  = seqNum.littleEndian;   data.append(Data(bytes: &sn, count: 4))
        var dly = delay.littleEndian;    data.append(Data(bytes: &dly, count: 8))
        return data
    }
}

// MARK: - UDP Client
class UDPClient : ObservableObject {
    private let connection: NWConnection
    private var clientID: UInt32 = 0
    private var seqNum: UInt32 = 0
    private let clientName = "iOS_Client"
    private var timer: DispatchSourceTimer?

    init(host: String, port: UInt16) {
        let params = NWParameters.udp
        connection = NWConnection(
            host: .init(host),
            port: .init(rawValue: port)!,
            using: params
        )
    }

    func start() {
        print("▶️ UDPClient.start() called")
        connection.stateUpdateHandler = { newState in
            print("▶️ [NW] stateUpdateHandler fired")
            print("🔀 [NW] connection state → \(newState)")
            switch newState {
            case .setup:
                print("🔧 setup")
            case .preparing:
                print("⚙️ preparing")
            case .waiting(let err):
                print("⏳ waiting: \(err.localizedDescription)")
            case .ready:
                print("✅ ready")
                self.sendInit()
                self.receiveLoop()
            case .failed(let err):
                print("❌ failed: \(err.localizedDescription)")
            case .cancelled:
                print("🛑 cancelled")
            @unknown default:
                print("❓ unknown state")
            }
        }
        connection.start(queue: .main)  
    }
    private func sendInit() {
        let pkt = Packet0(nameLength: UInt16(clientName.utf8.count), name: clientName)
        connection.send(content: pkt.toData(), completion: .contentProcessed { error in
            if let e = error { print("Init send error: \(e)") }
            else { print("➡️ Sent init packet") }
        })
    }

    private func receiveLoop() {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 1024) { data, _, _, error in
            if let data = data {
                self.handleReceived(data: data)
            }
            if error == nil {
                self.receiveLoop()
            }
        }
    }

    private func handleReceived(data: Data) {
        guard data.count >= 2 else { return }
        let raw = UInt16(littleEndian: data.withUnsafeBytes { $0.load(fromByteOffset: 0, as: UInt16.self) })
        guard let pktType = PacketType(rawValue: raw) else { return }

        switch pktType {
        case .initResponse:
            let res = data.withUnsafeBytes { $0.load(fromByteOffset: 2, as: UInt8.self) }
            if res == 0 {
                clientID = UInt32(littleEndian: data.withUnsafeBytes { $0.load(fromByteOffset: 4, as: UInt32.self) })
                print("🔗 Connected, clientID=\(clientID)")
                // Ack
                let ack = Packet2(clientID: clientID)
                connection.send(content: ack.toData(), completion: .contentProcessed{ _ in })
                // Start Delay Timer
                self.startDelayTimer()
            } else {
                print("🚫 Init failed: code=\(res)")
            }

        case .serverDelay:
            if let pkt = Packet3Receive(data: data) {
                let now = DispatchTime.now().uptimeNanoseconds
                let measured = now - pkt.timeClientSend
                print("⏱ Seq=\(pkt.seqNum), Delay=\(measured) ns")
                let result = Packet6(clientID: pkt.clientID, seqNum: pkt.seqNum, delay: measured)
                connection.send(content: result.toData(), completion: .contentProcessed{ _ in })
            }

        case .serverDisconnect:
            print("🔴 Server requested disconnect")
            connection.cancel()
            timer?.cancel()

        default:
            break
        }
    }

    private func startDelayTimer() {
        let t = DispatchSource.makeTimerSource(queue: .global())
        t.schedule(deadline: .now() + 1.0, repeating: 1.0)
        t.setEventHandler { [weak self] in
            guard let self = self else { return }
            let now = DispatchTime.now().uptimeNanoseconds
            let pkt = Packet3Send(clientID: self.clientID, seqNum: self.seqNum, timeClientSend: now)
            self.connection.send(content: pkt.toData(), completion: .contentProcessed{ _ in })
            self.seqNum += 1
        }
        t.resume()
        self.timer = t
    }

    func disconnect() {
        var data = Data()
        var t = PacketType.disconnect.rawValue.littleEndian
        data.append(Data(bytes: &t, count: 2))
        data.append(contentsOf: [0,0,0,0,0,0]) // pad to 8 bytes
        connection.send(content: data, completion: .contentProcessed{ _ in })
        connection.cancel()
        timer?.cancel()
    }
}

// Usage Example (e.g. in your AppDelegate or ViewController)
//
//  UDPClient.swift
//  iOS_client
//
//  Created by 김수오 on 4/18/25.
//

