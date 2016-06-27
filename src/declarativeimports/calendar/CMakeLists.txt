set(calendar_SRCS
    calendarplugin.cpp
    calendar.cpp
    calendardata.cpp
    daysmodel.cpp
    eventdatadecorator.cpp
    eventpluginsmanager.cpp
)

if(HAVE_LUNARDATE)
    include_directories(${LUNARDATE_INCLUDE_DIRS})
endif()

if(HAVE_LUNARDATE)
    add_library(calendarplugin SHARED ${calendar_SRCS} lunar.cpp)
else()
    add_library(calendarplugin SHARED ${calendar_SRCS})
endif()

target_link_libraries(calendarplugin
    Qt5::Core
    Qt5::Quick
    Qt5::Qml
    Qt5::Gui
    KF5::I18n
    KF5::CalendarEvents
)

if(HAVE_LUNARDATE)
    target_link_libraries(calendarplugin ${LUNARDATE_LIBRARIES})
endif()

install(TARGETS calendarplugin DESTINATION ${KDE_INSTALL_QMLDIR}/org/kde/plasma/calendar)
install(DIRECTORY qml/ DESTINATION ${KDE_INSTALL_QMLDIR}/org/kde/plasma/calendar)
