import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Item {
    id: hub
    signal formatSelected(string formatName)

    /* ---- Format Data ---- */
    ListModel {
        id: formatModel
        ListElement { name: "ProRes RAW"; initials: "PR"; r: 1.0; g: 0.58; b: 0.0; active: true; needsSDK: false; logo: "LogoProRes.png" }
        ListElement { name: "Blackmagic RAW"; initials: "BM"; r: 0.61; g: 0.35; b: 0.71; active: true; needsSDK: false; logo: "LogoBlackmagic.png" }
        ListElement { name: "ARRIRAW"; initials: "AR"; r: 0.16; g: 0.50; b: 0.73; active: true; needsSDK: false; logo: "LogoARRI.png" }
        ListElement { name: "RED R3D"; initials: "R3D"; r: 0.91; g: 0.30; b: 0.24; active: true; needsSDK: true; logo: "LogoRED.png" }
        ListElement { name: "CinemaDNG"; initials: "DNG"; r: 0.10; g: 0.74; b: 0.61; active: true; needsSDK: false; logo: "LogoCinemaDNG.png" }
        ListElement { name: "Canon CRM"; initials: "CRM"; r: 0.85; g: 0.11; b: 0.14; active: true; needsSDK: false; logo: "LogoCanon.png" }
        ListElement { name: "Nikon N-RAW"; initials: "NR"; r: 0.98; g: 0.82; b: 0.0; active: true; needsSDK: true; logo: "LogoNikon.png" }
        ListElement { name: "GoPro CineForm"; initials: "GP"; r: 0.0; g: 0.60; b: 0.87; active: true; needsSDK: false; logo: "LogoGoPro.png" }
        ListElement { name: "Z CAM ZRAW"; initials: "ZR"; r: 0.25; g: 0.25; b: 0.25; active: true; needsSDK: false; logo: "LogoZCam.png" }
    }

    property int itemCount: formatModel.count
    property real rotationAngle: 0
    property real dragOffset: 0
    property real stepAngle: 2 * Math.PI / itemCount
    property real carouselRadius: 180  /* match Mac */
    property real tickCounter: 0

    Behavior on rotationAngle {
        NumberAnimation { duration: 400; easing.type: Easing.OutBack; easing.overshoot: 0.8 }
    }

    property int focusedIndex: {
        var total = rotationAngle + dragOffset
        var raw = -total / stepAngle
        var idx = ((Math.round(raw) % itemCount) + itemCount) % itemCount
        return idx
    }

    /* ---- Background: perspective grid (match Mac's InfiniteGridBackground, dark theme) ---- */
    Canvas {
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            var w = width, h = height
            ctx.clearRect(0, 0, w, h)

            /* 1. Flat dark background — avoids 8-bit banding */
            ctx.fillStyle = "#0a0a0a"
            ctx.fillRect(0, 0, w, h)

            /* 2. Perspective vertical lines converging to vanishing point */
            var horizonY = h * 0.52
            var vCount = 16
            var bottomSpread = w * 3.5
            var horizonSpread = w * 0.6

            for (var i = 0; i <= vCount; i++) {
                var t = i / vCount
                var bottomX = (w - bottomSpread) / 2 + bottomSpread * t
                var horizonX = (w - horizonSpread) / 2 + horizonSpread * t

                /* Draw line segments with fading opacity */
                var segments = 15
                for (var s = 0; s < segments; s++) {
                    var t0 = s / segments
                    var t1 = (s + 1) / segments
                    var y0 = horizonY + (h - horizonY) * t0
                    var y1 = horizonY + (h - horizonY) * t1
                    if (y0 > h) break
                    var farBottom = h * 2.5
                    var x0 = horizonX + (bottomX - horizonX) * ((y0 - horizonY) / (farBottom - horizonY))
                    var x1 = horizonX + (bottomX - horizonX) * ((y1 - horizonY) / (farBottom - horizonY))
                    var alpha = t0 * 0.25

                    ctx.strokeStyle = "rgba(255,255,255," + alpha + ")"
                    ctx.lineWidth = 0.5
                    ctx.beginPath()
                    ctx.moveTo(x0, y0)
                    ctx.lineTo(x1, Math.min(y1, h))
                    ctx.stroke()
                }
            }

            /* 3. Horizontal lines with perspective spacing */
            var hCount = 10
            for (var j = 1; j <= hCount; j++) {
                var ht = j / hCount
                var y = horizonY + (h - horizonY) * Math.pow(ht, 1.8)
                if (y > h) continue
                var alpha2 = ht * 0.2

                ctx.strokeStyle = "rgba(255,255,255," + alpha2 + ")"
                ctx.lineWidth = 0.5
                ctx.beginPath()
                ctx.moveTo(0, y)
                ctx.lineTo(w, y)
                ctx.stroke()
            }
        }
    }

    /* ---- Title ---- */
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 40
        spacing: 8
        z: 200

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: "file:///C:/Users/kaden/BayerFlow-Win/qml/logo.png"
            width: 56; height: 56
            fillMode: Image.PreserveAspectFit
            smooth: true; mipmap: true
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "BayerFlow"
            color: "#e0e0e0"
            font.pixelSize: 28
            font.weight: Font.Bold
            font.family: "Segoe UI"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Select a RAW format to begin"
            color: "#666"
            font.pixelSize: 14
            font.family: "Segoe UI"
        }
    }

    /* ---- 3D Carousel ---- */
    Item {
        id: carouselContainer
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 30
        width: 500; height: 200  /* match Mac */

        Repeater {
            model: formatModel

            Item {
                id: orbItem
                required property int index
                required property string name
                required property string initials
                required property real r
                required property real g
                required property real b
                required property bool active
                required property bool needsSDK
                required property string logo

                property real itemAngle: hub.stepAngle * index
                property real angle: itemAngle + hub.rotationAngle + hub.dragOffset
                property real zDepth: Math.cos(angle)
                property real normalizedZ: (zDepth + 1.0) / 2.0

                /* Match Mac: scale 0.55-1.0, opacity 0.35-1.0 */
                property real orbScale: 0.55 + 0.45 * normalizedZ
                property real orbOpacity: 0.35 + 0.65 * normalizedZ
                property bool isFront: normalizedZ > 0.85

                /* Floating bob — match Mac: sin(time * 2/3π + phase) * 3 */
                property real phase: (index / hub.itemCount) * 2.0 * Math.PI
                property real floatY: Math.sin(hub.tickCounter * 0.035 * Math.PI + phase) * 3.0

                /* Depth Y: rear items slightly higher (match Mac: -12) */
                property real depthY: (1.0 - normalizedZ) * -12.0

                visible: zDepth > -0.3
                x: carouselContainer.width / 2 + Math.sin(angle) * hub.carouselRadius - 55
                y: carouselContainer.height / 2 + floatY + depthY - 55
                z: normalizedZ * 100
                scale: orbScale
                opacity: orbOpacity

                Behavior on x { NumberAnimation { duration: 400; easing.type: Easing.OutBack } }
                Behavior on scale { NumberAnimation { duration: 400; easing.type: Easing.OutBack } }
                Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }

                width: 110; height: 120

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    spacing: 4

                    /* ---- Multi-layer Orb (match Mac's 5 layers) ---- */
                    Item {
                        width: 80; height: 80
                        anchors.horizontalCenter: parent.horizontalCenter

                        /* Layer 1: Base gradient (darker at bottom for 3D) */
                        Rectangle {
                            id: orbBase
                            anchors.fill: parent; radius: 40
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: Qt.rgba(orbItem.r, orbItem.g, orbItem.b, orbItem.active ? 1.0 : 0.5) }
                                GradientStop { position: 1.0; color: Qt.rgba(orbItem.r * 0.5, orbItem.g * 0.5, orbItem.b * 0.5, orbItem.active ? 0.8 : 0.4) }
                            }
                        }

                        /* Layer 2: Specular highlight (top-left radial) */
                        Rectangle {
                            width: 50; height: 35
                            radius: 20
                            x: 10; y: 6
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.5) }
                                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
                            }
                        }

                        /* Layer 3: Logo image (fallback to initials) */
                        Image {
                            id: logoImg
                            anchors.centerIn: parent
                            width: 38; height: 38
                            source: "file:///C:/Users/kaden/BayerFlow-Win/qml/" + orbItem.logo
                            fillMode: Image.PreserveAspectFit
                            smooth: true; mipmap: true
                            visible: status === Image.Ready
                        }
                        Text {
                            anchors.centerIn: parent
                            text: orbItem.initials
                            color: "#ffffff"
                            font.pixelSize: orbItem.initials.length > 2 ? 16 : 22
                            font.weight: Font.Bold
                            font.family: "Segoe UI"
                            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.3)
                            visible: logoImg.status !== Image.Ready
                        }

                        /* Layer 4: Glossy rim (top bright, bottom dark) */
                        Rectangle {
                            anchors.fill: parent; radius: 40
                            color: "transparent"
                            border.width: 1.5
                            border.color: Qt.rgba(1, 1, 1, 0.25)
                        }

                        /* Layer 5: Bottom inner shadow */
                        Rectangle {
                            anchors.fill: parent; radius: 40
                            color: "transparent"
                            border.width: 2
                            border.color: "transparent"
                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width - 4; height: parent.height / 2
                                radius: parent.radius
                                color: "transparent"
                                border.width: 1
                                border.color: Qt.rgba(0, 0, 0, 0.15)
                            }
                        }

                        /* Click handler */
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (!orbItem.isFront) {
                                    rotateToIndex(orbItem.index)
                                } else if (!orbItem.active) {
                                    sdkToast.text = orbItem.name + " — Coming Soon"
                                    sdkToast.visible = true
                                    sdkToastTimer.restart()
                                } else if (orbItem.needsSDK) {
                                    sdkToast.text = orbItem.name + " requires a proprietary SDK"
                                    sdkToast.visible = true
                                    sdkToastTimer.restart()
                                } else {
                                    hub.formatSelected(orbItem.name)
                                }
                            }
                        }
                    }

                    /* Ground shadow */
                    Rectangle {
                        width: 50 * orbItem.orbScale; height: 8 * orbItem.orbScale
                        radius: width / 2
                        color: Qt.rgba(0, 0, 0, 0.15 * orbItem.normalizedZ)
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
            }
        }

        /* Drag to rotate */
        MouseArea {
            anchors.fill: parent
            property real startX: 0
            z: -1

            onPressed: function(mouse) { startX = mouse.x }
            onPositionChanged: function(mouse) {
                hub.dragOffset = (mouse.x - startX) / 120.0
            }
            onReleased: {
                hub.rotationAngle += hub.dragOffset
                hub.dragOffset = 0
                snapToNearest()
            }
        }

        /* Scroll wheel */
        WheelHandler {
            onWheel: function(event) {
                if (event.angleDelta.y > 0)
                    moveCarousel(-1)
                else if (event.angleDelta.y < 0)
                    moveCarousel(1)
            }
        }
    }

    /* ---- Focused Label ---- */
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 70
        spacing: 4
        z: 200

        Text {
            id: focusedNameText
            anchors.horizontalCenter: parent.horizontalCenter
            text: focusedIndex >= 0 && focusedIndex < formatModel.count ? formatModel.get(focusedIndex).name : ""
            color: "#e0e0e0"
            font.pixelSize: 20
            font.weight: Font.Bold
            font.family: "Segoe UI"
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: {
                if (focusedIndex < 0 || focusedIndex >= formatModel.count) return ""
                var item = formatModel.get(focusedIndex)
                if (!item.active) return "Coming Soon"
                if (item.needsSDK) return "Requires SDK"
                return "Click to select"
            }
            color: "#555"
            font.pixelSize: 12
        }
    }

    /* ---- Drop Zone ---- */
    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.hasUrls) {
                backend.inputPath = drop.urls[0]
                hub.formatSelected("auto")
            }
        }
    }

    /* ---- Navigation Hints ---- */
    Text {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 30
        text: "Scroll or drag to browse  \u2022  Drop a file to start"
        color: "#333"
        font.pixelSize: 11
        z: 200
    }

    /* ---- SDK Toast Notification ---- */
    Rectangle {
        id: sdkToast
        property alias text: sdkToastText.text
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 100
        width: sdkToastText.width + 40; height: 36
        radius: 18
        color: "#cc222222"
        border.color: "#e87a20"; border.width: 1
        visible: false
        opacity: visible ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        z: 300

        Text {
            id: sdkToastText
            anchors.centerIn: parent
            color: "#e0e0e0"
            font.pixelSize: 12
            font.family: "Segoe UI"
        }
    }
    Timer {
        id: sdkToastTimer
        interval: 2500
        onTriggered: sdkToast.visible = false
    }

    /* ---- Animation Timer ---- */
    Timer {
        interval: 50; running: true; repeat: true
        onTriggered: hub.tickCounter += 1
    }

    /* ---- Functions ---- */
    function snapToNearest() {
        var snapped = Math.round(rotationAngle / stepAngle) * stepAngle
        rotationAngle = snapped
    }

    function rotateToIndex(index) {
        var targetAngle = -stepAngle * index
        var diff = targetAngle - rotationAngle
        var normalized = Math.atan2(Math.sin(diff), Math.cos(diff))
        rotationAngle += normalized
    }

    function moveCarousel(direction) {
        rotationAngle -= stepAngle * direction
    }
}
