import QtQuick
import QtQuick.Window
import "baa-avatar.mjs" as Baa

// BAA's menu bar avatar for Omarchy's bar, drawn like the Mac app's status
// item (menu-bar/Sources/BAA) from the same pixels: the face in the bar's
// foreground color, blue active work on its left, green completions on its
// right, a blink every few seconds, the request side's pulse and a new
// completion's celebration. Load it from the Agents bar widget and bind the
// counts; it only draws, it never acts on tasks.
//
// At the menu bar's own 18 points it needs a bar at least 24 px tall; on a
// smaller bar it keeps to the face's 16 points: its jumps become flinches.
Item {
    id: root

    /// Working and blocked agents, as the Stick counts them.
    property int active: 0
    property int blocked: 0
    /// Unread completions.
    property int done: 0
    /// False when the counts can't be trusted: the face turns puzzled.
    property bool healthy: true
    /// False until the counts come from a real snapshot. Only a rise between
    /// two loaded snapshots is a new completion: loading isn't one.
    property bool loaded: true
    property color foreground: "white"
    property string fontFamily: ""
    /// The bar's height; below 24 px the avatar keeps to 16 px.
    property int barHeight: 26
    /// Stop timers (and celebrations) while hidden.
    property bool running: visible

    readonly property int headroom: barHeight >= 24 ? 1 : 0
    readonly property int pixelScale: Screen.devicePixelRatio > 1.25 ? 2 : 1
    readonly property var snapshot: ({ active: active, blocked: blocked, done: done, healthy: healthy && loaded })
    property var shown: null
    property var celebration: null
    property bool blinking: false
    property bool peak: false
    property var view: describe()

    implicitWidth: view.layout.width
    implicitHeight: headroom ? Baa.sizes.itemHeight : Baa.sizes.faceHeight
    width: implicitWidth
    height: implicitHeight

    function now() { return Date.now() / 1000 }

    function measure(text) {
        metrics.text = text
        return metrics.advanceWidth
    }

    function describe() {
        return Baa.describe(snapshot, celebration, now(), {
            blinking: blinking, peak: peak, headroom: headroom, measure: measure
        })
    }

    function redraw() {
        view = describe()
        base.requestPaint()
        star.requestPaint()
    }

    property bool completed: false
    // Counts arrive one binding at a time; judge the change once they've all settled.
    onSnapshotChanged: Qt.callLater(settle)
    function settle() {
        if (completed && shown && !celebration && running && Baa.celebrates(shown, snapshot)) {
            celebration = { started: now(), before: shown }
            celebrationTimer.start()
        }
        shown = snapshot
        redraw()
    }
    onForegroundChanged: redraw()
    onHeadroomChanged: redraw()
    onRunningChanged: if (!running) { celebration = null; blinking = false; redraw() }
    Component.onCompleted: {
        shown = snapshot
        redraw()
        // Bindings may still settle the first counts after this: arm afterwards.
        Qt.callLater(function() { root.shown = root.snapshot; root.completed = true })
    }

    readonly property var pixels: Baa.render(view, pixelScale, [foreground.r, foreground.g, foreground.b])

    TextMetrics {
        id: metrics
        font.family: root.fontFamily
        font.pixelSize: 11
        font.bold: true
    }
    FontMetrics {
        id: labelFont
        font: metrics.font
    }

    // The sides, the face and the side's label.
    Canvas {
        id: base
        anchors.fill: parent
        canvasSize: Qt.size(root.width * root.pixelScale, root.height * root.pixelScale)
        smooth: false
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const px = root.pixels
            put(ctx, px.base, px.width, px.height)
            const label = px.label
            if (!label) return
            const scale = px.scale
            ctx.font = "bold " + (11 * scale) + "px \"" + root.fontFamily + "\""
            ctx.textAlign = "center"
            ctx.textBaseline = "alphabetic"
            ctx.fillStyle = "black"
            // Digits are cut out of the blue like the eyes are cut out of the
            // face; the request mark is inked, as on the Stick.
            ctx.globalCompositeOperation = label.kind === "cut" ? "destination-out" : "source-over"
            const capHeight = labelFont.tightBoundingRect("8").height
            ctx.fillText(label.text, label.x * scale, (label.y + capHeight / 2) * scale)
        }
    }

    // A completion's starburst, over the label.
    Canvas {
        id: star
        anchors.fill: parent
        canvasSize: base.canvasSize
        smooth: false
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const px = root.pixels
            if (px.star) put(ctx, px.star, px.width, px.height)
        }
    }

    function put(ctx, bytes, width, height) {
        const image = ctx.createImageData(width, height)
        const data = image.data
        for (let i = 0; i < bytes.length; i++) data[i] = bytes[i]
        // Qt 6.8's Canvas ignores putImageData without the dirty rectangle.
        ctx.putImageData(image, 0, 0, 0, 0, width, height)
    }

    // Open eyes blink every few seconds.
    Timer {
        id: blinkTimer
        running: root.running && !root.celebration
        interval: 3500 + Math.random() * 3500
        repeat: true
        onTriggered: {
            interval = 3500 + Math.random() * 3500
            root.blinking = true
            root.redraw()
            reopen.start()
        }
    }
    Timer {
        id: reopen
        interval: 140
        onTriggered: { root.blinking = false; root.redraw() }
    }

    // The request side steps between the Stick's yellow and orange.
    Timer {
        running: root.running && root.healthy && root.blocked > 0
        interval: 650
        repeat: true
        onTriggered: { root.peak = !root.peak; root.redraw() }
        onRunningChanged: if (!running && root.peak) { root.peak = false; root.redraw() }
    }

    Timer {
        id: celebrationTimer
        interval: 16
        repeat: true
        onTriggered: {
            if (!root.celebration || root.now() - root.celebration.started >= Baa.celebrationLength(root.celebration)) {
                stop()
                root.celebration = null
            }
            root.redraw()
        }
    }
}
