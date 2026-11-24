import AppKit
import SwiftUI

@main
@MainActor
final class MenuBar: NSObject, NSApplicationDelegate {
    private let model = MenuModel()
    private let popover = NSPopover()
    private let completionDot = CompletionDot()
    private var item: NSStatusItem?
    private var attentionTimer: Timer?
    private var attentionBold = true

    static func main() {
        switch Array(CommandLine.arguments.dropFirst()) {
        case [], ["--demo"]: break
        case ["--help"], ["-h"]:
            print("Usage: CopilotMenuBar [--demo] (omit --demo for live Herdr Desktop sessions)")
            return
        default:
            fputs("Usage: CopilotMenuBar [--demo]\n", stderr)
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
        item.button?.action = #selector(togglePopover)
        if let button = item.button {
            button.imagePosition = .imageLeading
            button.image = Mascot.image
            completionDot.frame = button.bounds
            completionDot.autoresizingMask = [.width, .height]
            completionDot.isHidden = true
            button.addSubview(completionDot)
        }
        self.item = item

        popover.behavior = .transient
        popover.contentViewController = NSHostingController(rootView: StatusPanel(model: model))
        model.onChange = { [weak self] in self?.updateItem() }
        NSWorkspace.shared.notificationCenter.addObserver(
            self, selector: #selector(accessibilityDisplayChanged(_:)),
            name: NSWorkspace.accessibilityDisplayOptionsDidChangeNotification, object: nil
        )
        updateItem()
        model.start()
    }

    @objc private func togglePopover() {
        if popover.isShown {
            popover.performClose(nil)
        } else if let button = item?.button {
            model.refresh()
            popover.show(relativeTo: button.bounds, of: button, preferredEdge: .minY)
        }
    }

    private func updateItem() {
        guard let button = item?.button else { return }
        let snapshot = model.snapshot
        updateAttentionTimer(showAttention: snapshot.menuBadge == .attention)
        updateBadge(snapshot.menuBadge, on: button)
        completionDot.isHidden = !snapshot.hasUnreadCompletion
        completionDot.needsDisplay = true
        let unread = snapshot.hasUnreadCompletion && snapshot.status != .done ? "; unread completion" : ""
        let description = "Copilot\(model.isDemo ? " preview" : ""): \(snapshot.label)\(unread)"
        button.toolTip = description
        button.setAccessibilityLabel(description)
    }

    private func updateBadge(_ badge: MenuBadge, on button: NSStatusBarButton) {
        let style: (text: String, color: NSColor)
        switch badge {
        case .attention:
            button.attributedTitle = AttentionMark.title(bold: attentionBold)
            return
        case .working(let count): style = (count > 9 ? "9+" : String(count), .labelColor)
        case .offline: style = ("–", .secondaryLabelColor)
        case .unknown: style = ("?", .secondaryLabelColor)
        case .none: style = ("", .labelColor)
        }
        button.attributedTitle = NSAttributedString(string: style.text, attributes: [
            .font: NSFont.monospacedDigitSystemFont(ofSize: 12, weight: .semibold),
            .foregroundColor: style.color
        ])
    }

    @objc private func accessibilityDisplayChanged(_ notification: Notification) {
        updateItem()
    }

    private func updateAttentionTimer(showAttention: Bool) {
        guard showAttention && !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion else {
            attentionTimer?.invalidate()
            attentionTimer = nil
            attentionBold = true
            return
        }
        guard attentionTimer == nil else { return }
        let timer = Timer(timeInterval: 0.65, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, let button = self.item?.button else { return }
                self.attentionBold.toggle()
                self.updateBadge(.attention, on: button)
            }
        }
        timer.tolerance = 0.05
        attentionTimer = timer
        RunLoop.main.add(timer, forMode: .common)
    }
}

@MainActor
private final class MenuModel: ObservableObject {
    @Published private var live: Snapshot = .offline
    @Published var preview: Preview = .mixed { didSet { onChange?() } }
    @Published private(set) var focusFailed = false
    @Published private(set) var focusing = false

    let isDemo = CommandLine.arguments.contains("--demo")
    var snapshot: Snapshot { isDemo ? preview.snapshot : live }
    var onChange: (() -> Void)?
    private lazy var herdr = Herdr.locate()
    private var polling: Task<Void, Never>?
    private var refreshing = false

    func start() {
        guard !isDemo else { return }
        polling = Task { [weak self] in
            while !Task.isCancelled {
                self?.refresh()
                try? await Task.sleep(for: .seconds(2))
            }
        }
    }

    func refresh() {
        guard !isDemo, !refreshing, let herdr else { return }
        refreshing = true
        Task {
            let next = await Task.detached { herdr.snapshot() }.value
            if live != next {
                live = next
                onChange?()
            }
            refreshing = false
        }
    }

    func focus(_ session: Session) {
        guard !isDemo, !focusing, let herdr else { return }
        focusFailed = false
        focusing = true
        Task {
            let success = await Task.detached { herdr.focus(session.id) }.value
            focusing = false
            focusFailed = !success
            if success {
                let executable = herdr.executable.resolvingSymlinksInPath()
                let apps = NSWorkspace.shared.runningApplications.filter {
                    $0.executableURL?.resolvingSymlinksInPath() == executable
                }
                if apps.count == 1 { apps[0].activate() }
            }
            refresh()
        }
    }
}

private enum StickStatusColor {
    // sRGB inputs to firmware/avatar_palette.h's done color and
    // firmware/request_scene.h's yellow needs-input cue, before RGB565.
    static let attention = rgb(0xFA, 0xCC, 0x15)
    static let done = rgb(0x22, 0xC5, 0x5E)

    private static func rgb(_ red: CGFloat, _ green: CGFloat, _ blue: CGFloat) -> NSColor {
        NSColor(srgbRed: red / 255, green: green / 255, blue: blue / 255, alpha: 1)
    }
}

