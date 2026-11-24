import AppKit

// The Stick's avatar redrawn by hand for the menu bar (pixels in
// AvatarPixels.swift). It isn't scaled down from the device: each screen
// scale has its own art, copied into bitmaps without resampling, so every
// edge lands where it was drawn.
//
// The face is a 20 × 14 pt template image, so AppKit picks its color for the
// actual menu bar. The status colors are a separate bitmap of the same frame:
// like the Stick's status sides, they wrap the face's outline, held off it by
// a one-point moat so the face stays distinct.

/// The face's expression for the current status. A blink is a passing frame, not a face.
enum Face: CaseIterable, Equatable, Sendable {
    case idle, working, done, attention, asleep, puzzled
    /// Both eyes squeezed half shut: only seen as a completion's green wipes
    /// over the frightened face (`Celebration`).
    case wincing

    /// Expressions whose eyes are open enough to blink.
    var blinks: Bool {
        switch self {
        case .idle, .working, .attention, .puzzled: return true
        case .done, .asleep, .wincing: return false
        }
    }
}

/// The colored side left of the face, where the Stick shows active work.
enum LeftHint: Equatable, Hashable, Sendable {
    case working(Int), attention

    var text: String {
        switch self {
        case .working(let count): return count > 9 ? "9+" : String(count)
        case .attention: return "!"
        }
    }
}

enum AvatarArt {
    static let faceWidth = 20
    static let faceHeight = 14
    /// The status item image is taller than the face, centered in the menu bar.
    static let itemHeight = 18
    /// Visible width of the green completed side.
    static let doneWidth = 5
    /// How far a side runs under the face: its corner radius, past which the body is solid.
    static let tuck = 4
    /// Clear moat between the face and the sides, so the face stays distinct
    /// from the color it sits on: one point, whole pixels at 1× and 2×.
    static let moat = 1

    /// The face's alpha at one scale, rows top first, 0...1.
    static func faceAlpha(_ face: Face, blinking: Bool = false, scale: Int) -> [[Double]] {
        var alpha = body.rows(scale: scale).map { $0.map(level) }
        for eye in eyes(face, blinking: blinking) {
            for (row, line) in eye.art.rows(scale: scale).enumerated() {
                for (column, cut) in line.map(level).enumerated() {
                    alpha[eye.top * scale + row][eye.left * scale + column] *= 1 - cut
                }
            }
        }
        // Asleep (Herdr not running) draws the face at half strength, like a disabled icon.
        let strength = face == .asleep ? 0.5 : 1
        return alpha.map { $0.map { $0 * strength } }
    }

    /// The silhouette grown by `radius` pixels in every direction, keeping its
    /// antialiased edge: each pixel takes the strongest within a disc around it.
    static func grown(_ alpha: [[Double]], by radius: Int) -> [[Double]] {
        let offsets = (-radius...radius).flatMap { row in
            (-radius...radius).compactMap { column in
                row * row + column * column <= radius * radius ? (row, column) : nil
            }
        }
        return alpha.indices.map { row in
            alpha[row].indices.map { column in
                offsets.reduce(0) { strongest, offset in
                    let (nearRow, nearColumn) = (row + offset.0, column + offset.1)
                    guard alpha.indices.contains(nearRow), alpha[nearRow].indices.contains(nearColumn) else {
                        return strongest
                    }
                    return max(strongest, alpha[nearRow][nearColumn])
                }
            }
        }
    }

    // MARK: Layout

    /// Visible point widths of the sides around the face.
    struct Layout: Hashable {
        var left = 0
        var done = false

        var faceX: Int { left }
        var width: Int { left + faceWidth + (done ? doneWidth : 0) }
    }

    static let hintFont = NSFont.monospacedDigitSystemFont(ofSize: 11, weight: .bold)

    static func layout(left: LeftHint?, done: Bool) -> Layout {
        // Whole points keep the sides on the pixel grid.
        let width = left.map { hint in
            let text = NSAttributedString(string: hint.text, attributes: [.font: hintFont])
            return max(12, Int(text.size().width.rounded(.up)) + 6)
        } ?? 0
        return Layout(left: width, done: done)
    }

    // MARK: Images

