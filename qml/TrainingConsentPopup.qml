import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/* Opt-in training data consent dialog — shown once after onboarding */
Rectangle {
    id: consent
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.85)
    z: 500
    visible: false
    opacity: visible ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 300 } }

    signal accepted()
    signal declined()

    MouseArea { anchors.fill: parent }

    Rectangle {
        anchors.centerIn: parent
        width: 500; height: consentCol.height + 64
        radius: 16
        color: "#1a1a1a"
        border.color: "#2a2a2a"; border.width: 1

        Column {
            id: consentCol
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 32
            spacing: 18

            /* Icon */
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "\uD83E\uDDE0"
                font.pixelSize: 42
            }

            /* Title */
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Help Improve BayerFlow Denoising"
                color: "#e0e0e0"
                font.pixelSize: 18
                font.weight: Font.DemiBold
                font.family: "Segoe UI"
            }

            /* Description */
            Text {
                width: parent.width
                text: "BayerFlow can collect small anonymous pixel patches during processing to help train better denoising models."
                color: "#888"
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            /* Divider */
            Rectangle { width: parent.width; height: 1; color: "#2a2a2a" }

            /* What we collect */
            Row {
                spacing: 10; width: parent.width
                Text { text: "\u2705"; font.pixelSize: 14 }
                Column {
                    spacing: 3; width: parent.width - 30
                    Text { text: "What we collect"; color: "#e0e0e0"; font.pixelSize: 13; font.weight: Font.Bold }
                    Text {
                        text: "Small 256\u00D7256 pixel patches (noisy + denoised pairs), noise level, and ISO. No full frames, no filenames, no personal information."
                        color: "#888"; font.pixelSize: 11; wrapMode: Text.Wrap; width: parent.width
                    }
                }
            }

            /* What we don't collect */
            Row {
                spacing: 10; width: parent.width
                Text { text: "\u274C"; font.pixelSize: 14 }
                Column {
                    spacing: 3; width: parent.width - 30
                    Text { text: "What we don't collect"; color: "#e07070"; font.pixelSize: 13; font.weight: Font.Bold }
                    Text {
                        text: "No full images, no file paths, no GPS data, no camera model name (hashed only), no personally identifiable information."
                        color: "#888"; font.pixelSize: 11; wrapMode: Text.Wrap; width: parent.width
                    }
                }
            }

            /* How it works */
            Row {
                spacing: 10; width: parent.width
                Text { text: "\u2B06"; font.pixelSize: 14 }
                Column {
                    spacing: 3; width: parent.width - 30
                    Text { text: "How it works"; color: "#e0e0e0"; font.pixelSize: 13; font.weight: Font.Bold }
                    Text {
                        text: "Patches are saved locally and uploaded in the background when you're online. You can disable this at any time in Settings."
                        color: "#888"; font.pixelSize: 11; wrapMode: Text.Wrap; width: parent.width
                    }
                }
            }

            /* Buttons */
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 16

                /* No Thanks */
                Rectangle {
                    width: 120; height: 38; radius: 6
                    color: noMA.containsMouse ? "#333" : "#252525"
                    border.color: "#444"; border.width: 1
                    Behavior on color { ColorAnimation { duration: 100 } }

                    Text { anchors.centerIn: parent; text: "No Thanks"; color: "#aaa"; font.pixelSize: 13 }
                    MouseArea {
                        id: noMA; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { consent.visible = false; consent.declined() }
                    }
                }

                /* Contribute */
                Rectangle {
                    width: 140; height: 38; radius: 6
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: yesMA.containsMouse ? "#f09030" : "#e87a20" }
                        GradientStop { position: 1.0; color: yesMA.containsMouse ? "#c45e10" : "#a05510" }
                    }

                    Text { anchors.centerIn: parent; text: "Contribute"; color: "#fff"; font.pixelSize: 13; font.weight: Font.Bold }
                    MouseArea {
                        id: yesMA; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { consent.visible = false; consent.accepted() }
                    }
                }
            }
        }
    }
}
