import QtQuick
import QtQuick.Controls

/* Histogram + Waveform scope display. Matches Mac ScopeView.swift. */
Item {
    id: scopeRoot
    height: 140

    property var histData: null  /* { r: [256], g: [256], b: [256], luma: [256] } */
    property string mode: "histogram"  /* "histogram" or "waveform" */

    Row {
        anchors.top: parent.top; anchors.right: parent.right; anchors.rightMargin: 4; z: 2; spacing: 2
        Repeater {
            model: ["Histogram", "Waveform"]
            Rectangle {
                property bool active: (modelData === "Histogram" ? "histogram" : "waveform") === scopeRoot.mode
                width: 68; height: 18; radius: 3
                color: active ? "#e87a20" : scopeMA.containsMouse ? "#333" : "#222"
                Text { anchors.centerIn: parent; text: modelData; color: parent.active ? "#fff" : "#888"; font.pixelSize: 9 }
                MouseArea { id: scopeMA; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: scopeRoot.mode = (modelData === "Histogram" ? "histogram" : "waveform")
                }
            }
        }
    }

    Canvas {
        id: scopeCanvas
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            var w = width, h = height
            ctx.clearRect(0, 0, w, h)

            /* Dark background */
            ctx.fillStyle = "rgba(0,0,0,0.7)"
            ctx.fillRect(0, 0, w, h)

            if (!histData) return

            if (scopeRoot.mode === "histogram") {
                drawHistogram(ctx, w, h)
            } else {
                drawWaveform(ctx, w, h)
            }
        }

        function drawHistogram(ctx, w, h) {
            var binW = w / 256
            var channels = [
                { bins: histData.r, color: "rgba(255,80,80,0.3)" },
                { bins: histData.g, color: "rgba(80,255,80,0.3)" },
                { bins: histData.b, color: "rgba(80,80,255,0.3)" }
            ]

            for (var c = 0; c < channels.length; c++) {
                var bins = channels[c].bins
                ctx.fillStyle = channels[c].color
                ctx.beginPath()
                ctx.moveTo(0, h)
                for (var i = 0; i < 256; i++) {
                    var x = i * binW
                    var y = h - bins[i] * h * 0.9
                    ctx.lineTo(x, y)
                }
                ctx.lineTo(w, h)
                ctx.closePath()
                ctx.fill()
            }

            /* Luma outline */
            ctx.strokeStyle = "rgba(255,255,255,0.7)"
            ctx.lineWidth = 1
            ctx.beginPath()
            for (var j = 0; j < 256; j++) {
                var lx = j * binW
                var ly = h - histData.luma[j] * h * 0.9
                if (j === 0) ctx.moveTo(lx, ly)
                else ctx.lineTo(lx, ly)
            }
            ctx.stroke()
        }

        function drawWaveform(ctx, w, h) {
            /* Simple waveform: for each horizontal position, draw luma dots */
            var luma = histData.luma
            if (!luma || luma.length === 0) return

            /* Use luma histogram as approximate waveform */
            var binW = w / 256
            ctx.fillStyle = "rgba(0,255,0,0.4)"
            for (var i = 0; i < 256; i++) {
                var val = luma[i]
                if (val < 0.01) continue
                var x = i * binW
                var y = h - (i / 255) * h * 0.95 - h * 0.025
                var barH = Math.max(1, val * 40)
                ctx.fillRect(x, y - barH / 2, binW, barH)
            }

            /* Reference lines at 10% and 90% */
            ctx.strokeStyle = "rgba(255,255,255,0.15)"
            ctx.lineWidth = 0.5
            for (var level = 0; level < 2; level++) {
                var lv = level === 0 ? 0.1 : 0.9
                var ly = h - lv * h * 0.95 - h * 0.025
                ctx.beginPath(); ctx.moveTo(0, ly); ctx.lineTo(w, ly); ctx.stroke()
            }
        }
    }

    onHistDataChanged: scopeCanvas.requestPaint()
    onModeChanged: scopeCanvas.requestPaint()
}
