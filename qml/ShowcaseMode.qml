import QtQuick
import QtQuick.Controls

/* Showcase/demo mode — scripted walkthrough for marketing.
 * Simplified version of Mac ShowcaseController.swift.
 * Drives: carousel tour → file load → preset cycling → preview → denoise */
Item {
    id: showcase
    anchors.fill: parent
    visible: active
    z: 50

    property bool active: false
    property string demoFilePath: ""

    signal completed()

    property int step: 0
    property string stepLabel: ""

    /* Overlay with step label */
    Rectangle {
        anchors.bottom: parent.bottom; anchors.bottomMargin: 60
        anchors.horizontalCenter: parent.horizontalCenter
        width: labelText.width + 32; height: 36; radius: 18
        color: Qt.rgba(0, 0, 0, 0.7)
        visible: stepLabel !== ""

        Text {
            id: labelText; anchors.centerIn: parent
            text: showcase.stepLabel; color: "#e87a20"
            font.pixelSize: 13; font.weight: Font.Medium
        }
    }

    /* Step sequencer */
    Timer {
        id: stepTimer; repeat: false
        onTriggered: advanceStep()
    }

    function start(filePath) {
        demoFilePath = filePath
        active = true
        step = 0
        advanceStep()
    }

    function stop() {
        active = false
        stepTimer.stop()
        stepLabel = ""
        step = 0
    }

    function advanceStep() {
        step++
        switch (step) {
        case 1:
            stepLabel = "Welcome to BayerFlow"
            root.showHub = true
            stepTimer.interval = 2000; stepTimer.start()
            break
        case 2:
            stepLabel = "Browse camera formats..."
            /* Carousel auto-rotates via FormatHub's built-in animation */
            stepTimer.interval = 3000; stepTimer.start()
            break
        case 3:
            stepLabel = "Select ProRes RAW"
            root.showHub = false
            stepTimer.interval = 1500; stepTimer.start()
            break
        case 4:
            stepLabel = "Loading footage..."
            if (demoFilePath !== "") backend.inputPath = demoFilePath
            stepTimer.interval = 2000; stepTimer.start()
            break
        case 5:
            stepLabel = "Loading preview frame..."
            backend.loadPreview()
            stepTimer.interval = 3000; stepTimer.start()
            break
        case 6:
            stepLabel = "Trying Light preset..."
            backend.preset = "Light"
            stepTimer.interval = 1500; stepTimer.start()
            break
        case 7:
            stepLabel = "Trying Standard preset..."
            backend.preset = "Standard"
            stepTimer.interval = 1500; stepTimer.start()
            break
        case 8:
            stepLabel = "Selecting Strong preset"
            backend.preset = "Strong"
            stepTimer.interval = 1500; stepTimer.start()
            break
        case 9:
            stepLabel = "Generating denoised preview..."
            backend.generateDenoisedPreview()
            stepTimer.interval = 8000; stepTimer.start()
            break
        case 10:
            stepLabel = "Compare: Before vs After"
            /* Switch to compare mode */
            stepTimer.interval = 4000; stepTimer.start()
            break
        case 11:
            stepLabel = "Demo complete!"
            stepTimer.interval = 2000; stepTimer.start()
            break
        default:
            stop()
            completed()
            break
        }
    }
}