    /// One straight (unpremultiplied) sRGB pixel.
    struct Pixel {
        var red = 0.0, green = 0.0, blue = 0.0, alpha = 0.0
        static let clear = Pixel()
        static func black(_ alpha: Double) -> Pixel { Pixel(alpha: alpha) }
    }

    private enum Key: Hashable {
        case face(Face, blinking: Bool, Layout)
        case sides(LeftHint?, peak: Bool, charged: Bool, Layout)
        case flash(Layout, edge: Int?)
        case wipe(LeftHint?, Layout, edge: Int)
        case blank(Layout)
    }

    @MainActor private static var cache: [Key: NSImage] = [:]

    /// A template image of the face in the status item's frame, at 1× and 2×.
    @MainActor
    static func image(_ face: Face, blinking: Bool = false, layout: Layout = Layout()) -> NSImage {
        let blinking = blinking && face.blinks
        return cached(.face(face, blinking: blinking, layout)) {
            let image = NSImage(size: NSSize(width: layout.width, height: itemHeight))
            for scale in [1, 2] {
                let alpha = faceAlpha(face, blinking: blinking, scale: scale)
                image.addRepresentation(bitmap(width: layout.width, scale: scale) { column, row in
                    .black(faceAlpha(alpha, at: column, row, layout: layout, scale: scale))
                })
            }
            image.isTemplate = true
            return image
        }
    }

    /// The colored sides for the same frame as `image`, or nil without any.
    /// `charged` lights the working side with the Stick's charge, before a completion.
    @MainActor
    static func sides(left: LeftHint?, attentionPeak: Bool = false, charged: Bool = false,
                      layout: Layout) -> NSImage? {
        guard left != nil || layout.done else { return nil }
        let peak = attentionPeak && left == .attention
        let charged = charged && left.map { if case .working = $0 { true } else { false } } == true
        return cached(.sides(left, peak: peak, charged: charged, layout)) {
            let image = NSImage(size: NSSize(width: layout.width, height: itemHeight))
            for scale in [1, 2] {
                image.addRepresentation(sidesBitmap(left: left, peak: peak, charged: charged,
                                                    layout: layout, scale: scale))
            }
            return image
        }
    }

    /// The completion flash: the whole icon, face, sides and moat, in solid
    /// green, the avatar gone into it. Not a template: it's drawn over `blank`,
    /// or with an `edge` (in points), only left of a ragged line around it, as
    /// the green wipes in.
    @MainActor
    static func flash(layout: Layout, edge: Int? = nil) -> NSImage {
        cached(.flash(layout, edge: edge)) {
            let image = NSImage(size: NSSize(width: layout.width, height: itemHeight))
            guard let green = StickColor.done.usingColorSpace(.sRGB) else { return image }
            for scale in [1, 2] {
                let silhouette = body.rows(scale: scale).map { $0.map(level) }
                image.addRepresentation(bitmap(width: layout.width, scale: scale) { column, row in
                    let faceRow = row - (itemHeight - faceHeight) / 2 * scale
                    guard silhouette.indices.contains(faceRow),
                          column < wipeReach(edge: edge, row: row / scale, width: layout.width) * scale else {
                        return .clear
                    }
                    let side = sideCoverage(column: column, faceRow: faceRow, silhouette: silhouette,
                                            layout: layout, scale: scale)
                    let faceColumn = column - layout.faceX * scale
                    // The face's body, and the sides around it.
                    let alpha = silhouette[faceRow].indices.contains(faceColumn)
                        ? max(silhouette[faceRow][faceColumn], max(side.left, side.done))
                        : max(side.left, side.done)
                    return Pixel(red: green.redComponent, green: green.greenComponent,
                                 blue: green.blueComponent, alpha: alpha)
                })
            }
            return image
        }
    }

    /// The completion wipe: green sweeping across the icon from the left, up to
    /// `edge` points, over its charged sides. Drawn over the frightened face,
    /// it covers the face as it passes; the face shows right of the edge.
    @MainActor
    static func wipe(left: LeftHint?, layout: Layout, edge: Int) -> NSImage {
        let sides = sides(left: left, charged: true, layout: layout)
        let green = flash(layout: layout, edge: edge)
        return cached(.wipe(left, layout, edge: edge)) {
            NSImage(size: NSSize(width: layout.width, height: itemHeight), flipped: false) { rect in
                sides?.draw(in: rect)
                green.draw(in: rect)
                return true
            }
        }
    }

