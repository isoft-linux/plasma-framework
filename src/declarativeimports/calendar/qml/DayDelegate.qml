/*
 * Copyright 2013 Heena Mahour <heena393@gmail.com>
 * Copyright 2013 Sebastian Kügler <sebas@kde.org>
 * Copyright 2015 Kai Uwe Broulik <kde@privat.broulik.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
import QtQuick 2.0
import org.kde.plasma.calendar 2.0
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 2.0 as Components

import org.kde.plasma.calendar 2.0

MouseArea {
    id: dayStyle

    hoverEnabled: true

    signal activated

    readonly property date thisDate: new Date(yearNumber, typeof monthNumber !== "undefined" ? monthNumber - 1 : 0, typeof dayNumber !== "undefined" ? dayNumber : 1)
    readonly property bool today: {
        var today = root.today;
        var result = true;
        if (dateMatchingPrecision >= Calendar.MatchYear) {
            result = result && today.getFullYear() === thisDate.getFullYear()
        }
        if (dateMatchingPrecision >= Calendar.MatchYearAndMonth) {
            result = result && today.getMonth() === thisDate.getMonth()
        }
        if (dateMatchingPrecision >= Calendar.MatchYearMonthAndDay) {
            result = result && today.getDate() === thisDate.getDate()
        }
        return result
    }
    readonly property bool selected: {
        var current = root.currentDate;
        var result = true;
        if (dateMatchingPrecision >= Calendar.MatchYear) {
            result = result && current.getFullYear() === thisDate.getFullYear()
        }
        if (dateMatchingPrecision >= Calendar.MatchYearAndMonth) {
            result = result && current.getMonth() === thisDate.getMonth()
        }
        if (dateMatchingPrecision >= Calendar.MatchYearMonthAndDay) {
            result = result && current.getDate() === thisDate.getDate()
        }
        return result
    }

    property QtObject lunar: Qt.createQmlObject('import org.kde.plasma.calendar 2.0; Lunar { year: ' + thisDate.getFullYear() + '; month: ' + thisDate.getMonth() + ' }', dayStyle, 'lunarObject');

    onHeightChanged: {
        // this is needed here as the text is first rendered, counting with the default root.cellHeight
        // then root.cellHeight actually changes to whatever it should be, but the Label does not pick
        // it up after that, so we need to change it explicitly after the cell size changes
        label.font.pixelSize = Math.max(theme.smallestFont.pixelSize, Math.floor(daysCalendar.cellHeight / 3))
    }

    Component.onCompleted: {
        if (lunar) {
            lunar.day = model.label || dayNumber;
        }
    }

    Rectangle {
        id: todayRect
        anchors.fill: parent
        opacity: {
            if (selected && today) {
                0.6
            } else if (today) {
                0.4
            } else {
                0
            }
        }
        Behavior on opacity { NumberAnimation { duration: units.shortDuration*2 } }
        color: theme.textColor
    }

    Rectangle {
        id: highlightDate
        anchors.fill: todayRect
        opacity: {
            if (selected) {
                0.6
            } else if (dayStyle.containsMouse) {
                0.4
            } else {
                0
            }
        }
        visible: !today
        Behavior on opacity { NumberAnimation { duration: units.shortDuration*2 } }
        color: theme.highlightColor
        z: todayRect.z - 1
    }

    Loader {
        active: model.containsEventItems !== undefined && model.containsEventItems
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        height: parent.height / 3
        width: height
        sourceComponent: eventsMarkerComponent
    }

    Components.Label {
        id: label
        anchors {
            fill: todayRect
            margins: units.smallSpacing
        }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: model.label || dayNumber
        opacity: isCurrent ? 1.0 : 0.5
        wrapMode: Text.NoWrap
        elide: Text.ElideRight
        fontSizeMode: Text.HorizontalFit
        font.pixelSize: Math.max(theme.smallestFont.pixelSize, Math.floor(daysCalendar.cellHeight / 3))
        // This is to avoid the "Both point size and
        // pixel size set. Using pixel size" warnings
        font.pointSize: undefined
        color: today ? theme.backgroundColor : theme.textColor
        Behavior on color {
            ColorAnimation { duration: units.shortDuration * 2 }
        }
    }

    Rectangle {
         width: 5
         height: width
         x: 5
         y: 5
         color: theme.highlightColor
         border.color: theme.highlightColor
         border.width: 1
         radius: width * 0.5
         visible: lunar ? lunar.festival.length > 0 && dateMatchingPrecision >= Calendar.MatchYearMonthAndDay : dateMatchingPrecision >= Calendar.MatchYearMonthAndDay
    }

    Timer {
        id: showTimer

        readonly property bool day: dateMatchingPrecision >= Calendar.MatchYearMonthAndDay ? true : false

        interval: 1000
        running: (day && dayStyle.containsMouse && !calendarGrid.tip.visible)
        onTriggered: {
            var column = index % 7
            var cellwidth = root.borderWidth + daysCalendar.cellWidth
            calendarGrid.mouseOnIndex = index
            calendarGrid.tip.text = lunar ? lunar.text : ""
            var tipOverCell = Math.round(tip.width / cellwidth)
            var boundCell = 7 - tipOverCell
            var alignment = column < boundCell ? column : boundCell
            calendarGrid.tip.show()
            calendarGrid.tip.x = alignment * cellwidth
            calendarGrid.tip.y = y
        }
    }

    Timer {
        id: hideTimer
        interval: 100 // ms before the tip is hidden
        running: index == calendarGrid.mouseOnIndex && !dayStyle.containsMouse && calendarGrid.tip.visible
        onTriggered: calendarGrid.tip.hide(); // this is the js code that hides the tip.
                                              // you could also use visible=false; if you
                                              // don't need animations
    }
}
