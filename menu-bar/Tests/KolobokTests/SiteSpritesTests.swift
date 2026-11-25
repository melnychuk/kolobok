import AppKit
import XCTest
@testable import Kolobok

/// Renders menu bar frames for the product site (site/js/menubar.js), as Kolobok
/// draws them on a dark menu bar: the white face with the status sides, 2×
/// pixels. Key states by name, and a completion's whole celebration, from
/// one active task to one done, sampled at 60 fps: each distinct frame as
/// celebrate-N.png, listed with its start time in celebrate.js. Skipped
/// unless asked for:
///
///     KOLOBOK_SPRITES_DIR=site/media/kolobok swift test --package-path menu-bar --filter SiteSprites
@MainActor
final class SiteSpritesTests: XCTestCase {
    private struct Step: Encodable {
        let frame: String
        let ms: Int
        let width: Int
    }

    func testRenderSiteSprites() throws {
        guard let path = ProcessInfo.processInfo.environment["KOLOBOK_SPRITES_DIR"] else {
            throw XCTSkip("Set KOLOBOK_SPRITES_DIR to render the site's menu bar sprites")
        }
        // Relative to the repo root, whatever directory the tests run in.
        let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let directory = URL(fileURLWithPath: path, isDirectory: true, relativeTo: root)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)

        func icon(_ face: Face, _ left: LeftHint?, done: Bool = false, blinking: Bool = false,
                  peak: Bool = false) -> (NSImage, NSImage?) {
            let layout = AvatarArt.layout(left: left, done: done)
            return (AvatarArt.image(face, blinking: blinking, layout: layout),
                    AvatarArt.sides(left: left, attentionPeak: peak, layout: layout))
        }
        let states: [(String, (NSImage, NSImage?))] = [
            ("idle", icon(.idle, nil)),
            ("idle-blink", icon(.idle, nil, blinking: true)),
            ("working-1", icon(.working, .working(1))),
            ("working-1-blink", icon(.working, .working(1), blinking: true)),
            ("working-2", icon(.working, .working(2))),
            ("attention", icon(.attention, .attention)),
            ("attention-peak", icon(.attention, .attention, peak: true)),
            ("done", icon(.done, nil, done: true))
        ]
        for (name, (face, sides)) in states {
            let png = try XCTUnwrap(Self.composite(face: face, sides: sides))
            try png.write(to: directory.appendingPathComponent("\(name).png"))
        }

        let celebration = Celebration(started: 0, before: Preview.working.snapshot)
        var steps: [Step] = []
        var last: Data?
        for tick in 0...Int((celebration.length * 60).rounded(.up)) {
            let now = Double(tick) / 60
            let images = StatusIcon.images(for: Preview.done.snapshot, celebration: celebration, at: now)
            let png = try XCTUnwrap(Self.composite(face: images.face, sides: images.sides))
            guard png != last else { continue }
            last = png
            let name = "celebrate-\(steps.count + 1)"
            try png.write(to: directory.appendingPathComponent("\(name).png"))
            steps.append(Step(frame: name, ms: Int((now * 1000).rounded()), width: Int(images.face.size.width) * 2))
        }
        // As a module the site imports: no fetch before the scene can play.
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let json = try XCTUnwrap(String(data: encoder.encode(steps), encoding: .utf8))
        let module = "// The menu bar's completion celebration, by menu-bar's SiteSprites test.\n"
            + "export default \(json);\n"
        try Data(module.utf8).write(to: directory.appendingPathComponent("celebrate.js"))
    }

    /// The face tinted white as on a dark menu bar, the sides over it, at 2×.
    private static func composite(face: NSImage, sides: NSImage?) -> Data? {
        let size = face.size
        guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width) * 2,
                                         pixelsHigh: Int(size.height) * 2, bitsPerSample: 8, samplesPerPixel: 4,
                                         hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                                         bytesPerRow: 0, bitsPerPixel: 0)?.retagging(with: .sRGB) else { return nil }
        rep.size = size
        NSGraphicsContext.saveGraphicsState()
        defer { NSGraphicsContext.restoreGraphicsState() }
        let context = NSGraphicsContext(bitmapImageRep: rep)
        context?.imageInterpolation = .none
        NSGraphicsContext.current = context
        let bounds = NSRect(origin: .zero, size: size)
        let white = NSImage(size: size, flipped: false) { rect in
            face.draw(in: rect)
            NSColor.white.set()
            rect.fill(using: .sourceAtop)
            return true
        }
        white.draw(in: bounds)
        sides?.draw(in: bounds)
        context?.flushGraphics()
        return rep.representation(using: .png, properties: [:])
    }
}