    /// An empty template image the size of the icon: keeps the status item's
    /// width while `flash` covers it.
    @MainActor
    static func blank(layout: Layout) -> NSImage {
        cached(.blank(layout)) {
            let image = NSImage(size: NSSize(width: layout.width, height: itemHeight))
            for scale in [1, 2] { image.addRepresentation(bitmap(width: layout.width, scale: scale) { _, _ in .clear }) }
            image.isTemplate = true
            return image
        }
    }

    @MainActor
    private static func cached(_ key: Key, _ make: () -> NSImage) -> NSImage {
        if let image = cache[key] { return image }
        let image = make()
        cache[key] = image
        return image
    }

    /// The face's alpha at an item-image pixel; zero outside the face.
    private static func faceAlpha(_ face: [[Double]], at column: Int, _ row: Int,
                                  layout: Layout, scale: Int) -> Double {
        let faceRow = row - (itemHeight - faceHeight) / 2 * scale
        let faceColumn = column - layout.faceX * scale
        guard face.indices.contains(faceRow), face[faceRow].indices.contains(faceColumn) else { return 0 }
        return face[faceRow][faceColumn]
    }

    /// How much of each side covers an item-image pixel, before the moat. A side
    /// is a solid band whose outer corners copy the face's own corners, running
    /// `tuck` points under the face.
    private static func sideCoverage(column: Int, faceRow: Int, silhouette: [[Double]],
                                     layout: Layout, scale: Int) -> (left: Double, done: Double) {
        let corner = tuck * scale
        let bodyWidth = faceWidth * scale
        if layout.left > 0, column < (layout.left + tuck) * scale {
            return (column < corner ? silhouette[faceRow][column] : 1, 0)
        }
        if layout.done, column - layout.faceX * scale >= bodyWidth - corner {
            let fromEdge = layout.width * scale - 1 - column
            return (0, fromEdge < corner ? silhouette[faceRow][bodyWidth - 1 - fromEdge] : 1)
        }
        return (0, 0)
    }

    private static func sidesBitmap(left: LeftHint?, peak: Bool, charged: Bool,
                                    layout: Layout, scale: Int) -> NSBitmapImageRep {
        let silhouette = body.rows(scale: scale).map { $0.map(level) }
        let moatRadius = moat * scale
        // The face plus its moat, padded so the moat can reach past the face's edge.
        let padding = Array(repeating: 0.0, count: moatRadius)
        let keepOut = grown(silhouette.map { padding + $0 + padding }, by: moatRadius)
        let leftColor = left.map { hint -> NSColor in
            switch hint {
            case .working: return charged ? StickColor.charge : StickColor.working
            case .attention: return peak ? StickColor.requestPeak : StickColor.request
            }
        }
        let rep = bitmap(width: layout.width, scale: scale) { column, row in
            let faceRow = row - (itemHeight - faceHeight) / 2 * scale
            guard silhouette.indices.contains(faceRow) else { return .clear }
            let faceColumn = column - layout.faceX * scale
            let side = sideCoverage(column: column, faceRow: faceRow, silhouette: silhouette,
                                    layout: layout, scale: scale)
            let color: NSColor?
            let coverage: Double
            if side.left > 0, let leftColor {
                (color, coverage) = (leftColor, side.left)
            } else if side.done > 0 {
                (color, coverage) = (StickColor.done, side.done)
            } else {
                return .clear
            }
            // Clear of the face and its moat; no pixel holds both face and color.
            let onFace = silhouette[faceRow].indices.contains(faceColumn) && silhouette[faceRow][faceColumn] > 0
            let nearFace = keepOut[faceRow].indices.contains(faceColumn + moatRadius)
                ? keepOut[faceRow][faceColumn + moatRadius] : 0
            guard !onFace, let rgb = color?.usingColorSpace(.sRGB) else { return .clear }
            return Pixel(red: rgb.redComponent, green: rgb.greenComponent, blue: rgb.blueComponent,
                         alpha: coverage * (1 - nearFace))
        }
        if let left { drawLabel(left, on: rep, layout: layout) }
        return rep
    }

