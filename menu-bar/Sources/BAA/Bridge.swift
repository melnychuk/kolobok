import Darwin
import Foundation

// The Stick bridge's status socket (bridge/status_socket.py, protocol v1):
// newline-delimited JSON over a Unix socket in the per-user temp directory.
// Nothing here touches the disk.

enum FocusOutcome: Equatable, Sendable {
    case ok, busy, stale, failed
}

enum BridgeMessage: Equatable, Sendable {
    case status(BridgeStatus)
    case focus(id: Int, outcome: FocusOutcome)

    private struct Envelope: Decodable {
        let version: Int
        let type: String
        enum CodingKeys: String, CodingKey { case version = "v", type }
    }

    private struct Reply: Decodable {
        let id: Int
        let ok: Bool
        let error: String?
    }

    static func decode(_ line: Data) -> BridgeMessage? {
        let decoder = JSONDecoder()
        guard let envelope = try? decoder.decode(Envelope.self, from: line), envelope.version == 1 else { return nil }
        switch envelope.type {
        case "status":
            return (try? decoder.decode(BridgeStatus.self, from: line)).map { .status($0) }
        case "focus":
            guard let reply = try? decoder.decode(Reply.self, from: line) else { return nil }
            let outcome: FocusOutcome
            switch (reply.ok, reply.error) {
            case (true, _): outcome = .ok
            case (false, "busy"): outcome = .busy
            case (false, "stale"): outcome = .stale
            default: outcome = .failed
            }
            return .focus(id: reply.id, outcome: outcome)
        default:
            return nil
        }
    }
}

/// Splits a byte stream into lines, refusing to buffer an unending one.
struct LineFramer {
    static let maxLine = 1 << 20
    private var buffer = Data()

    /// The complete lines so far, or nil when a line outgrew `maxLine`.
    mutating func append(_ data: Data) -> [Data]? {
        buffer.append(data)
        var lines: [Data] = []
        while let newline = buffer.firstIndex(of: UInt8(ascii: "\n")) {
            lines.append(buffer[buffer.startIndex..<newline])
            buffer.removeSubrange(buffer.startIndex...newline)
        }
        return buffer.count > Self.maxLine ? nil : lines
    }
}

enum BridgeEvent: Sendable {
    case snapshot(Snapshot)
    case focus(id: Int, outcome: FocusOutcome)
}

/// Connects to the bridge, keeps reconnecting, and reports on the main actor.
/// All socket work happens on one background thread.
final class BridgeClient: @unchecked Sendable {
    /// The bridge repeats its status every 5 s; silence for longer means it's stuck.
    static let watchdog: TimeInterval = 12

    private let path: String
    private let handler: @MainActor (BridgeEvent) -> Void
    private let lock = NSLock()
    private var outgoing: [Data] = []
    private var nextID = 1
    private var stopped = false
    private var wake: [Int32] = [-1, -1]

    /// The same path the bridge binds: derived from the user, not from $TMPDIR.
    static func defaultPath() -> String {
        var buffer = [CChar](repeating: 0, count: Int(PATH_MAX))
        let length = confstr(_CS_DARWIN_USER_TEMP_DIR, &buffer, buffer.count)
        let base = length > 0 ? String(cString: buffer) : NSTemporaryDirectory()
        return (base as NSString).appendingPathComponent("stick-s3/bridge.sock")
    }

    init(path: String = BridgeClient.defaultPath(), handler: @escaping @MainActor (BridgeEvent) -> Void) {
        self.path = path
        self.handler = handler
    }

    func start() {
        guard pipe(&wake) == 0 else { return }
        for end in wake { _ = fcntl(end, F_SETFL, O_NONBLOCK) }
        let thread = Thread { [self] in run() }
        thread.name = "BAA bridge"
        thread.qualityOfService = .utility
        thread.start()
    }

    func stop() {
        lock.withLock { stopped = true }
        poke()
    }

    /// Asks the bridge to show an agent; the reply arrives as `.focus` with this id.
    func focus(_ key: String) -> Int {
        lock.lock()
        let id = nextID
        nextID += 1
        if let line = try? JSONSerialization.data(withJSONObject: ["id": id, "focus": key]) {
            outgoing.append(line + Data("\n".utf8))
        }
        lock.unlock()
        poke()
        return id
    }

