import AppKit
import XCTest
@testable import CopilotMenuBar

final class StatusTests: XCTestCase {
    func testReadsOnlyCopilotAndUsesHerdrSeenStatus() throws {
        let data = Data(#"""
        {"spaces":[{"tabs":[
          {"name":"Workspace","terminals":[
            {"terminal":"t1","app":"pi","status":"blocked"},
            {"terminal":"t2","app":"copilot","status":"done","name":" Named "},
            {"terminal":"t3","app":"Copilot","status":"working"},
            {"terminal":"t4","app":null,"status":null}
          ]},
          {"name":" ","terminals":[
            {"terminal":"t2","app":"copilot","status":"done"},
            {"terminal":"t5","app":"copilot","status":"idle"}
          ]}
        ]}]}
        """#.utf8)
        let world = try JSONDecoder().decode(HerdrSnapshot.self, from: data)
        XCTAssertEqual(world.snapshot.sessions, [
            Session(id: "t3", title: "Workspace", status: .working),
            Session(id: "t2", title: "Named", status: .done),
            Session(id: "t5", title: "Copilot CLI", status: .idle)
        ])
        XCTAssertEqual(world.snapshot.status, .working)
    }

    func testEmptyAndUnrecognizedStatusesAreNotOfflineOrIdle() throws {
        let data = Data(#"{"spaces":[]}"#.utf8)
        let world = try JSONDecoder().decode(HerdrSnapshot.self, from: data)
        XCTAssertEqual(world.snapshot, .online([]))
        XCTAssertNotEqual(world.snapshot.label, Snapshot.offline.label)
        XCTAssertEqual(SessionStatus(herdrStatus: nil), .unknown)
        XCTAssertEqual(SessionStatus(herdrStatus: "future-state"), .unknown)
        XCTAssertEqual(SessionStatus(herdrStatus: "error"), .failed)
    }

    func testRollupUsesHighestPriorityRegardlessOfInputOrder() {
        let statuses = Array(SessionStatus.allCases.reversed())
        let sessions = statuses.map { Session(id: $0.rawValue, title: $0.label, status: $0) }
        for count in 1...sessions.count {
            XCTAssertEqual(Snapshot.online(Array(sessions.prefix(count))).status, statuses[count - 1])
        }
        XCTAssertEqual(Preview.mixed.snapshot.status, .blocked)
    }

    func testPreviewCoversEveryStatusAndEmptyOffline() {
        XCTAssertEqual(Set(Preview.allCases.compactMap { $0.snapshot.status }), Set(SessionStatus.allCases))
        XCTAssertEqual(Preview.empty.snapshot, .online([]))
        XCTAssertEqual(Preview.offline.snapshot, .offline)
    }

    func testMenuBadgeKeepsCompletionIndependentOfAttentionAndWorking() {
        let mixed = Preview.mixed.snapshot
        XCTAssertTrue(mixed.hasUnreadCompletion)
        XCTAssertEqual(mixed.menuBadge, .attention)
        XCTAssertEqual(Preview.failed.snapshot.menuBadge, .attention)
        let workingAndDone = Preview.workingAndDone.snapshot
        XCTAssertTrue(workingAndDone.hasUnreadCompletion)
        XCTAssertEqual(workingAndDone.menuBadge, .working(2))
        XCTAssertTrue(Preview.done.snapshot.hasUnreadCompletion)
        XCTAssertEqual(Preview.done.snapshot.menuBadge, .none)
        XCTAssertFalse(Preview.working.snapshot.hasUnreadCompletion)
        XCTAssertEqual(Preview.working.snapshot.menuBadge, .working(1))
        XCTAssertFalse(Preview.blocked.snapshot.hasUnreadCompletion)
    }

    @MainActor
    func testAttentionAnimationChangesWeightWithoutChangingWidth() {
        let regular = AttentionMark.title(bold: false)
        let bold = AttentionMark.title(bold: true)
        XCTAssertEqual(regular.string, "!")
        XCTAssertEqual(bold.string, regular.string)
        XCTAssertNotEqual(regular.attribute(.font, at: 0, effectiveRange: nil) as? NSFont,
                          bold.attribute(.font, at: 0, effectiveRange: nil) as? NSFont)
        XCTAssertEqual(regular.size().width, bold.size().width, accuracy: 0.01)
    }

    @MainActor
    func testMascotHasTransparentBitmapTemplatesAtBothScales() {
        XCTAssertTrue(Mascot.image.isTemplate)
        XCTAssertEqual(Mascot.image.size, NSSize(width: 20, height: 20))
        let reps = Mascot.image.representations.compactMap { $0 as? NSBitmapImageRep }
        XCTAssertEqual(reps.map(\.pixelsWide), [20, 40])
        for rep in reps {
            let alphas = (0..<rep.pixelsHigh).flatMap { row in
                (0..<rep.pixelsWide).compactMap { column in rep.colorAt(x: column, y: row)?.alphaComponent }
            }
            XCTAssertEqual(alphas.min(), 0)
            XCTAssertEqual(alphas.max(), 1)
        }
    }
}
