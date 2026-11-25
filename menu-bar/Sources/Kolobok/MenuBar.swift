import AppKit

@main
@MainActor
final class MenuBar: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private let model = MenuModel()
    private let sides = SidesView()
    private let agentsMenu = NSMenu()
    private var item: NSStatusItem?
    private var agentsMenuOpen = false
    private var attentionTimer: Timer?
    private var attentionPeak = false
    private var blinkTimer: Timer?
    private var blinking = false
    /// The last snapshot drawn, to notice new completions.
    private var shown: Snapshot?
    private var celebration: Celebration?
    private var celebrationTimer: Timer?

    static func main() {
        switch Array(CommandLine.arguments.dropFirst()) {
        case [], ["--demo"]: break
        case ["--help"], ["-h"]:
            print("Usage: Kolobok [--demo] (omit --demo to show the agents the Stick bridge reports)")
            return
        default:
            fputs("Usage: Kolobok [--demo]\n", stderr)
            exit(2)
        }
        let app = NSApplication.shared
        let delegate = MenuBar()
        app.delegate = delegate
        app.setActivationPolicy(.accessory)
        withExtendedLifetime(delegate) { app.run() }
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        item.button?.target = self
        item.button?.action = #selector(clicked)
        // A click opens the agents; a right-click (or Control-click), Quit.
        item.button?.sendAction(on: [.leftMouseUp, .rightMouseUp])
        if let button = item.button {
            button.imagePosition = .imageOnly
            sides.frame = button.bounds
            sides.autoresizingMask = [.width, .height]
            button.addSubview(sides)
        }
        self.item = item
        agentsMenu.delegate = self

        model.onChange = { [weak self] in self?.updateItem() }
        NSWorkspace.shared.notificationCenter.addObserver(
            self, selector: #selector(accessibilityDisplayChanged(_:)),
            name: NSWorkspace.accessibilityDisplayOptionsDidChangeNotification, object: nil
        )
        updateItem()
        model.start()
    }

    // MARK: Menus

    @objc private func clicked() {
        let event = NSApp.currentEvent
        if event?.type == .rightMouseUp || event?.modifierFlags.contains(.control) == true {
            let menu = NSMenu()
            menu.addItem(NSMenuItem(title: "Quit Kolobok", action: #selector(NSApplication.terminate(_:)),
                                    keyEquivalent: "q"))
            open(menu)
        } else {
            fillAgentsMenu()
            open(agentsMenu)
        }
    }

    /// Opens a menu from the status item. It's attached only for this click, so
    /// each kind of click gets its own menu, placed and highlighted natively.
    private func open(_ menu: NSMenu) {
        guard let item, let button = item.button else { return }
        item.menu = menu
        button.performClick(nil)
        item.menu = nil
    }

    private func fillAgentsMenu() {
        let actions = AgentsMenu.Actions(target: self, focus: #selector(chooseAgent(_:)),
                                         preview: #selector(choosePreview(_:)))
        agentsMenu.items = AgentsMenu.items(for: model.snapshot, problem: model.problem,
                                            preview: model.isDemo ? model.preview : nil, actions: actions)
    }

    @objc private func chooseAgent(_ sender: NSMenuItem) {
        guard let key = sender.representedObject as? String else { return }
        model.focus(key: key)
    }

    @objc private func choosePreview(_ sender: NSMenuItem) {
        guard let name = sender.representedObject as? String, let preview = Preview(rawValue: name) else { return }
        model.preview = preview
    }

    func menuWillOpen(_ menu: NSMenu) {
        if menu === agentsMenu { agentsMenuOpen = true }
    }

    func menuDidClose(_ menu: NSMenu) {
        guard menu === agentsMenu else { return }
        agentsMenuOpen = false
        model.clearProblem()  // Shown once.
    }

    // MARK: Status item

    private func updateItem() {
        guard let button = item?.button else { return }
        let snapshot = model.snapshot
        if let shown, celebration == nil, Celebration.starts(from: shown, to: snapshot),
           !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion {
            celebrate(from: shown)
        }
        shown = snapshot
        updateAttentionTimer(showAttention: snapshot.leftHint == .attention)
        updateBlinkTimer(face: snapshot.face)
        drawAvatar(snapshot, on: button)
        let unread = snapshot.hasUnreadCompletion && snapshot.face != .done ? "; unread completion" : ""
        let description = "Kolobok\(model.isDemo ? " preview" : ""): \(snapshot.label)\(unread)"
        button.toolTip = description
        button.setAccessibilityLabel(description)
        if agentsMenuOpen { fillAgentsMenu() }  // Keep an open menu current.
    }

    private func drawAvatar(_ snapshot: Snapshot, on button: NSStatusBarButton) {
        let images = StatusIcon.images(for: snapshot, celebration: celebration,
                                       at: ProcessInfo.processInfo.systemUptime,
                                       blinking: blinking, attentionPeak: attentionPeak)
        button.image = images.face
        sides.image = images.sides
    }

    /// Plays a new completion's charge, fright, burst and return. A completion
    /// during it doesn't replay it; the solid green and the face's return show
    /// the latest counts.
    private func celebrate(from before: Snapshot) {
        celebration = Celebration(started: ProcessInfo.processInfo.systemUptime, before: before)
        // Fast enough for the face to move a point or so at a time.
        let timer = Timer(timeInterval: 1.0 / 60, repeats: true) { [weak self] timer in
            MainActor.assumeIsolated {
                guard let self, let button = self.item?.button else { return timer.invalidate() }
                if self.celebration?.frame(at: ProcessInfo.processInfo.systemUptime) == .finished {
                    timer.invalidate()
                    self.celebration = nil
                    self.celebrationTimer = nil
                }
                self.drawAvatar(self.model.snapshot, on: button)
            }
        }
        celebrationTimer = timer
        RunLoop.main.add(timer, forMode: .common)
    }

    @objc private func accessibilityDisplayChanged(_ notification: Notification) {
        updateItem()
    }

    private func updateAttentionTimer(showAttention: Bool) {
        guard showAttention && !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion else {
            attentionTimer?.invalidate()
            attentionTimer = nil
            attentionPeak = false
            return
        }
        guard attentionTimer == nil else { return }
        // The Stick's request field pulses from yellow to orange; here it steps between them.
        let timer = Timer(timeInterval: 0.65, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, let button = self.item?.button else { return }
                self.attentionPeak.toggle()
                self.drawAvatar(self.model.snapshot, on: button)
            }
        }
        timer.tolerance = 0.05
        attentionTimer = timer
        RunLoop.main.add(timer, forMode: .common)
    }

    /// Blinks every few seconds while the eyes are open. Polling doesn't reset
    /// the countdown, so a blink isn't starved by the two-second refresh.
    private func updateBlinkTimer(face: Face) {
        guard face.blinks && !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion else {
            blinkTimer?.invalidate()
            blinkTimer = nil
            blinking = false
            return
        }
        guard blinkTimer == nil else { return }
        let timer = Timer(timeInterval: .random(in: 3.5...7), repeats: false) { [weak self] _ in
            MainActor.assumeIsolated { self?.blink() }
        }
        timer.tolerance = 0.3
        blinkTimer = timer
        RunLoop.main.add(timer, forMode: .common)
    }

    private func blink() {
        guard let button = item?.button else { return }
        blinking = true
        drawAvatar(model.snapshot, on: button)
        let reopen = Timer(timeInterval: 0.14, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, let button = self.item?.button else { return }
                self.blinking = false
                self.blinkTimer = nil
                self.drawAvatar(self.model.snapshot, on: button)
                self.updateBlinkTimer(face: self.model.snapshot.face)
            }
        }
        blinkTimer = reopen
        RunLoop.main.add(reopen, forMode: .common)
    }
}

