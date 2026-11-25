import Darwin
import XCTest
@testable import Kolobok

final class BridgeTests: XCTestCase {
    func testDecodesFocusRepliesAndIgnoresOtherVersions() {
        func decode(_ text: String) -> BridgeMessage? { BridgeMessage.decode(Data(text.utf8)) }
        XCTAssertEqual(decode(#"{"v":1,"type":"focus","id":4,"ok":true}"#), .focus(id: 4, outcome: .ok))
        XCTAssertEqual(decode(#"{"v":1,"type":"focus","id":4,"ok":false,"error":"busy"}"#),
                       .focus(id: 4, outcome: .busy))
        XCTAssertEqual(decode(#"{"v":1,"type":"focus","id":4,"ok":false,"error":"stale"}"#),
                       .focus(id: 4, outcome: .stale))
        XCTAssertEqual(decode(#"{"v":1,"type":"focus","id":4,"ok":false,"error":"new"}"#),
                       .focus(id: 4, outcome: .failed))
        XCTAssertNil(decode(#"{"v":2,"type":"status"}"#))
        XCTAssertNil(decode("not json"))
    }

    func testFramerSplitsLinesAcrossChunksAndRefusesEndlessOnes() {
        var framer = LineFramer()
        XCTAssertEqual(framer.append(Data("ab".utf8)), [])
        XCTAssertEqual(framer.append(Data("c\nd\ne".utf8)), [Data("abc".utf8), Data("d".utf8)])
        XCTAssertEqual(framer.append(Data("\n".utf8)), [Data("e".utf8)])
        XCTAssertNil(framer.append(Data(repeating: 65, count: LineFramer.maxLine + 1)))
    }

    func testDefaultPathMatchesTheBridges() {
        XCTAssertTrue(BridgeClient.defaultPath().hasSuffix("/stick-s3/bridge.sock"))
        XCTAssertLessThan(BridgeClient.defaultPath().utf8.count, 104)  // sun_path
    }

    // A real socket: status arrives, a focus request round-trips, and losing the server reads as no bridge.
    @MainActor
    func testTalksToABridgeOverItsSocket() throws {
        let path = "/tmp/kolobok-\(getpid()).sock"
        let server = try XCTUnwrap(FakeBridge(path: path))
        defer { server.close() }
        var events: [BridgeEvent] = []
        let client = BridgeClient(path: path) { events.append($0) }
        client.start()
        defer { client.stop() }

        let peer = try XCTUnwrap(server.accept())
        server.send(peer, String(decoding: StatusTests.line, as: UTF8.self) + "\n")
        wait { events.contains { if case .snapshot(.status) = $0 { return true }; return false } }

        let id = client.focus("desktop:t2")
        let request = server.readLine(peer)
        XCTAssertEqual(try JSONSerialization.jsonObject(with: Data(request.utf8)) as? NSDictionary,
                       ["id": id, "focus": "desktop:t2"])
        server.send(peer, #"{"v":1,"type":"focus","id":\#(id),"ok":false,"error":"stale"}"# + "\n")
        wait { events.contains { if case .focus(id, .stale) = $0 { return true }; return false } }

        Darwin.close(peer)
        wait { if case .snapshot(.noBridge)? = events.last { return true }; return false }
    }

    @MainActor
    private func wait(_ condition: () -> Bool, file: StaticString = #filePath, line: UInt = #line) {
        let deadline = Date().addingTimeInterval(5)
        while !condition() && Date() < deadline {
            RunLoop.main.run(until: Date().addingTimeInterval(0.02))
        }
        XCTAssertTrue(condition(), "timed out", file: file, line: line)
    }
}

/// A minimal stand-in for bridge/status_socket.py.
private final class FakeBridge {
    let path: String
    let listener: Int32

    init?(path: String) {
        self.path = path
        unlink(path)
        listener = socket(AF_UNIX, SOCK_STREAM, 0)
        var address = sockaddr_un()
        address.sun_family = sa_family_t(AF_UNIX)
        withUnsafeMutableBytes(of: &address.sun_path) { raw in
            raw.copyBytes(from: path.utf8)
            raw[path.utf8.count] = 0
        }
        let bound = withUnsafePointer(to: &address) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                bind(listener, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        guard bound == 0, listen(listener, 1) == 0 else { return nil }
    }

    func accept() -> Int32? {
        var fds = [pollfd(fd: listener, events: Int16(POLLIN), revents: 0)]
        guard poll(&fds, 1, 5000) > 0 else { return nil }
        let peer = Darwin.accept(listener, nil, nil)
        return peer >= 0 ? peer : nil
    }

    func send(_ peer: Int32, _ text: String) {
        _ = text.withCString { write(peer, $0, strlen($0)) }
    }

    func readLine(_ peer: Int32) -> String {
        var line = Data()
        var byte: UInt8 = 0
        var fds = [pollfd(fd: peer, events: Int16(POLLIN), revents: 0)]
        while poll(&fds, 1, 5000) > 0, read(peer, &byte, 1) == 1, byte != UInt8(ascii: "\n") {
            line.append(byte)
        }
        return String(decoding: line, as: UTF8.self)
    }

    func close() {
        Darwin.close(listener)
        unlink(path)
    }
}
