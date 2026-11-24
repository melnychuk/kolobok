import AppKit
import XCTest
@testable import BAA

@MainActor
final class CelebrationTests: XCTestCase {
    func testOnlyANewCompletionCelebrates() {
        XCTAssertTrue(Celebration.starts(from: Preview.working.snapshot, to: Preview.done.snapshot))
        XCTAssertFalse(Celebration.starts(from: Preview.done.snapshot, to: Preview.workingAndDone.snapshot))
        XCTAssertFalse(Celebration.starts(from: Preview.done.snapshot, to: Preview.done.snapshot))
        // Connecting to a bridge that already has completions isn't one.
        XCTAssertFalse(Celebration.starts(from: .noBridge, to: Preview.done.snapshot))
        XCTAssertFalse(Celebration.starts(from: .herdrUnavailable, to: Preview.done.snapshot))
    }

    /// A frame's name, with the wipe's progress to two places.
    private func name(_ frame: Celebration.Frame) -> String {
        if case .wipe(let progress) = frame { return String(format: "wipe %.2f", progress) }
        return "\(frame)"
    }

    func testActiveWorkChargesThenIsStartledThenWipedGreen() {
        let celebration = Celebration(started: 100, before: Preview.working.snapshot)
        XCTAssertTrue(celebration.charges)
        let times = [0, 0.1, 0.25, 0.4, 0.5, 0.56, 0.62, 0.7, 0.75, 0.98, 1.06, 1.18, 1.25, 1.41, 1.43]
        XCTAssertEqual(times.map { name(celebration.frame(at: 100 + $0)) }, [
            "charge(lit: true)", "charge(lit: true)", "charge(lit: false)", "charge(lit: true)",
            "charge(lit: false)", "charge(lit: true)", "charge(lit: false)", "charge(lit: true)",
            "fright", "fright", "wipe 0.25", "wipe 0.75", "gone", "gone", "finished"
        ])
        // Each flicker is shorter than the one before.
        let gaps = zip(Celebration.flickers.dropFirst(), Celebration.flickers).map { $0 - $1 }
        XCTAssertEqual(gaps, gaps.sorted(by: >))
    }

    func testWithoutActiveWorkItIsStartledThenWipedGreen() {
        let celebration = Celebration(started: 0, before: Preview.blocked.snapshot)
        XCTAssertFalse(celebration.charges)
        let (fright, wipe, gone) = (Celebration.frightLength, Celebration.wipeLength, Celebration.goneLength)
        XCTAssertEqual(celebration.frame(at: 0), .fright)
        XCTAssertEqual(celebration.frame(at: fright), .wipe(0))
        XCTAssertEqual(celebration.frame(at: fright + wipe), .gone)
        XCTAssertEqual(celebration.frame(at: fright + wipe + gone), .finished)
    }

    /// Whether the face shows any eye holes: nearly clear pixels inside it.
    private func eyeHoles(_ image: NSImage, layout: AvatarArt.Layout) throws -> Bool {
        let rep = try XCTUnwrap(image.representations.compactMap { $0 as? NSBitmapImageRep }
            .first { $0.pixelsWide == layout.width })
        return (4..<AvatarArt.faceWidth - 4).contains { column in
            (5..<AvatarArt.itemHeight - 4).contains { row in
                (rep.colorAt(x: layout.faceX + column, y: row)?.alphaComponent ?? 1) < 0.1
            }
        }
    }

    private func rep(_ image: NSImage, _ layout: AvatarArt.Layout) throws -> NSBitmapImageRep {
        try XCTUnwrap(image.representations.compactMap { $0 as? NSBitmapImageRep }
            .first { $0.pixelsWide == layout.width })
    }

