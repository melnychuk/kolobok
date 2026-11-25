import XCTest
@testable import Kolobok

final class StatusTests: XCTestCase {
    // The shape bridge/status_socket.py publishes (see tests/test_status_socket.py): one line per message.
    static let line = Data(#"""
    {"v":1,"type":"status","online":true,"source":"desktop","active":2,"blocked":1,"done":1,
     "agents":[{"key":"desktop:t1","agent":"pi","title":"Docs","status":"working","unread":false},
               {"key":"desktop:t2","agent":"claude","title":"Build","status":"blocked","unread":false},
               {"key":"desktop:t3","agent":"codex","title":"Tests","status":"idle","unread":true}]}
    """#.replacingOccurrences(of: "\n", with: " ").utf8)

    func testDecodesTheBridgesStatusForAnyAgentKind() throws {
        guard case .status(let status)? = BridgeMessage.decode(Self.line) else {
            return XCTFail("Not a status")
        }
        XCTAssertEqual(status.agents.map(\.agent), ["pi", "claude", "codex"])
        XCTAssertEqual(status.agents.map(\.statusLabel), ["Working", "Needs attention", "Done"])
        let snapshot = Snapshot.status(status)
        XCTAssertEqual(snapshot.face, .attention)
        XCTAssertEqual(snapshot.leftHint, .attention)
        XCTAssertTrue(snapshot.hasUnreadCompletion)
        XCTAssertEqual(snapshot.label, "Needs attention")
    }

    func testFaceAndSidesFollowTheSticksCounts() {
        func status(active: Int = 0, blocked: Int = 0, done: Int = 0, agents: [Agent] = []) -> Snapshot {
            .status(BridgeStatus(online: true, active: active, blocked: blocked, done: done, agents: agents))
        }
        XCTAssertEqual(status(active: 3).face, .working)
        XCTAssertEqual(status(active: 3).leftHint, .working(3))  // Blocked agents count as active, as on the Stick.
        XCTAssertEqual(status(active: 3, done: 2).face, .working)
        XCTAssertTrue(status(active: 3, done: 2).hasUnreadCompletion)
        XCTAssertEqual(status(done: 1).face, .done)
        XCTAssertNil(status(done: 1).leftHint)
        XCTAssertEqual(status().face, .idle)
        XCTAssertEqual(status().label, "No agents")
        XCTAssertEqual(Snapshot.noBridge.face, .puzzled)
        XCTAssertEqual(Snapshot.herdrUnavailable.face, .asleep)
        XCTAssertNil(Snapshot.noBridge.leftHint)
        XCTAssertFalse(Snapshot.herdrUnavailable.hasUnreadCompletion)
        XCTAssertEqual(LeftHint.working(12).text, "9+")
    }

    func testPreviewsCoverEveryFaceAndSeveralAgentKinds() {
        // Every face a status can show; wincing, peeking and looking down only
        // pass by in a completion's celebration.
        XCTAssertEqual(Set(Preview.allCases.map(\.snapshot.face)),
                       Set(Face.allCases).subtracting([.wincing, .peeking, .lookingDown]))
        XCTAssertGreaterThan(Set(Preview.mixed.snapshot.agents.map(\.agent)).count, 2)
        XCTAssertEqual(Preview.mixed.snapshot.leftHint, .attention)
        XCTAssertEqual(Preview.workingAndDone.snapshot.leftHint, .working(2))
    }
}
