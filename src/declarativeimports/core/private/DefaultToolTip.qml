/*
*   Copyright 2013-2015 by Sebastian Kügler <sebas@kde.org>
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU Library General Public License as
*   published by the Free Software Foundation; either version 2, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU Library General Public License for more details
*
*   You should have received a copy of the GNU Library General Public
*   License along with this program; if not, write to the
*   Free Software Foundation, Inc.,
*   51 Franklin Street, Fifth Floor, Boston, MA  2.010-1301, USA.
*/

import QtQuick 2.0
import QtQuick.Layouts 1.1
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 2.0 as PlasmaComponents
import org.kde.plasma.extras 2.0 as PlasmaExtras


Item {
    id: tooltipContentItem

    property Item toolTip
    property int preferredTextWidth: units.gridUnit * 20

    Layout.minimumWidth: childrenRect.width + units.gridUnit
    Layout.minimumHeight: childrenRect.height + units.gridUnit
    Layout.maximumWidth: childrenRect.width + units.gridUnit
    Layout.maximumHeight: childrenRect.height + units.gridUnit

    RowLayout {

        anchors {
            left: parent.left
            top: parent.top
            margins: units.gridUnit / 2
        }

        spacing: units.largeSpacing

        Image {
            id: tooltipImage
            source: toolTip ? toolTip.image : ""
            visible: toolTip != null && toolTip.image != ""
            Layout.alignment: Qt.AlignTop
        }

        PlasmaCore.IconItem {
            id: tooltipIcon
            animated: false
            source: toolTip ? toolTip.icon : ""
            Layout.alignment: Qt.AlignTop
            visible: toolTip != null && toolTip.icon != "" && toolTip.image == ""
            implicitWidth: toolTip && toolTip.icon != "" ? units.iconSizes.medium : 0
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitWidth
        }

        ColumnLayout {

            PlasmaExtras.Heading {
                id: tooltipMaintext
                level: 3
                property int _width: Math.min(implicitWidth, preferredTextWidth)
                Layout.minimumWidth: _width
                Layout.maximumWidth: preferredTextWidth
                elide: Text.ElideRight
                text: toolTip ? toolTip.mainText : ""
                visible: text != ""
            }
            PlasmaComponents.Label {
                id: tooltipSubtext
                property int _width: Math.min(implicitWidth, preferredTextWidth)
                Layout.minimumWidth: _width
                Layout.maximumWidth: preferredTextWidth
                wrapMode: Text.WordWrap
                text: toolTip ? toolTip.subText : ""
                textFormat: toolTip ? toolTip.textFormat : Text.AutoText
                opacity: 0.6
                visible: text != ""
                maximumLineCount: 8
            }
        }
    }
}