enum AttentionMark {
    static func title(bold: Bool) -> NSAttributedString {
        NSAttributedString(string: "!", attributes: [
            // Fixed glyph advance keeps adjacent menu bar items from shifting.
            .font: NSFont.monospacedSystemFont(ofSize: 12, weight: bold ? .bold : .regular),
            .foregroundColor: StickStatusColor.attention
        ])
    }
}

// Keep the green mark separate so AppKit can still tint the mascot as a template.
private final class CompletionDot: NSView {
    override func hitTest(_ point: NSPoint) -> NSView? { nil }

    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        needsDisplay = true // The image moves when the button switches between badge and no badge.
    }

    override func draw(_ dirtyRect: NSRect) {
        guard let button = superview as? NSStatusBarButton,
              let imageRect = button.cell?.imageRect(forBounds: button.bounds),
              !imageRect.isEmpty else { return }
        let icon = convert(imageRect, from: button)
        let dot = NSRect(x: icon.maxX - 4, y: icon.maxY - 6, width: 5, height: 5)
        // A solid rim covers the mascot underneath and separates the green from light and dark icons.
        NSColor.black.setFill()
        NSBezierPath(ovalIn: dot.insetBy(dx: -1, dy: -1)).fill()
        StickStatusColor.done.setFill()
        NSBezierPath(ovalIn: dot).fill()
    }
}

@MainActor
enum Mascot {
    private static let artwork: NSImage = {
        let url = Bundle.module.url(forResource: "copilot", withExtension: "svg", subdirectory: "Resources")
        guard let url, let image = NSImage(contentsOf: url) else {
            preconditionFailure("Missing Copilot mascot SVG")
        }
        return image
    }()

    static let image = rasterize()

    private static func rasterize() -> NSImage {
        let size = NSSize(width: 20, height: 20)
        let bounds = NSRect(origin: .zero, size: size)
        let image = NSImage(size: size)
        // Give the status bar a real alpha-mask template, not the SVG's
        // currentColor (which AppKit can draw as black in dark mode).
        for scale in [1, 2] {
            let pixels = Int(size.width) * scale
            guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels,
                                             pixelsHigh: pixels, bitsPerSample: 8, samplesPerPixel: 4,
                                             hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                                             bytesPerRow: 0, bitsPerPixel: 0),
                  let context = NSGraphicsContext(bitmapImageRep: rep) else { continue }
            NSGraphicsContext.saveGraphicsState()
            NSGraphicsContext.current = context
            context.cgContext.scaleBy(x: CGFloat(scale), y: CGFloat(scale))
            context.cgContext.clear(bounds)
            artwork.draw(in: bounds)
            NSGraphicsContext.restoreGraphicsState()
            rep.size = size
            image.addRepresentation(rep)
        }
        // AppKit selects the foreground for the actual status bar, which can
        // differ from the app's appearance depending on the desktop wallpaper.
        image.isTemplate = true
        return image
    }
}

private struct StatusPanel: View {
    @ObservedObject var model: MenuModel

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 8) {
                Image(nsImage: Mascot.image).renderingMode(.template)
                    .foregroundStyle(.primary).accessibilityHidden(true)
                Text("Copilot").font(.headline)
                Spacer()
                if model.isDemo { Text("PREVIEW").font(.caption2.weight(.semibold)).foregroundStyle(.secondary) }
            }
            if model.isDemo {
                Picker("Preview state", selection: $model.preview) {
                    ForEach(Preview.allCases) { Text($0.rawValue).tag($0) }
                }
            }
            Divider()
            if model.snapshot.sessions.isEmpty {
                Text(model.snapshot.label)
                if model.snapshot == .offline && !model.isDemo {
                    Text("Open Herdr Desktop to see live Copilot sessions.")
                        .font(.caption).foregroundStyle(.secondary)
                }
            } else {
                let sessions = model.snapshot.sessions
                if sessions.count > 5 {
                    ScrollView { rows(sessions) }.frame(height: 236)
                } else {
                    rows(sessions)
                }
            }
            if model.focusFailed {
                Text("Couldn’t open that session in Herdr.").font(.caption).foregroundStyle(.secondary)
            }
            Divider()
            HStack {
                Spacer()
                Button("Quit") { NSApplication.shared.terminate(nil) }.keyboardShortcut("q", modifiers: .command)
            }
        }
        .padding(14)
        .frame(width: 312)
    }

    private func rows(_ sessions: [Session]) -> some View {
        VStack(spacing: 5) {
            ForEach(sessions) { session in
                if model.isDemo {
                    row(session)
                } else {
                    Button { model.focus(session) } label: { row(session) }
                        .buttonStyle(.plain)
                        .disabled(model.focusing)
                        .help("Show this Copilot session in Herdr")
                }
            }
        }
    }

    private func row(_ session: Session) -> some View {
        HStack(spacing: 10) {
            Circle().fill(color(session.status)).frame(width: 7, height: 7).accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 2) {
                Text(session.title).fontWeight(.medium).lineLimit(1)
                Text(session.status.label).font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            if !model.isDemo {
                Image(systemName: "arrow.up.right").font(.caption).foregroundStyle(.secondary)
                    .accessibilityHidden(true)
            }
        }
        .padding(7)
        .background(.primary.opacity(0.06), in: RoundedRectangle(cornerRadius: 6))
        .contentShape(Rectangle())
    }

    private func color(_ status: SessionStatus) -> Color {
        switch status {
        case .working: return .primary
        case .failed, .blocked: return Color(nsColor: StickStatusColor.attention)
        case .done: return Color(nsColor: StickStatusColor.done)
        case .idle, .unknown: return .secondary
        }
    }
}