    // MARK: Background thread

    private var isStopped: Bool { lock.withLock { stopped } }

    private func poke() {
        var byte: UInt8 = 1
        _ = write(wake[1], &byte, 1)
    }

    private func run() {
        var backoff: TimeInterval = 0.5
        var reported: Snapshot?
        func report(_ snapshot: Snapshot) {
            guard snapshot != reported else { return }
            reported = snapshot
            deliver(.snapshot(snapshot))
        }
        while !isStopped {
            guard let socket = connectSocket() else {
                report(.noBridge)
                waitForWake(timeout: backoff)
                backoff = min(backoff * 2, 5)
                continue
            }
            backoff = 0.5
            serve(socket) { report($0) }
            close(socket)
            report(.noBridge)
        }
        close(wake[0])
        close(wake[1])
    }

    private func serve(_ socket: Int32, report: (Snapshot) -> Void) {
        var framer = LineFramer()
        var lastMessage = ProcessInfo.processInfo.systemUptime
        var chunk = [UInt8](repeating: 0, count: 65_536)
        while !isStopped {
            guard flushOutgoing(socket) else { return }
            var fds = [pollfd(fd: socket, events: Int16(POLLIN), revents: 0),
                       pollfd(fd: wake[0], events: Int16(POLLIN), revents: 0)]
            if poll(&fds, 2, 1000) < 0 && errno != EINTR { return }
            if fds[1].revents != 0 { drainWake() }
            if fds[0].revents != 0 {
                let count = read(socket, &chunk, chunk.count)
                guard count > 0, let lines = framer.append(Data(chunk[0..<count])) else { return }
                for line in lines {
                    switch BridgeMessage.decode(line) {
                    case .status(let status)?:
                        lastMessage = ProcessInfo.processInfo.systemUptime
                        report(status.online ? .status(status) : .herdrUnavailable)
                    case .focus(let id, let outcome)?:
                        lastMessage = ProcessInfo.processInfo.systemUptime
                        deliver(.focus(id: id, outcome: outcome))
                    case nil:
                        continue
                    }
                }
            }
            if ProcessInfo.processInfo.systemUptime - lastMessage > Self.watchdog { return }
        }
    }

    private func connectSocket() -> Int32? {
        var address = sockaddr_un()
        address.sun_family = sa_family_t(AF_UNIX)
        let capacity = MemoryLayout.size(ofValue: address.sun_path)
        guard path.utf8.count < capacity else { return nil }
        withUnsafeMutableBytes(of: &address.sun_path) { raw in
            raw.copyBytes(from: path.utf8)
            raw[path.utf8.count] = 0
        }
        let socket = Darwin.socket(AF_UNIX, SOCK_STREAM, 0)
        guard socket >= 0 else { return nil }
        var on: Int32 = 1
        _ = setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &on, socklen_t(MemoryLayout<Int32>.size))
        let connected = withUnsafePointer(to: &address) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                connect(socket, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        guard connected == 0 else {
            close(socket)
            return nil
        }
        return socket
    }

    /// Sends queued focus requests; false if the connection broke.
    private func flushOutgoing(_ socket: Int32) -> Bool {
        let pending: [Data] = lock.withLock {
            defer { outgoing.removeAll() }
            return outgoing
        }
        for line in pending {
            let sent = line.withUnsafeBytes { write(socket, $0.baseAddress, $0.count) }
            if sent != line.count { return false }
        }
        return true
    }

    private func waitForWake(timeout: TimeInterval) {
        var fds = [pollfd(fd: wake[0], events: Int16(POLLIN), revents: 0)]
        if poll(&fds, 1, Int32(timeout * 1000)) > 0 { drainWake() }
    }

    private func drainWake() {
        var bytes = [UInt8](repeating: 0, count: 64)
        while read(wake[0], &bytes, bytes.count) > 0 {}
    }

    private func deliver(_ event: BridgeEvent) {
        let handler = handler
        DispatchQueue.main.async {
            MainActor.assumeIsolated { handler(event) }
        }
    }
}
