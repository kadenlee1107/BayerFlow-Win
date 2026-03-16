import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Universal
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    visible: true
    width: 1080
    height: 1040
    minimumWidth: 800
    minimumHeight: 700
    title: "BayerFlow"
    color: "#0a0a0a"
    flags: Qt.Window | Qt.FramelessWindowHint

    property bool showSplash: true
    property bool showHub: true
    property bool showRecoveryDialog: false
    property var recoveredSessions: []

    /* ---- Resize handles ---- */
    MouseArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 12; height: 12
        cursorShape: Qt.SizeFDiagCursor
        property point clickPos
        onPressed: clickPos = Qt.point(mouseX, mouseY)
        onPositionChanged: {
            root.width = Math.max(root.minimumWidth, root.width + mouseX - clickPos.x)
            root.height = Math.max(root.minimumHeight, root.height + mouseY - clickPos.y)
        }
    }

    /* ---- Custom Title Bar ---- */
    Rectangle {
        id: titleBar
        visible: !root.showSplash
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 40
        color: "#111111"
        z: 10

        /* Subtle bottom border */
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#1a1a1a" }

        /* Back to Hub button */
        Rectangle {
            anchors.left: parent.left; anchors.leftMargin: 140
            anchors.verticalCenter: parent.verticalCenter
            width: 70; height: 26; radius: 4
            color: hubBackMA.containsMouse ? "#333" : "#222"
            visible: !root.showHub
            Row {
                anchors.centerIn: parent; spacing: 4
                Text { text: "\u2190"; color: "#e87a20"; font.pixelSize: 13 }
                Text { text: "Hub"; color: "#aaa"; font.pixelSize: 11 }
            }
            MouseArea { id: hubBackMA; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: root.showHub = true
            }
        }

        MouseArea {
            anchors.fill: parent
            property point clickPos
            onPressed: clickPos = Qt.point(mouseX, mouseY)
            onPositionChanged: { root.x += mouseX - clickPos.x; root.y += mouseY - clickPos.y }
            onDoubleClicked: root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            /* App logo */
            Image {
                width: 22; height: 22
                source: "file:///C:/Users/kaden/BayerFlow-Win/qml/logo.png"
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }

            Text {
                text: "BayerFlow"
                color: "#909090"
                font.pixelSize: 13
                font.family: "Segoe UI"
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            Repeater {
                model: [
                    { icon: "\u2013", hoverColor: "#2a2a2a", action: "min" },
                    { icon: "\u25a1", hoverColor: "#2a2a2a", action: "max" },
                    { icon: "\u2715", hoverColor: "#c42b1c", action: "close" }
                ]
                Rectangle {
                    required property var modelData
                    required property int index
                    width: 46; height: 40
                    color: winBtnMA.containsMouse ? modelData.hoverColor : "transparent"
                    Text {
                        anchors.centerIn: parent; text: modelData.icon
                        color: (index === 2 && winBtnMA.containsMouse) ? "#fff" : "#888"
                        font.pixelSize: index === 2 ? 11 : 13
                    }
                    MouseArea {
                        id: winBtnMA; anchors.fill: parent; hoverEnabled: true
                        onClicked: {
                            if (modelData.action === "min") root.showMinimized()
                            else if (modelData.action === "max") root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
                            else Qt.quit()
                        }
                    }
                }
            }
        }
    }

    /* ---- Status Bar ---- */
    Rectangle {
        id: statusBar
        visible: !root.showSplash
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 26; z: 10
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#c45e10" }
            GradientStop { position: 1.0; color: "#a04a0a" }
        }
        Text {
            anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
            text: backend.statusText; color: "#e0e0e0"; font.pixelSize: 11; font.family: "Segoe UI"
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
            text: "v1.0"; color: "#f0c090"; font.pixelSize: 11
        }
    }

    /* ---- Card Component ---- */
    component Card: Rectangle {
        property string title: ""
        property alias content: cardContent
        default property alias cardChildren: cardContent.data

        color: cardHover.containsMouse ? "#191919" : "#151515"
        radius: 8
        border.color: cardHover.containsMouse ? "#303030" : "#222222"
        border.width: 1
        Behavior on color { ColorAnimation { duration: 150 } }
        Behavior on border.color { ColorAnimation { duration: 150 } }

        HoverHandler { id: cardHover }

        Column {
            id: cardContent
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Text {
                text: title
                color: "#e87a20"
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 2
                visible: title !== ""
            }
        }
    }

    /* ---- Styled Button Component ---- */
    component StyledButton: Rectangle {
        property string label: ""
        property color baseColor: "#252525"
        property color hoverColor: "#333333"
        property color textColor: "#aaaaaa"
        property bool enabled_: true
        signal clicked()

        width: 90; height: 32; radius: 5
        color: btnMA.containsMouse && enabled_ ? hoverColor : baseColor
        opacity: enabled_ ? 1.0 : 0.4
        Behavior on color { ColorAnimation { duration: 100 } }

        Text { anchors.centerIn: parent; text: label; color: textColor; font.pixelSize: 12 }
        MouseArea { id: btnMA; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: if (parent.enabled_) parent.clicked()
        }
    }

    /* ---- Format Hub (landing screen) ---- */
    FormatHub {
        id: formatHub
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        visible: root.showHub
        opacity: root.showHub ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }

        onFormatSelected: function(formatName) {
            root.showHub = false
        }
    }

    /* ---- Back to Hub button (inside title bar) ---- */

    /* ---- Tab Bar ---- */
    property var sessions: [{ id: 0, label: "Untitled", color: "#e87a20", state: {} }]
    property int activeTabId: 0
    property int nextTabId: 1

    function switchTab(tabId) {
        if (tabId === activeTabId) return
        /* Save current session state */
        for (var i = 0; i < sessions.length; i++) {
            if (sessions[i].id === activeTabId) {
                sessions[i].state = backend.saveSessionState()
                sessions[i].label = backend.inputPath !== "" ?
                    backend.inputPath.split(/[/\\]/).pop().replace(/\.[^.]+$/, '') : "Untitled"
                break
            }
        }
        activeTabId = tabId
        /* Restore selected session state */
        for (var j = 0; j < sessions.length; j++) {
            if (sessions[j].id === tabId) {
                backend.restoreSessionState(sessions[j].state)
                break
            }
        }
        sessionsChanged()
    }

    function addTab() {
        /* Save current state first */
        for (var i = 0; i < sessions.length; i++) {
            if (sessions[i].id === activeTabId) {
                sessions[i].state = backend.saveSessionState()
                sessions[i].label = backend.inputPath !== "" ?
                    backend.inputPath.split(/[/\\]/).pop().replace(/\.[^.]+$/, '') : "Untitled"
                break
            }
        }
        var newId = nextTabId++
        sessions.push({ id: newId, label: "Untitled", color: "#e87a20", state: {} })
        activeTabId = newId
        backend.restoreSessionState({})  /* Reset to empty state */
        root.showHub = true  /* New tab opens to format hub */
        sessionsChanged()
    }

    function closeTab(tabId) {
        if (sessions.length <= 1) return
        var idx = -1
        for (var i = 0; i < sessions.length; i++) {
            if (sessions[i].id === tabId) { idx = i; break }
        }
        if (idx < 0) return
        /* Clean up persisted session on normal close */
        backend.deletePersistedSession(tabId.toString())
        sessions.splice(idx, 1)
        if (activeTabId === tabId) {
            var newIdx = Math.min(idx, sessions.length - 1)
            activeTabId = sessions[newIdx].id
            backend.restoreSessionState(sessions[newIdx].state)
        }
        sessionsChanged()
    }

    Rectangle {
        id: tabBar
        anchors.top: titleBar.bottom
        anchors.left: parent.left; anchors.right: parent.right
        height: !root.showHub ? 32 : 0; visible: !root.showHub
        color: "#161616"; z: 8
        clip: true

        Row {
            anchors.left: parent.left; anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Repeater {
                model: root.sessions.length
                Rectangle {
                    property var sess: root.sessions[index]
                    property bool isActive: sess.id === root.activeTabId
                    property bool isHovered: tabMA.containsMouse
                    width: tabLabel.width + (root.sessions.length > 1 ? 42 : 24); height: 26; radius: 4
                    color: isActive ? "#2a2a2a" : isHovered ? "#222" : "transparent"
                    border.color: isActive ? "#3a3a3a" : "transparent"; border.width: 1

                    Row {
                        anchors.centerIn: parent; spacing: 5
                        Rectangle { width: 7; height: 7; radius: 3.5; color: sess.color; anchors.verticalCenter: parent.verticalCenter }
                        Text { id: tabLabel; text: sess.label; color: isActive ? "#ddd" : "#777"; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideMiddle; maximumLineCount: 1
                        }
                        /* Close button */
                        Text {
                            visible: root.sessions.length > 1
                            text: "\u00d7"; color: tabCloseMA.containsMouse ? "#e87a20" : "#555"; font.pixelSize: 14
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea { id: tabCloseMA; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: root.closeTab(sess.id)
                            }
                        }
                    }
                    MouseArea { id: tabMA; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: root.switchTab(sess.id)
                        z: -1  /* below close button */
                    }
                }
            }

            /* Add tab button */
            Rectangle {
                width: 26; height: 26; radius: 4
                color: addTabMA.containsMouse ? "#2a2a2a" : "transparent"
                Text { anchors.centerIn: parent; text: "+"; color: "#666"; font.pixelSize: 16 }
                MouseArea { id: addTabMA; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: root.addTab()
                }
            }
        }

        /* Bottom border */
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#1a1a1a" }
    }

    /* ---- Denoise Content ---- */
    Flickable {
        visible: !root.showHub
        anchors.top: tabBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        anchors.margins: 14
        contentHeight: mainCol.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: mainCol
            width: parent.width
            spacing: 10

            /* ======== FILES ======== */
            Card {
                width: parent.width; height: 125; title: "FILES"

                Grid {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: parent.top; anchors.topMargin: 26
                    columns: 3; columnSpacing: 8; rowSpacing: 8
                    verticalItemAlignment: Grid.AlignVCenter

                    /* Row 1: Input */
                    Text { text: "Input"; color: "#777"; width: 55; font.pixelSize: 12 }
                    Rectangle {
                        width: mainCol.width - 32 - 55 - 90 - 16; height: 30
                        color: "#1a1a1a"; radius: 4; border.color: inputFocus.activeFocus ? "#e87a20" : "#2a2a2a"; border.width: 1
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                        TextInput { id: inputFocus; anchors.fill: parent; anchors.margins: 6; color: "#d4d4d4"; font.pixelSize: 12; clip: true
                            text: backend.inputPath; onTextChanged: backend.inputPath = text; selectByMouse: true }
                    }
                    StyledButton { label: "Browse"; onClicked: inputDialog.open() }

                    /* Row 2: Output */
                    Text { text: "Output"; color: "#777"; width: 55; font.pixelSize: 12 }
                    Rectangle {
                        width: mainCol.width - 32 - 55 - 90 - 16; height: 30
                        color: "#1a1a1a"; radius: 4; border.color: outputFocus.activeFocus ? "#e87a20" : "#2a2a2a"; border.width: 1
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                        TextInput { id: outputFocus; anchors.fill: parent; anchors.margins: 6; color: "#d4d4d4"; font.pixelSize: 12; clip: true
                            text: backend.outputPath; onTextChanged: backend.outputPath = text; selectByMouse: true }
                    }
                    StyledButton { label: "Browse"; onClicked: outputDialog.open() }
                }
            }

            /* ======== PREVIEW ======== */
            Card {
                width: parent.width; height: 320; title: ""

                /* ---- Preview header row ---- */
                Row {
                    anchors.top: parent.top; anchors.topMargin: 0
                    width: parent.width; spacing: 6
                    Text { text: "PREVIEW"; color: "#e87a20"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 2; anchors.verticalCenter: parent.verticalCenter }
                    Item { Layout.fillWidth: true; width: 10; height: 1 }

                    /* Before / After toggle */
                    Rectangle {
                        visible: backend.hasDenoised
                        width: beforeAfterRow.width + 8; height: 28; radius: 4
                        color: "#2a2a2a"; border.color: "#3a3a3a"; border.width: 1
                        anchors.verticalCenter: parent.verticalCenter
                        Row {
                            id: beforeAfterRow; anchors.centerIn: parent; spacing: 2
                            Rectangle {
                                width: 52; height: 22; radius: 3
                                color: !backend.showDenoised ? "#e87a20" : "transparent"
                                Text { anchors.centerIn: parent; text: "Before"; color: !backend.showDenoised ? "#fff" : "#888"; font.pixelSize: 10; font.weight: Font.Medium }
                                MouseArea { anchors.fill: parent; onClicked: { backend.showDenoised = false } }
                            }
                            Rectangle {
                                width: 46; height: 22; radius: 3
                                color: backend.showDenoised ? "#e87a20" : "transparent"
                                Text { anchors.centerIn: parent; text: "After"; color: backend.showDenoised ? "#fff" : "#888"; font.pixelSize: 10; font.weight: Font.Medium }
                                MouseArea { anchors.fill: parent; onClicked: { backend.showDenoised = true } }
                            }
                        }
                    }

                    StyledButton {
                        label: backend.isPreviewLoading ? "Denoising..." : "Preview Denoise"
                        width: 120; height: 28
                        enabled: !backend.isPreviewLoading && backend.inputPath !== ""
                        onClicked: backend.generateDenoisedPreview()
                    }
                    StyledButton { label: "Load Frame"; width: 80; height: 28; onClicked: backend.loadPreview() }
                }

                /* ---- Frame scrubber ---- */
                Row {
                    visible: backend.frameCount > 1
                    anchors.top: parent.top; anchors.topMargin: 30
                    width: parent.width; spacing: 8
                    Text { text: "Frame:"; color: "#aaa"; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                    Slider {
                        id: frameScrubber
                        width: parent.width - 120; height: 20
                        from: 0; to: Math.max(0, backend.frameCount - 1)
                        value: backend.previewFrameIndex; stepSize: 1
                        onMoved: { backend.previewFrameIndex = Math.round(value) }
                    }
                    Text { text: Math.round(frameScrubber.value) + " / " + backend.frameCount; color: "#888"; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                }

                Rectangle {
                    anchors.top: parent.top; anchors.topMargin: backend.frameCount > 1 ? 56 : 36
                    width: parent.width; height: parent.height - (backend.frameCount > 1 ? 68 : 48)
                    color: "#0a0a0a"; radius: 6; clip: true
                    border.color: "#1a1a1a"; border.width: 1

                    Image {
                        id: previewImg
                        anchors.fill: parent; anchors.margins: 2
                        fillMode: Image.PreserveAspectFit
                        source: ""; cache: false
                    }

                    Text {
                        anchors.centerIn: parent
                        text: previewImg.source == "" ? "Drag to select noise patch" : ""
                        color: "#333"; font.pixelSize: 13
                    }

                    MouseArea {
                        anchors.fill: parent
                        property point origin; property bool isDragging: false
                        onPressed: function(mouse) {
                            origin = Qt.point(mouse.x, mouse.y)
                            rubberBand.x = mouse.x; rubberBand.y = mouse.y
                            rubberBand.width = 0; rubberBand.height = 0
                            rubberBand.visible = true; isDragging = true
                        }
                        onPositionChanged: function(mouse) {
                            if (!isDragging) return
                            rubberBand.x = Math.min(origin.x, mouse.x)
                            rubberBand.y = Math.min(origin.y, mouse.y)
                            rubberBand.width = Math.abs(mouse.x - origin.x)
                            rubberBand.height = Math.abs(mouse.y - origin.y)
                        }
                        onReleased: function(mouse) {
                            isDragging = false
                            if (rubberBand.width < 10 || rubberBand.height < 10) { rubberBand.visible = false; return }
                            if (previewImg.sourceSize.width <= 0) { rubberBand.visible = false; return }
                            var pw = previewImg.paintedWidth > 0 ? previewImg.paintedWidth : previewImg.width
                            var ph = previewImg.paintedHeight > 0 ? previewImg.paintedHeight : previewImg.height
                            var sx = previewImg.sourceSize.width / pw, sy = previewImg.sourceSize.height / ph
                            var ox = (previewImg.width - pw) / 2, oy = (previewImg.height - ph) / 2
                            backend.profileNoise(
                                Math.round((rubberBand.x - ox) * sx) * 2, Math.round((rubberBand.y - oy) * sy) * 2,
                                Math.round(rubberBand.width * sx) * 2, Math.round(rubberBand.height * sy) * 2)
                            rubberBand.visible = false
                        }
                    }

                    Rectangle {
                        id: rubberBand; visible: false; radius: 2
                        color: Qt.rgba(0.34, 0.61, 0.84, 0.12)
                        border.color: "#e87a20"; border.width: 1.5
                    }
                }
            }

            /* ======== NOISE PROFILE + SETTINGS ======== */
            Row {
                width: parent.width; spacing: 10

                Card {
                    width: (parent.width - 10) / 2; height: 240; title: "NOISE PROFILE"

                    Grid {
                        anchors.top: parent.top; anchors.topMargin: 32
                        columns: 2; columnSpacing: 24; rowSpacing: 8

                        Text { text: "Black Level"; color: "#666"; font.pixelSize: 12 }
                        Text { text: backend.noiseProfileValid ? backend.noiseBlackLevel.toFixed(1) : "--"; color: "#f0a050"; font.pixelSize: 13; font.family: "Cascadia Mono" }
                        Text { text: "Shot Gain"; color: "#666"; font.pixelSize: 12 }
                        Text { text: backend.noiseProfileValid ? backend.noiseShotGain.toFixed(3) : "--"; color: "#f0a050"; font.pixelSize: 13; font.family: "Cascadia Mono" }
                        Text { text: "Read Noise"; color: "#666"; font.pixelSize: 12 }
                        Text { text: backend.noiseProfileValid ? backend.noiseReadNoise.toFixed(1) : "--"; color: "#f0a050"; font.pixelSize: 13; font.family: "Cascadia Mono" }
                        Text { text: "Sigma"; color: "#666"; font.pixelSize: 12 }
                        Text { text: backend.noiseProfileValid ? backend.noiseSigma.toFixed(1) : "--"; color: "#f0a050"; font.pixelSize: 13; font.family: "Cascadia Mono" }
                    }
                }

                Card {
                    width: (parent.width - 10) / 2; height: 240; title: "SETTINGS"

                    Column {
                        anchors.top: parent.top; anchors.topMargin: 30
                        anchors.left: parent.left; anchors.leftMargin: 12
                        anchors.right: parent.right; anchors.rightMargin: 12
                        spacing: 8

                        /* Preset selector */
                        Row {
                            spacing: 4; width: parent.width
                            Text { text: "Preset"; color: "#666"; font.pixelSize: 11; width: 45; anchors.verticalCenter: parent.verticalCenter }
                            Repeater {
                                model: ["Light", "Standard", "Strong", "Custom"]
                                Rectangle {
                                    property bool isSelected: backend.preset === modelData
                                    property bool isHovered: pma.containsMouse
                                    width: (parent.width - 57) / 4; height: 24; radius: 4
                                    color: isSelected ? "#e87a20" : isHovered ? "#333" : "#222"
                                    border.color: isSelected ? "#e87a20" : "#3a3a3a"; border.width: 1
                                    Text { anchors.centerIn: parent; text: modelData; color: parent.isSelected ? "#fff" : "#999"; font.pixelSize: 10; font.weight: Font.Medium }
                                    MouseArea { id: pma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: backend.preset = modelData
                                    }
                                }
                            }
                        }

                        /* Strength + Window (read-only unless Custom) */
                        Row {
                            spacing: 12; width: parent.width
                            Text { text: "Strength"; color: "#666"; font.pixelSize: 11; width: 55; anchors.verticalCenter: parent.verticalCenter }
                            SpinBox {
                                id: strengthSpin; from: 1; to: 50; stepSize: 1; width: 100; height: 26
                                value: Math.round(backend.strength * 10)
                                editable: backend.preset === "Custom"
                                textFromValue: function(v) { return (v / 10.0).toFixed(1) }
                                onValueModified: { backend.strength = value / 10.0; backend.preset = "Custom" }
                            }
                            Text { text: "Window"; color: "#666"; font.pixelSize: 11; width: 45; anchors.verticalCenter: parent.verticalCenter }
                            SpinBox {
                                id: windowSpin; from: 3; to: 31; stepSize: 2; width: 80; height: 26
                                value: backend.windowSize
                                editable: backend.preset === "Custom"
                                onValueModified: { backend.windowSize = value; backend.preset = "Custom" }
                            }
                        }

                        /* CNN toggle */
                        Row {
                            spacing: 8; width: parent.width
                            Text { text: "CNN Post-Filter"; color: "#666"; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                            Switch {
                                checked: backend.useCNN
                                onToggled: backend.useCNN = checked
                                scale: 0.7
                            }
                            Text { text: backend.useCNN ? "ON (slower, better quality)" : "OFF (faster)"; color: "#555"; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                        }

                        /* Frame range */
                        Row {
                            spacing: 8; width: parent.width
                            Text { text: "Frames"; color: "#666"; font.pixelSize: 11; width: 45; anchors.verticalCenter: parent.verticalCenter }
                            SpinBox { from: 0; to: Math.max(0, backend.frameCount - 1); value: backend.startFrame; width: 80; height: 26
                                onValueModified: backend.startFrame = value
                            }
                            Text { text: "to"; color: "#555"; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                            SpinBox { from: 0; to: backend.frameCount; value: backend.endFrame; width: 80; height: 26
                                onValueModified: backend.endFrame = value
                            }
                            Text { text: backend.endFrame === 0 ? "(all)" : "(" + (backend.endFrame - backend.startFrame) + " frames)"; color: "#555"; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                        }

                        /* Output format */
                        Row {
                            spacing: 8; width: parent.width
                            Text { text: "Output"; color: "#666"; font.pixelSize: 11; width: 45; anchors.verticalCenter: parent.verticalCenter }
                            ComboBox {
                                model: ["Auto (match input)", "ProRes RAW", "DNG Sequence", "BRAW", "EXR Sequence"]
                                currentIndex: backend.outputFormat
                                onActivated: backend.outputFormat = currentIndex
                                width: 160; height: 26
                            }
                        }
                    }
                }
            }

            /* ======== ACTION BAR ======== */
            Card {
                width: parent.width; height: 68; title: ""

                Row {
                    anchors.fill: parent; spacing: 12
                    anchors.topMargin: 4

                    /* Start */
                    Rectangle {
                        width: 150; height: 42; radius: 6
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: startMA.containsMouse ? "#f09030" : "#c45e10" }
                            GradientStop { position: 1.0; color: startMA.containsMouse ? "#c45e10" : "#8a3e08" }
                        }
                        opacity: backend.processing ? 0.4 : 1.0
                        Behavior on opacity { NumberAnimation { duration: 200 } }
                        Text { anchors.centerIn: parent; text: "Start Denoise"; color: "#fff"; font.pixelSize: 14; font.weight: Font.Bold }
                        MouseArea { id: startMA; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: if (!backend.processing) backend.startDenoise()
                        }
                    }

                    /* Cancel */
                    Rectangle {
                        width: 90; height: 42; radius: 6
                        color: cancelMA.containsMouse && backend.processing ? "#8b2d2d" : "#2a1515"
                        opacity: backend.processing ? 1.0 : 0.3
                        Behavior on color { ColorAnimation { duration: 100 } }
                        Text { anchors.centerIn: parent; text: "Cancel"; color: "#e08080"; font.pixelSize: 13 }
                        MouseArea { id: cancelMA; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: if (backend.processing) backend.cancelDenoise()
                        }
                    }

                    /* Progress */
                    Rectangle {
                        width: parent.width - 270; height: 42
                        color: "#0d0d0d"; radius: 5
                        border.color: "#1a1a1a"; border.width: 1

                        Rectangle {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            anchors.margins: 2
                            width: Math.max(0, (parent.width - 4) * backend.progressPercent / 100)
                            radius: 4
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: "#c45e10" }
                                GradientStop { position: 1.0; color: "#f09030" }
                            }
                            Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
                        }
                        Row {
                            anchors.centerIn: parent; spacing: 16
                            Text {
                                text: backend.processing ? backend.progressPercent + "%" : ""
                                color: "#d4d4d4"; font.pixelSize: 13; font.family: "Cascadia Mono"
                            }
                            Text {
                                visible: backend.processing && backend.fpsValue > 0.01
                                text: backend.fpsValue.toFixed(1) + " fps"
                                color: "#888"; font.pixelSize: 11; font.family: "Cascadia Mono"
                            }
                            Text {
                                visible: backend.processing && backend.etaText !== ""
                                text: backend.etaText
                                color: "#888"; font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            /* ======== BATCH QUEUE ======== */
            Rectangle {
                id: queueCard
                visible: backend.queueCount > 0 || backend.isQueueRunning
                width: parent.width
                height: Math.min(70 + backend.queueCount * 50, 320)
                color: "#151515"; radius: 8
                border.color: "#222222"; border.width: 1

                Text {
                    x: 16; y: 12
                    text: "QUEUE (" + backend.queueCount + ")"
                    color: "#e87a20"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 2
                }

                Row {
                    x: 16; y: 36; spacing: 8
                    StyledButton {
                        label: "Cancel All"; width: 80; height: 24
                        visible: backend.isQueueRunning
                        onClicked: backend.cancelQueue()
                    }
                    StyledButton {
                        label: "Clear"; width: 50; height: 24
                        visible: !backend.isQueueRunning
                        onClicked: backend.clearQueue()
                    }
                }

                Column {
                    x: 16; y: 64
                    width: parent.width - 32
                    spacing: 4

                    Repeater {
                        model: backend.queueModel
                        Rectangle {
                            width: parent.width; height: 42; radius: 4
                            color: "#161616"; border.color: "#1a1a1a"; border.width: 1

                            Row {
                                anchors.fill: parent; anchors.margins: 8; spacing: 8

                                Text {
                                    text: modelData.status === "done" ? "\u2713" :
                                          modelData.status === "failed" ? "\u2717" :
                                          modelData.status === "processing" ? "\u25B6" : "\u25CB"
                                    color: modelData.status === "done" ? "#4caf50" :
                                           modelData.status === "failed" ? "#f44336" :
                                           modelData.status === "processing" ? "#e87a20" : "#555"
                                    font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter
                                }

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 80
                                    Text { text: modelData.filename; color: "#ccc"; font.pixelSize: 11; elide: Text.ElideMiddle; width: parent.width }
                                    Text {
                                        visible: modelData.status === "processing" || modelData.status === "done" || modelData.status === "failed"
                                        text: modelData.message
                                        color: modelData.status === "failed" ? "#f44336" : "#666"; font.pixelSize: 9
                                    }
                                    Rectangle {
                                        visible: modelData.status === "processing"
                                        width: parent.width; height: 2; radius: 1; color: "#2a2a2a"
                                        Rectangle {
                                            width: parent.width * modelData.progressPercent / 100; height: 2; radius: 1; color: "#e87a20"
                                        }
                                    }
                                }

                                Text {
                                    visible: modelData.status !== "processing"
                                    text: "\u00d7"; color: qrMA.containsMouse ? "#e87a20" : "#444"; font.pixelSize: 16
                                    anchors.verticalCenter: parent.verticalCenter
                                    MouseArea { id: qrMA; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: backend.removeFromQueue(index)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            /* ======== WATCH FOLDER ======== */
            Rectangle {
                width: parent.width; height: 70
                color: "#151515"; radius: 8
                border.color: "#222222"; border.width: 1

                Text {
                    x: 16; y: 12
                    text: "WATCH FOLDER"
                    color: "#e87a20"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 2
                }

                Row {
                    x: 16; y: 36; spacing: 8

                    StyledButton {
                        label: backend.isWatching ? "Stop" : "Choose Folder..."
                        width: backend.isWatching ? 60 : 120; height: 24
                        onClicked: {
                            if (backend.isWatching) backend.stopWatchFolder()
                            else watchFolderDialog.open()
                        }
                    }

                    /* Green dot + folder name when watching */
                    Rectangle {
                        visible: backend.isWatching
                        width: watchLabel.width + 22; height: 24; radius: 4
                        color: "#0a1a0a"; border.color: "#1a3a1a"; border.width: 1
                        Row {
                            anchors.centerIn: parent; spacing: 6
                            Rectangle { width: 6; height: 6; radius: 3; color: "#4caf50"; anchors.verticalCenter: parent.verticalCenter }
                            Text { id: watchLabel; text: backend.watchFolderPath.split(/[/\\]/).pop(); color: "#8c8"; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }

                    Text {
                        visible: !backend.isWatching
                        text: "Auto-queue new files dropped in a folder"
                        color: "#555"; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }

    /* ---- File Dialogs ---- */
    FileDialog {
        id: inputDialog; title: "Select Input File(s)"
        nameFilters: ["Video files (*.mov *.MOV *.braw *.dng *.ari *.crm *.r3d *.nraw *.mxf)"]
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            var files = selectedFiles
            if (files.length === 0) return
            /* First file goes to the main input */
            backend.inputPath = files[0]
            /* Additional files get added to the queue */
            for (var i = 1; i < files.length; i++) {
                var f = files[i].toString()
                if (f.startsWith("file:///")) f = f.substring(8)
                var out = f.replace(/(\.[^.]+)$/, "_denoised$1")
                backend.addToQueue(f, out)
            }
            if (files.length > 1) {
                /* Also add the first file to queue */
                backend.addToQueue(backend.inputPath, backend.outputPath)
            }
        }
    }
    FileDialog {
        id: outputDialog; title: "Select Output"
        nameFilters: ["MOV (*.mov)", "DNG (*.dng)", "EXR (*.exr)"]
        fileMode: FileDialog.SaveFile
        onAccepted: backend.outputPath = selectedFile
    }

    FolderDialog {
        id: watchFolderDialog; title: "Select Watch Folder"
        onAccepted: backend.startWatchFolder(selectedFolder.toString().replace("file:///", ""))
    }

    /* ---- Preview reload ---- */
    Connections {
        target: backend
        function onPreviewChanged() { previewImg.source = ""; previewImg.source = "image://preview/frame?" + Date.now() }
        function onPreviewModeChanged() { previewImg.source = ""; previewImg.source = "image://preview/frame?" + Date.now() }
        function onDenoisedPreviewReady() { previewImg.source = ""; previewImg.source = "image://preview/frame?" + Date.now() }
    }

    /* ---- Onboarding Popup (first launch) ---- */
    OnboardingPopup {
        id: onboardingPopup
        onDismissed: {
            backend.markOnboardingDone()
            trainingConsentPopup.visible = true
        }
    }

    /* ---- Training Consent Popup (shown after onboarding) ---- */
    TrainingConsentPopup {
        id: trainingConsentPopup
        onAccepted: backend.trainingConsent = true
        onDeclined: backend.trainingConsent = false
    }

    /* Show onboarding on first launch */
    Component.onCompleted: {
        console.log("BF: isFirstLaunch =", backend.isFirstLaunch)
        if (backend.isFirstLaunch) {
            console.log("BF: showing onboarding")
            onboardingPopup.visible = true
        }

        /* Check for crash-recovered sessions */
        var saved = backend.loadPersistedSessions()
        if (saved.length > 0) {
            root.recoveredSessions = saved
            root.showRecoveryDialog = true
        }
    }

    /* Auto-save session state every 5 seconds */
    Timer {
        interval: 5000; running: true; repeat: true
        onTriggered: {
            var state = backend.saveSessionState()
            state["sessionId"] = root.activeTabId.toString()
            state["label"] = root.sessions.length > 0 ? root.sessions[0].label : "Untitled"
            state["showHub"] = root.showHub
            state["timestamp"] = new Date().toISOString()
            backend.persistSession(root.activeTabId.toString(), state)
        }
    }

    /* Recovery dialog */
    Dialog {
        id: recoveryDialog
        visible: root.showRecoveryDialog && !root.showSplash
        anchors.centerIn: parent
        width: 400; height: 180
        title: "Recover Sessions?"
        modal: true
        standardButtons: Dialog.NoButton

        Column {
            anchors.fill: parent; spacing: 12
            Text {
                text: root.recoveredSessions.length + " session(s) from a previous run were found.\nWould you like to restore them?"
                color: "#d4d4d4"; font.pixelSize: 13; wrapMode: Text.Wrap; width: parent.width
            }
            Row {
                spacing: 12; anchors.horizontalCenter: parent.horizontalCenter
                StyledButton {
                    label: "Recover"; width: 100; height: 32
                    onClicked: {
                        for (var i = 0; i < root.recoveredSessions.length; i++) {
                            var s = root.recoveredSessions[i]
                            if (i === 0) {
                                backend.restoreSessionState(s)
                                if (s.showHub !== undefined) root.showHub = s.showHub
                            } else {
                                root.addTab()
                                backend.restoreSessionState(s)
                            }
                        }
                        root.showRecoveryDialog = false
                    }
                }
                StyledButton {
                    label: "Discard"; width: 100; height: 32
                    onClicked: {
                        backend.deleteAllPersistedSessions()
                        root.showRecoveryDialog = false
                    }
                }
            }
        }
    }

    /* ---- Splash Screen ---- */
    /* ---- Keyboard Shortcuts ---- */
    Shortcut { sequence: "Ctrl+T"; onActivated: root.addTab() }
    Shortcut { sequence: "Ctrl+W"; onActivated: { if (root.sessions.length > 1) root.closeTab(root.activeTabId) } }
    Shortcut { sequence: "Ctrl+O"; onActivated: inputDialog.open() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: { if (!backend.processing) backend.startDenoise() } }
    Shortcut { sequence: "Escape"; onActivated: { if (backend.processing) backend.cancelDenoise() } }
    Shortcut { sequence: "Ctrl+H"; onActivated: root.showHub = true }
    Shortcut { sequence: "Space"; onActivated: { if (backend.hasDenoised) backend.showDenoised = !backend.showDenoised } }

    SplashScreen {
        id: splashScreen
        visible: root.showSplash
        onFinished: root.showSplash = false
    }
}
