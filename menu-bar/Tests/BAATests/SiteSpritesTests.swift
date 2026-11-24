import AppKit
import XCTest
@testable import BAA

/// Renders the key menu bar frames for the product site (site/js/menubar.js),
/// as BAA draws them on a dark menu bar: the white face with the status
/// sides, 2× pixels. Skipped unless asked for:
///
///     BAA_SPRITES_DIR=site/media/baa swift test --package-path menu-bar --filter SiteSprites
@MainActor
final class SiteSpritesTests: XCTestCase {
    func testRenderSiteSprites() throws {
        guard let path = ProcessInfo.processInfo.environment["BAA_SPRITES_DIR"] else {
            throw XCTSkip("Set BAA_SPRITES_DIR to render the site's menu bar sprites")
        }
        // Relative to the repo root, whatever directory the tests run in.
        let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let directory = URL(fileURLWithPath: path, isDirectory: true, relativeTo: root)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)

        func icon(_ face: Face, _ left: LeftHint?, done: Bool = false, blinking: Bool = false,
                  peak: Bool = false, charged: Bool = false) -> (NSImage, NSImage?) {
            let layout = AvatarArt.layout(left: left, done: done)
            return (AvatarArt.image(face, blinking: blinking, layout: layout),
                    AvatarArt.sides(left: left, attentionPeak: peak, charged: charged, layout: layout))
        }
        let doneLayout = AvatarArt.layout(left: nil, done: true)
        let workingLayout = AvatarArt.layout(left: .working(1), done: false)
        // The wipe in quarters: green over the frightened face up to each edge,
        // with the eyes as they are midway through that quarter.
        func wipe(_ quarter: Int) -> (NSImage, NSImage?) {
            let edge = Int((Double(workingLayout.width * quarter) / 4).rounded(.up))
            let eyes = Celebration.eyes(wiped: (Double(quarter) - 0.5) / 4)
            return (AvatarArt.image(eyes.face, blinking: eyes.blinking, layout: workingLayout),
                    AvatarArt.wipe(left: .working(1), layout: workingLayout, edge: edge))
        }
        let frames: [(String, (NSImage, NSImage?))] = [
            ("idle", icon(.idle, nil)),
            ("idle-blink", icon(.idle, nil, blinking: true)),
            ("working-1", icon(.working, .working(1))),
            ("working-1-blink", icon(.working, .working(1), blinking: true)),
            ("working-2", icon(.working, .working(2))),
            ("attention", icon(.attention, .attention)),
            ("attention-peak", icon(.attention, .attention, peak: true)),
            ("charge", icon(.working, .working(1), charged: true)),
            ("fright", icon(.attention, .working(1), charged: true)),
            ("wipe-1", wipe(1)), ("wipe-2", wipe(2)), ("wipe-3", wipe(3)), ("wipe-4", wipe(4)),
            ("gone", (AvatarArt.blank(layout: doneLayout), AvatarArt.flash(layout: doneLayout))),
            ("done", icon(.done, nil, done: true))
        ]
        for (name, (face, sides)) in frames {
            let png = try XCTUnwrap(Self.composite(face: face, sides: sides))
            try png.write(to: directory.appendingPathComponent("\(name).png"))
        }
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