    func testTheWipeCoversTheIconUpToItsEdgeWithinTheIconsOutline() throws {
        let layout = AvatarArt.layout(left: .working(1), done: false)
        let edge = layout.faceX + AvatarArt.faceWidth / 2  // halfway across the face
        let wipe = AvatarArt.wipe(left: .working(1), layout: layout, edge: edge)
        // Drawn into a bitmap at 1x, as the menu bar would.
        let bitmap = try XCTUnwrap(NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: layout.width,
                                                    pixelsHigh: AvatarArt.itemHeight, bitsPerSample: 8,
                                                    samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                                    colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0))
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
        wipe.draw(in: NSRect(x: 0, y: 0, width: layout.width, height: AvatarArt.itemHeight))
        NSGraphicsContext.restoreGraphicsState()
        let middle = AvatarArt.itemHeight / 2
        // Solid green from the left over the side, the moat and half the face,
        // give or take the ragged edge...
        for column in 1..<edge - 3 {
            XCTAssertEqual(bitmap.colorAt(x: column, y: middle)?.alphaComponent ?? 0, 1, accuracy: 0.01,
                           "column \(column)")
        }
        // ...and nothing over the rest of the face, which still shows.
        for column in edge + 3..<layout.width {
            XCTAssertEqual(bitmap.colorAt(x: column, y: middle)?.alphaComponent ?? 1, 0, accuracy: 0.01,
                           "column \(column)")
        }
        // Within the icon's rounded outline: its corners stay clear.
        XCTAssertEqual(bitmap.colorAt(x: 0, y: 0)?.alphaComponent ?? 1, 0, accuracy: 0.01)
        XCTAssertFalse(wipe.isTemplate)
    }

    func testTheEyesSqueezeShutAsTheGreenSpreads() {
        // Wide in fright, then half shut as soon as the wipe starts, then shut.
        let steps = [0, 0.1, 0.35, 0.45, 0.95].map { Celebration.eyes(wiped: $0) }
        XCTAssertEqual(steps.map(\.face), [.wincing, .wincing, .wincing, .attention, .attention])
        XCTAssertEqual(steps.map(\.blinking), [false, false, false, true, true])
        // Each step shows less eye: more of the face stays solid.
        func eyeArea(_ eyes: (face: Face, blinking: Bool)) -> Double {
            AvatarArt.faceAlpha(eyes.face, blinking: eyes.blinking, scale: 2).joined().reduce(0) { $0 + (1 - $1) }
        }
        let fright = eyeArea((.attention, false))
        let areas = [0, 0.6].map { eyeArea(Celebration.eyes(wiped: $0)) }
        XCTAssertGreaterThan(fright, areas[0])
        XCTAssertGreaterThan(areas[0], areas[1])
    }

    func testTheWipesEdgeIsRaggedAndFizzesButLandsSolid() {
        let width = AvatarArt.layout(left: .working(1), done: false).width
        let rows = 0..<AvatarArt.itemHeight
        let reaches = rows.map { AvatarArt.wipeReach(edge: 20, row: $0, width: width) }
        // Ragged: bands of rows reach different distances, within three points
        // of the edge, each band three points tall.
        XCTAssertGreaterThanOrEqual(Set(reaches).count, 3)
        XCTAssertTrue(reaches.allSatisfy { (17...23).contains($0) })
        XCTAssertTrue(stride(from: 0, to: reaches.count, by: 3).allSatisfy { Set(reaches[$0..<$0 + 3]).count == 1 })
        // Fizzing: the next step's pattern isn't just this one moved along.
        let next = rows.map { AvatarArt.wipeReach(edge: 21, row: $0, width: width) - 1 }
        XCTAssertNotEqual(reaches, next)
        // Landing: a finished wipe, or none, covers the whole row.
        XCTAssertTrue(rows.allSatisfy { AvatarArt.wipeReach(edge: width, row: $0, width: width) == width })
        XCTAssertTrue(rows.allSatisfy { AvatarArt.wipeReach(edge: nil, row: $0, width: width) == width })
    }

    func testThenTheIconIsSolidGreenWithNoEyes() throws {
        let layout = AvatarArt.layout(left: nil, done: true)
        let flash = try rep(AvatarArt.flash(layout: layout), layout)
        XCTAssertFalse(try eyeHoles(AvatarArt.flash(layout: layout), layout: layout))
        let color = try XCTUnwrap(flash.colorAt(x: layout.width - 2, y: AvatarArt.itemHeight / 2)?
            .usingColorSpace(.sRGB))
        XCTAssertGreaterThan(color.greenComponent, color.redComponent + 0.3)
        XCTAssertTrue(AvatarArt.blank(layout: layout).isTemplate)
        XCTAssertEqual(AvatarArt.blank(layout: layout).size, AvatarArt.flash(layout: layout).size)
    }

    func testChargeLightsOnlyTheWorkingSide() {
        let layout = AvatarArt.layout(left: .working(2), done: false)
        XCTAssertFalse(AvatarArt.sides(left: .working(2), charged: true, layout: layout)
                       === AvatarArt.sides(left: .working(2), layout: layout))
        let request = AvatarArt.layout(left: .attention, done: false)
        XCTAssertTrue(AvatarArt.sides(left: .attention, charged: true, layout: request)
                      === AvatarArt.sides(left: .attention, layout: request))
    }
}
