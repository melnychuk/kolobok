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

    func testActiveWorkChargesWithQuickeningFlickerThenFlashes() {
        let celebration = Celebration(started: 100, before: Preview.working.snapshot)
        XCTAssertTrue(celebration.charges)
        let frames = [0, 0.1, 0.25, 0.4, 0.5, 0.56, 0.62, 0.7, 0.8, 1.03, 1.1].map { celebration.frame(at: 100 + $0) }
        XCTAssertEqual(frames, [
            .charge(lit: true), .charge(lit: true), .charge(lit: false), .charge(lit: true),
            .charge(lit: false), .charge(lit: true), .charge(lit: false), .charge(lit: true),
            .flash, .finished, .finished
        ])
        // Each flicker is shorter than the one before.
        let gaps = zip(Celebration.flickers.dropFirst(), Celebration.flickers).map { $0 - $1 }
        XCTAssertEqual(gaps, gaps.sorted(by: >))
    }

    func testWithoutActiveWorkItJustFlashes() {
        let celebration = Celebration(started: 0, before: Preview.blocked.snapshot)
        XCTAssertFalse(celebration.charges)
        XCTAssertEqual(celebration.frame(at: 0), .flash)
        XCTAssertEqual(celebration.frame(at: Celebration.flashLength), .finished)
    }

    func testTheFlashIsTheWholeIconInGreenWithTheEyesCutOut() throws {
        let layout = AvatarArt.layout(left: .working(1), done: true)
        let flash = AvatarArt.flash(.done, layout: layout)
        let rep = try XCTUnwrap(flash.representations.compactMap { $0 as? NSBitmapImageRep }.first { $0.pixelsWide == layout.width })
        let middle = AvatarArt.itemHeight / 2
        // Solid across the sides and moats, where the normal icon has gaps.
        for column in [1, layout.faceX - 1, layout.faceX + 10, layout.width - 2] {
            let color = try XCTUnwrap(rep.colorAt(x: column, y: middle)?.usingColorSpace(.sRGB))
            XCTAssertEqual(color.alphaComponent, 1, accuracy: 0.01, "column \(column)")
            XCTAssertGreaterThan(color.greenComponent, color.redComponent + 0.3)
        }
        // Somewhere on the face, the eyes show through.
        let eyeHoles = (0..<AvatarArt.faceWidth).contains { column in
            (0..<AvatarArt.itemHeight).contains { row in
                (rep.colorAt(x: layout.faceX + column, y: row)?.alphaComponent ?? 1) < 0.1
                    && row > 4 && row < AvatarArt.itemHeight - 4 && column > 3 && column < AvatarArt.faceWidth - 4
            }
        }
        XCTAssertTrue(eyeHoles)
        XCTAssertFalse(flash.isTemplate)
        XCTAssertTrue(AvatarArt.blank(layout: layout).isTemplate)
        XCTAssertEqual(AvatarArt.blank(layout: layout).size, flash.size)
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
