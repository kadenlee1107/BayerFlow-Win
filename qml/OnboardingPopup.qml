import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/* First-launch welcome overlay — 3 steps matching Mac's OnboardingView */
Rectangle {
    id: onboarding
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.85)
    z: 500
    visible: false
    opacity: visible ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 300 } }

    signal dismissed()

    /* Block clicks from passing through */
    MouseArea { anchors.fill: parent }

    /* Card */
    Rectangle {
        anchors.centerIn: parent
        width: 460; height: contentCol.height + 64
        radius: 16
        color: "#1a1a1a"
        border.color: "#2a2a2a"; border.width: 1

        Column {
            id: contentCol
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 32
            spacing: 20

            /* Logo + Title */
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 8

                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    source: "file:///C:/Users/kaden/BayerFlow-Win/qml/logo.png"
                    width: 48; height: 48
                    fillMode: Image.PreserveAspectFit
                    smooth: true; mipmap: true
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Welcome to BayerFlow"
                    color: "#e0e0e0"
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    font.family: "Segoe UI"
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Professional RAW video denoising"
                    color: "#666"
                    font.pixelSize: 13
                    font.family: "Segoe UI"
                }
            }

            /* Steps */
            Column {
                width: parent.width
                spacing: 16

                /* Step 1 */
                Row {
                    spacing: 14; width: parent.width
                    Rectangle {
                        width: 36; height: 36; radius: 18
                        color: "#e87a20"; opacity: 0.15
                        Text { anchors.centerIn: parent; text: "\uD83C\uDFAC"; font.pixelSize: 18 }
                    }
                    Column {
                        spacing: 3; width: parent.width - 50
                        Text { text: "Load Your Footage"; color: "#e0e0e0"; font.pixelSize: 14; font.weight: Font.Bold }
                        Text {
                            text: "Drag & drop a RAW video file or click a format orb to browse. Supports ProRes RAW, BRAW, ARRIRAW, CinemaDNG, Canon CRM, and more."
                            color: "#888"; font.pixelSize: 12; wrapMode: Text.Wrap; width: parent.width
                        }
                    }
                }

                /* Step 2 */
                Row {
                    spacing: 14; width: parent.width
                    Rectangle {
                        width: 36; height: 36; radius: 18
                        color: "#e87a20"; opacity: 0.15
                        Text { anchors.centerIn: parent; text: "\u2699"; font.pixelSize: 18 }
                    }
                    Column {
                        spacing: 3; width: parent.width - 50
                        Text { text: "Adjust Settings"; color: "#e0e0e0"; font.pixelSize: 14; font.weight: Font.Bold }
                        Text {
                            text: "Tune temporal strength, window size, and spatial filtering. Drag a rectangle on the preview to profile sensor noise for optimal results."
                            color: "#888"; font.pixelSize: 12; wrapMode: Text.Wrap; width: parent.width
                        }
                    }
                }

                /* Step 3 */
                Row {
                    spacing: 14; width: parent.width
                    Rectangle {
                        width: 36; height: 36; radius: 18
                        color: "#e87a20"; opacity: 0.15
                        Text { anchors.centerIn: parent; text: "\u26A1"; font.pixelSize: 18 }
                    }
                    Column {
                        spacing: 3; width: parent.width - 50
                        Text { text: "Denoise"; color: "#e0e0e0"; font.pixelSize: 14; font.weight: Font.Bold }
                        Text {
                            text: "Hit Start to process. BayerFlow uses CUDA-accelerated temporal filtering with RAFT optical flow — your output keeps the original RAW format."
                            color: "#888"; font.pixelSize: 12; wrapMode: Text.Wrap; width: parent.width
                        }
                    }
                }
            }

            /* Get Started button */
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 160; height: 42; radius: 8
                gradient: Gradient {
                    GradientStop { position: 0.0; color: startHover.containsMouse ? "#f09030" : "#e87a20" }
                    GradientStop { position: 1.0; color: startHover.containsMouse ? "#c45e10" : "#a05510" }
                }

                Text {
                    anchors.centerIn: parent
                    text: "Get Started"
                    color: "#ffffff"
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }

                MouseArea {
                    id: startHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        onboarding.visible = false
                        onboarding.dismissed()
                    }
                }
            }
        }
    }
}
