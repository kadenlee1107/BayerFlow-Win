import QtQuick
import QtQuick.Controls

/* BayerFlow splash screen — shown on app launch.
 * Animated progress bar with status text, fades out when done. */
Item {
    id: splash
    anchors.fill: parent
    z: 100

    signal finished()

    property real progress: 0
    property string statusText: "Initializing..."

    /* Solid opaque background */
    Rectangle {
        anchors.fill: parent
        color: "#0a0a0a"
    }

    /* Center content */
    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -30
        spacing: 8

        /* Logo */
        Image {
            source: "file:///C:/Users/kaden/BayerFlow-Win/qml/logo.png"
            width: 160; height: 160
            fillMode: Image.PreserveAspectFit
            smooth: true; mipmap: true
            anchors.horizontalCenter: parent.horizontalCenter
        }

        /* App name */
        Text {
            text: "BayerFlow"
            color: "#e0e0e0"
            font.pixelSize: 32
            font.family: "Segoe UI"
            font.weight: Font.Light
            font.letterSpacing: 4
            anchors.horizontalCenter: parent.horizontalCenter
        }

        /* Subtitle */
        Text {
            text: "ProRes RAW Denoiser"
            color: Qt.rgba(1, 1, 1, 0.4)
            font.pixelSize: 13
            font.weight: Font.Medium
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    /* Progress bar + status at bottom */
    Column {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 50
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 10

        /* Progress bar track */
        Rectangle {
            width: 200; height: 2; radius: 1
            color: "#1a1a1a"
            anchors.horizontalCenter: parent.horizontalCenter

            /* Progress fill */
            Rectangle {
                width: parent.width * splash.progress; height: parent.height; radius: 1.5
                color: "#a05a15"
                Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.InOutQuad } }
            }
        }

        /* Status text */
        Text {
            text: splash.statusText
            color: Qt.rgba(1, 1, 1, 0.35)
            font.pixelSize: 11
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    /* Version at very bottom */
    Text {
        anchors.bottom: parent.bottom; anchors.bottomMargin: 16
        anchors.horizontalCenter: parent.horizontalCenter
        text: "v1.0.0"
        color: Qt.rgba(1, 1, 1, 0.15)
        font.pixelSize: 10
    }

    /* No fade — instant remove to avoid color blending artifacts */

    /* Startup sequence */
    Timer {
        id: step1; interval: 100; running: true; repeat: false
        onTriggered: { splash.statusText = "Detecting GPU..."; splash.progress = 0.2; step2.start() }
    }
    Timer {
        id: step2; interval: 600; repeat: false
        onTriggered: { splash.statusText = "Loading RAFT optical flow..."; splash.progress = 0.45; step3.start() }
    }
    Timer {
        id: step3; interval: 700; repeat: false
        onTriggered: { splash.statusText = "Loading DnCNN post-filter..."; splash.progress = 0.7; step4.start() }
    }
    Timer {
        id: step4; interval: 500; repeat: false
        onTriggered: { splash.statusText = "Preparing workspace..."; splash.progress = 0.9; step5.start() }
    }
    Timer {
        id: step5; interval: 400; repeat: false
        onTriggered: { splash.statusText = "Ready"; splash.progress = 1.0; fadeOut.start() }
    }
    Timer {
        id: fadeOut; interval: 500; repeat: false
        onTriggered: { splash.visible = false; splash.finished() }
    }
}
