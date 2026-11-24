import Foundation

/// A fresh completion, staged like the Stick's transfer cue
/// (firmware/task_cues.h): the old active side charges with flickering green
/// light, the whole icon flashes green, and the final icon replaces it. The
/// counts are real throughout; this only stages how the change is revealed.
struct Celebration: Equatable {
    enum Frame: Equatable {
        /// The old icon, its blue side lit with charge or not.
        case charge(lit: Bool)
        /// The whole icon solid green, eyes cut out.
        case flash
        case finished
    }

    /// The charge flicker toggles here, ever faster, then holds lit until the flash.
    static let flickers: [TimeInterval] = [0, 0.2, 0.34, 0.45, 0.53, 0.59, 0.64]
    static let chargeLength: TimeInterval = 0.72
    static let flashLength: TimeInterval = 0.3

    /// When it started, in `ProcessInfo.systemUptime` seconds.
    let started: TimeInterval
    /// What the icon showed before the completion.
    let before: Snapshot

    /// A new completion: the done count rose between two live updates.
    /// Connecting to the bridge isn't one.
    static func starts(from old: Snapshot, to new: Snapshot) -> Bool {
        guard case .status(let old) = old, case .status(let new) = new else { return false }
        return new.done > old.done
    }

    /// Only active work charges up, as on the Stick; otherwise it simply flashes.
    var charges: Bool {
        if case .working = before.leftHint { return true }
        return false
    }

    func frame(at now: TimeInterval) -> Frame {
        let elapsed = now - started
        let flashAt = charges ? Self.chargeLength : 0
        if elapsed < 0 || elapsed >= flashAt + Self.flashLength { return .finished }
        if elapsed >= flashAt { return .flash }
        let flicker = Self.flickers.lastIndex { $0 <= elapsed } ?? 0
        return .charge(lit: flicker.isMultiple(of: 2))
    }
}
