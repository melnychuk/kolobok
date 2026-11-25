import AppKit
import XCTest
@testable import Kolobok

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

    /// A frame's name, with any progress to two places.
    private func name(_ frame: Celebration.Frame) -> String {
        switch frame {
        case .burst(let progress): return String(format: "burst %.2f", progress)
        case .back(let beat, let progress): return String(format: "\(beat) %.2f", progress)
        default: return "\(frame)"
        }
    }

    func testActiveWorkChargesJumpsBurstsThenTheFaceComesBack() {
        let celebration = Celebration(started: 100, before: Preview.working.snapshot)
        XCTAssertTrue(celebration.charges)
        let times = [0, 0.1, 0.25, 0.4, 0.5, 0.56, 0.62, 0.7, 0.75, 0.9, 1.05, 1.25, 1.33, 1.4, 1.5, 1.8,
                     2.05, 2.2, 2.35, 2.45, 2.55]
        XCTAssertEqual(times.map { name(celebration.frame(at: 100 + $0)) }, [
            "charge(lit: true)", "charge(lit: true)", "charge(lit: false)", "charge(lit: true)",
            "charge(lit: false)", "charge(lit: true)", "charge(lit: false)", "charge(lit: true)",
            "fright(jumped: true)", "fright(jumped: false)", "burst 0.17", "burst 0.83",
            "gone", "gone", "creep 0.18", "peek 0.35", "crouch 0.30",
            "jump 0.40", "land 0.30", "rebound 0.30", "finished"
        ])
        XCTAssertEqual(celebration.length, 2.52, accuracy: 0.001)
        // Each flicker is shorter than the one before.
        let gaps = zip(Celebration.flickers.dropFirst(), Celebration.flickers).map { $0 - $1 }
        XCTAssertEqual(gaps, gaps.sorted(by: >))
    }

    func testWithoutActiveWorkItIsStartledStraightAway() {
        let celebration = Celebration(started: 0, before: Preview.blocked.snapshot)
        XCTAssertFalse(celebration.charges)
        let (fright, burst, gone) = (Celebration.frightLength, Celebration.burstLength, Celebration.goneLength)
        XCTAssertEqual(celebration.frame(at: 0), .fright(jumped: true))
        XCTAssertEqual(celebration.frame(at: fright), .burst(0))
        XCTAssertEqual(celebration.frame(at: fright + burst), .gone)
        XCTAssertEqual(celebration.frame(at: fright + burst + gone), .back(.creep, 0))
        if case .back(.rebound, _) = celebration.frame(at: celebration.length - 0.001) {} else {
            XCTFail("Should end springing back up")
        }
        XCTAssertEqual(celebration.frame(at: celebration.length + 0.001), .finished)
    }

    /// An image drawn into a 1x bitmap, as the menu bar would.
    private func drawn(_ image: NSImage, _ layout: AvatarArt.Layout) throws -> NSBitmapImageRep {
        let bitmap = try XCTUnwrap(NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: layout.width,
                                                    pixelsHigh: AvatarArt.itemHeight, bitsPerSample: 8,
                                                    samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                                    colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0))
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
        image.draw(in: NSRect(x: 0, y: 0, width: layout.width, height: AvatarArt.itemHeight))
        NSGraphicsContext.restoreGraphicsState()
        return bitmap
    }

    private func alpha(_ bitmap: NSBitmapImageRep, _ column: Int, _ row: Int) -> Double {
        Double(bitmap.colorAt(x: column, y: row)?.alphaComponent ?? 0)
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

    func testTheBurstSpinsOutFromTheChargedSideAndBlastsTheFaceAway() throws {
        let layout = AvatarArt.layout(left: .working(1), done: false)
        let middle = AvatarArt.itemHeight / 2
        let band = (AvatarArt.itemHeight - AvatarArt.sideHeight) / 2..<(AvatarArt.itemHeight + AvatarArt.sideHeight) / 2
        XCTAssertEqual(AvatarArt.burstOrigin(layout).0, Double(layout.faceX - AvatarArt.moat))
        // It starts where the charged side meets the face, biting into the face...
        let first = try drawn(AvatarArt.burst(left: .working(1), layout: layout, step: 1), layout)
        XCTAssertEqual(alpha(first, layout.faceX + 1, middle), 1, accuracy: 0.01)
        XCTAssertEqual(alpha(first, layout.width - 3, middle), 0, accuracy: 0.01)
        // ...the green never leaves the sides' band, while above and below
        // it the star cuts the face away...
        var cut = false
        for step in 1...AvatarArt.burstSteps {
            let green = try drawn(AvatarArt.burst(left: .working(1), layout: layout, step: step), layout)
            let face = try drawn(AvatarArt.image(.wincing, layout: layout, blast: step), layout)
            let whole = try drawn(AvatarArt.image(.wincing, layout: layout), layout)
            let star = AvatarArt.starCoverage(layout: layout, step: step, scale: 1)
            for row in 0..<AvatarArt.itemHeight where !band.contains(row) {
                for column in 0..<layout.width {
                    XCTAssertEqual(alpha(green, column, row), 0, accuracy: 0.01, "(\(column), \(row)) at step \(step)")
                    if star[row][column] == 1 {
                        XCTAssertEqual(alpha(face, column, row), 0, accuracy: 0.01, "(\(column), \(row)) at step \(step)")
                        if alpha(whole, column, row) > 0.5 { cut = true }
                    }
                }
            }
        }
        XCTAssertTrue(cut)
        // ...and at its last step the face is gone and the band solid green, right across.
        let last = try drawn(AvatarArt.burst(left: .working(1), layout: layout, step: AvatarArt.burstSteps), layout)
        let gone = try drawn(AvatarArt.image(.wincing, layout: layout, blast: AvatarArt.burstSteps), layout)
        for column in 0..<layout.width {
            XCTAssertEqual(alpha(last, column, middle), 1, accuracy: 0.01, "column \(column)")
            for row in 0..<AvatarArt.itemHeight {
                XCTAssertEqual(alpha(gone, column, row), 0, accuracy: 0.01, "(\(column), \(row))")
            }
        }
        XCTAssertFalse(AvatarArt.burst(left: .working(1), layout: layout, step: 1).isTemplate)
        XCTAssertTrue(AvatarArt.image(.wincing, layout: layout, blast: 1).isTemplate)
        XCTAssertEqual(AvatarArt.burstStep(0), 1)
        XCTAssertEqual(AvatarArt.burstStep(0.999), AvatarArt.burstSteps)
        // Without a charged side, it starts from the face's middle.
        let request = AvatarArt.layout(left: nil, done: false)
        XCTAssertEqual(AvatarArt.burstOrigin(request).0, Double(AvatarArt.faceWidth / 2))
    }

    func testTheEyesSqueezeShutAsTheGreenBursts() {
        // Wide in fright, then half shut as soon as the burst starts, then shut.
        let steps = [0, 0.1, 0.35, 0.45, 0.95].map { Celebration.eyes(bursting: $0) }
        XCTAssertEqual(steps.map(\.face), [.wincing, .wincing, .wincing, .attention, .attention])
        XCTAssertEqual(steps.map(\.blinking), [false, false, false, true, true])
        // Each step shows less eye: more of the face stays solid.
        func eyeArea(_ eyes: (face: Face, blinking: Bool)) -> Double {
            AvatarArt.faceAlpha(eyes.face, blinking: eyes.blinking, scale: 2).joined().reduce(0) { $0 + (1 - $1) }
        }
        let fright = eyeArea((.attention, false))
        let areas = [0, 0.6].map { eyeArea(Celebration.eyes(bursting: $0)) }
        XCTAssertGreaterThan(fright, areas[0])
        XCTAssertGreaterThan(areas[0], areas[1])
    }

    func testThenTheFaceIsBlastedAwayLeavingTheSidesBarSolidGreen() throws {
        let layout = AvatarArt.layout(left: nil, done: true)
        let flash = try rep(AvatarArt.flash(layout: layout), layout)
        XCTAssertFalse(try eyeHoles(AvatarArt.flash(layout: layout), layout: layout))
        let color = try XCTUnwrap(flash.colorAt(x: layout.width - 2, y: AvatarArt.itemHeight / 2)?
            .usingColorSpace(.sRGB))
        XCTAssertGreaterThan(color.greenComponent, color.redComponent + 0.3)
        // The bar is the sides' band, right across: solid along its middle,
        // and nothing above or below it.
        let top = (AvatarArt.itemHeight - AvatarArt.sideHeight) / 2
        for column in 0..<layout.width {
            XCTAssertEqual(flash.colorAt(x: column, y: AvatarArt.itemHeight / 2)?.alphaComponent ?? 0, 1,
                           accuracy: 0.01, "column \(column)")
            for row in Array(0..<top) + Array((top + AvatarArt.sideHeight)..<AvatarArt.itemHeight) {
                XCTAssertEqual(flash.colorAt(x: column, y: row)?.alphaComponent ?? 1, 0, accuracy: 0.01)
            }
        }
        XCTAssertTrue(AvatarArt.blank(layout: layout).isTemplate)
        XCTAssertEqual(AvatarArt.blank(layout: layout).size, AvatarArt.flash(layout: layout).size)
    }

    private func acts(_ layout: AvatarArt.Layout) -> [(Celebration.Beat, Celebration.Act)] {
        Celebration.Beat.allCases.flatMap { beat in
            (0..<20).map { (beat, Celebration.act(beat, Double($0) / 20, layout: layout)) }
        }
    }

    /// Checks every way home keeps: eyes inside the face, the face never
    /// below the item, the green bar under its home until it lands, a blink
    /// as it looks, and landing squashed, happy, at home.
    private func checkWayHome(_ layout: AvatarArt.Layout) {
        for (beat, act) in acts(layout) {
            let pose = act.pose
            XCTAssertLessThanOrEqual(pose.top + pose.height, AvatarArt.itemHeight, "\(beat)")
            let shift = (pose.height - AvatarArt.faceHeight) / 2
            for eye in AvatarArt.eyes(act.face, blinking: act.blinking) {
                XCTAssertGreaterThanOrEqual(eye.top + shift, 1, "\(beat)")
                XCTAssertLessThanOrEqual(eye.top + shift + eye.art.rows1.count, pose.height - 1, "\(beat)")
            }
            XCTAssertEqual(act.wash, ![Celebration.Beat.land, .rebound].contains(beat), "\(beat)")
        }
        XCTAssertTrue((0..<20).contains { Celebration.act(.peek, Double($0) / 20, layout: layout).blinking })
        let land = Celebration.act(.land, 0.5, layout: layout)
        XCTAssertEqual(land.pose.dx, 0)
        XCTAssertEqual(land.pose.lift, 0)
        XCTAssertLessThan(land.pose.height, AvatarArt.faceHeight)
        XCTAssertEqual(land.face, .done)
    }

    func testTheFaceComesBackFromTheLeftAndHopsHome() throws {
        let layout = AvatarArt.layout(left: nil, done: true)
        checkWayHome(layout)
        for (beat, act) in acts(layout) {
            XCTAssertGreaterThanOrEqual(act.pose.top, 0, "\(beat)")  // Within the item.
        }
        // It only ever moves toward home...
        let xs = acts(layout).map { layout.faceX + $0.1.pose.dx }
        XCTAssertEqual(xs, xs.sorted())
        // ...from three points showing at the left edge, to one eye...
        XCTAssertEqual(xs.first.map { $0 + AvatarArt.faceWidth }, 3)
        let peek = Celebration.act(.peek, 0.2, layout: layout)
        let eyes = AvatarArt.eyes(peek.face)
        XCTAssertLessThanOrEqual(layout.faceX + peek.pose.dx + eyes[0].left + eyes[0].art.rows1[0].count, 0)
        XCTAssertGreaterThanOrEqual(layout.faceX + peek.pose.dx + eyes[1].left, 0)
        let peekRep = try rep(AvatarArt.image(peek.face, layout: layout, pose: peek.pose), layout)
        let eyeX = layout.faceX + peek.pose.dx + eyes[1].left + 2
        XCTAssertLessThan(peekRep.colorAt(x: eyeX, y: peek.pose.top + eyes[1].top + 3)?.alphaComponent ?? 1, 0.1)
        XCTAssertEqual(peekRep.colorAt(x: eyeX, y: peek.pose.top + 1)?.alphaComponent ?? 0, 1, accuracy: 0.01)
        // ...squashed before the hop, stretched taking off, a point up in the air.
        XCTAssertLessThan(Celebration.act(.crouch, 0.5, layout: layout).pose.height, AvatarArt.faceHeight)
        XCTAssertGreaterThan(Celebration.act(.jump, 0.1, layout: layout).pose.height, AvatarArt.faceHeight)
        XCTAssertEqual(Celebration.act(.jump, 0.5, layout: layout).pose.lift, 1)
    }

    func testWithActiveWorkLeftTheFaceDropsInFromAbove() throws {
        let layout = AvatarArt.layout(left: .working(1), done: true)
        checkWayHome(layout)
        // Straight down, never across the active side.
        XCTAssertTrue(acts(layout).allSatisfy { $0.1.pose.dx == 0 })
        // Three points showing at the top, sliding down until its eyes show, looking down...
        let creep = (0..<20).map { Celebration.act(.creep, Double($0) / 20, layout: layout).pose }
        XCTAssertEqual(creep.first.map { $0.top + $0.height }, 3)
        XCTAssertEqual(creep.map(\.top), creep.map(\.top).sorted())
        let peek = Celebration.act(.peek, 0.2, layout: layout)
        XCTAssertEqual(peek.face, .lookingDown)
        for eye in AvatarArt.eyes(peek.face) {
            XCTAssertGreaterThanOrEqual(peek.pose.top + eye.top, 0)
        }
        // ...braces a point back up, then falls faster and faster, stretched, onto the bar.
        XCTAssertEqual(Celebration.act(.crouch, 0.5, layout: layout).pose.top, peek.pose.top - 1)
        let fall = (0..<20).map { Celebration.act(.jump, Double($0) / 20, layout: layout).pose }
        let bottoms = fall.map { $0.top + $0.height }
        XCTAssertEqual(bottoms, bottoms.sorted())
        XCTAssertGreaterThan(fall[10].height, AvatarArt.faceHeight)
        XCTAssertLessThan(bottoms[10] - bottoms[5], bottoms[19] - bottoms[14])
    }

    func testTheSidesMakeWayForTheReturningFace() throws {
        let layout = AvatarArt.layout(left: .working(1), done: true)
        let pose = Celebration.act(.creep, 0, layout: layout).pose
        let face = try drawn(AvatarArt.image(.lookingDown, layout: layout, pose: pose), layout)
        let sides = try drawn(try XCTUnwrap(AvatarArt.sides(left: .working(1), layout: layout, pose: pose,
                                                            wash: true)), layout)
        // No pixel holds both face and color, wherever the face is.
        for row in 0..<AvatarArt.itemHeight {
            for column in 0..<layout.width where alpha(face, column, row) > 0 {
                XCTAssertEqual(alpha(sides, column, row), 0, accuracy: 0.01, "(\(column), \(row))")
            }
        }
        // The bar runs on under the face's home, blue blending into green.
        let middle = AvatarArt.itemHeight / 2
        func color(_ column: Int) throws -> NSColor {
            XCTAssertEqual(alpha(sides, column, middle), 1, accuracy: 0.01, "column \(column)")
            return try XCTUnwrap(sides.colorAt(x: column, y: middle)?.usingColorSpace(.sRGB))
        }
        let blue = try color(layout.faceX - AvatarArt.moat - 1)
        let between = try color(layout.faceX + AvatarArt.faceWidth / 2)
        let green = try color(layout.width - 2)
        XCTAssertGreaterThan(blue.blueComponent, blue.greenComponent + 0.2)
        XCTAssertGreaterThan(green.greenComponent, green.blueComponent + 0.2)
        XCTAssertLessThan(between.blueComponent, blue.blueComponent - 0.1)
        XCTAssertGreaterThan(between.blueComponent, green.blueComponent + 0.1)
    }

    func testChargeLightsOnlyTheWorkingSideBlendingIntoTheFaceWithADoneSide() throws {
        let middle = AvatarArt.itemHeight / 2
        func charged(_ layout: AvatarArt.Layout) throws -> (edge: NSColor, inner: NSColor) {
            let bitmap = try drawn(try XCTUnwrap(AvatarArt.sides(left: .working(2), charged: true, layout: layout)),
                                   layout)
            return (try XCTUnwrap(bitmap.colorAt(x: 1, y: middle)?.usingColorSpace(.sRGB)),
                    try XCTUnwrap(bitmap.colorAt(x: layout.left - AvatarArt.moat - 1, y: middle)?
                        .usingColorSpace(.sRGB)))
        }
        // Active only: all green.
        let alone = try charged(AvatarArt.layout(left: .working(2), done: false))
        XCTAssertGreaterThan(alone.edge.greenComponent, alone.edge.blueComponent + 0.2)
        XCTAssertGreaterThan(alone.inner.greenComponent, alone.inner.blueComponent + 0.2)
        // With a done side: blue at its outer edge, green by the face.
        let both = try charged(AvatarArt.layout(left: .working(2), done: true))
        XCTAssertGreaterThan(both.edge.blueComponent, both.edge.greenComponent + 0.2)
        XCTAssertGreaterThan(both.inner.greenComponent, both.inner.blueComponent + 0.2)
        let layout = AvatarArt.layout(left: .working(2), done: false)
        XCTAssertFalse(AvatarArt.sides(left: .working(2), charged: true, layout: layout)
                       === AvatarArt.sides(left: .working(2), layout: layout))
        let request = AvatarArt.layout(left: .attention, done: false)
        XCTAssertTrue(AvatarArt.sides(left: .attention, charged: true, layout: request)
                      === AvatarArt.sides(left: .attention, layout: request))
    }
}
