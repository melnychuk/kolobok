import AppKit
import XCTest
@testable import BAA

@MainActor
final class AgentsMenuTests: XCTestCase {
    private let actions = AgentsMenu.Actions(target: nil, focus: #selector(NSObject.copy), preview: #selector(NSObject.mutableCopy))

    private func rows(_ items: [NSMenuItem]) -> [String] {
        items.map { item in
            item.isSectionHeader ? "# \(item.title)" : [item.title, item.subtitle].compactMap { $0 }.joined(separator: " | ")
        }
    }

    func testAgentsAreGroupedBySpaceWithNativeHeadersInHerdrsOrder() {
        let items = AgentsMenu.items(for: Preview.mixed.snapshot, actions: actions)
        // One line each: no subtitles, details in secondary text after the name.
        XCTAssertTrue(items.allSatisfy { $0.subtitle == nil })
        let details = items[1].attributedTitle?.attribute(.foregroundColor, at: "codex  ".count, effectiveRange: nil)
        XCTAssertEqual(details as? NSColor, .secondaryLabelColor)
        XCTAssertEqual(rows(items), [
            "# stick-s3", "codex  Needs attention · Tab 1", "claude  Working · Tab 2",
            "# website on devbox", "copilot  Done",
            "# docs", "Release notes  pi · Idle"
        ])
        let agents = items.filter { !$0.isSectionHeader }
        XCTAssertEqual(agents.map { $0.representedObject as? String }, Preview.mixed.snapshot.agents.map(\.key))
        XCTAssertTrue(agents.allSatisfy { $0.action == #selector(NSObject.copy) && $0.image != nil })
        XCTAssertFalse(items.contains { $0.isSeparatorItem })  // Only the list.
    }

    func testASpaceKeepsItsPlaceAndGathersItsAgents() {
        func agent(_ key: String, _ space: String?) -> Agent {
            Agent(key: key, agent: "pi", title: key, status: "idle", unread: false, space: space, tab: "1")
        }
        let groups = AgentsMenu.groups([agent("a", "website"), agent("b", "stick-s3"), agent("c", "website"), agent("d", nil)])
        XCTAssertEqual(groups.map(\.space), ["website", "stick-s3", nil])
        XCTAssertEqual(groups.map { $0.agents.map(\.key) }, [["a", "c"], ["b"], ["d"]])
    }

    func testAgentsWithoutASpaceKeepTheirTitleAndKind() {
        let agent = Agent(key: "default:p1", agent: "claude", title: "Refactor", status: "working", unread: false)
        let snapshot = Snapshot.status(BridgeStatus(online: true, active: 1, blocked: 0, done: 0, agents: [agent]))
        XCTAssertEqual(rows(AgentsMenu.items(for: snapshot)), ["Refactor  claude · Working"])
    }

    func testEmptyStatesExplainThemselvesWithoutActions() {
        for snapshot in [Snapshot.noBridge, .herdrUnavailable, Preview.empty.snapshot] {
            let items = AgentsMenu.items(for: snapshot, actions: actions)
            XCTAssertEqual(items.count, 1)
            XCTAssertEqual(items[0].title, snapshot.label)
            XCTAssertNil(items[0].action)
        }
        XCTAssertNotNil(AgentsMenu.items(for: .noBridge)[0].subtitle)
    }

    func testAFailedFocusIsExplainedAboveTheList() {
        let items = AgentsMenu.items(for: Preview.working.snapshot, problem: "That agent has gone or changed")
        XCTAssertEqual(items[0].title, "That agent has gone or changed")
        XCTAssertNil(items[0].action)
        XCTAssertTrue(items[1].isSeparatorItem)
        XCTAssertEqual(rows(Array(items.dropFirst(2))), ["# stick-s3", "claude  Working"])
    }

    func testDemoAddsAPreviewSubmenuWithTheCurrentStateChecked() throws {
        let items = AgentsMenu.items(for: Preview.done.snapshot, preview: .done, actions: actions)
        let submenu = try XCTUnwrap(items.last?.submenu)
        XCTAssertEqual(items.last?.title, "Preview")
        XCTAssertEqual(submenu.items.map(\.title), Preview.allCases.map(\.rawValue))
        XCTAssertEqual(submenu.items.filter { $0.state == .on }.map(\.title), ["Done"])
    }
}
