import Foundation

// First entry wins when several sessions need different kinds of attention.
enum SessionStatus: String, CaseIterable, Sendable {
    case failed, blocked, working, done, idle, unknown

    var label: String {
        switch self {
        case .failed: return "Failed"
        case .blocked: return "Needs attention"
        case .working: return "Working"
        case .done: return "Done"
        case .idle: return "Idle"
        case .unknown: return "Unknown"
        }
    }

    init(herdrStatus: String?) {
        self = herdrStatus == "error" ? .failed : Self(rawValue: herdrStatus ?? "") ?? .unknown
    }
}

struct Session: Identifiable, Equatable, Sendable {
    let id: String
    let title: String
    let status: SessionStatus
}

enum MenuBadge: Equatable {
    case none, working(Int), attention, unknown, offline
}

enum Snapshot: Equatable, Sendable {
    case offline
    case online([Session])

    var sessions: [Session] {
        if case .online(let sessions) = self { return sessions }
        return []
    }

    var status: SessionStatus? {
        SessionStatus.allCases.first { status in sessions.contains { $0.status == status } }
    }

    var label: String {
        if self == .offline { return "Herdr unavailable" }
        return status?.label ?? "No Copilot sessions"
    }

    var hasUnreadCompletion: Bool { sessions.contains { $0.status == .done } }

    var menuBadge: MenuBadge {
        if self == .offline { return .offline }
        switch status {
        case .failed, .blocked: return .attention
        case .working: return .working(sessions.filter { $0.status == .working }.count)
        case .unknown: return .unknown
        default: return .none
        }
    }
}

enum Preview: String, CaseIterable, Identifiable {
    case empty = "No sessions", idle = "Idle", working = "Working"
    case blocked = "Needs attention", done = "Done", failed = "Failed"
    case unknown = "Unknown", workingAndDone = "Working + done", mixed = "Mixed", offline = "Offline"

    var id: String { rawValue }

    var snapshot: Snapshot {
        func example(_ status: SessionStatus, _ title: String) -> Session {
            Session(id: title, title: title, status: status)
        }
        switch self {
        case .empty: return .online([])
        case .idle: return .online([example(.idle, "Docs cleanup")])
        case .working: return .online([example(.working, "Refactor settings")])
        case .blocked: return .online([example(.blocked, "Fix build warnings")])
        case .done: return .online([example(.done, "Add snapshot tests")])
        case .failed: return .online([example(.failed, "Update dependencies")])
        case .unknown: return .online([example(.unknown, "Start Copilot")])
        case .workingAndDone: return .online([
            example(.working, "Refactor settings"), example(.working, "Update dependencies"),
            example(.done, "Add snapshot tests")
        ])
        case .mixed: return .online([
            example(.blocked, "Fix build warnings"), example(.working, "Refactor settings"),
            example(.done, "Add snapshot tests"), example(.idle, "Docs cleanup")
        ])
        case .offline: return .offline
        }
    }
}

// Read the UI's status, including Herdr's shared done/seen state, not transcripts.
struct HerdrSnapshot: Decodable {
    struct Space: Decodable { let tabs: [Tab] }
    struct Tab: Decodable {
        let name: String
        let terminals: [Terminal]
    }
    struct Terminal: Decodable {
        let terminal: String
        let app: String?
        let name: String?
        let status: String?
    }

    let spaces: [Space]

    var snapshot: Snapshot {
        var sessions: [Session] = []
        var seen = Set<String>()
        for tab in spaces.flatMap(\.tabs) {
            for terminal in tab.terminals where terminal.app?.lowercased() == "copilot" {
                guard seen.insert(terminal.terminal).inserted else { continue }
                let title = [terminal.name, tab.name].compactMap {
                    $0?.trimmingCharacters(in: .whitespacesAndNewlines)
                }.first { !$0.isEmpty } ?? "Copilot CLI"
                sessions.append(Session(id: terminal.terminal, title: title,
                                        status: SessionStatus(herdrStatus: terminal.status)))
            }
        }
        return .online(SessionStatus.allCases.flatMap { status in sessions.filter { $0.status == status } })
    }
}
