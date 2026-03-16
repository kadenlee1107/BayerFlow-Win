import QtQuick
import QtQuick.Controls

/* License activation dialog — matches Mac LicenseView.swift */
Dialog {
    id: licenseDialog
    title: "Activate License"
    width: 420; height: 300
    modal: true
    standardButtons: Dialog.NoButton
    anchors.centerIn: parent

    background: Rectangle { color: "#1a1a1a"; radius: 10; border.color: "#333"; border.width: 1 }

    Column {
        anchors.fill: parent; anchors.margins: 10; spacing: 14

        /* Status */
        Text {
            text: backend.licenseStatus
            color: backend.isLicensed ? "#4caf50" : (backend.trialDaysRemaining > 0 ? "#e87a20" : "#f44336")
            font.pixelSize: 16; font.weight: Font.Bold
        }

        Text {
            visible: !backend.isLicensed
            text: backend.trialDaysRemaining > 0
                ? "Enter your license key to unlock permanent access."
                : "Your trial has expired. Please activate to continue."
            color: "#888"; font.pixelSize: 12; wrapMode: Text.Wrap; width: parent.width
        }

        /* Email input */
        Text { text: "Email"; color: "#888"; font.pixelSize: 11; visible: !backend.isLicensed }
        Rectangle {
            visible: !backend.isLicensed
            width: parent.width; height: 32; radius: 4
            color: "#222"; border.color: emailInput.activeFocus ? "#e87a20" : "#333"; border.width: 1
            TextInput {
                id: emailInput; anchors.fill: parent; anchors.margins: 8
                color: "#ddd"; font.pixelSize: 13; selectByMouse: true
                placeholderText: "you@example.com"
            }
        }

        /* License key input */
        Text { text: "License Key"; color: "#888"; font.pixelSize: 11; visible: !backend.isLicensed }
        Rectangle {
            visible: !backend.isLicensed
            width: parent.width; height: 32; radius: 4
            color: "#222"; border.color: keyInput.activeFocus ? "#e87a20" : "#333"; border.width: 1
            TextInput {
                id: keyInput; anchors.fill: parent; anchors.margins: 8
                color: "#ddd"; font.pixelSize: 12; font.family: "Cascadia Mono"; selectByMouse: true
                placeholderText: "XXXX-XXXX-XXXX-XXXX"
            }
        }

        /* Buttons */
        Row {
            spacing: 10

            StyledButton {
                visible: !backend.isLicensed
                label: "Activate"; width: 100; height: 32
                onClicked: {
                    if (!backend.activateLicense(emailInput.text, keyInput.text)) {
                        /* Error shown via status bar */
                    } else {
                        licenseDialog.close()
                    }
                }
            }

            StyledButton {
                visible: backend.isLicensed
                label: "Deactivate"; width: 100; height: 32
                onClicked: backend.deactivateLicense()
            }

            StyledButton {
                label: "Close"; width: 60; height: 32
                onClicked: licenseDialog.close()
            }
        }

        /* Purchase link */
        Text {
            visible: !backend.isLicensed
            text: "Purchase at bayerflow.com"
            color: "#e87a20"; font.pixelSize: 11
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                onClicked: Qt.openUrlExternally("https://bayerflow.com")
            }
        }
    }
}
