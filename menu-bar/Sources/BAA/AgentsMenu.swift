import AppKit

// The agents menu: a plain, native NSMenu, as Apple recommends for menu bar
// extras, so the system supplies selection, keyboard navigation, VoiceOver
// and appearance. Agents come grouped under their Herdr spaces as native
// section headers; each row is one line: its name or kind, then its status
// (and tab) in secondary text, after a dot in the Stick's status color.

enum AgentsMenu {
    struct Actions {
        let target: AnyObject?
        let focus: Selector
        let preview: Selector
    }

    /// The menu's items for a snapshot. `problem` explains the last failed focus.
    @MainActor
    static func items(for snapshot: Snapshot, problem: String? = nil, preview: Preview? = nil,
                      actions: Actions? = nil) -> [NSMenuItem] {
        var items: [NSMenuItem] = []
        if let problem {
            let item = NSMenuItem(title: problem, action: nil, keyEquivalent: "")
            item.image = NSImage(systemSymbolName: "exclamationmark.triangle", accessibilityDescription: "Problem")
            item.isEnabled = false
            items += [item, .separator()]
        }
        if snapshot.agents.isEmpty {
            let item = NSMenuItem(title: snapshot.label, action: nil, keyEquivalent: "")
            item.subtitle = emptyHint(snapshot)
            item.isEnabled = false
            items.append(item)
        }
        for group in groups(snapshot.agents) {
            if let space = group.space { items.append(.sectionHeader(title: space)) }
            for agent in group.agents {
                let item = row(agent, inSpace: group.space != nil, shared: group.agents.count > 1)
                item.action = actions?.focus
                item.target = actions?.target
                items.append(item)
            }
        }
        if let preview {
            let states = NSMenu()
            for state in Preview.allCases {
                let item = NSMenuItem(title: state.rawValue, action: actions?.preview, keyEquivalent: "")
                item.target = actions?.target
                item.representedObject = state.rawValue
                item.state = state == preview ? .on : .off
                states.addItem(item)
            }
            let item = NSMenuItem(title: "Preview", action: nil, keyEquivalent: "")
            item.submenu = states
            items += [.separator(), item]
        }
        return items
    }

    /// Agents by space, in Herdr's order (as its sidebar lists them); agents
    /// without one, from sources that don't say, come in a group without a header.
    static func groups(_ agents: [Agent]) -> [(space: String?, agents: [Agent])] {
        var groups: [(space: String?, agents: [Agent])] = []
        for agent in agents {
            let space = agent.space.flatMap { $0.isEmpty ? nil : $0 }
            if let index = groups.firstIndex(where: { $0.space == space }) {
                groups[index].agents.append(agent)
            } else {
                groups.append((space, [agent]))
            }
        }
        return groups
    }

    /// Under its space's header an agent is named by a pane name you set, else
    /// its kind; the tab tells apart agents sharing a space. One line each: the
    /// name, then the details in the menu's secondary color.
    @MainActor
    private static func row(_ agent: Agent, inSpace: Bool, shared: Bool) -> NSMenuItem {
        let name = agent.name.flatMap { $0.isEmpty ? nil : $0 }
        let title = inSpace ? name ?? agent.agent : agent.title
        let tab = agent.tab.flatMap { $0.isEmpty || !shared || name != nil ? nil : "Tab \($0)" }
        let kind = inSpace && name == nil ? nil : agent.agent
        let details = [kind, agent.statusLabel, tab].compactMap { $0 }.joined(separator: " · ")
        // The plain title, for type-to-select and VoiceOver, reads the same.
        let item = NSMenuItem(title: "\(title)  \(details)", action: nil, keyEquivalent: "")
        let font = NSFont.menuFont(ofSize: 0)
        let text = NSMutableAttributedString(string: title, attributes: [.font: font])
        text.append(NSAttributedString(string: "  \(details)",
                                       attributes: [.font: font, .foregroundColor: NSColor.secondaryLabelColor]))
        item.attributedTitle = text
        item.image = dot(color(agent))
        item.representedObject = agent.key
        item.toolTip = "Show this agent in Herdr"
        return item
    }

    private static func emptyHint(_ snapshot: Snapshot) -> String? {
        switch snapshot {
        case .noBridge: return "Start bridge/macos_herdr_bridge.py --herdr-source desktop"
        case .herdrUnavailable: return "Open Herdr Desktop to see your agents"
        case .status: return nil
        }
    }

    private static func color(_ agent: Agent) -> NSColor {
        if agent.needsAttention { return StickColor.request }
        if agent.unread { return StickColor.done }
        if agent.status == "working" { return StickColor.working }
        return .tertiaryLabelColor
    }

    /// A small status dot; drawn when shown, so system colors follow the appearance.
    private static func dot(_ color: NSColor) -> NSImage {
        let image = NSImage(size: NSSize(width: 8, height: 8), flipped: false) { bounds in
            color.setFill()
            NSBezierPath(ovalIn: bounds).fill()
            return true
        }
        image.accessibilityDescription = nil  // The subtitle already says the status.
        return image
    }
}

/// What the menu shows: live bridge state, or a preview in demo mode.
@MainActor
final class MenuModel {
    let isDemo = CommandLine.arguments.contains("--demo")
    var preview: Preview = .mixed { didSet { onChange?() } }
    var snapshot: Snapshot { isDemo ? preview.snapshot : live }
    /// Why the last focus failed; shown once, the next time the menu opens.
    private(set) var problem: String?
    var onChange: (() -> Void)?

    private var live: Snapshot = .noBridge
    private var bridge: BridgeClient?
    private var pendingFocus: Int?

    func start() {
        guard !isDemo else { return }
        let bridge = BridgeClient { [weak self] event in self?.handle(event) }
        bridge.start()
        self.bridge = bridge
    }

    func clearProblem() { problem = nil }

    /// Focusing goes through the bridge, which checks the agent against Herdr first.
    func focus(key: String) {
        guard !isDemo, pendingFocus == nil, let bridge else { return }
        problem = nil
        let id = bridge.focus(key)
        pendingFocus = id
        Task { [weak self] in
            try? await Task.sleep(for: .seconds(6))
            guard let self, self.pendingFocus == id else { return }
            self.finishFocus(problem: "The bridge didn’t answer")
        }
    }

    private func handle(_ event: BridgeEvent) {
        switch event {
        case .snapshot(let next):
            if pendingFocus != nil && next == .noBridge { finishFocus(problem: "Lost the bridge") }
            guard live != next else { return }
            live = next
            onChange?()
        case .focus(let id, let outcome):
            guard id == pendingFocus else { return }
            switch outcome {
            case .ok: finishFocus(problem: nil)
            case .busy: finishFocus(problem: "Busy opening another agent")
            case .stale: finishFocus(problem: "That agent has gone or changed")
            case .failed: finishFocus(problem: "Couldn’t open that agent in Herdr")
            }
        }
    }

    private func finishFocus(problem: String?) {
        pendingFocus = nil
        self.problem = problem
        if problem != nil { NSSound.beep() }
    }
}
