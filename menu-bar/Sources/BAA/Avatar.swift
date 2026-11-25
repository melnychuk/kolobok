import AppKit

// The Stick's avatar redrawn by hand for the menu bar (pixels in
// AvatarPixels.swift). It isn't scaled down from the device: each screen
// scale has its own art, copied into bitmaps without resampling, so every
// edge lands where it was drawn.
//
// The face is a 20 × 16 pt template image, so AppKit picks its color for the
// actual menu bar. The status colors are a separate bitmap of the same frame:
// like the Stick's status sides, they wrap the face's outline, held off it by
// a one-point moat so the face stays distinct. The sides are two points
// shorter than the face, so it stands a point proud of them, above and below.

/// The face's expression for the current status. A blink is a passing frame, not a face.
enum Face: CaseIterable, Equatable, Sendable {
    case idle, working, done, attention, asleep, puzzled
    /// Both eyes squeezed half shut: only seen as a completion's green bursts
    /// over the frightened face (`Celebration`).
    case wincing
    /// Peeking back in after a completion, toward the green side (`Celebration`).
    case peeking
    /// Peeking down from above at its home, before dropping back in (`Celebration`).
    case lookingDown

    /// Expressions whose eyes are open enough to blink.
    var blinks: Bool {
        switch self {
        case .idle, .working, .attention, .puzzled, .peeking, .lookingDown: return true
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
    static let faceHeight = 16
    /// The sides' height: a point short of the face's at the top and at the bottom.
    static let sideHeight = 14
    /// The status item image is taller than the face, centered in the menu bar.
    static let itemHeight = 18
    /// Visible width of the green completed side.
    static let doneWidth = 5
    /// How far a side runs under the face: its corner radius, past which the body is solid.
    static let tuck = 4
    /// Clear moat between the face and the sides, so the face stays distinct
    /// from the color it sits on: one point, whole pixels at 1× and 2×.
    static let moat = 1

    /// Where the face is in the item, in points: at home, or as it acts out a
    /// completion, moved sideways, hopping, squashed or stretched. Its bottom
    /// stays put unless it's lifted.
    struct Pose: Hashable, Sendable {
        var dx = 0
        var lift = 0
        var height = AvatarArt.faceHeight

        static let home = Pose()

        /// The face's top row in the item, in points.
        var top: Int { (AvatarArt.itemHeight + AvatarArt.faceHeight) / 2 - lift - height }
    }

    /// The body's rows `height` points tall: its drawn corners, with solid
    /// rows between them. The face is `faceHeight` tall; squashed, stretched
    /// or as the sides' band, it keeps the same corners.
    static func bodyRows(height: Int, scale: Int) -> [String] {
        let rows = body.rows(scale: scale)
        let corner = rows.prefix(while: { row in row.contains { $0 != "#" } }).count
        let solid = String(repeating: "#", count: rows[0].count)
        let middle = max(0, height * scale - 2 * corner)
        return Array(rows.prefix(corner)) + Array(repeating: solid, count: middle) + Array(rows.suffix(corner))
    }

    /// The face's alpha at one scale, rows top first, 0...1.
    static func faceAlpha(_ face: Face, blinking: Bool = false, height: Int = AvatarArt.faceHeight,
                          scale: Int) -> [[Double]] {
        var alpha = bodyRows(height: height, scale: scale).map { $0.map(level) }
        // Squashed or stretched, the eyes stay about the face's middle.
        let shift = (height - faceHeight) / 2 * scale
        for eye in eyes(face, blinking: blinking) {
            for (row, line) in eye.art.rows(scale: scale).enumerated() {
                let faceRow = eye.top * scale + shift + row
                guard alpha.indices.contains(faceRow) else { continue }
                for (column, cut) in line.map(level).enumerated() {
                    alpha[faceRow][eye.left * scale + column] *= 1 - cut
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

    // MARK: Item-sized grids

    /// A face-sized grid placed where `pose` puts the face in the item, at
    /// one scale, clipped to the item: item rows top first, zero elsewhere.
    static func placed(_ grid: [[Double]], layout: Layout, pose: Pose = .home, scale: Int) -> [[Double]] {
        let left = (layout.faceX + pose.dx) * scale, top = pose.top * scale
        return (0..<itemHeight * scale).map { row -> [Double] in
            (0..<layout.width * scale).map { column -> Double in
                let (gridRow, gridColumn) = (row - top, column - left)
                guard grid.indices.contains(gridRow), grid[gridRow].indices.contains(gridColumn) else { return 0 }
                return grid[gridRow][gridColumn]
            }
        }
    }

    /// The face's body, without eyes, where `pose` puts it.
    static func bodyAlpha(layout: Layout, pose: Pose = .home, scale: Int) -> [[Double]] {
        placed(bodyRows(height: pose.height, scale: scale).map { $0.map(level) },
               layout: layout, pose: pose, scale: scale)
    }

    /// How much of each side covers each item pixel. A side is a band
    /// `sideHeight` points tall, centered, whose outer corners are the face's
    /// own, running `tuck` points under the face's home.
    static func sideCoverage(layout: Layout, scale: Int) -> (left: [[Double]], done: [[Double]]) {
        let band = bodyRows(height: sideHeight, scale: scale).map { $0.map(level) }
        let top = (itemHeight - sideHeight) / 2 * scale
        let corner = tuck * scale, width = layout.width * scale
        var left = Array(repeating: Array(repeating: 0.0, count: width), count: itemHeight * scale)
        var done = left
        for (bandRow, line) in band.enumerated() {
            for column in 0..<width {
                if layout.left > 0, column < (layout.left + tuck) * scale {
                    left[top + bandRow][column] = column < corner ? line[column] : 1
                } else if layout.done, column >= (layout.faceX + faceWidth - tuck) * scale {
                    let fromEdge = width - 1 - column
                    done[top + bandRow][column] = fromEdge < corner ? line[line.count - 1 - fromEdge] : 1
                }
            }
        }
        return (left, done)
    }

    /// The sides' band right across the item, `sideHeight` points tall with
    /// the sides' outer corners at both ends: what stays of the icon while the
    /// face is blasted away by a completion and comes back.
    static func bar(layout: Layout, scale: Int) -> [[Double]] {
        let band = bodyRows(height: sideHeight, scale: scale).map { $0.map(level) }
        let top = (itemHeight - sideHeight) / 2 * scale
        let corner = tuck * scale, width = layout.width * scale
        var bar = Array(repeating: Array(repeating: 0.0, count: width), count: itemHeight * scale)
        for (bandRow, line) in band.enumerated() {
            for column in 0..<width {
                let fromEdge = width - 1 - column
                bar[top + bandRow][column] = column < corner ? line[column]
                    : fromEdge < corner ? line[line.count - 1 - fromEdge] : 1
            }
        }
        return bar
    }

    /// The whole icon's outline at home: the face and its sides, with no moat.
    static func silhouette(layout: Layout, scale: Int) -> [[Double]] {
        let sides = sideCoverage(layout: layout, scale: scale)
        let body = bodyAlpha(layout: layout, scale: scale)
        return combined(body, sides.left, sides.done)
    }

    /// The strongest of some item-sized grids, pixel by pixel.
    private static func combined(_ first: [[Double]], _ others: [[Double]]...) -> [[Double]] {
        first.indices.map { row -> [Double] in
            first[row].indices.map { column -> Double in
                others.reduce(first[row][column]) { max($0, $1[row][column]) }
            }
        }
    }

    // MARK: Images

    /// One straight (unpremultiplied) sRGB pixel.
    struct Pixel {
        var red = 0.0, green = 0.0, blue = 0.0, alpha = 0.0
        static let clear = Pixel()
        static func black(_ alpha: Double) -> Pixel { Pixel(alpha: alpha) }
    }

    private enum Key: Hashable {
        case face(Face, blinking: Bool, Layout, Pose, blast: Int?)
        case sides(LeftHint?, peak: Bool, charged: Bool, Layout, Pose, wash: Bool)
        case flash(Layout)
        case star(Layout, step: Int)
        case burst(LeftHint?, Layout, step: Int)
        case blank(Layout)
    }

    @MainActor private static var cache: [Key: NSImage] = [:]

    /// A template image of the face in the status item's frame, at 1× and 2×.
    /// `blast` is a completion burst's step: the face cut away wherever its
    /// star has reached, since the green itself stays in the sides' band.
    @MainActor
    static func image(_ face: Face, blinking: Bool = false, layout: Layout = Layout(),
                      pose: Pose = .home, blast: Int? = nil) -> NSImage {
        let blinking = blinking && face.blinks
        return cached(.face(face, blinking: blinking, layout, pose, blast: blast)) {
            let image = NSImage(size: NSSize(width: layout.width, height: itemHeight))
            for scale in [1, 2] {
                let alpha = placed(faceAlpha(face, blinking: blinking, height: pose.height, scale: scale),
                                   layout: layout, pose: pose, scale: scale)
                let star = blast.map { starCoverage(layout: layout, step: $0, scale: scale) }
                image.addRepresentation(bitmap(width: layout.width, scale: scale) { column, row in
                    .black(alpha[row][column] * (1 - (star?[row][column] ?? 0)))
                })
            }
            image.isTemplate = true
            return image
        }
    }

    /// The colored sides for the same frame as `image`, or nil without any,
    /// clear of the face and its moat where `pose` puts the face.
    /// `charged` lights the working side with the Stick's charge, before a
    /// completion. `wash` lays the green bar the face comes back onto after
    /// one, under the sides: the band they share, right across the item.
    @MainActor
    static func sides(left: LeftHint?, attentionPeak: Bool = false, charged: Bool = false,
                      layout: Layout, pose: Pose = .home, wash: Bool = false) -> NSImage? {
        guard left != nil || layout.done || wash else { return nil }
        let peak = attentionPeak && left == .attention
        let charged = charged && left.map { if case .working = $0 { true } else { false } } == true
        return cached(.sides(left, peak: peak, charged: charged, layout, pose, wash: wash)) {
            let image = NSImage(size: NSSize(width: layout.width, height: itemHeight))
            for scale in [1, 2] {
                image.addRepresentation(sidesBitmap(left: left, peak: peak, charged: charged, layout: layout,
                                                    pose: pose, wash: wash, scale: scale))
            }
            return image
        }
    }

    /// The completion flash: a solid green bar, the sides' band right across
    /// the icon, the face blasted away. Not a template: it's drawn over `blank`.
    @MainActor
    static func flash(layout: Layout) -> NSImage {
        cached(.flash(layout)) {
            let image = NSImage(size: NSSize(width: layout.width, height: itemHeight))
            for scale in [1, 2] {
                let alpha = bar(layout: layout, scale: scale)
                image.addRepresentation(bitmap(width: layout.width, scale: scale) { column, row in
                    paint(StickColor.done, alpha[row][column])
                })
            }
            return image
        }
    }

    /// Steps the completion burst grows in.
    static let burstSteps = 6

    /// The burst's step, 1...`burstSteps`, at 0 to 1 of the way through it.
    static func burstStep(_ progress: Double) -> Int {
        min(burstSteps, max(1, Int(progress * Double(burstSteps)) + 1))
    }

    /// The completion burst: a green star spinning out from where the
    /// charged side meets the face (or from the face's middle, without one),
    /// over the charged sides and the frightened face, but only ever in the
    /// sides' band, right across the icon: above and below it, the star cuts
    /// the face away instead (`image(blast:)`). At its last step the face is
    /// gone and the band is solid green.
    @MainActor
    static func burst(left: LeftHint?, layout: Layout, step: Int) -> NSImage {
        let sides = sides(left: left, charged: true, layout: layout)
        let star = star(layout: layout, step: step)
        return cached(.burst(left, layout, step: step)) {
            NSImage(size: NSSize(width: layout.width, height: itemHeight), flipped: false) { rect in
                sides?.draw(in: rect)
                star.draw(in: rect)
                return true
            }
        }
    }

    /// The burst's star alone, in the sides' band.
    @MainActor
    private static func star(layout: Layout, step: Int) -> NSImage {
        cached(.star(layout, step: step)) {
            let image = NSImage(size: NSSize(width: layout.width, height: itemHeight))
            for scale in [1, 2] {
                let star = starCoverage(layout: layout, step: step, scale: scale)
                let bar = bar(layout: layout, scale: scale)
                image.addRepresentation(bitmap(width: layout.width, scale: scale) { column, row in
                    paint(StickColor.done, star[row][column] * bar[row][column])
                })
            }
            return image
        }
    }

    /// How much of each item pixel the burst's star covers at a step,
    /// unclipped: ten spinning rays, grown until at the last step its inner
    /// points, between the rays, pass the farthest corner.
    static func starCoverage(layout: Layout, step: Int, scale: Int) -> [[Double]] {
        let (originX, originY) = burstOrigin(layout)
        let reach = [0.0, Double(layout.width)].flatMap { x in
            [0.0, Double(itemHeight)].map { y in hypot(x - originX, y - originY) }
        }.max() ?? 0
        let progress = Double(step) / Double(burstSteps)
        let full = (reach + 1) / starInner
        let outer = 4 + (full - 4) * pow(progress, 1.6)
        let inner = starInner * outer
        let spin = Double(step) * 0.35
        return (0..<itemHeight * scale).map { row -> [Double] in
            (0..<layout.width * scale).map { column -> Double in
                let dx = (Double(column) + 0.5) / Double(scale) - originX
                let dy = (Double(row) + 0.5) / Double(scale) - originY
                // Ten rays, sharpened by the cube.
                let ray = abs(cos(5 * (atan2(dy, dx) + spin)))
                let radius = inner + (outer - inner) * ray * ray * ray
                return min(max((radius - hypot(dx, dy)) * Double(scale) + 0.5, 0), 1)
            }
        }
    }

    /// The star's inner points, between its rays, as a share of its outer ones.
    private static let starInner = 0.62

    /// Where the burst starts, in points from the item's top-left.
    static func burstOrigin(_ layout: Layout) -> (Double, Double) {
        let x = layout.left > 0 ? layout.left - moat : layout.faceX + faceWidth / 2
        return (Double(x), Double(itemHeight) / 2)
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

    private static func paint(_ color: NSColor, _ alpha: Double) -> Pixel {
        guard alpha > 0, let rgb = color.usingColorSpace(.sRGB) else { return .clear }
        return Pixel(red: rgb.redComponent, green: rgb.greenComponent, blue: rgb.blueComponent, alpha: alpha)
    }

    /// One straight sRGB color, for blending.
    private struct Shade {
        var red, green, blue: Double

        init(_ color: NSColor) {
            let rgb = color.usingColorSpace(.sRGB) ?? color
            red = Double(rgb.redComponent)
            green = Double(rgb.greenComponent)
            blue = Double(rgb.blueComponent)
        }

        init(red: Double, green: Double, blue: Double) {
            self.red = red
            self.green = green
            self.blue = blue
        }

        /// This shade blending smoothly into `other` between two points
        /// across the item, at a pixel's centre.
        func blend(into other: Shade, from start: Double, to end: Double, column: Int, scale: Int) -> Shade {
            let x = (Double(column) + 0.5) / Double(scale)
            let t = min(max((x - start) / (end - start), 0), 1)
            let amount = t * t * (3 - 2 * t)
            return Shade(red: red + (other.red - red) * amount, green: green + (other.green - green) * amount,
                         blue: blue + (other.blue - blue) * amount)
        }
    }

    private static func sidesBitmap(left: LeftHint?, peak: Bool, charged: Bool, layout: Layout,
                                    pose: Pose, wash: Bool, scale: Int) -> NSBitmapImageRep {
        let coverage = sideCoverage(layout: layout, scale: scale)
        let green = Shade(StickColor.done)
        let leftShade = left.map { hint -> Shade in
            switch hint {
            case .working: return Shade(StickColor.working)
            case .attention: return Shade(peak ? StickColor.requestPeak : StickColor.request)
            }
        }
        // Layers, painted in order: how much each covers, and its shade by column.
        var layers: [(coverage: [[Double]], shade: (Int) -> Shade)] = []
        if wash {
            // One bar, the left side's color blending into green across the face's home.
            let (start, end) = (Double(layout.faceX - moat), Double(layout.faceX + faceWidth))
            layers.append((bar(layout: layout, scale: scale), { column in
                leftShade?.blend(into: green, from: start, to: end, column: column, scale: scale) ?? green
            }))
        } else {
            if let leftShade {
                // Charged, the working side lights up green; with a green done
                // side too, it blends from blue at its edge into green at the
                // face, as if the light runs into it.
                let charge = Shade(StickColor.charge), end = Double(layout.left - moat)
                layers.append((coverage.left, { column in
                    !charged ? leftShade
                        : layout.done ? leftShade.blend(into: charge, from: 0, to: end, column: column, scale: scale)
                        : charge
                }))
            }
            layers.append((coverage.done, { _ in green }))
        }
        // Clear of the face and its moat; no pixel holds both face and color.
        let face = bodyAlpha(layout: layout, pose: pose, scale: scale)
        let nearFace = grown(face, by: moat * scale)
        let rep = bitmap(width: layout.width, scale: scale) { column, row in
            guard face[row][column] == 0 else { return .clear }
            var pixel = Pixel.clear
            for layer in layers {
                let alpha = layer.coverage[row][column]
                guard alpha > 0 else { continue }
                let shade = layer.shade(column)
                let below = pixel.alpha * (1 - alpha)
                let total = alpha + below
                pixel = Pixel(red: (shade.red * alpha + pixel.red * below) / total,
                              green: (shade.green * alpha + pixel.green * below) / total,
                              blue: (shade.blue * alpha + pixel.blue * below) / total,
                              alpha: total)
            }
            pixel.alpha *= 1 - nearFace[row][column]
            return pixel
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
    /// The charge before a completion's flash, blending into the working side.
    /// firmware/task_cues.h charges with a pale lime (#A6FF5C), too faint on a
    /// light menu bar, so this uses the done green.
    static let charge = done

    private static func rgb(_ red: CGFloat, _ green: CGFloat, _ blue: CGFloat) -> NSColor {
        NSColor(srgbRed: red / 255, green: green / 255, blue: blue / 255, alpha: 1)
    }
}