    /// Active-task digits are cut out of the blue, like the eyes are cut out of
    /// the face, so they match the eyes on any menu bar. The request `!` stays
    /// solid black on yellow, as on the Stick.
    private static func drawLabel(_ hint: LeftHint, on rep: NSBitmapImageRep, layout: Layout) {
        NSGraphicsContext.saveGraphicsState()
        defer { NSGraphicsContext.restoreGraphicsState() }
        // The context already maps the rep's point size onto its pixels.
        let context = NSGraphicsContext(bitmapImageRep: rep)
        NSGraphicsContext.current = context
        if case .working = hint { context?.cgContext.setBlendMode(.destinationOut) }
        let text = NSAttributedString(string: hint.text, attributes: [
            .font: hintFont, .foregroundColor: NSColor.black
        ])
        // Centered on the part of the side outside the moat.
        let center = NSPoint(x: CGFloat(layout.left - moat) / 2, y: CGFloat(itemHeight) / 2)
        let baseline = center.y - hintFont.capHeight / 2
        text.draw(at: NSPoint(x: center.x - text.size().width / 2, y: baseline + hintFont.descender))
    }

    /// An sRGB bitmap of `width` × `height` points, filled pixel by pixel (row 0 at the top).
    private static func bitmap(width: Int, height: Int = itemHeight, scale: Int,
                               pixel: (Int, Int) -> Pixel) -> NSBitmapImageRep {
        let pixelsWide = width * scale, pixelsHigh = height * scale
        guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixelsWide, pixelsHigh: pixelsHigh,
                                         bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                         colorSpaceName: .deviceRGB, bytesPerRow: pixelsWide * 4, bitsPerPixel: 32)?
                .retagging(with: .sRGB),
              let data = rep.bitmapData else {
            preconditionFailure("Couldn't allocate the avatar bitmap")
        }
        func byte(_ value: Double) -> UInt8 { UInt8((min(max(value, 0), 1) * 255).rounded()) }
        for row in 0..<pixelsHigh {
            for column in 0..<pixelsWide {
                let value = pixel(column, row)
                let offset = (row * pixelsWide + column) * 4
                // Premultiplied.
                data[offset] = byte(value.red * value.alpha)
                data[offset + 1] = byte(value.green * value.alpha)
                data[offset + 2] = byte(value.blue * value.alpha)
                data[offset + 3] = byte(value.alpha)
            }
        }
        rep.size = NSSize(width: width, height: height)
        return rep
    }
}

enum StickColor {
    // sRGB inputs to firmware/avatar_palette.h's dark theme and
    // firmware/request_scene.h's request field, before RGB565.
    static let working = rgb(0x3B, 0x82, 0xF6)
    static let done = rgb(0x22, 0xC5, 0x5E)
    static let request = rgb(0xFA, 0xCC, 0x15)
    static let requestPeak = rgb(0xFB, 0x92, 0x16)
    /// The charge before a completion's flash. firmware/task_cues.h charges with
    /// a pale lime (#A6FF5C), too faint on a light menu bar, so this uses the done green.
    static let charge = done

    private static func rgb(_ red: CGFloat, _ green: CGFloat, _ blue: CGFloat) -> NSColor {
        NSColor(srgbRed: red / 255, green: green / 255, blue: blue / 255, alpha: 1)
    }
}

// MARK: - Completion wipe

extension AvatarArt {
    /// How far the completion wipe reaches along one row, in points: `edge`,
    /// give or take three points in bands three points tall, so its leading
    /// edge breaks into tongues like a burst. The same at every scale and
    /// different at each step, so it flickers as it crosses. No wipe (nil) or
    /// a finished one reaches the whole way.
    static func wipeReach(edge: Int?, row: Int, width: Int) -> Int {
        guard let edge, edge < width else { return width }
        let band = row / 3
        var hash = UInt32(truncatingIfNeeded: (band &* 73_856_093) ^ (edge &* 19_349_663))
        hash ^= hash >> 13
        hash = hash &* 0x5BD1_E995
        hash ^= hash >> 15
        return edge + Int(hash % 7) - 3
    }
}
