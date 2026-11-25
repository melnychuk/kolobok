import Foundation

/// One agent as the bridge reports it: any kind Herdr runs (pi, claude, codex, copilot…).
struct Agent: Identifiable, Equatable, Sendable, Decodable {
    let key: String
    let agent: String
    let title: String
    let status: String
    /// The bridge's effective unread completion, the same one the Stick counts.
    let unread: Bool
    /// Where Herdr Desktop shows it: the space (project, plus the machine for
    /// remote ones), the tab, and a pane name you set. Other sources omit them.
    let space: String?
    let tab: String?
    let name: String?

    init(key: String, agent: String, title: String, status: String, unread: Bool,
         space: String? = nil, tab: String? = nil, name: String? = nil) {
        (self.key, self.agent, self.title, self.status, self.unread) = (key, agent, title, status, unread)
        (self.space, self.tab, self.name) = (space, tab, name)
    }

    var id: String { key }

    var statusLabel: String {
        if unread { return "Done" }
        switch status {
        case "working": return "Working"
        case "blocked": return "Needs attention"
        case "done": return "Done"
        case "idle": return "Idle"
        case "error": return "Failed"
        default: return "Unknown"
        }
    }

    var needsAttention: Bool { status == "blocked" || status == "error" }
}

/// What the bridge publishes on its status socket: the counts it sends the Stick, plus one row per agent.
struct BridgeStatus: Equatable, Sendable, Decodable {
    let online: Bool
    let active: Int  // Working and blocked, as on the Stick.
    let blocked: Int
    let done: Int
    let agents: [Agent]
}

enum Snapshot: Equatable, Sendable {
    /// Kolobok can't reach the bridge, so it can't know what the agents are doing.
    case noBridge
    /// The bridge is running but can't read Herdr.
    case herdrUnavailable
    case status(BridgeStatus)

    var agents: [Agent] {
        if case .status(let status) = self { return status.agents }
        return []
    }

    // The face and sides follow the Stick's counts, so the two always agree.
    var face: Face {
        switch self {
        case .noBridge: return .puzzled
        case .herdrUnavailable: return .asleep
        case .status(let status):
            if status.blocked > 0 { return .attention }
            if status.active > 0 { return .working }
            if status.done > 0 { return .done }
            return .idle
        }
    }

    /// The colored side left of the face. The green right side is `hasUnreadCompletion`.
    var leftHint: LeftHint? {
        guard case .status(let status) = self else { return nil }
        if status.blocked > 0 { return .attention }
        if status.active > 0 { return .working(status.active) }
        return nil
    }

    var hasUnreadCompletion: Bool {
        if case .status(let status) = self { return status.done > 0 }
        return false
    }

    var label: String {
        switch self {
        case .noBridge: return "Stick bridge not running"
        case .herdrUnavailable: return "Herdr unavailable"
        case .status(let status):
            if status.blocked > 0 { return "Needs attention" }
            if status.active > 0 { return "Working" }
            if status.done > 0 { return "Done" }
            return status.agents.isEmpty ? "No agents" : "Idle"
        }
    }
}

enum Preview: String, CaseIterable, Identifiable {
    case empty = "No agents", idle = "Idle", working = "Working"
    case blocked = "Needs attention", done = "Done", workingAndDone = "Working + done"
    case mixed = "Mixed", herdrUnavailable = "Herdr unavailable", noBridge = "No bridge"

    var id: String { rawValue }

    var snapshot: Snapshot {
        func agent(_ kind: String, in space: String, tab: String = "1", _ status: String,
                   unread: Bool = false, name: String = "") -> Agent {
            Agent(key: "demo:\(space)/\(tab)/\(kind)", agent: kind, title: name.isEmpty ? space : name,
                  status: status, unread: unread, space: space, tab: tab, name: name)
        }
        func online(_ agents: [Agent]) -> Snapshot {
            let active = agents.filter { $0.status == "working" || $0.status == "blocked" }.count
            return .status(BridgeStatus(online: true, active: active,
                                        blocked: agents.filter { $0.status == "blocked" }.count,
                                        done: agents.filter(\.unread).count, agents: agents))
        }
        switch self {
        case .empty: return online([])
        case .idle: return online([agent("pi", in: "docs", "idle")])
        case .working: return online([agent("claude", in: "stick-s3", "working")])
        case .blocked: return online([agent("codex", in: "stick-s3", "blocked")])
        case .done: return online([agent("copilot", in: "website", "done", unread: true)])
        case .workingAndDone: return online([
            agent("claude", in: "stick-s3", "working"), agent("pi", in: "stick-s3", tab: "2", "working"),
            agent("copilot", in: "website", "done", unread: true)
        ])
        case .mixed: return online([
            agent("codex", in: "stick-s3", "blocked"), agent("claude", in: "stick-s3", tab: "2", "working"),
            agent("copilot", in: "website on devbox", "done", unread: true),
            agent("pi", in: "docs", "idle", name: "Release notes")
        ])
        case .herdrUnavailable: return .herdrUnavailable
        case .noBridge: return .noBridge
        }
    }
}
