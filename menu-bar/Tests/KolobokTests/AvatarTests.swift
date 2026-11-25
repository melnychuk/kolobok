import AppKit
import XCTest
@testable import Kolobok

final class AvatarTests: XCTestCase {
    private let arts: [String: AvatarArt.Art] = [
        "open": AvatarArt.open, "resting": AvatarArt.resting, "focused": AvatarArt.focused, "happy": AvatarArt.happy,
        "wide": AvatarArt.wide, "closed": AvatarArt.closed, "squint": AvatarArt.squint
    ]

    func testEveryGridIsRectangularAndTwiceAsFineAtTwoX() {
        let body = AvatarArt.body
        XCTAssertEqual(body.rows1.count, AvatarArt.faceHeight)
        XCTAssertEqual(body.rows2.count, AvatarArt.faceHeight * 2)
        for (name, art) in arts.merging(["body": body], uniquingKeysWith: { first, _ in first }) {
            for scale in [1, 2] {
                let rows = art.rows(scale: scale)
                XCTAssertEqual(Set(rows.map(\.count)).count, 1, "\(name) @\(scale)x is ragged")
                XCTAssertTrue(rows.joined().allSatisfy { ".#123456789".contains($0) }, name)
            }
            XCTAssertEqual(art.rows2.count, art.rows1.count * 2, name)
            XCTAssertEqual(art.rows2[0].count, art.rows1[0].count * 2, name)
        }
    }

    func testTheSidesBandIsTheFaceTwoPointsShorterWithTheSameCorners() {
        for scale in [1, 2] {
            let face = AvatarArt.body.rows(scale: scale)
            let band = AvatarArt.bodyRows(height: AvatarArt.sideHeight, scale: scale)
            XCTAssertEqual(AvatarArt.bodyRows(height: AvatarArt.faceHeight, scale: scale), face)
            XCTAssertEqual(band.count, AvatarArt.sideHeight * scale)
            XCTAssertEqual(Array(band.prefix(3 * scale)), Array(face.prefix(3 * scale)))
            XCTAssertEqual(Array(band.suffix(3 * scale)), Array(face.suffix(3 * scale)))
        }
        XCTAssertEqual(AvatarArt.faceHeight - AvatarArt.sideHeight, 2)
    }

    func testEveryFaceFitsInsideTheBodyAndOpensItsEyes() {
        for face in Face.allCases {
            for blinking in [false, true] {
                for eye in AvatarArt.eyes(face, blinking: blinking) {
                    XCTAssertGreaterThanOrEqual(eye.left, 1, "\(face)")
                    XCTAssertLessThanOrEqual(eye.left + eye.art.rows1[0].count, AvatarArt.faceWidth - 1, "\(face)")
                    XCTAssertGreaterThanOrEqual(eye.top, 1, "\(face)")
                    XCTAssertLessThanOrEqual(eye.top + eye.art.rows1.count, AvatarArt.faceHeight - 1, "\(face)")
                }
                for scale in [1, 2] {
                    // Every expression cuts fully clear pixels out of the face.
                    let alpha = AvatarArt.faceAlpha(face, blinking: blinking, scale: scale).joined()
                    XCTAssertEqual(alpha.min(), 0, "\(face) @\(scale)x")
                }
            }
        }
        XCTAssertNotEqual(AvatarArt.faceAlpha(.idle, blinking: true, scale: 1), AvatarArt.faceAlpha(.idle, scale: 1))
        XCTAssertEqual(AvatarArt.faceAlpha(.done, blinking: true, scale: 1), AvatarArt.faceAlpha(.done, scale: 1))
    }

    func testRestingEyesAreShorterAndCenteredAroundTheBlink() {
        let eyes = AvatarArt.eyes(.idle)
        let lids = AvatarArt.eyes(.idle, blinking: true)
        for (eye, lid) in zip(eyes, lids) {
            XCTAssertEqual(eye.art.rows1.count, AvatarArt.open.rows1.count - 2)
            XCTAssertEqual(eye.art.rows1[0].count, AvatarArt.open.rows1[0].count)
            XCTAssertEqual(eye.top + eye.art.rows1.count / 2, lid.top)
        }
    }

