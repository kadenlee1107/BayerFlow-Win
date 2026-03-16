import QtQuick

/* Draggable wipe comparison: before (left) / after (right) with vertical divider.
 * Matches Mac CompareView.swift exactly. */
Item {
    id: compareRoot
    clip: true

    property alias beforeSource: beforeImg.source
    property alias afterSource: afterImg.source
    property real dividerFraction: 0.5

    /* After image (full, behind) */
    Image {
        id: afterImg
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        cache: false
    }

    /* Before image clipped to left of divider */
    Item {
        anchors.fill: parent
        clip: true
        width: parent.width * dividerFraction

        Image {
            id: beforeImg
            width: compareRoot.width; height: compareRoot.height
            fillMode: Image.PreserveAspectFit
            cache: false
        }
    }

    /* Divider line */
    Rectangle {
        x: parent.width * dividerFraction - 1; y: 0
        width: 2; height: parent.height
        color: "white"
        opacity: 0.9

        /* Drop shadow effect via dark border */
        Rectangle { anchors.fill: parent; anchors.margins: -1; color: "black"; opacity: 0.3; z: -1 }
    }

    /* Drag handle (pill) */
    Rectangle {
        x: parent.width * dividerFraction - 3
        y: parent.height / 2 - 18
        width: 6; height: 36; radius: 3
        color: "white"
        opacity: 0.9

        Rectangle { anchors.fill: parent; anchors.margins: -2; radius: 5; color: "black"; opacity: 0.3; z: -1 }
    }

    /* Labels */
    Row {
        anchors.top: parent.top; anchors.topMargin: 8
        anchors.left: parent.left; anchors.leftMargin: 8
        Rectangle {
            width: origLabel.width + 12; height: 20; radius: 10
            color: Qt.rgba(0, 0, 0, 0.5)
            Text { id: origLabel; anchors.centerIn: parent; text: "Original"; color: "#fff"; font.pixelSize: 10; font.weight: Font.Bold }
        }
    }
    Row {
        anchors.top: parent.top; anchors.topMargin: 8
        anchors.right: parent.right; anchors.rightMargin: 8
        Rectangle {
            width: denLabel.width + 12; height: 20; radius: 10
            color: Qt.rgba(0, 0, 0, 0.5)
            Text { id: denLabel; anchors.centerIn: parent; text: "Denoised"; color: "#fff"; font.pixelSize: 10; font.weight: Font.Bold }
        }
    }

    /* Drag gesture */
    MouseArea {
        anchors.fill: parent
        onPositionChanged: function(mouse) {
            var frac = mouse.x / width
            dividerFraction = Math.min(Math.max(frac, 0.05), 0.95)
        }
        onPressed: function(mouse) {
            var frac = mouse.x / width
            dividerFraction = Math.min(Math.max(frac, 0.05), 0.95)
        }
        cursorShape: Qt.SplitHCursor
    }
}
