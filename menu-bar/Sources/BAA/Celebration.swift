import Foundation

/// A fresh completion, staged like the Stick's transfer cue
/// (firmware/task_cues.h): the old active side charges with flickering green
/// light, the face jumps, eyes wide, a green starburst spins out over it and
/// blasts it away, leaving a solid green bar; then the face peeks back in
/// from the left, blinks, crouches and hops home onto it, landing happy. With
/// active work still showing on the left, it drops back in from above instead.
/// The counts are real throughout; this only stages how the change is revealed.
struct Celebration: Equatable {
    enum Frame: Equatable {
        /// The old icon, its blue side lit with charge or not.
        case charge(lit: Bool)
        /// The old icon, charged, its eyes wide open, the face jumping a
        /// point at first, just before the burst.
        case fright(jumped: Bool)
        /// A green star spinning out over the old icon in the sides' band,
        /// blasting the frightened face away; 0 to 1 of the way through.
        case burst(Double)
        /// A solid green bar, the sides' band right across: the face blasted away.
        case gone
        /// The face on its way back home in the new icon, 0 to 1 of the way
        /// through each beat (`act`).
        case back(Beat, Double)
        case finished
    }

    /// The way back home after the flash, in order.
    enum Beat: CaseIterable, Equatable {
        /// Sliding in from the left edge until one eye shows (or down from
        /// the top until both do).
        case creep
        /// Looking at the green (or down at its home), with a blink.
        case peek
        /// Crouching (or bracing a point back up) before the leap.
        case crouch
        /// Hopping home: stretched at take-off, a point up in the air (or
        /// falling, stretched).
        case jump
        /// Squashed on landing, then springing back up, happy.
        case land, rebound

        var length: TimeInterval {
            switch self {
            case .creep: return 0.22
            case .peek: return 0.34
            case .crouch: return 0.1
            case .jump: return 0.2
            case .land, .rebound: return 0.1
            }
        }
    }

    /// The charge flicker toggles here, ever faster, then holds lit into the fright.
    static let flickers: [TimeInterval] = [0, 0.2, 0.34, 0.45, 0.53, 0.59, 0.64]
    static let chargeLength: TimeInterval = 0.72
    static let frightLength: TimeInterval = 0.28
    /// How long the face stays up when it jumps in fright.
    static let jumpLength: TimeInterval = 0.12
    static let burstLength: TimeInterval = 0.3
    static let goneLength: TimeInterval = 0.16
    /// How much of the face stays out of sight past the icon's left edge as it peeks, in points.
    static let peekHidden = 12

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

    /// The whole performance, in seconds.
    var length: TimeInterval {
        (charges ? Self.chargeLength : 0) + Self.frightLength + Self.burstLength + Self.goneLength
            + Beat.allCases.reduce(0) { $0 + $1.length }
    }

    func frame(at now: TimeInterval) -> Frame {
        let elapsed = now - started
        let frightAt = charges ? Self.chargeLength : 0
        let burstAt = frightAt + Self.frightLength
        let goneAt = burstAt + Self.burstLength
        let backAt = goneAt + Self.goneLength
        if elapsed < 0 { return .finished }
        if elapsed >= backAt {
            var start = backAt
            for beat in Beat.allCases {
                if elapsed < start + beat.length { return .back(beat, (elapsed - start) / beat.length) }
                start += beat.length
            }
            return .finished
        }
        if elapsed >= goneAt { return .gone }
        if elapsed >= burstAt { return .burst((elapsed - burstAt) / Self.burstLength) }
        if elapsed >= frightAt { return .fright(jumped: elapsed < frightAt + Self.jumpLength) }
        let flicker = Self.flickers.lastIndex { $0 <= elapsed } ?? 0
        return .charge(lit: flicker.isMultiple(of: 2))
    }

    /// The frightened eyes as the green bursts over the face, 0 to 1 of the
    /// way through: squeezed half shut as it starts, then shut.
    static func eyes(bursting progress: Double) -> (face: Face, blinking: Bool) {
        progress < 0.4 ? (.wincing, false) : (.attention, true)  // The request face's lids, met.
    }

    /// The face on its way home, in the new icon.
    struct Act: Equatable {
        var face: Face
        var blinking = false
        var pose = AvatarArt.Pose.home
        /// The green bar runs under the face's home until it lands there.
        var wash = true
    }

    /// How the face moves and looks at one point of a beat of its way home:
    /// in from the left, or, with active work still showing there, dropping
    /// in from above.
    static func act(_ beat: Beat, _ progress: Double, layout: AvatarArt.Layout) -> Act {
        if layout.left > 0 { return drop(beat, progress) }
        // Offsets from home: three points showing, then one eye.
        let start = -(layout.faceX + AvatarArt.faceWidth - 3)
        let peek = -(layout.faceX + peekHidden)
        switch beat {
        case .creep:
            let eased = 1 - (1 - progress) * (1 - progress)
            return Act(face: .peeking, pose: AvatarArt.Pose(dx: mix(start, peek, eased)))
        case .peek:
            return Act(face: .peeking, blinking: (0.45..<0.75).contains(progress),
                       pose: AvatarArt.Pose(dx: peek))
        case .crouch:
            return Act(face: .peeking, pose: AvatarArt.Pose(dx: peek, height: 14))
        case .jump:
            let dx = mix(peek, 0, progress * progress * (3 - 2 * progress))
            let pose = progress < 0.25 ? AvatarArt.Pose(dx: dx, height: 17)
                : progress < 0.85 ? AvatarArt.Pose(dx: dx, lift: 1)
                : AvatarArt.Pose(dx: dx)
            return Act(face: .attention, pose: pose)
        case .land, .rebound:
            return landing(beat)
        }
    }

    /// How far above home the face peeks down before dropping in, in points:
    /// its eyes just inside the icon's top.
    static let dropPeek = 7

    /// Dropping back in from above, so it doesn't cross the active side: it
    /// slides down until its eyes show, looks down at its home and blinks,
    /// braces a point back up, then falls, stretched, faster and faster.
    private static func drop(_ beat: Beat, _ progress: Double) -> Act {
        // Lifts above home: three points showing at the icon's top, then the eyes.
        let start = AvatarArt.faceHeight - 2
        switch beat {
        case .creep:
            let eased = 1 - (1 - progress) * (1 - progress)
            return Act(face: .lookingDown, pose: AvatarArt.Pose(lift: mix(start, dropPeek, eased)))
        case .peek:
            return Act(face: .lookingDown, blinking: (0.45..<0.75).contains(progress),
                       pose: AvatarArt.Pose(lift: dropPeek))
        case .crouch:
            return Act(face: .lookingDown, pose: AvatarArt.Pose(lift: dropPeek + 1))
        case .jump:
            let lift = mix(dropPeek + 1, 0, progress * progress)
            return Act(face: .attention, pose: progress < 0.85 ? AvatarArt.Pose(lift: lift, height: 17)
                                                                : AvatarArt.Pose(lift: lift))
        case .land, .rebound:
            return landing(beat)
        }
    }

    /// Home again: squashed on landing, then springing back up, happy.
    private static func landing(_ beat: Beat) -> Act {
        Act(face: .done, pose: AvatarArt.Pose(height: beat == .land ? 14 : 17), wash: false)
    }

    private static func mix(_ from: Int, _ to: Int, _ amount: Double) -> Int {
        from + Int((Double(to - from) * amount).rounded())
    }
}