    @MainActor
    func testTemplateBitmapsAreTheArtPixelForPixel() throws {
        let layout = AvatarArt.layout(left: .working(3), done: true)
        let image = AvatarArt.image(.idle, layout: layout)
        XCTAssertTrue(image.isTemplate)
        XCTAssertEqual(image.size, NSSize(width: layout.width, height: AvatarArt.itemHeight))
        let reps = image.representations.compactMap { $0 as? NSBitmapImageRep }
        XCTAssertEqual(reps.map(\.pixelsWide), [layout.width, layout.width * 2])
        for (rep, scale) in zip(reps, [1, 2]) {
            let face = AvatarArt.faceAlpha(.idle, scale: scale)
            let top = AvatarArt.Pose.home.top * scale
            XCTAssertEqual(AvatarArt.Pose.home.top, (AvatarArt.itemHeight - AvatarArt.faceHeight) / 2)
            for row in 0..<rep.pixelsHigh {
                for column in 0..<rep.pixelsWide {
                    let faceRow = row - top, faceColumn = column - layout.faceX * scale
                    let inside = face.indices.contains(faceRow) && face[faceRow].indices.contains(faceColumn)
                    let expected = inside ? UInt8((face[faceRow][faceColumn] * 255).rounded()) : 0
                    let alpha = try XCTUnwrap(rep.bitmapData)[(row * rep.pixelsWide + column) * 4 + 3]
                    XCTAssertEqual(alpha, expected, "(\(column), \(row)) @\(scale)x")
                }
            }
        }
    }

    @MainActor
    func testSidesFollowTheFaceAcrossAOnePointMoat() throws {
        let layout = AvatarArt.layout(left: .working(2), done: true)
        XCTAssertNil(AvatarArt.sides(left: nil, layout: AvatarArt.layout(left: nil, done: false)))
        let sides = try XCTUnwrap(AvatarArt.sides(left: .working(2), layout: layout))
        let face = AvatarArt.image(.working, layout: layout)
        for scale in [1, 2] {
            func rep(_ image: NSImage) throws -> NSBitmapImageRep {
                try XCTUnwrap(image.representations.compactMap { $0 as? NSBitmapImageRep }
                    .first { $0.pixelsWide == layout.width * scale })
            }
            let sideRep = try rep(sides), faceRep = try rep(face)
            func alpha(_ rep: NSBitmapImageRep, _ column: Int, _ row: Int) -> UInt8 {
                rep.bitmapData![(row * rep.pixelsWide + column) * 4 + 3]
            }
            let top = AvatarArt.Pose.home.top * scale
            let sideTop = (AvatarArt.itemHeight - AvatarArt.sideHeight) / 2 * scale
            let moat = AvatarArt.moat * scale
            let faceLeft = layout.faceX * scale, faceRight = (layout.faceX + AvatarArt.faceWidth) * scale - 1
            // Across the middle: solid side, then exactly the moat clear, then solid face, on both sides.
            let row = top + AvatarArt.faceHeight * scale / 2
            XCTAssertEqual(alpha(faceRep, faceLeft, row), 255)
            XCTAssertEqual(alpha(faceRep, faceRight, row), 255)
            for column in (faceLeft - moat)..<faceLeft {
                XCTAssertEqual(alpha(sideRep, column, row), 0, "moat @\(scale)x")
                XCTAssertEqual(alpha(faceRep, column, row), 0, "moat @\(scale)x")
            }
            for column in (faceRight + 1)...(faceRight + moat) {
                XCTAssertEqual(alpha(sideRep, column, row), 0, "moat @\(scale)x")
            }
            XCTAssertEqual(alpha(sideRep, faceLeft - moat - 1, row), 255)
            XCTAssertEqual(alpha(sideRep, faceRight + moat + 1, row), 255)
            // The sides never touch the face anywhere, corners included.
            for row in 0..<sideRep.pixelsHigh {
                for column in 0..<sideRep.pixelsWide where alpha(faceRep, column, row) > 0 {
                    XCTAssertEqual(alpha(sideRep, column, row), 0, "(\(column), \(row)) @\(scale)x")
                }
            }
            // A side's outer corner is the face's own corner, a point lower
            // (its top, clear of the label).
            for row in 0..<3 * scale {
                for column in 0..<AvatarArt.tuck * scale {
                    XCTAssertEqual(alpha(sideRep, column, sideTop + row), alpha(faceRep, faceLeft + column, top + row),
                                   "corner (\(column), \(row)) @\(scale)x")
                }
            }
            // The face stands a point proud of the sides, above and below:
            // nothing colored in its first and last point of rows.
            for row in Array(0..<sideTop) + Array((sideTop + AvatarArt.sideHeight * scale)..<sideRep.pixelsHigh) {
                for column in 0..<sideRep.pixelsWide {
                    XCTAssertEqual(alpha(sideRep, column, row), 0, "(\(column), \(row)) @\(scale)x")
                }
            }
            XCTAssertEqual(sideTop - top, scale)
            XCTAssertEqual(alpha(faceRep, faceLeft + AvatarArt.faceWidth * scale / 2, top), 255)
            XCTAssertEqual(alpha(faceRep, faceLeft + AvatarArt.faceWidth * scale / 2, top + AvatarArt.faceHeight * scale - 1), 255)
        }
    }