/// The status item's images for a snapshot at a moment: the template face,
/// and the colored sides drawn over it. During a completion's celebration,
/// the old icon until the green covers it, as on the Stick, then the new one.
@MainActor
enum StatusIcon {
    static func images(for snapshot: Snapshot, celebration: Celebration?, at now: TimeInterval,
                       blinking: Bool = false, attentionPeak: Bool = false) -> (face: NSImage, sides: NSImage?) {
        let before = celebration?.before ?? snapshot
        let old = AvatarArt.layout(left: before.leftHint, done: before.hasUnreadCompletion)
        let new = AvatarArt.layout(left: snapshot.leftHint, done: snapshot.hasUnreadCompletion)
        switch celebration?.frame(at: now) {
        case .charge(let lit)?:
            return (AvatarArt.image(before.face, layout: old),
                    AvatarArt.sides(left: before.leftHint, charged: lit, layout: old))
        case .fright(let jumped)?:
            // The request face's wide eyes, and a jump.
            let pose = AvatarArt.Pose(lift: jumped ? 1 : 0)
            return (AvatarArt.image(.attention, layout: old, pose: pose),
                    AvatarArt.sides(left: before.leftHint, charged: true, layout: old, pose: pose))
        case .burst(let progress)?:
            // A green star spins out from the charged side along the sides'
            // band, blasting away the frightened face, whose eyes squeeze shut.
            let eyes = Celebration.eyes(bursting: progress)
            let step = AvatarArt.burstStep(progress)
            return (AvatarArt.image(eyes.face, blinking: eyes.blinking, layout: old, blast: step),
                    AvatarArt.burst(left: before.leftHint, layout: old, step: step))
        case .gone?:
            // A solid green bar across the new icon, before the face comes back.
            return (AvatarArt.blank(layout: new), AvatarArt.flash(layout: new))
        case .back(let beat, let progress)?:
            // The face peeks back in from the left and hops home onto the bar.
            let act = Celebration.act(beat, progress, layout: new)
            return (AvatarArt.image(act.face, blinking: act.blinking, layout: new, pose: act.pose),
                    AvatarArt.sides(left: snapshot.leftHint, attentionPeak: attentionPeak, layout: new,
                                    pose: act.pose, wash: act.wash))
        case .finished?, nil:
            return (AvatarArt.image(snapshot.face, blinking: blinking, layout: new),
                    AvatarArt.sides(left: snapshot.leftHint, attentionPeak: attentionPeak, layout: new))
        }
    }
}

// The status colors aren't part of the template image, so AppKit can still
// tint the face; they're drawn over it, already clear of the face and its moat.
private final class SidesView: NSView {
    var image: NSImage? {
        didSet { if image !== oldValue { needsDisplay = true } }
    }

    override func hitTest(_ point: NSPoint) -> NSView? { nil }

    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        needsDisplay = true // The button resizes when the sides come and go.
    }

    override func draw(_ dirtyRect: NSRect) {
        guard let image, let button = superview as? NSStatusBarButton,
              let imageRect = button.cell?.imageRect(forBounds: button.bounds),
              !imageRect.isEmpty else { return }
        // Same pixels as the face image, so the cut-out lines up exactly.
        let frame = backingAlignedRect(convert(imageRect, from: button), options: .alignAllEdgesNearest)
        image.draw(in: frame)
    }
}
