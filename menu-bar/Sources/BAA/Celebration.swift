import Foundation

/// A fresh completion, staged like the Stick's transfer cue
/// (firmware/task_cues.h): the old active side charges with flickering green
/// light, the face's eyes fly open in fright, green wipes across it from the
/// charged side until the icon is solid green, and the face comes back happy.
/// The counts are real throughout; this only stages how the change is revealed.
struct Celebration: Equatable {
    enum Frame: Equatable {
        /// The old icon, its blue side lit with charge or not.
        case charge(lit: Bool)
        /// The old icon, charged, its eyes wide open just before the flash.
        case fright
        /// Green sweeping across the old icon from the left, covering the
        /// frightened face; 0 to 1 of the way across.
        case wipe(Double)
        /// Solid green, no eyes: the avatar has vanished into it.
        case gone
        case finished
    }

    /// The charge flicker toggles here, ever faster, then holds lit into the fright.
    static let flickers: [TimeInterval] = [0, 0.2, 0.34, 0.45, 0.53, 0.59, 0.64]
    static let chargeLength: TimeInterval = 0.72
    static let frightLength: TimeInterval = 0.28
    static let wipeLength: TimeInterval = 0.24
    static let goneLength: TimeInterval = 0.18

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

    /// Only active work charges up, as on the Stick; otherwise the face is
    /// startled straight away.
    var charges: Bool {
        if case .working = before.leftHint { return true }
        return false
    }

    func frame(at now: TimeInterval) -> Frame {
        let elapsed = now - started
        let frightAt = charges ? Self.chargeLength : 0
        let wipeAt = frightAt + Self.frightLength
        let goneAt = wipeAt + Self.wipeLength
        if elapsed < 0 || elapsed >= goneAt + Self.goneLength { return .finished }
        if elapsed >= goneAt { return .gone }
        if elapsed >= wipeAt { return .wipe((elapsed - wipeAt) / Self.wipeLength) }
        if elapsed >= frightAt { return .fright }
        let flicker = Self.flickers.lastIndex { $0 <= elapsed } ?? 0
        return .charge(lit: flicker.isMultiple(of: 2))
    }

    /// The frightened eyes as the green spreads over the face, 0 to 1 of the
    /// way across: they start closing as it starts, squeezed half shut, then
    /// shut.
    static func eyes(wiped progress: Double) -> (face: Face, blinking: Bool) {
        progress < 0.4 ? (.wincing, false) : (.attention, true)  // The request face's lids, met.
    }
}