    func testMoatGrowsTheSilhouetteByADisc() {
        let dot = [[0.0, 0, 0, 0, 0], [0, 0, 0, 0, 0], [0, 0, 1, 0, 0], [0, 0, 0, 0, 0], [0, 0, 0, 0, 0]]
        XCTAssertEqual(AvatarArt.grown(dot, by: 1), [
            [0, 0, 0, 0, 0], [0, 0, 1, 0, 0], [0, 1, 1, 1, 0], [0, 0, 1, 0, 0], [0, 0, 0, 0, 0]
        ])
        XCTAssertEqual(AvatarArt.grown(dot, by: 2)[0][2], 1)
        XCTAssertEqual(AvatarArt.grown(dot, by: 2)[0][0], 0)
    }

    @MainActor
    func testActiveDigitsAreWhiteAndTheRequestMarkIsBlack() throws {
        func hasInk(_ shade: UInt8, for hint: LeftHint, scale: Int) throws -> Bool {
            let layout = AvatarArt.layout(left: hint, done: false)
            let image = try XCTUnwrap(AvatarArt.sides(left: hint, layout: layout))
            let rep = try XCTUnwrap(image.representations.compactMap { $0 as? NSBitmapImageRep }
                .first { $0.pixelsWide == layout.width * scale })
            let data = try XCTUnwrap(rep.bitmapData)
            // Away from the side's rounded corners and the moat, it would otherwise be solid.
            let top = ((AvatarArt.itemHeight - AvatarArt.sideHeight) / 2 + AvatarArt.tuck) * scale
            let rows = top..<((AvatarArt.itemHeight + AvatarArt.sideHeight) / 2 - AvatarArt.tuck) * scale
            let columns = AvatarArt.tuck * scale..<(layout.left - AvatarArt.moat) * scale
            var found = false
            for row in rows {
                for column in columns {
                    let offset = (row * rep.pixelsWide + column) * 4
                    XCTAssertEqual(data[offset + 3], 255, "The label must not punch through the side")
                    if data[offset] == shade && data[offset + 1] == shade && data[offset + 2] == shade {
                        found = true
                    }
                }
            }
            return found
        }
        for scale in [1, 2] {
            XCTAssertTrue(try hasInk(255, for: .working(2), scale: scale), "@\(scale)x")
            XCTAssertTrue(try hasInk(0, for: .attention, scale: scale), "@\(scale)x")
        }
    }

    @MainActor
    func testAttentionPulseKeepsTheItemWidth() throws {
        let layout = AvatarArt.layout(left: .attention, done: false)
        let calm = try XCTUnwrap(AvatarArt.sides(left: .attention, attentionPeak: false, layout: layout))
        let peak = try XCTUnwrap(AvatarArt.sides(left: .attention, attentionPeak: true, layout: layout))
        XCTAssertEqual(calm.size, peak.size)
        XCTAssertFalse(calm === peak)
        XCTAssertGreaterThan(AvatarArt.layout(left: .working(12), done: false).left, layout.left)
    }
}
