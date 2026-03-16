import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

/* Settings panel — matches Mac SettingsView.swift */
Dialog {
    id: settingsDialog
    title: "Settings"
    width: 480; height: 520
    modal: true
    standardButtons: Dialog.Close

    background: Rectangle { color: "#1a1a1a"; radius: 10; border.color: "#333"; border.width: 1 }

    Flickable {
        anchors.fill: parent
        contentHeight: settingsCol.height
        clip: true

        Column {
            id: settingsCol
            width: parent.width - 20
            x: 10; spacing: 16

            /* ---- Output Section ---- */
            Text { text: "OUTPUT"; color: "#e87a20"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 2 }

            Row {
                spacing: 8; width: parent.width
                Text { text: "Default output directory"; color: "#aaa"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                Item { width: parent.width - 300; height: 1 }
                StyledButton { label: "Choose..."; width: 80; height: 24; onClicked: outputDirDialog.open() }
                StyledButton { label: "Reset"; width: 50; height: 24; onClicked: backend.defaultOutputDir = "" }
            }
            Text { text: backend.defaultOutputDir !== "" ? backend.defaultOutputDir : "(Default: same as input)"; color: "#666"; font.pixelSize: 10 }

            Row {
                spacing: 8
                Switch { checked: backend.autoRevealOutput; onToggled: backend.autoRevealOutput = checked; scale: 0.6 }
                Text { text: "Auto-reveal output in Explorer"; color: "#aaa"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
            }

            Rectangle { width: parent.width; height: 1; color: "#2a2a2a" }

            /* ---- Notifications Section ---- */
            Text { text: "NOTIFICATIONS"; color: "#e87a20"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 2 }

            Row {
                spacing: 8
                Switch { checked: backend.playSoundOnComplete; onToggled: backend.playSoundOnComplete = checked; scale: 0.6 }
                Text { text: "Play sound on completion"; color: "#aaa"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
            }
            Row {
                spacing: 8
                Switch { checked: backend.showNotification; onToggled: backend.showNotification = checked; scale: 0.6 }
                Text { text: "Show system notification"; color: "#aaa"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
            }

            Rectangle { width: parent.width; height: 1; color: "#2a2a2a" }

            /* ---- Defaults Section ---- */
            Text { text: "DEFAULTS"; color: "#e87a20"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 2 }

            Row {
                spacing: 8
                Text { text: "Default window size"; color: "#aaa"; font.pixelSize: 12; width: 140; anchors.verticalCenter: parent.verticalCenter }
                SpinBox { from: 3; to: 31; value: backend.defaultWindowSize; stepSize: 2; width: 80; height: 26
                    onValueModified: backend.defaultWindowSize = value
                }
            }

            Row {
                spacing: 8
                Switch { checked: backend.rememberSettings; onToggled: backend.rememberSettings = checked; scale: 0.6 }
                Text { text: "Remember settings across sessions"; color: "#aaa"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
            }

            Rectangle { width: parent.width; height: 1; color: "#2a2a2a" }

            /* ---- Training Data Section ---- */
            Text { text: "TRAINING DATA"; color: "#e87a20"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 2 }

            Row {
                spacing: 8
                Switch { checked: backend.trainingConsent; onToggled: backend.trainingConsent = checked; scale: 0.6 }
                Text { text: "Contribute anonymous noise patches"; color: "#aaa"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
            }
            Text { text: "Helps improve denoising quality. Only 256x256 patches collected — no full frames, no filenames, no personal data."; color: "#555"; font.pixelSize: 10; wrapMode: Text.Wrap; width: parent.width }

            Rectangle { width: parent.width; height: 1; color: "#2a2a2a" }

            /* ---- About Section ---- */
            Text { text: "ABOUT"; color: "#e87a20"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 2 }
            Text { text: "BayerFlow v1.0.0"; color: "#aaa"; font.pixelSize: 12 }
            Text { text: "GPU: " + backend.gpuName; color: "#666"; font.pixelSize: 10 }
            Text { text: "RAFT Optical Flow + CUDA VST+Bilateral + DnCNN"; color: "#555"; font.pixelSize: 10 }

            Item { height: 16; width: 1 }
        }
    }

    FolderDialog {
        id: outputDirDialog; title: "Select Default Output Directory"
        onAccepted: backend.defaultOutputDir = selectedFolder.toString().replace("file:///", "")
    }
}
